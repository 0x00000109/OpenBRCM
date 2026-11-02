// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — mac80211 registration and radio capability exposure.
 *
 * M2 scope: register an ieee80211_hw so the PHY appears in `iw phy`.
 * Only capabilities backed by recovered/provenance-tracked data are advertised.
 *
 * Deliberately NOT advertised yet (missing provenance):
 *   - 5 GHz band            (per-chip channel set not recovered)
 *   - VHT / 80 MHz          (5 GHz only; not advertised while 5 GHz is absent)
 *   - LDPC / MAX_AMSDU / >2 spatial streams
 *
 * `.start` validates first-up input but keeps hardware initialization closed
 * until the complete LPF/radio/calibration/channel transition is admitted.
 * Channel changes use the recovered per-channel rows when the PHY is
 * operational; the production start gate still controls hardware admission.
 */
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/bcma/bcma_driver_chipcommon.h>

#include "ob_core.h"
#include "bcm43xx_chan_tuning.h"
#include "bcm43xx_chan_tuning_data.h"
#include "ob_mac80211.h"
#include "ob_channel.h"
#include "ob_rate.h"
#include "ob_radio.h"
#include "ob_tx_path.h"

#define OB_MAC_TX_SW_QUEUE_DEPTH 32u

/*
 * Kernel-API adapters.
 *
 * Each mac80211 prototype that differs across supported kernels is gated by
 * the release that changed it; the boundaries and the headers they were read
 * from live in ob_mac80211_compat.h. Build-verified against 6.12.114 and
 * 7.0.0-38. The 5.10 target (CIP SLTS, the stand kernel) keeps the old
 * branches.
 */
#include "ob_mac80211_compat.h"

struct ob_pub {
	struct ob_hw		*hw;
	struct ieee80211_hw	*ieee;
	struct ob_tx_path	tx_path;
	struct ob_tx_sw_slot	*tx_slots;
	spinlock_t		tx_lock;
	bool			tx_path_initialized;
};

static int ob_mac_tx_map(void *ctx, unsigned int queue, void *packet,
			 ob_tx_sw_u32 length, ob_tx_sw_u64 *dma)
{
	/* Production TX has no admitted D11 FIFO/ring submission backend yet.
	 * Refuse before creating a device-visible DMA mapping.
	 */
	(void)ctx;
	(void)queue;
	(void)packet;
	(void)length;
	(void)dma;
	return -EOPNOTSUPP;
}

static void ob_mac_tx_unmap(void *ctx, unsigned int queue,
			    ob_tx_sw_u64 dma, ob_tx_sw_u32 length)
{
	/* No production mapping can be acquired while the hardware backend is
	 * closed. Fake backends provide and release their own mappings in tests.
	 */
	(void)ctx;
	(void)queue;
	(void)dma;
	(void)length;
}

static int ob_mac_tx_prepare(void *ctx, unsigned int queue, void *packet,
			     ob_tx_sw_u64 dma, ob_tx_sw_u32 length,
			     const struct ob_tx_sw_ticket *ticket,
			     ob_tx_sw_u16 ring_slot)
{
	/* The D11 TX-header/FIFO descriptor inputs are not fully admitted. */
	(void)ctx;
	(void)queue;
	(void)packet;
	(void)dma;
	(void)length;
	(void)ticket;
	(void)ring_slot;
	return -EOPNOTSUPP;
}

static int ob_mac_tx_submit(void *ctx, unsigned int queue, void *packet,
			    ob_tx_sw_u64 dma, ob_tx_sw_u32 length,
			    const struct ob_tx_sw_ticket *ticket,
			    ob_tx_sw_u16 ring_slot)
{
	/* No D11 TX engine, FIFO, ring base or doorbell is programmed here. */
	(void)ctx;
	(void)queue;
	(void)packet;
	(void)dma;
	(void)length;
	(void)ticket;
	(void)ring_slot;
	return -EOPNOTSUPP;
}

static int ob_mac_tx_cancel(void *ctx, unsigned int queue,
			    const struct ob_tx_sw_ticket *ticket)
{
	/* There is no admitted hardware submission to cancel or quiesce. */
	(void)ctx;
	(void)queue;
	(void)ticket;
	return -EOPNOTSUPP;
}

static void ob_mac_tx_complete(void *ctx, unsigned int queue, void *packet,
			       int status)
{
	struct ob_pub *pub = ctx;

	(void)queue;
	(void)status;
	/* No production TX-status decoder exists. Never translate a software
	 * completion into IEEE80211_TX_STAT_ACK; dispose only as an unreported
	 * failure until the real status contract is connected.
	 */
	if (pub && pub->ieee && packet)
		ieee80211_free_txskb(pub->ieee, packet);
}

static void ob_mac_tx_queue_stop(void *ctx, unsigned int queue)
{
	struct ob_pub *pub = ctx;

	if (pub && pub->ieee && queue < OB_TX_PATH_QUEUE_COUNT)
		ieee80211_stop_queue(pub->ieee, queue);
}

static void ob_mac_tx_queue_wake(void *ctx, unsigned int queue)
{
	struct ob_pub *pub = ctx;

	if (pub && pub->ieee && queue < OB_TX_PATH_QUEUE_COUNT)
		ieee80211_wake_queue(pub->ieee, queue);
}

