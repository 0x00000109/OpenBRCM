// SPDX-License-Identifier: GPL-2.0-only
/* Host-testable RX frame envelope and trusted-metadata handoff. */
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#include "ob_rx_sw.h"
#include "ob_rx_status.h"

#define OB_RX_SW_FC_MIN_LEN 2u
#define OB_RX_SW_MGMT_MIN_LEN 24u
#define OB_RX_SW_DATA_MIN_LEN 24u
#define OB_RX_SW_CTRL_ACK_CTS_MIN_LEN 10u

/*
 * Minimum delivered MPDU length. This is the Linux ieee80211_hdrlen()
 * contract, not the vendor's internal parser contract: 24 base, +6 for
 * Address4, +2 for QoS control, and +4 for the Order-indicated field on
 * management and QoS data. mac80211 applies the same function to the skb
 * this layer hands over, so the two must agree byte for byte.
 *
 * The vendor's wlc_recv/wlc_recvdata predicates additionally require HT PHY
 * because the vendor parser never admits a legacy Order=1 frame. No trusted
 * PHY fact exists at this boundary, and absence of OB_RX_SW_META_RATE_HT is
 * not proof of a legacy PHY. A legacy Order=1 frame is malformed and
 * mac80211 still computes the four-byte field, so the shorter form must be
 * rejected rather than accepted from a guessed PHY type.
 */
static int ob_rx_sw_minimum_frame_len(const ob_rx_sw_u8 *frame,
				      ob_rx_sw_size *minimum)
{
	ob_rx_sw_u16 fc;
	ob_rx_sw_u8 type, subtype;
	ob_rx_sw_size len;

	if (!frame || !minimum)
		return -EINVAL;
	fc = (ob_rx_sw_u16)frame[0] | ((ob_rx_sw_u16)frame[1] << 8);
	if (fc & 0x3u) /* only protocol version zero is admitted */
		return -EPROTONOSUPPORT;

	type = (fc >> 2) & 0x3u;
	subtype = (fc >> 4) & 0xfu;
	switch (type) {
	case 0: /* management */
		len = OB_RX_SW_MGMT_MIN_LEN;
		if (fc & (1u << 15))
			len += 4; /* conditional 4-byte header field */
		break;
	case 1: /* control: admit only the fixed-size ACK and CTS forms */
		if (subtype != 12 && subtype != 13)
			return -EOPNOTSUPP;
		len = OB_RX_SW_CTRL_ACK_CTS_MIN_LEN;
		break;
	case 2: /* data */
		len = OB_RX_SW_DATA_MIN_LEN;
		if ((fc & (1u << 8)) && (fc & (1u << 9)))
			len += 6; /* address 4 */
		if (subtype & 0x8u)
			len += 2; /* QoS control */
		if ((subtype & 0x8u) && (fc & (1u << 15)))
			len += 4; /* conditional 4-byte header field */
		break;
	default: /* extension frames require a separate format contract */
		return -EOPNOTSUPP;
	}
	*minimum = len;
	return 0;
}

int ob_rx_sw_validate(const ob_rx_sw_u8 *frame,
		      ob_rx_sw_size available_len,
		      const struct ob_rx_sw_metadata *metadata,
		      struct ob_rx_sw_status *status)
{
	struct ob_rx_sw_status clean = { 0 };
	ob_rx_sw_size minimum;
	int ret;

	if (!frame || !metadata || !status)
		return -EINVAL;
	*status = clean;
	if (metadata->provenance != OB_RX_SW_PROVENANCE_VALIDATED)
		return -ENODATA;
	if (metadata->valid_fields & ~OB_RX_SW_META_KNOWN_MASK)
		return -EINVAL;
	if (!(metadata->valid_fields & OB_RX_SW_META_FCS_LEN))
		return -ENODATA;
	if (metadata->fcs_len != 0 && metadata->fcs_len != 4)
		return -EINVAL;
	if (metadata->complete != 1 || !available_len ||
	    metadata->declared_len != available_len)
		return -EMSGSIZE;
	/* skb->len is an unsigned int on supported Linux ABIs. */
	if (available_len > 0xffffffffUL)
		return -EMSGSIZE;
	if (available_len < OB_RX_SW_FC_MIN_LEN)
		return -EMSGSIZE;

	ret = ob_rx_sw_minimum_frame_len(frame, &minimum);
	if (ret)
		return ret;
	minimum += metadata->fcs_len;
	if (available_len < minimum)
		return -EMSGSIZE;
	if ((metadata->valid_fields & OB_RX_SW_META_CHANNEL) &&
	    !((metadata->channel >= OB_RX_CHAN_MIN_2G &&
	       metadata->channel <= OB_RX_CHAN_MAX_2G) ||
	      (metadata->channel >= OB_RX_CHAN_MIN_5G &&
	       metadata->channel <= OB_RX_CHAN_MAX_5G)))
		return -ERANGE;
	if ((metadata->valid_fields & OB_RX_SW_META_RATE_HT) &&
	    !(metadata->valid_fields & OB_RX_SW_META_RATE))
		return -EINVAL;
	if (metadata->valid_fields & OB_RX_SW_META_RATE) {
		if (metadata->valid_fields & OB_RX_SW_META_RATE_HT) {
			/* The mac80211 HT rate index is an MCS value 0..31. */
			if (metadata->rate_index >= 32)
				return -ERANGE;
			if (metadata->rate_bw40 > 1 || metadata->rate_sgi > 1)
				return -EINVAL;
		} else {
			if (metadata->rate_index >= 12)
				return -ERANGE;
			/* Width/GI flags are meaningful only for an HT rate. */
			if (metadata->rate_bw40 || metadata->rate_sgi)
				return -EINVAL;
		}
	}
	if ((metadata->valid_fields & OB_RX_SW_META_SIGNAL) &&
	    (metadata->signal_dbm < -128 || metadata->signal_dbm > 127))
		return -ERANGE;

	clean.valid_fields = metadata->valid_fields;
	if (metadata->valid_fields & OB_RX_SW_META_CHANNEL)
		clean.channel = metadata->channel;
	if (metadata->valid_fields & OB_RX_SW_META_RATE) {
		clean.rate_index = metadata->rate_index;
		clean.rate_is_ht =
			!!(metadata->valid_fields & OB_RX_SW_META_RATE_HT);
		if (clean.rate_is_ht) {
			clean.rate_bw40 = metadata->rate_bw40;
			clean.rate_sgi = metadata->rate_sgi;
		}
	}
	if (metadata->valid_fields & OB_RX_SW_META_SIGNAL)
		clean.signal_dbm = metadata->signal_dbm;
	if (metadata->valid_fields & OB_RX_SW_META_FCS_LEN)
		clean.fcs_len = metadata->fcs_len;
	*status = clean;
	return 0;
}

int ob_rx_sw_deliver(const ob_rx_sw_u8 *frame,
		     ob_rx_sw_size available_len,
		     const struct ob_rx_sw_metadata *metadata,
		     ob_rx_sw_sink_fn sink, void *sink_ctx)
{
	struct ob_rx_sw_status status;
	int ret;

	if (!sink)
		return -EINVAL;
	ret = ob_rx_sw_validate(frame, available_len, metadata, &status);
	if (ret)
		return ret;
	/* One synchronous invocation; sink borrows the input and may copy it. */
	return sink(sink_ctx, frame, available_len, &status);
}
