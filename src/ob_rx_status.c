// SPDX-License-Identifier: GPL-2.0-only
/*
 * BCM4352 / D11 rev42 receive-status decoding.
 *
 * Provenance summary (instruction addresses are section-relative in the pinned
 * wlc_hybrid.o_shipped):
 *
 *   RxFrameSize@0x00   u16 LE       sub_f504 (0xf53a), wlc_recvdata 0x43c9d
 *   PhyRxStatus_0@0x04 u16          wlc_recv 0x4c1f3 (drop mask 0x310)
 *   PhyRxStatus_1@0x06 u16          wlc_recvdata 0x43e2f / 0x441.. (JSSI)
 *   RxStatus1@0x10     u16          wlc_recv 0x4c17f (0x4), 0x4c31a (0x1)
 *   RxStatus2@0x12     u16          wlc_recv 0x4c210 (0x1), 0x4c245 (0x6)
 *   RxTSFTime@0x14     u16          wlc_recv_prep_event_rx_frame_data
 *   RxChan@0x16        u16          wlc_recvdata 0x43c9d, wlc_recv 0x4c867
 *   TSF high@0x18      u32          wlc_recv_prep_event_rx_frame_data
 *
 * RxChan decoding is anchored at wlc_recv 0x4c56d (`cmpb $0xe,0x16(%r14)`),
 * 0x4c867/0x4d31d (`test $0xc0,%ah`) and the legacy normalization in
 * wlc_bmac_recv 0x67e37..0x67e80; see ob_rx_chan_decode().
 *
 * Rate: wlc_recv_compute_rspec 0x168eac plus the eight-byte OFDM table at
 * .rodata 0x2cdbe0 (`60 30 18 0c 6c 48 24 12`, i.e. 48/24/12/6/54/36/18/9
 * Mbps for indices 0..7). The table is indexed by plcp[0] & 7; it is not
 * sorted by rate. wlc_phy_get_ofdm_rate_lookup 0xb2598 returns this exact
 * table (`nm` symbol ofdm_rate_lookup = 0x2cdbe0).
 */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_rx_status.h"

/*
 * Length-gate provenance (see ob_rx_len_plausible): the vendor subtracts 0xa
 * from the pulled packet length at wlc_recv 0x4c488, then compares the result
 * with 0x18 + 4*flag at 0x4c4a9. Both constants are literal immediates in the
 * pinned object, not inferred.
 */

static ob_rx_status_u16 get_le16(const ob_rx_status_u8 *p)
{
	return (ob_rx_status_u16)(p[0] | ((ob_rx_status_u16)p[1] << 8));
}

static ob_rx_status_u32 get_le32(const ob_rx_status_u8 *p)
{
	return (ob_rx_status_u32)p[0] | ((ob_rx_status_u32)p[1] << 8) |
		((ob_rx_status_u32)p[2] << 16) | ((ob_rx_status_u32)p[3] << 24);
}

int ob_rx_status_decode(const ob_rx_status_u8 *prefix,
			ob_rx_status_size prefix_len,
			struct ob_rx_status *out)
{
	struct ob_rx_status clean;

	if (!out)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*out = clean;
	if (!prefix || prefix_len < OB_RX_STATUS_BASE_LEN)
		return -EINVAL;

	clean.observed_len = (ob_rx_status_u32)prefix_len;

	/* Base d11rxhdr fields consumed by wlc_recv / wlc_recvdata. */
	clean.rx_frame_size = get_le16(prefix + OB_RX_STATUS_OFF_FRAME_SIZE);
	clean.phy_rx_status_0 = get_le16(prefix + OB_RX_STATUS_OFF_PHY_0);
	clean.phy_rx_status_1 = get_le16(prefix + OB_RX_STATUS_OFF_PHY_1);
	clean.rx_status1 = get_le16(prefix + OB_RX_STATUS_OFF_STATUS1);
	clean.rx_status2 = get_le16(prefix + OB_RX_STATUS_OFF_STATUS2);
	clean.rx_tsf_low = get_le16(prefix + OB_RX_STATUS_OFF_TSF_LOW);
	clean.rx_chan = get_le16(prefix + OB_RX_STATUS_OFF_CHAN);