static const struct ob_tx_path_ops ob_mac_tx_ops = {
	.map = ob_mac_tx_map,
	.unmap = ob_mac_tx_unmap,
	.prepare = ob_mac_tx_prepare,
	.submit = ob_mac_tx_submit,
	.cancel = ob_mac_tx_cancel,
	.complete = ob_mac_tx_complete,
	.queue_stop = ob_mac_tx_queue_stop,
	.queue_wake = ob_mac_tx_queue_wake,
};

/* Static capability tables (identical for every device). */
static struct ieee80211_channel ob_2ghz_chan[OB_N_2GHZ_CHANNELS];
static struct ieee80211_rate ob_2ghz_rate[OB_N_2GHZ_RATES];
static struct ieee80211_supported_band ob_band_2ghz;

/*
 * Experimental 5 GHz capability table for bringup_test_only. The standard
 * 5 GHz channel numbers are advertised only in that explicit test mode: the
 * channel-tuning lookup still validates every request against the recovered
 * row frequency, and calibration/channel transition remain NOT RUN. This is
 * not a production 5 GHz capability claim.
 */
#define OB_N_5GHZ_CHANNELS 25
#define OB_N_5GHZ_RATES 8
static struct ieee80211_channel ob_5ghz_chan[OB_N_5GHZ_CHANNELS];
static struct ieee80211_rate ob_5ghz_rate[OB_N_5GHZ_RATES];
static struct ieee80211_supported_band ob_band_5ghz;

static const u16 ob_5ghz_channels[OB_N_5GHZ_CHANNELS] = {
	36, 40, 44, 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165,
};

static const u16 ob_5ghz_rates[OB_N_5GHZ_RATES] = {
	60, 90, 120, 180, 240, 360, 480, 540,
};

static void ob_fill_2ghz(bool wide)
{
	int i;

	for (i = 0; i < OB_N_2GHZ_CHANNELS; i++) {
		u16 ch = ob_2ghz_channels[i];

		ob_2ghz_chan[i].band = NL80211_BAND_2GHZ;
		ob_2ghz_chan[i].center_freq = ob_channel_to_freq(ch, OB_BAND_2GHZ);
		ob_2ghz_chan[i].hw_value = ch;
		/* ch14 has no HT40 on either side; leave other flags to cfg80211. */
		if (ch == 14)
			ob_2ghz_chan[i].flags = IEEE80211_CHAN_NO_HT40MINUS |
						IEEE80211_CHAN_NO_HT40PLUS;
		else
			ob_2ghz_chan[i].flags = 0;
	}

	for (i = 0; i < OB_N_2GHZ_RATES; i++) {
		/* bitrate is expressed in 100 kbps units. */
		ob_2ghz_rate[i].bitrate = (ob_2ghz_rates[i].rate500 & 0x7f) * 5;
		ob_2ghz_rate[i].hw_value = i;
		ob_2ghz_rate[i].hw_value_short = i;
		ob_2ghz_rate[i].flags = ob_2ghz_rates[i].basic ?
			(IEEE80211_RATE_MANDATORY_B | IEEE80211_RATE_MANDATORY_G) : 0;
	}

	ob_band_2ghz.band = NL80211_BAND_2GHZ;
	ob_band_2ghz.channels = ob_2ghz_chan;
	ob_band_2ghz.n_channels = OB_N_2GHZ_CHANNELS;
	ob_band_2ghz.bitrates = ob_2ghz_rate;
	ob_band_2ghz.n_bitrates = OB_N_2GHZ_RATES;

	/*
	 * HT: 20 MHz and short GI are proven by the recovered rate formula
	 * (Nsd = 52 and the sgi branch). 2 spatial streams are proven by the
	 * acphy 2x2 target. 8K is the minimum A-MPDU receive length encoded by
	 * the HT capability (there is no "aggregation off" value); no
	 * .ampdu_action exists, so mac80211 cannot start an aggregation session.
	 * LDPC / MAX_AMSDU / 40 MHz are not advertised (unproven or unconfigured).
	 */
	memset(&ob_band_2ghz.ht_cap, 0, sizeof(ob_band_2ghz.ht_cap));
	ob_band_2ghz.ht_cap.ht_supported = true;
	/*
	 * 20 MHz only: .start accepts NL80211_CHAN_WIDTH_20 and no channel
	 * switch is implemented, so advertising 40 MHz (SUP_WIDTH_20_40 /
	 * SGI_40) would promise a width the driver cannot configure. This is a
	 * capability correction under the Linux wireless contract, not a new
	 * hardware feature. HT40 stays unadvertised until a channel transition
	 * is admitted.
	 */
	ob_band_2ghz.ht_cap.cap = IEEE80211_HT_CAP_SGI_20 |
		(wide ? (IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
			 IEEE80211_HT_CAP_SGI_40) : 0);
	ob_band_2ghz.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	ob_band_2ghz.ht_cap.ampdu_density = 0;
	ob_band_2ghz.ht_cap.mcs.rx_mask[0] = 0xff;	/* MCS 0-7  */
	ob_band_2ghz.ht_cap.mcs.rx_mask[1] = 0xff;	/* 2 streams */
	/*
	 * Highest supported data rate (Mbps) must match the advertised width.
	 * MCS7 x 2 streams x 20 MHz x short-GI = 144 Mbps (ob_rate.c). The HT40
	 * value (300) is deliberately not advertised while only 20 MHz is
	 * accepted.
	 */
	ob_band_2ghz.ht_cap.mcs.rx_highest =
		cpu_to_le16((wide ? ob_mcs_to_rate_kbps(7, 40, 2, true) :
			     ob_mcs_to_rate_kbps(7, 20, 2, true)) / 1000);
	ob_band_2ghz.ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED |
		((2 - 1) << IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);

	/* No VHT on the 2.4 GHz band. */
}

