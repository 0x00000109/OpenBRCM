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
 * Hardware bring-up, DMA, TX/RX are M3+; ops that would require them fail
 * explicitly with -EOPNOTSUPP (see ob_ops_start/ob_ops_config).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "ob_core.h"
#include "ob_mac80211.h"
#include "ob_channel.h"
#include "ob_rate.h"

struct ob_pub {
	struct ob_hw		*hw;
	struct ieee80211_hw	*ieee;
};

/* Static capability tables (identical for every device). */
static struct ieee80211_channel ob_2ghz_chan[OB_N_2GHZ_CHANNELS];
static struct ieee80211_rate ob_2ghz_rate[OB_N_2GHZ_RATES];
static struct ieee80211_supported_band ob_band_2ghz;

static void ob_fill_2ghz(void)
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
	 * HT: 20/40 MHz and short GI are proven by the recovered rate formula
	 * (Nsd = 52/108 and the sgi branch). 2 spatial streams are proven by the
	 * acphy 2x2 target. We advertise the conservative A-MPDU size (8K) and no
	 * density requirement. LDPC / MAX_AMSDU are not advertised (unproven).
	 */
	memset(&ob_band_2ghz.ht_cap, 0, sizeof(ob_band_2ghz.ht_cap));
	ob_band_2ghz.ht_cap.ht_supported = true;
	ob_band_2ghz.ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				  IEEE80211_HT_CAP_SGI_20 |
				  IEEE80211_HT_CAP_SGI_40;
	ob_band_2ghz.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	ob_band_2ghz.ht_cap.ampdu_density = 0;
	ob_band_2ghz.ht_cap.mcs.rx_mask[0] = 0xff;	/* MCS 0-7  */
	ob_band_2ghz.ht_cap.mcs.rx_mask[1] = 0xff;	/* 2 streams */
	/*
	 * Highest supported data rate (Mbps) must be consistent with the
	 * advertised MCS set (MCS 0-15, 2 streams) and HT40 + SGI. Derived from
	 * our recovered formula: MCS7 x 2 streams x 40 MHz x short-GI = 300 Mbps
	 * (ob_rate.c). Using the 20 MHz-only value (144) would misreport the max
	 * rate for a 2x2 HT40 radio.
	 */
	ob_band_2ghz.ht_cap.mcs.rx_highest =
		cpu_to_le16(ob_mcs_to_rate_kbps(7, 40, 2, true) / 1000);
	ob_band_2ghz.ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED |
		((2 - 1) << IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);

	/* No VHT on the 2.4 GHz band. */
}

/* ---- ieee80211_ops: unsupported actions fail explicitly ---- */

static int ob_ops_start(struct ieee80211_hw *hw)
{
	struct ob_pub *pub = hw->priv;

	dev_err(pub->hw->dev,
		"start: hardware bring-up not implemented yet (M3)\n");
	return -EOPNOTSUPP;
}

static void ob_ops_stop(struct ieee80211_hw *hw, bool retry)
{
	/* Nothing was started. */
}

static void ob_ops_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct ob_pub *pub = hw->priv;

	/* TX cannot report an error; drop safely and say so. */
	dev_kfree_skb_any(skb);
	if (net_ratelimit())
		dev_warn(pub->hw->dev, "tx: not implemented yet (M3); frame dropped\n");
}

static int ob_ops_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ob_pub *pub = hw->priv;

	if (vif->type != NL80211_IFTYPE_STATION) {
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

static int ob_ops_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	struct ob_pub *pub = hw->priv;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		dev_warn(pub->hw->dev,
			 "config: channel change not implemented yet (M3)\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static void ob_ops_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *info,
				    u64 changed)
{
	/* Program points appear in M4. */
}

static void ob_ops_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	/* No hardware filter programming yet; accept without changing policy. */
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
	.wake_tx_queue		= ieee80211_handle_wake_tx_queue,
	.add_chanctx		= ieee80211_emulate_add_chanctx,
	.remove_chanctx		= ieee80211_emulate_remove_chanctx,
	.change_chanctx		= ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx	= ieee80211_emulate_switch_vif_chanctx,
};

int ob_mac80211_register(struct ob_hw *ob)
{
	struct ieee80211_hw *ieee;
	struct ob_pub *pub;
	int ret;

	ob_fill_2ghz();

	ieee = ieee80211_alloc_hw(sizeof(*pub), &ob_ops);
	if (!ieee) {
		dev_err(ob->dev, "ieee80211_alloc_hw failed\n");
		return -ENOMEM;
	}

	pub = ieee->priv;
	pub->hw = ob;
	pub->ieee = ieee;

	SET_IEEE80211_DEV(ieee, ob->dev);

	ieee->wiphy->bands[NL80211_BAND_2GHZ] = &ob_band_2ghz;
	ieee->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	ieee->queues = 4;

	/*
	 * 5 GHz is intentionally not advertised: the exact BCM4352/acphy channel
	 * set has not been recovered from the blob. Enabling it without
	 * provenance would advertise guessed capabilities. See docs/milestones.md.
	 */
	ieee->wiphy->bands[NL80211_BAND_5GHZ] = NULL;

	ret = ieee80211_register_hw(ieee);
	if (ret) {
		dev_err(ob->dev, "ieee80211_register_hw failed: %d\n", ret);
		ieee80211_free_hw(ieee);
		return ret;
	}

	ob->ieee = ieee;
	dev_info(ob->dev, "registered with mac80211 (2.4 GHz band, HT 2x2)\n");
	return 0;
}

void ob_mac80211_unregister(struct ob_hw *ob)
{
	if (!ob->ieee)
		return;
	ieee80211_unregister_hw(ob->ieee);
	ieee80211_free_hw(ob->ieee);
	ob->ieee = NULL;
}