	clean.verified |= OB_RX_FACT_FRAME_SIZE | OB_RX_FACT_PHY_TYPE |
		OB_RX_FACT_FCS_ERROR | OB_RX_FACT_PAD_PRESENT |
		OB_RX_FACT_PHY_DROP | OB_RX_FACT_AMSDU | OB_RX_FACT_AGGTYPE |
		OB_RX_FACT_TSF_LOW | OB_RX_FACT_CHAN_RAW | OB_RX_FACT_BAND;

	clean.phy_type = (ob_rx_status_u8)(clean.phy_rx_status_0 &
					   OB_RX_PHY0_TYPE_MASK);
	clean.fcs_error = !!(clean.rx_status1 & OB_RX_STATUS1_FCS_ERR);
	clean.pad_present = !!(clean.rx_status1 & OB_RX_STATUS1_PAD_PRESENT);
	clean.dec_attempted =
		!!(clean.rx_status1 & OB_RX_STATUS1_DEC_ATTEMPT);
	clean.dec_error = !!(clean.rx_status1 & OB_RX_STATUS1_DEC_ERR);
	clean.sec_index = (ob_rx_status_u16)
		((clean.rx_status1 & OB_RX_STATUS1_SEC_INDEX_MASK) >>
		 OB_RX_STATUS1_SEC_INDEX_SHIFT);
	clean.beacon_sent = !!(clean.rx_status1 & OB_RX_STATUS1_BCN_SENT);
	clean.amsdu = !!(clean.rx_status2 & OB_RX_STATUS2_AMSDU);
	clean.agg_type = (ob_rx_status_u8)
		((clean.rx_status2 & OB_RX_STATUS2_AGGTYPE_MASK) >>
		 OB_RX_STATUS2_AGGTYPE_SHIFT);
	clean.phy_status_valid =
		!!(clean.rx_status2 & OB_RX_STATUS2_PHYST_VALID);
	clean.rx_antenna = (ob_rx_status_u8)
		((clean.rx_status2 & OB_RX_STATUS2_RXANT_MASK) >>
		 OB_RX_STATUS2_RXANT_SHIFT);
	clean.band_5g = !!(clean.rx_chan & OB_RX_CHAN_BAND_MASK);
	clean.jssi = (ob_rx_status_u8)(clean.phy_rx_status_1 & 0xffu);
	clean.sq = (ob_rx_status_u8)(clean.phy_rx_status_1 >> 8);

	/*
	 * Decryption status, antenna and JSSI are layout-corroborated but no
	 * pinned instruction in this object consumes them; they are never
	 * reported as verified.
	 */
	clean.corroborated |= OB_RX_FACT_DECRYPT | OB_RX_FACT_ANTENNA |
		OB_RX_FACT_JSSI;

	/*
	 * The extended TSF word is only meaningful in the 40-byte prefix. Both
	 * halves are consumed by wlc_recv_prep_event_rx_frame_data (0x2ea46
	 * reads +0x18 as u32, 0x2ea49 reads +0x14 as u16) to build one 48-bit
	 * timestamp, so the pair is a verified read once both are present.
	 */
	if (prefix_len >= OB_RX_STATUS_OFF_TSF_HIGH + 4u) {
		clean.rx_tsf_high = get_le32(prefix + OB_RX_STATUS_OFF_TSF_HIGH);
		clean.verified |= OB_RX_FACT_TSF_HIGH;
		clean.tsf48 = ob_rx_tsf_merge(clean.rx_tsf_low, clean.rx_tsf_high);
		clean.verified |= OB_RX_FACT_TSF48;
	}

	*out = clean;
	return 0;
}

ob_rx_status_u64 ob_rx_tsf_merge(ob_rx_status_u16 tsf_low,
				  ob_rx_status_u32 tsf_high)
{
	ob_rx_status_u64 merged;

	merged = ((ob_rx_status_u64)(tsf_high & 0xffff0000u)) | tsf_low;
	if ((ob_rx_status_u32)tsf_low > (tsf_high & 0xffffu))
		merged -= 0x10000u;
	return merged & 0xffffffffffffull;
}