static void ob_fill_5ghz(void)
{
	int i;

	for (i = 0; i < OB_N_5GHZ_CHANNELS; i++) {
		u16 ch = ob_5ghz_channels[i];

		ob_5ghz_chan[i].band = NL80211_BAND_5GHZ;
		ob_5ghz_chan[i].center_freq = ob_channel_to_freq(ch, OB_BAND_5GHZ);
		ob_5ghz_chan[i].hw_value = ch;
		ob_5ghz_chan[i].flags = 0;
	}

	for (i = 0; i < OB_N_5GHZ_RATES; i++) {
		ob_5ghz_rate[i].bitrate = ob_5ghz_rates[i];
		ob_5ghz_rate[i].hw_value = i;
		ob_5ghz_rate[i].hw_value_short = i;
		ob_5ghz_rate[i].flags = 0;
	}

	ob_band_5ghz.band = NL80211_BAND_5GHZ;
	ob_band_5ghz.channels = ob_5ghz_chan;
	ob_band_5ghz.n_channels = OB_N_5GHZ_CHANNELS;
	ob_band_5ghz.bitrates = ob_5ghz_rate;
	ob_band_5ghz.n_bitrates = OB_N_5GHZ_RATES;

	memset(&ob_band_5ghz.ht_cap, 0, sizeof(ob_band_5ghz.ht_cap));
	ob_band_5ghz.ht_cap.ht_supported = true;
	ob_band_5ghz.ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40;
	ob_band_5ghz.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	ob_band_5ghz.ht_cap.ampdu_density = 0;
	ob_band_5ghz.ht_cap.mcs.rx_mask[0] = 0xff;
	ob_band_5ghz.ht_cap.mcs.rx_mask[1] = 0xff;
	ob_band_5ghz.ht_cap.mcs.rx_highest =
		cpu_to_le16(ob_mcs_to_rate_kbps(7, 40, 2, true) / 1000);
	ob_band_5ghz.ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED |
		((2 - 1) << IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);

	memset(&ob_band_5ghz.vht_cap, 0, sizeof(ob_band_5ghz.vht_cap));
	ob_band_5ghz.vht_cap.vht_supported = true;
	ob_band_5ghz.vht_cap.cap = IEEE80211_VHT_CAP_SHORT_GI_80;
	ob_band_5ghz.vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0x003a);
	ob_band_5ghz.vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0x003a);
}

/* ---- ieee80211_ops ------------------------------------------------------ */

static int ob_chan_tuning_encode(const struct cfg80211_chan_def *chandef,
				 u16 *chanspec);
static int ob_ops_apply_chan_tuning(struct ob_hw *ob, u16 chanspec, u16 freq);

static int ob_start_chanspec(struct ieee80211_hw *ieee, u16 *encoded)
{
	const struct cfg80211_chan_def *chandef = &ieee->conf.chandef;
	const struct ieee80211_channel *chan;
	struct ob_pub *pub = ieee->priv;

	if (!encoded || !chandef->chan || !cfg80211_chandef_valid(chandef))
		return -22;
	if (!cfg80211_chandef_usable(ieee->wiphy, chandef, 0))
		return -22;
	if (pub && pub->hw && pub->hw->bringup_test_only) {
		if (chandef->center_freq2)
			return -EINVAL;
		switch (chandef->width) {
		case NL80211_CHAN_WIDTH_20:
		case NL80211_CHAN_WIDTH_20_NOHT:
		case NL80211_CHAN_WIDTH_40:
		case NL80211_CHAN_WIDTH_80:
			return ob_chan_tuning_encode(chandef, encoded);
		default:
			return -EOPNOTSUPP;
		}
	}
	if (chandef->chan->band != NL80211_BAND_2GHZ ||
	    chandef->width != NL80211_CHAN_WIDTH_20)
		return -95;

	chan = chandef->chan;
	if (chandef->center_freq1 != chan->center_freq ||
	    chandef->center_freq2 != 0)
		return -22;

	return ob_acphy_encode_2g_20mhz(chan->hw_value,
				       chandef->center_freq1, encoded);
}

static bool ob_start_target_ready(const struct ob_hw *ob)
{
	return ob && ob->core && ob->bus && ob->core->wrap && !ob->dev_lost &&
		ob->chip_id == 0x4352 && ob->chip_rev == 3 &&
		ob->core->id.id == BCMA_CORE_80211 && ob->core->id.rev == 42 &&
		ob->phy_type == 0x0b && ob->phy_radio_identity_valid &&
		ob->radio_access_initialized &&
		ob->d11_state == OB_D11_POST_UCODE_READY;
}

/* ---- ieee80211_ops ------------------------------------------------------ */

