// SPDX-License-Identifier: GPL-2.0-only
/* Offline RX translation boundary; no descriptor decode and no hardware. */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_rx_mac80211.h"
#include "ob_rx_status.h"

int ob_rx_mac80211_translate(const struct ob_rx_sw_status *status,
			     const struct ob_rx_mac80211_evidence *evidence,
			     struct ob_rx_mac80211_report *report)
{
	struct ob_rx_mac80211_report clean;
	ob_rx_mac80211_u8 band_5g;

	if (!report)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*report = clean;
	if (!status || !evidence)
		return -EINVAL;
	if (evidence->proven & ~OB_RX_MAC80211_FACT_KNOWN_MASK)
		return -EINVAL;
	if (status->valid_fields & ~OB_RX_SW_META_KNOWN_MASK)
		return -EINVAL;

	/* A frame boundary is meaningful only when the FCS state is proven. */
	if (!(evidence->proven & OB_RX_MAC80211_FACT_FCS_PROVEN))
		return -ENODATA;
	if (!(status->valid_fields & OB_RX_SW_META_FCS_LEN))
		return -ENODATA;
	if (evidence->fcs_present)
		return -EOPNOTSUPP;	/* no FCS strip is implemented */
	if (status->fcs_len != 0)
		return -EOPNOTSUPP;

	/*
	 * The rev42 receive descriptor carries the channel in RxChan: wlc_recv
	 * uses its low byte as the channel number and bits 15:14 as the 5 GHz
	 * selector (0x4c56d, 0x4c86c, 0x4d31d). Both legal bands therefore cross
	 * this boundary. Only the channel number is carried; no frequency is
	 * synthesized.
	 */
	if (!(evidence->proven & OB_RX_MAC80211_FACT_CHANNEL_PROVEN))
		return -ENODATA;
	if (!(status->valid_fields & OB_RX_SW_META_CHANNEL))
		return -ENODATA;
	if (!((status->channel >= OB_RX_CHAN_MIN_2G &&
	       status->channel <= OB_RX_CHAN_MAX_2G) ||
	      (status->channel >= OB_RX_CHAN_MIN_5G &&
	       status->channel <= OB_RX_CHAN_MAX_5G)))
		return -ERANGE;
	band_5g = status->channel >= OB_RX_CHAN_MIN_5G;
	if (!(evidence->proven & OB_RX_MAC80211_FACT_RATE_PROVEN))
		return -ENODATA;
	if (!(status->valid_fields & OB_RX_SW_META_RATE))
		return -ENODATA;
	/*
	 * HT and legacy rates use different index spaces. HT carries an MCS
	 * index 0..31 plus the proven SGI bit; legacy carries a band table
	 * index 0..11. The two must never be silently interchanged.
	 */
	if (status->valid_fields & OB_RX_SW_META_RATE_HT) {
		if (!status->rate_is_ht)
			return -EINVAL;
		if (status->rate_index >= 32)
			return -ERANGE;
		if (status->rate_bw40 > 1 || status->rate_sgi > 1)
			return -EINVAL;
		/*
		 * The advertised profile is HT20 only. Refuse a proven 40 MHz
		 * descriptor instead of reporting a width the driver cannot use.
		 */
		if (status->rate_bw40 && !OB_RX_MAC80211_HT40_SUPPORTED)
			return -EOPNOTSUPP;
	} else {
		if (status->rate_is_ht || status->rate_bw40 || status->rate_sgi)
			return -EINVAL;
		/* 5 GHz exposes only the eight OFDM entries 0..7. */
		if (status->rate_index >= (band_5g ? 8u : 12u))
			return -ERANGE;
	}

	/* A status value without its evidence bit is exactly the fabrication this
	 * boundary exists to reject.
	 */
	if ((status->valid_fields & OB_RX_SW_META_SIGNAL) &&
	    !(evidence->proven & OB_RX_MAC80211_FACT_SIGNAL_PROVEN))
		return -ENODATA;
	if ((evidence->proven & OB_RX_MAC80211_FACT_SIGNAL_PROVEN) &&
	    !(status->valid_fields & OB_RX_SW_META_SIGNAL))
		return -ENODATA;
	if ((evidence->proven & OB_RX_MAC80211_FACT_SIGNAL_PROVEN) &&
	    (status->signal_dbm < -128 || status->signal_dbm > 127))
		return -ERANGE;
	if (evidence->decrypted &&
	    !(evidence->proven & OB_RX_MAC80211_FACT_DECRYPT_PROVEN))
		return -ENODATA;

	clean.channel = status->channel;
	clean.rate_index = status->rate_index;
	clean.rate_is_ht = status->rate_is_ht;
	clean.bw40 = status->rate_bw40;
	clean.sgi = status->rate_sgi;
	if (evidence->proven & OB_RX_MAC80211_FACT_SIGNAL_PROVEN) {
		clean.flags |= OB_RX_MAC80211_REPORT_SIGNAL_VALID;
		clean.signal_dbm = status->signal_dbm;
	}
	if ((evidence->proven & OB_RX_MAC80211_FACT_DECRYPT_PROVEN) &&
	    evidence->decrypted)
		clean.flags |= OB_RX_MAC80211_REPORT_DECRYPTED;
	*report = clean;
	return 0;
}

int ob_rx_mac80211_handoff(void *skb, const struct ob_rx_sw_status *status,
			   const struct ob_rx_mac80211_evidence *evidence,
			   const struct ob_rx_mac80211_ops *ops, void *ctx)
{
	struct ob_rx_mac80211_report report;
	int ret;

	if (!ops || !ops->deliver || !ops->free || !skb)
		return -EINVAL;
	ret = ob_rx_mac80211_translate(status, evidence, &report);
	if (ret) {
		ops->free(ctx, skb);
		/* Positive: consumed but not delivered. Never a translation errno. */
		return OB_RX_MAC80211_HANDOFF_CONSUMED;
	}
	ops->deliver(ctx, skb, &report);
	return 0;
}

bool ob_rx_mac80211_live_delivery_enabled(void)
{
	return false;
}