int ob_rx_status_tsf48(struct ob_rx_status *status, ob_rx_status_u64 *tsf)
{
	if (!status || !tsf)
		return -EINVAL;
	if ((status->verified & (OB_RX_FACT_TSF_LOW | OB_RX_FACT_TSF_HIGH)) !=
	    (OB_RX_FACT_TSF_LOW | OB_RX_FACT_TSF_HIGH))
		return -EOPNOTSUPP;

	status->tsf48 = ob_rx_tsf_merge(status->rx_tsf_low,
					status->rx_tsf_high);
	status->verified |= OB_RX_FACT_TSF48;
	*tsf = status->tsf48;
	return 0;
}

int ob_rx_len_plausible(ob_rx_status_u32 pulled_len,
			ob_rx_status_u8 extra_tail)
{
	ob_rx_status_u32 min_len = OB_RX_MIN_MPDU_LEN;

	if (extra_tail)
		min_len += OB_RX_LEN_EXTRA_TAIL;
	if (pulled_len < OB_RX_LEN_GATE_SUB)
		return 0;
	return (pulled_len - OB_RX_LEN_GATE_SUB) >= min_len;
}

int ob_rx_chan_decode(ob_rx_status_u32 d11_core_revision,
		      ob_rx_status_u16 raw_chan,
		      struct ob_rx_chan *out)
{
	struct ob_rx_chan clean;
	ob_rx_status_u8 chan;
	ob_rx_status_u8 band_5g;

	if (!out)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*out = clean;

	clean.raw = raw_chan;
	if (d11_core_revision > OB_RX_D11_ACPHY_COREREV_MIN - 1u) {
		chan = (ob_rx_status_u8)(raw_chan & OB_RX_CHAN_NORM_CHAN_MASK);
		band_5g = !!(raw_chan & OB_RX_CHAN_BAND_MASK);
		clean.bw40_known = 0;
	} else {
		chan = (ob_rx_status_u8)
			((raw_chan & OB_RX_CHAN_LEGACY_ID_MASK) >>
			 OB_RX_CHAN_LEGACY_ID_SHIFT);
		band_5g = !!(raw_chan & OB_RX_CHAN_LEGACY_BAND_5G);
		clean.bw40 = !!(raw_chan & OB_RX_CHAN_LEGACY_40MHZ);
		clean.bw40_known = 1;
	}

	if (band_5g) {
		if (chan < OB_RX_CHAN_MIN_5G || chan > OB_RX_CHAN_MAX_5G)
			return -ERANGE;
	} else {
		if (chan < OB_RX_CHAN_MIN_2G || chan > OB_RX_CHAN_MAX_2G)
			return -ERANGE;
	}

	clean.chan = chan;
	clean.band_5g = band_5g;
	*out = clean;
	return 0;
}

int ob_rx_frame_bounds(const struct ob_rx_status *status,
		       struct ob_rx_frame_bounds *bounds)
{
	struct ob_rx_frame_bounds clean;
	ob_rx_status_u32 total;

	if (!bounds)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*bounds = clean;
	if (!status)
		return -EINVAL;
	if (!(status->verified & OB_RX_FACT_FRAME_SIZE))
		return -EOPNOTSUPP;

	clean.rx_frame_size = status->rx_frame_size;
	clean.pad_len = status->pad_present ? 2u : 0u;
	clean.phy_header_len = OB_RX_PLCP_LEN;
	clean.fcs_len = OB_RX_FCS_LEN;
	total = clean.pad_len + clean.phy_header_len + clean.fcs_len;
	if (clean.rx_frame_size < total)
		return -ERANGE;
	clean.mpdu_len = clean.rx_frame_size - total;
	*bounds = clean;
	return 0;
}

int ob_rx_rate_decode(const struct ob_rx_status *status,
		      const ob_rx_status_u8 *plcp,
		      ob_rx_status_size plcp_len,
		      struct ob_rx_rate *rate)
{
	/*
	 * .rodata 0x2cdbe0, indexed by plcp[0] & 7, units of 500 kbps. The
	 * vendor table is 48/24/12/6/54/36/18/9 Mbps, not ascending order.
	 */
	static const ob_rx_status_u16 ofdm_rate_lookup[8] = {
		96, 48, 24, 12, 108, 72, 36, 18,
	};
	struct ob_rx_rate clean;
	ob_rx_status_u8 signal;