static int ob_ops_start(struct ieee80211_hw *hw)
{
	struct ob_pub *pub;
	struct ob_hw *ob;
	u16 encoded;
	int ret;

	if (!hw || !hw->priv)
		return -22;
	pub = hw->priv;
	ob = pub->hw;
	if (!ob)
		return -22;
	if (ob->base_ready) {
		/* The controlled test may be downed/renamed/upped by userspace after
		 * the first incomplete bring-up. The hardware remains at the same
		 * base state; do not replay the one-shot bootstrap. Production never
		 * sets base_ready. */
		dev_info(ob->dev,
			 "BRINGUP_TEST_ALREADY_READY chanspec=0x%04x; administrative re-up only\n",
			 ob->base_chanspec);
		return 0;
	}

	/* The vendor pi+0xc24 reference clock is the BCMA ALP clock here. */
	if (ob->bus)
		ob->refclk_hz = bcma_chipco_get_alp_clock(&ob->bus->drv_cc);
	ob->chan_tuning_tables = &bcm43xx_chan_tuning_default_tables;

	/* IRQ may be registered, but D11 service remains closed through this path. */
	ob->irq.active = false;
	ret = ob_acphy_first_up_begin(&ob->acphy_start_state);
	if (ret)
		return ret;
	ret = ob_phy_init_begin(&ob->phy_init);
	if (ret)
		goto fail;
	dev_info(ob->dev,
		 "BRINGUP_TEST_START_BEGIN chip=0x%04x rev=%u d11=%u phy=0x%02x radio=0x%04x/%u d11_state=%d\n",
		 ob->chip_id, ob->chip_rev, ob->core->id.rev, ob->phy_type,
		 ob->radio_id, ob->radio_revision, (int)ob->d11_state);
	if (ob->chanspec_valid) {
		ret = -16;
		goto fail;
	}

	ret = ob_start_chanspec(hw, &encoded);
	if (ret)
		goto fail;
	if (!ob_start_target_ready(ob)) {
		ret = ob->dev_lost ? -19 : -95;
		goto fail;
	}

	/* The isolated pre-radio/radio helpers and key lookup are implemented, but
	 * row-driven tuning -> calibration -> channel transition is not
	 * admitted for production.
	 * This gate runs before the known pre-radio hardware prefix. Keep the
	 * coherent stage sequence below connected for when the complete path is
	 * admitted; today the gate returns -EOPNOTSUPP without device access. */
	ret = ob_acphy_init_production_gate(ob);
	if (ret)
		goto fail;

	/* Publish only after the complete-sequence gate admits initialization. */
	ob->chanspec = encoded;
	ob->chanspec_valid = true;
	ret = ob_acphy_pre_radio_setup(ob);
	if (ret)
		goto fail;
	ret = ob_phy_init_advance(&ob->phy_init,
				  OB_PHY_INIT_PRE_RADIO_READY);
	if (ret)
		goto fail;
	ob_acphy_first_up_finish(&ob->acphy_start_state, true);

	ret = ob_acphy_radio_initialize(ob);
	if (ret == -EOPNOTSUPP && ob->bringup_test_only &&
	    ((ob->phy_init.phase == OB_PHY_INIT_BLOCKED &&
	      (ob->phy_init.blocker == OB_PHY_BLOCK_REV1_AFTER_SUB92500 ||
	       ob->phy_init.blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3 ||
	       ob->phy_init.blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH ||
	       ob->phy_init.blocker == OB_PHY_BLOCK_LPF_TARGET)) ||
	     (ob->phy_init.phase == OB_PHY_INIT_PRE_RADIO_READY &&
	      ob->phy_init.blocker == OB_PHY_BLOCK_NONE))) {
		/*
		 * The controlled test stops at the first documented blocker: either
		 * the complete radio prefix (REV1_AFTER_SUB92500) or the missing
		 * rev11 LPF inputs. Apply the recovered per-channel row, publish only
		 * base_ready, and leave calibration/channel transition/data path
		 * closed. This is an administrative interface success, not an
		 * operational PHY claim.
		 */
		dev_info(ob->dev,
			 "BRINGUP_TEST_RADIO_STOP phase=%s blocker=%s; applying recovered chan_tuning row\n",
			 ob_phy_init_phase_name(ob->phy_init.phase),
			 ob_phy_init_blocker_name(ob->phy_init.blocker));
		ret = ob_ops_apply_chan_tuning(ob, encoded,
					hw->conf.chandef.chan->center_freq);
		if (ret)
			goto fail;
		ob->base_ready = true;
		ob->base_chanspec = encoded;
		dev_info(ob->dev,
			 "OPENBRCM_BRINGUP_TEST_READY chanspec=0x%04x base_ready=1 radio_prefix=%s calibration=NOT_RUN channel_transition=NOT_RUN data_path=DISABLED\n",
			 encoded,
			 ob->phy_init.blocker == OB_PHY_BLOCK_REV1_AFTER_SUB92500 ?
			 "COMPLETE_TO_KNOWN_STOP" : "NOT_RUN_LPF_BLOCKED");
		return 0;
	}
	if (ret)
		goto fail;
	ret = ob_acphy_calibration_initialize(ob);
	if (ret == -EOPNOTSUPP && ob->bringup_test_only &&
	    ob->phy_init.phase == OB_PHY_INIT_BLOCKED &&
	    ob->phy_init.blocker == OB_PHY_BLOCK_CALIBRATION_INPUTS) {
		/*
		 * The LPF/radio prefix completed on this run, so the first blocker is
		 * now calibration input ownership. Stop at that boundary, publish the
		 * administrative base state and apply the recovered tuning row; do not
		 * enter calibration or claim the channel lifecycle completed.
		 */
		dev_info(ob->dev,
			 "BRINGUP_TEST_RADIO_STOP phase=%s blocker=%s; radio prefix complete; applying recovered chan_tuning row\n",
			 ob_phy_init_phase_name(ob->phy_init.phase),
			 ob_phy_init_blocker_name(ob->phy_init.blocker));
		ret = ob_ops_apply_chan_tuning(ob, encoded,
					hw->conf.chandef.chan->center_freq);
		if (ret)
			goto fail;
		ob->base_ready = true;
		ob->base_chanspec = encoded;
		dev_info(ob->dev,
			 "OPENBRCM_BRINGUP_TEST_READY chanspec=0x%04x base_ready=1 radio_prefix=COMPLETE_TO_CALIBRATION_STOP calibration=NOT_RUN channel_transition=NOT_RUN data_path=DISABLED\n",
			 encoded);
		return 0;
	}
	if (ret)
		goto fail;
	ret = ob_acphy_channel_initialize(ob);
	if (ret)
		goto fail;
	if (!ob_phy_init_is_operational(&ob->phy_init)) {
		ret = -EIO;
		goto fail;
	}
	return 0;

fail:
	ob->irq.active = false;
	ob_phy_init_fail(&ob->phy_init, ret);
	ob_acphy_first_up_fail(&ob->acphy_start_state);
	dev_err(ob->dev,
		"start: PHY lifecycle stopped at phase=%s blocker=%s error=%d\n",
		ob_phy_init_phase_name(ob->phy_init.phase),
		ob_phy_init_blocker_name(ob->phy_init.blocker), ret);
	return ret;
}