	if (!rate)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*rate = clean;
	if (!status || !plcp || plcp_len < 1)
		return -EINVAL;
	/* A modulation field without its decode fact is not evidence. */
	if (!(status->verified & OB_RX_FACT_PHY_TYPE))
		return -EOPNOTSUPP;

	signal = plcp[0];

	switch (status->phy_type) {
	case OB_RX_PHY0_TYPE_CCK:
		/*
		 * wlc_recv_compute_rspec divides the CCK PLCP signal byte by
		 * five. Only the four standard 802.11b signal values decode.
		 */
		if (signal != 0x0a && signal != 0x14 &&
		    signal != 0x37 && signal != 0x6e)
			return -EOPNOTSUPP;
		clean.kind = OB_RX_RATE_CCK;
		clean.legacy_rate_500kbps =
			(ob_rx_status_u16)(signal / 5u);
		break;
	case OB_RX_PHY0_TYPE_OFDM:
		clean.kind = OB_RX_RATE_OFDM;
		clean.legacy_rate_500kbps = ofdm_rate_lookup[signal & 7u];
		break;
	case OB_RX_PHY0_TYPE_HT:
		if (plcp_len < 4)
			return -EINVAL;
		clean.kind = OB_RX_RATE_HT;
		clean.mcs = (ob_rx_status_u8)(signal & 0x7fu);
		clean.bw40 = !!(signal & 0x80u);
		clean.sgi = !!(plcp[3] & 0x80u);
		break;
	case OB_RX_PHY0_TYPE_VHT:
		/*
		 * wlc_vht_get_rspec_from_plcp is not decoded here and OpenBRCM
		 * advertises no VHT capability: refuse rather than guess. The
		 * output stays zeroed, matching the other failure paths.
		 */
		return -EOPNOTSUPP;
	default:
		return -EOPNOTSUPP;
	}

	*rate = clean;
	return 0;
}

int ob_rx_rate_to_mac80211(const struct ob_rx_rate *rate,
			   ob_rx_status_u8 band_5g,
			   struct ob_rx_rate_80211 *out)
{
	/*
	 * 2.4 GHz legacy index order is the mac80211 bitrate table order.
	 * 5 GHz exposes only the OFDM subset, which starts four entries later.
	 */
	static const ob_rx_status_u16 legacy_2g[12] = {
		2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108,
	};
	static const ob_rx_status_u16 legacy_5g[8] = {
		12, 18, 24, 36, 48, 72, 96, 108,
	};
	struct ob_rx_rate_80211 clean;
	ob_rx_status_u32 i;

	if (!out)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*out = clean;
	if (!rate)
		return -EINVAL;

	switch (rate->kind) {
	case OB_RX_RATE_HT:
		/*
		 * mac80211's HT rate_idx is the 802.11n MCS index 0..31. The
		 * vendor field is seven bits wide, so values above 31 cannot be
		 * represented; refuse them instead of truncating.
		 */
		if (rate->mcs >= 32u)
			return -EOPNOTSUPP;
		clean.is_ht = 1;
		clean.rate_idx = rate->mcs;
		clean.bw40 = rate->bw40;
		clean.sgi = rate->sgi;
		break;
	case OB_RX_RATE_CCK:
		if (band_5g)
			return -EOPNOTSUPP;	/* CCK is 2.4 GHz only */
		for (i = 0; i < 4u; i++) {
			if (rate->legacy_rate_500kbps == legacy_2g[i]) {
				clean.rate_idx = (ob_rx_status_u16)i;
				*out = clean;
				return 0;
			}
		}
		return -EOPNOTSUPP;
	case OB_RX_RATE_OFDM:
		for (i = 0; i < 8u; i++) {
			ob_rx_status_u16 base = band_5g ?
				legacy_5g[i] : legacy_2g[i + 4u];

			if (rate->legacy_rate_500kbps == base) {
				clean.rate_idx = band_5g ?
					(ob_rx_status_u16)i :
					(ob_rx_status_u16)(i + 4u);
				*out = clean;
				return 0;
			}
		}
		return -EOPNOTSUPP;
	case OB_RX_RATE_VHT:
		return -EOPNOTSUPP;
	case OB_RX_RATE_UNKNOWN:
	default:
		return -EOPNOTSUPP;
	}

	*out = clean;
	return 0;
}