static void ob_ops_stop(struct ieee80211_hw *hw
#if OB_MAC80211_STOP_HAS_SUSPEND
			, bool suspend
#endif
			)
{
	struct ob_pub *pub = hw ? hw->priv : NULL;
	unsigned long flags;
	int ret;

	/* First-up is deliberately one-shot; stop does not reset its state. The
	 * TX path is not admitted on production hardware, but stop its software
	 * queues if a future admitted backend has started them.
	 */
	if (!pub || !pub->tx_path_initialized)
		return;
	spin_lock_irqsave(&pub->tx_lock, flags);
	ret = ob_tx_path_stop(&pub->tx_path);
	spin_unlock_irqrestore(&pub->tx_lock, flags);
	if (ret && net_ratelimit())
		dev_err(pub->hw->dev,
			"tx: software queue stop could not confirm backend cancellation: %d\n",
			ret);
}

static void ob_ops_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct ob_pub *pub;
	struct ob_tx_sw_ticket ticket;
	unsigned long flags;
	u8 fc_bytes[2];
	u16 fc;
	unsigned int queue;
	int ret;

	(void)control;
	if (!skb)
		return;
	if (!hw) {
		dev_kfree_skb_any(skb);
		return;
	}
	pub = hw->priv;
	if (!pub || !pub->hw || !pub->tx_path_initialized ||
	    skb_copy_bits(skb, 0, fc_bytes, sizeof(fc_bytes))) {
		ieee80211_free_txskb(hw, skb);
		return;
	}
	fc = (u16)fc_bytes[0] | ((u16)fc_bytes[1] << 8);
	queue = skb_get_queue_mapping(skb);

	/* The callback uses the same validated software path as the host fake.
	 * The production path stays stopped, so valid packets return -ESHUTDOWN
	 * before DMA mapping. Even if accidentally started, TX preparation rejects
	 * the unproven D11 header/FIFO contract before descriptor publication.
	 */
	spin_lock_irqsave(&pub->tx_lock, flags);
	ret = ob_tx_path_enqueue(&pub->tx_path, skb, fc, skb->len, queue,
				 &ticket);
	spin_unlock_irqrestore(&pub->tx_lock, flags);
	if (ret) {
		ieee80211_free_txskb(hw, skb);
		if (ret != -ESHUTDOWN && net_ratelimit())
			dev_warn(pub->hw->dev,
				 "tx: packet rejected at software TX boundary: %d\n",
				 ret);
	}
}

static int ob_ops_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ob_pub *pub = hw->priv;

	/*
	 * The incomplete bring-up test needs a monitor interface because cfg80211
	 * only exposes explicit per-device channel changes for monitor-only
	 * wiphys. Production remains station-only: this branch is reachable only
	 * with the explicit bringup_test_only module parameter.
	 */
	if (vif->type != NL80211_IFTYPE_STATION &&
	    !(pub->hw->bringup_test_only &&
	      vif->type == NL80211_IFTYPE_MONITOR)) {
		dev_warn(pub->hw->dev, "unsupported interface type %d\n",
			 vif->type);
		return -EOPNOTSUPP;
	}
	return 0;
}

static void ob_ops_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
}

/*
 * Encode the primary channel plus band and width bits.  The vendor provider
 * keys its row by channel only; band and width remain in the chanspec so the
 * caller and diagnostics preserve the full request.  Frequency is validated
 * by the row lookup against row+0x02 (including the 4.9 GHz mapping).
 */
static int ob_chan_tuning_encode(const struct cfg80211_chan_def *chandef,
				 u16 *chanspec)
{
	u16 channel, bw, band;

	if (!chandef || !chandef->chan || !chanspec || !chandef->chan->center_freq)
		return -EINVAL;
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_20_NOHT:
		bw = BCM43XX_CHANSPEC_BW20;
		break;
	case NL80211_CHAN_WIDTH_40:
		bw = BCM43XX_CHANSPEC_BW40;
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = BCM43XX_CHANSPEC_BW80;
		break;
	default:
		return -EOPNOTSUPP;
	}
	channel = chandef->chan->hw_value;
	switch (chandef->chan->band) {
	case NL80211_BAND_2GHZ:
		if (channel < 1 || channel > 14)
			return -EINVAL;
		band = 0;
		break;
	case NL80211_BAND_5GHZ:
		/* 5 GHz 34..165 and 4.9 GHz 184..216 share the 5 GHz band bits. */
		if (!((channel >= 34 && channel <= 165) ||
		      (channel >= 184 && channel <= 216)))
			return -EINVAL;
		band = BCM43XX_CHANSPEC_BAND_MASK;
		break;
	default:
		return -EOPNOTSUPP;
	}
	*chanspec = band | bw | (channel & BCM43XX_CHANSPEC_CHANNEL_MASK);
	return 0;
}

struct ob_chan_tuning_ctx {
	struct ob_hw *hw;
	int error;
	unsigned int radio_writes;
	unsigned int phy_writes;
};

static void ob_chan_tuning_radio_write(void *ctx, u16 reg, u16 value)
{
	struct ob_chan_tuning_ctx *c = ctx;

	if (!c->error) {
		c->radio_writes++;
		c->error = ob_radio_write16(c->hw, reg, value);
	}
}

static void ob_chan_tuning_phy_write(void *ctx, u16 reg, u16 value)
{
	struct ob_chan_tuning_ctx *c = ctx;

	if (!c->error) {
		c->phy_writes++;
		pr_info("openbrcm: chan_tuning phy[0x%03x] = 0x%04x\n",
			reg, value);
		c->error = ob_phy_write16(c->hw, reg, value);
	}
}

static int ob_ops_apply_chan_tuning(struct ob_hw *ob, u16 chanspec, u16 freq)
{
	struct bcm43xx_chan_tuning_env env;
	struct bcm43xx_chan_tuning_io io;
	struct ob_chan_tuning_ctx io_ctx = { .hw = ob, .error = 0 };
	const void *row74, *row5e, *row60, *row5e_mode2;
	u32 code;
	int kind, ret;

	if (!ob || !ob->chan_tuning_tables)
		return -EOPNOTSUPP;
	env.radio_class = ob->radio_revision_class;
	env.radio_rev = ob->radio_revision;
	env.refclk_hz = ob->refclk_hz;
	ret = bcm43xx_get_chan_tuning_row(&env, ob->chan_tuning_tables,
					  chanspec, freq, &code, &row74,
					  &row5e, &row60, &row5e_mode2);
	if (ret)
		return ret;
	pr_info("openbrcm: chan_tuning freq=%u chanspec=0x%04x class=%u rev=0x%02x refclk=%u\n",
		freq, chanspec, env.radio_class, env.radio_rev, env.refclk_hz);
	kind = row74 ? BCM43XX_ROW_LEGACY :
	       row60 ? BCM43XX_ROW_GE25 : BCM43XX_ROW_2069;
	io = (struct bcm43xx_chan_tuning_io){
		.ctx = &io_ctx,
		.write_radio_reg = ob_chan_tuning_radio_write,
		.phy_reg_write = ob_chan_tuning_phy_write,
		.chip_id = ob->chip_id,
	};
	ret = bcm43xx_apply_chan_tuning(kind, row74 ?: row60 ?: row5e ?:
					row5e_mode2, &io);
	if (ret)
		return ret;
	if (io_ctx.error)
		return io_ctx.error;
	if (io_ctx.phy_writes != BCM43XX_CHAN_TUNING_BW_COUNT) {
		pr_err("openbrcm: chan_tuning incomplete phy_writes=%u expected=%u\n",
		       io_ctx.phy_writes, BCM43XX_CHAN_TUNING_BW_COUNT);
		return -EIO;
	}
	pr_info("openbrcm: CHAN_TUNING_APPLIED freq=%u chanspec=0x%04x radio_writes=%u phy_writes=%u phy_bw_regs=0x371..0x376\n",
		freq, chanspec, io_ctx.radio_writes, io_ctx.phy_writes);
	return 0;
}

static int ob_ops_config(struct ieee80211_hw *hw
#if OB_MAC80211_CONFIG_HAS_RADIO_IDX
			 , int radio_idx
#endif
			 , u32 changed)
{
	struct ob_pub *pub = hw->priv;
	struct ob_hw *ob = pub->hw;
	const struct cfg80211_chan_def *chandef;
	u16 chanspec;
	int ret;

	if (!(changed & IEEE80211_CONF_CHANGE_CHANNEL))
		return 0;
	if (!ob_phy_init_is_operational(&ob->phy_init) && !ob->base_ready) {
		dev_warn(ob->dev,
			 "config: channel change rejected before operational/base-ready PHY state\n");
		return -EOPNOTSUPP;
	}
	if (!ob->chan_tuning_tables)
		return -EOPNOTSUPP;

	chandef = &hw->conf.chandef;
	if (!chandef->chan)
		return -EINVAL;
	ret = ob_chan_tuning_encode(chandef, &chanspec);
	if (ret)
		return ret;

	if (ob->chanspec_valid && ob->chanspec == chanspec)
		return 0;
	ret = ob_ops_apply_chan_tuning(ob, chanspec,
				       chandef->chan->center_freq);
	if (ret)
		return ret;

	ob->chanspec = chanspec;
	ob->chanspec_valid = true;
	if (ob->base_ready)
		ob->base_chanspec = chanspec;
	return 0;
}

static void ob_ops_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
#if OB_MAC80211_BSS_INFO_CHANGED_U64
				    u64 changed)
#else
				    u32 changed)
#endif
{
	/* Program points appear in M4. */
}

static void ob_ops_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	/*
	 * The RX path is not implemented, so this device cannot currently pass
	 * any requested frame class to mac80211. Clear every unsupported flag as
	 * required by the mac80211 configure_filter contract. Do not imply that
	 * hardware receive filtering or frame delivery works.
	 */
	if (total_flags)
		*total_flags = ob_mac80211_filter_flags_supported(*total_flags, 0);
}

static const struct ieee80211_ops ob_ops = {
	.tx			= ob_ops_tx,
	.start			= ob_ops_start,
	.stop			= ob_ops_stop,
	.add_interface		= ob_ops_add_interface,
	.remove_interface	= ob_ops_remove_interface,
	.config			= ob_ops_config,
	.bss_info_changed	= ob_ops_bss_info_changed,
	.configure_filter	= ob_ops_configure_filter,
#if OB_MAC80211_HAS_CHANCTX_EMULATION
	/* Present from 6.9 on; absent on 5.10, which drives the legacy .tx path
	 * with single-channel (no-chanctx) operation. */
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
	.add_chanctx		= ieee80211_emulate_add_chanctx,
	.remove_chanctx		= ieee80211_emulate_remove_chanctx,
	.change_chanctx		= ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx	= ieee80211_emulate_switch_vif_chanctx,
#endif
};

/*
 * Gated RX handoff. The policy gate is false until a proven-stripped FCS, a
 * channel mapping and a rate mapping exist. Returning -EOPNOTSUPP leaves the
 * skb owned by the caller; nothing is freed here on the closed path.
 */
int ob_mac80211_rx_deliver(struct ieee80211_hw *ieee, struct sk_buff *skb,
			    const struct ob_rx_mac80211_report *report)
{
	struct ieee80211_rx_status *status;

	if (!ob_rx_mac80211_live_delivery_enabled())
		return -EOPNOTSUPP;
	if (!ieee || !skb || !report)
		return -EINVAL;

	/* mac80211 expects the FCS removed unless RX_INCLUDES_FCS is set. */
	status = IEEE80211_SKB_RXCB(skb);
	memset(status, 0, sizeof(*status));
	status->freq = ieee80211_channel_to_frequency(report->channel,
						      NL80211_BAND_2GHZ);
	if (!status->freq)
		return -EINVAL;
	status->band = NL80211_BAND_2GHZ;
	status->rate_idx = report->rate_index;
	if (report->rate_is_ht) {
		status->encoding = RX_ENC_HT;
		if (report->bw40)
			status->bw = RATE_INFO_BW_40;
		if (report->sgi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	} else {
		status->encoding = RX_ENC_LEGACY;
	}
	status->flag = RX_FLAG_NO_SIGNAL_VAL;
	if (report->flags & OB_RX_MAC80211_REPORT_SIGNAL_VALID) {
		status->signal = report->signal_dbm;
		status->flag &= ~RX_FLAG_NO_SIGNAL_VAL;
	}
	if (report->flags & OB_RX_MAC80211_REPORT_DECRYPTED)
		status->flag |= RX_FLAG_DECRYPTED;

	/* Ownership transfers to mac80211 on this call. */
	ieee80211_rx_irqsafe(ieee, skb);
	return 0;
}

/*
 * Sink callbacks for the single TX skb ownership transfer.
 *
 * ob_tx_status_handoff() calls exactly one of them exactly once per skb, so
 * these callbacks must not free, requeue or report on their own.
 */
static void ob_mac_tx_skb_drop(void *ctx, void *skb)
{
	ieee80211_free_txskb(ctx, skb);
}

static void ob_mac_tx_skb_report(void *ctx, void *skb,
				 const struct ob_tx_status_report *report)
{
	struct ieee80211_hw *ieee = ctx;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ob_tx_status_tx_view view;

	/*
	 * info->flags and info->status are separate members: clearing status does
	 * not clear a stale ACK bit left in flags by an earlier transmission of a
	 * reused skb. The shared composer masks that bit, keeps every other flag,
	 * and emits the -1/0 "no rate" sentinel instead of rate index 0.
	 */
	BUILD_BUG_ON(OB_TX_STATUS_MAC80211_FLAG_ACK != IEEE80211_TX_STAT_ACK);
	BUILD_BUG_ON(OB_TX_STATUS_MAC80211_RATE_UNKNOWN != -1);

	ob_tx_status_mac80211_view(report, info->flags, &view);

	memset(&info->status, 0, sizeof(info->status));
	info->status.rates[0].idx = view.rate_idx;
	info->status.rates[0].count = view.rate_count;
	info->flags = view.flags;
	ob_ieee80211_tx_status(ieee, skb);
}

static const struct ob_tx_status_skb_ops ob_mac_tx_skb_ops = {
	.drop = ob_mac_tx_skb_drop,
	.report = ob_mac_tx_skb_report,
};

/*
 * Gated TX-status handoff with an unambiguous skb contract:
 *
 *   negative     skb was not touched; the caller still owns it. Returned only
 *                for a missing mac80211 hw pointer or a missing skb, i.e. when
 *                this function cannot legally consume a mac80211 skb.
 *   0            skb was handed to mac80211 exactly once (consumed).
 *   positive     skb was freed exactly once as an unreported outcome.
 *
 * The ownership effect itself lives in ob_tx_status_handoff(), which is host
 * tested; this wrapper only supplies the mac80211 sink. Live reporting stays
 * compile-time disabled, and a future open path may only set
 * IEEE80211_TX_STAT_ACK when the translation carries proven ACK evidence.
 */
int ob_mac80211_tx_status(struct ieee80211_hw *ieee, struct sk_buff *skb,
			   const struct ob_tx_status_report *report)
{
	if (!ieee)
		return -EINVAL;

	return ob_tx_status_handoff(skb, report,
				    ob_tx_status_live_reporting_enabled(),
				    &ob_mac_tx_skb_ops, ieee);
}

int ob_mac80211_register(struct ob_hw *ob)
{
	struct ieee80211_hw *ieee;
	struct ob_pub *pub;
	int ret;

	ob_fill_2ghz(ob->bringup_test_only);
	if (ob->bringup_test_only)
		ob_fill_5ghz();

	ieee = ieee80211_alloc_hw(sizeof(*pub), &ob_ops);
	if (!ieee) {
		dev_err(ob->dev, "ieee80211_alloc_hw failed\n");
		return -ENOMEM;
	}

	pub = ieee->priv;
	pub->hw = ob;
	pub->ieee = ieee;
	spin_lock_init(&pub->tx_lock);
	pub->tx_slots = kcalloc(OB_TX_PATH_QUEUE_COUNT *
				OB_MAC_TX_SW_QUEUE_DEPTH,
				sizeof(*pub->tx_slots), GFP_KERNEL);
	if (!pub->tx_slots) {
		ieee80211_free_hw(ieee);
		return -ENOMEM;
	}
	ret = ob_tx_path_init(&pub->tx_path, pub->tx_slots,
			      OB_MAC_TX_SW_QUEUE_DEPTH, 32, ~0U,
			      &ob->dma.tx,
			      &ob_mac_tx_ops, pub);
	if (ret) {
		kfree(pub->tx_slots);
		ieee80211_free_hw(ieee);
		return ret;
	}
	pub->tx_path_initialized = true;

	SET_IEEE80211_DEV(ieee, ob->dev);

	/* A network interface must never receive an invented/default MAC. */
	if (ob->bringup_test_only &&
	    (!ob->mac_valid || ob->mac_source == OB_MAC_SOURCE_UNKNOWN)) {
		u8 test_mac[ETH_ALEN];

		get_random_bytes(test_mac, ETH_ALEN);
		test_mac[0] &= 0xfe;	/* clear multicast */
		test_mac[0] |= 0x02;	/* locally administered */
		SET_IEEE80211_PERM_ADDR(ieee, test_mac);
		dev_warn(ob->dev,
			 "BRINGUP_TEST_LOCAL_MAC %pM generated locally; no hardware provenance and no data path\n",
			 test_mac);
	} else if (!ob->mac_valid || ob->mac_source == OB_MAC_SOURCE_UNKNOWN) {
		dev_err(ob->dev,
			"no validated MAC with known provenance; refusing mac80211 registration\n");
		kfree(pub->tx_slots);
		ieee80211_free_hw(ieee);
		return -ENODATA;
	} else {
		SET_IEEE80211_PERM_ADDR(ieee, ob->mac);
		dev_info(ob->dev, "permanent MAC %pM source=%u\n", ob->mac,
			 ob->mac_source);
	}

	ieee->wiphy->bands[NL80211_BAND_2GHZ] = &ob_band_2ghz;
	ieee->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	if (ob->bringup_test_only)
		ieee->wiphy->interface_modes |= BIT(NL80211_IFTYPE_MONITOR);
	ieee->queues = 4;

	/* 5 GHz/VHT is exposed only by the explicit incomplete bring-up test. */
	ieee->wiphy->bands[NL80211_BAND_5GHZ] =
		ob->bringup_test_only ? &ob_band_5ghz : NULL;

	ret = ieee80211_register_hw(ieee);
	if (ret) {
		dev_err(ob->dev, "ieee80211_register_hw failed: %d\n", ret);
		kfree(pub->tx_slots);
		ieee80211_free_hw(ieee);
		return ret;
	}

	ob->ieee = ieee;
	dev_info(ob->dev, "registered with mac80211 (2.4 GHz band, HT 2x2%s)\n",
		 ob->bringup_test_only ? ", experimental 5 GHz/VHT80" : "");
	return 0;
}

void ob_mac80211_unregister(struct ob_hw *ob)
{
	struct ob_pub *pub;
	unsigned int queue;
	unsigned long flags;
	bool pending = false;

	if (!ob->ieee)
		return;
	pub = ob->ieee->priv;
	ieee80211_unregister_hw(ob->ieee);
	if (pub && pub->tx_path_initialized) {
		spin_lock_irqsave(&pub->tx_lock, flags);
		(void)ob_tx_path_stop(&pub->tx_path);
		for (queue = 0; queue < OB_TX_PATH_QUEUE_COUNT; queue++)
			pending |= ob_tx_path_pending(&pub->tx_path, queue) != 0;
		spin_unlock_irqrestore(&pub->tx_lock, flags);
		if (pending) {
			dev_crit(ob->dev,
				 "tx: impossible pending ownership at unregister; retaining ieee80211_hw and TX owner state\n");
			/* Keep the owner context alive rather than freeing memory still
			 * referenced by a queued packet. The current production backend
			 * cannot map or submit, so this indicates an invariant violation;
			 * hardware TX must stay disabled until a real quiescence contract
			 * provides a reclaim path.
			 */
			ob->ieee = NULL;
			return;
		}
		kfree(pub->tx_slots);
	}
	ieee80211_free_hw(ob->ieee);
	ob->ieee = NULL;
}
