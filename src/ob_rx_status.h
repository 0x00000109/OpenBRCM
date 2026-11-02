/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — BCM4352 / D11 rev42 receive-status decoding (Phase 36).
 *
 * Every field here is tied to an instruction in the pinned vendor object
 * (wlc_hybrid.o_shipped, sha256 352a6e34...4743) or to a documented sibling
 * function that consumes it. The three confidence tiers are kept apart:
 *
 *   VERIFIED     a pinned vendor instruction reads/tests this exact field and
 *                the value changes a decision in that instruction;
 *   CORROBORATED the field belongs to the same header and its position is
 *                corroborated by a sibling function or the Broadcom-derived
 *                d11.h layout, but no pinned instruction consumes it here;
 *   UNKNOWN      not decoded. Never fabricated.
 *
 * Channel: `wlc_recv` compares the low byte of RxChan with 14 (0x4c56d) to
 * decide "5 GHz" and tests the top two bits as a band selector (0x4c867,
 * 0x4d31d). `wlc_bmac_recv` normalizes the legacy hardware encoding
 * (channel<<3, 0x800=5 GHz, 0x1000=40 MHz) into this form before the same
 * consumer runs (0x67e37..0x67e80), which fixes the meaning of the low byte
 * and of the 0xc000 band bits. `ob_rx_chan_decode()` exposes that decode with
 * explicit provenance; it does not invent a frequency or a control-channel
 * sideband.
 */
#ifndef _OB_RX_STATUS_H_
#define _OB_RX_STATUS_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_rx_status_u8;
typedef u16 ob_rx_status_u16;
typedef u32 ob_rx_status_u32;
typedef u64 ob_rx_status_u64;
typedef unsigned long ob_rx_status_size;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ob_rx_status_u8;
typedef uint16_t ob_rx_status_u16;
typedef uint32_t ob_rx_status_u32;
typedef uint64_t ob_rx_status_u64;
typedef size_t ob_rx_status_size;
#endif

/* Classic d11rxhdr length; the rev42 DMA prefix is longer (40 bytes). */
#define OB_RX_STATUS_BASE_LEN		24u
#define OB_RX_STATUS_REV42_PREFIX_LEN	40u

/* Header byte offsets used below (all little-endian in the DMA buffer). */
#define OB_RX_STATUS_OFF_FRAME_SIZE	0x00u
#define OB_RX_STATUS_OFF_PHY_0		0x04u
#define OB_RX_STATUS_OFF_PHY_1		0x06u
#define OB_RX_STATUS_OFF_STATUS1	0x10u
#define OB_RX_STATUS_OFF_STATUS2	0x12u
#define OB_RX_STATUS_OFF_TSF_LOW	0x14u
#define OB_RX_STATUS_OFF_CHAN		0x16u
#define OB_RX_STATUS_OFF_TSF_HIGH	0x18u

/* PhyRxStatus_0 bits. */
#define OB_RX_PHY0_TYPE_MASK		0x0003u	/* wlc_recv_compute_rspec */
#define OB_RX_PHY0_TYPE_CCK		0u
#define OB_RX_PHY0_TYPE_OFDM		1u
#define OB_RX_PHY0_TYPE_HT		2u
#define OB_RX_PHY0_TYPE_VHT		3u
#define OB_RX_PHY0_UNSUP_RATE		0x0010u	/* wlc_recv drop test */
#define OB_RX_PHY0_ANT_UPSUBBAND	0x0020u
#define OB_RX_PHY0_SHORT_PREAMBLE	0x0080u
#define OB_RX_PHY0_PLCP_VIOLATION	0x0100u	/* wlc_recv drop test */
#define OB_RX_PHY0_PLCP_HDR_FAIL	0x0200u	/* wlc_recv drop test */
#define OB_RX_PHY0_DROP_MASK		0x0310u	/* exact wlc_recv mask */

/* RxStatus1 bits. */
#define OB_RX_STATUS1_FCS_ERR		0x0001u	/* wlc_recv drop test */
#define OB_RX_STATUS1_RESP_FRAME	0x0002u
#define OB_RX_STATUS1_PAD_PRESENT	0x0004u	/* wlc_recv pulls two bytes */
#define OB_RX_STATUS1_DEC_ATTEMPT	0x0008u
#define OB_RX_STATUS1_DEC_ERR		0x0010u
#define OB_RX_STATUS1_SEC_INDEX_MASK	0x07e0u
#define OB_RX_STATUS1_SEC_INDEX_SHIFT	5u
#define OB_RX_STATUS1_BCN_SENT		0x8000u

/* RxStatus2 bits. */
#define OB_RX_STATUS2_AMSDU		0x0001u	/* wlc_recv A-MSDU branch */
#define OB_RX_STATUS2_AGGTYPE_MASK	0x0006u	/* wlc_recv >> 1 */
#define OB_RX_STATUS2_AGGTYPE_SHIFT	1u
#define OB_RX_STATUS2_PHYST_VALID	0x0100u
#define OB_RX_STATUS2_RXANT_MASK	0x3000u
#define OB_RX_STATUS2_RXANT_SHIFT	12u

/*
 * RxChan (+0x16). Two encodings reach the consumer:
 *   - normalized form, emitted for rev > 0x27 (BCM4352 rev42): the low byte is
 *     the channel number and bits 15:14 are set for 5 GHz;
 *   - legacy hardware form, produced for rev <= 0x27: bits 10:3 are the
 *     channel, bit 11 is 5 GHz and bit 12 is 40 MHz. wlc_bmac_recv converts
 *     it (0x67e37..0x67e80) before wlc_recv consumes the same fields.
 */
#define OB_RX_CHAN_BAND_MASK		0xc000u	/* normalized form */
#define OB_RX_CHAN_NORM_CHAN_MASK	0x00ffu
#define OB_RX_CHAN_LEGACY_BAND_5G	0x0800u
#define OB_RX_CHAN_LEGACY_40MHZ		0x1000u
#define OB_RX_CHAN_LEGACY_ID_MASK	0x07f8u
#define OB_RX_CHAN_LEGACY_ID_SHIFT	3u
#define OB_RX_CHAN_MIN_2G		1u
#define OB_RX_CHAN_MAX_2G		14u
#define OB_RX_CHAN_MIN_5G		32u
#define OB_RX_CHAN_MAX_5G		196u

/* rev > 0x27 selects the normalized RxChan path in wlc_bmac_recv. */
#define OB_RX_D11_ACPHY_COREREV_MIN	40u

/*
 * FCS bytes counted inside RxFrameSize and removed by the host before the
 * frame is handed up. Two disjoint paths trim exactly four tail bytes:
 *
 *   wlc_recvdata 0x440a1..0x440aa  osl_pktsetlen(pkt, osl_pktlen(pkt) - 4)
 *   wlc_recv     0x4c733, 0x4c746  osl_pktpull(pkt, 6) then
 *                                  osl_pktsetlen(pkt, (pkttake-0xa))
 *
 * osl_pktsetlen() is __skb_trim() (old/amd64/src/shared/linux_osl.c), so the
 * second form removes 6 bytes at the head and 4 at the tail. Neither path
 * tests a strip-enable bit: the D11 does not remove the FCS for rev42.
 */
#define OB_RX_FCS_LEN			4u

/*
 * Minimum-length gate of the single-MPDU path (wlc_recv 0x4c482..0x4c4af):
 *
 *   len = pkttake(pkt, 0)          ; 0x4c47d, result in eax
 *   len -= 0xa                     ; 0x4c488: 6-byte PHY header + 4-byte FCS
 *   if (type == 0)                 ; 0x4c4a0 test %r15w,%r15w
 *           reject if len < 0x18 + 4*flag   ; 0x4c49d, 0x4c4a6, 0x4c4a9
 *
 * r15w is the frame-control type ((fc >> 2) & 3) extracted at
 * 0x4c446/0x4c44a, not the RxStatus2 aggregation type. The 0xa subtraction
 * above removes the PHY header and FCS from the comparison; the physical
 * tail trim runs later at 0x4c746.
 *
 * `flag` is the byte the vendor stores at -0xc0 (0x4c477) and it is only ever
 * 1 for an HT frame (PhyRxStatus_0 type 2) whose u16 has bit 15 set and whose
 * bits 3:2 are zero (0x4c441..0x4c46c). The same flag adds 4 to this gate
 * (0x4c49d/0x4c4a6) and makes the management delivery path pull 4 more bytes
 * after the 24-byte header (0x4ca3c/0x4ca55). The analogous pull at
 * 0x4c7df/0x4c7f8 is unreachable: that branch requires type 1, while flag 1
 * requires type 0. The extra 4 bytes therefore follow the management MAC
 * header and are not the FCS. No instruction names or reads the field, so
 * HT Control remains CORROBORATED here, not VERIFIED.
 */
#define OB_RX_MIN_MPDU_LEN		24u
#define OB_RX_LEN_GATE_SUB		0x0au
#define OB_RX_LEN_EXTRA_TAIL		4u

/* Facts carried by a decoded status. */
#define OB_RX_FACT_FRAME_SIZE		(1u << 0)
#define OB_RX_FACT_PHY_TYPE		(1u << 1)
#define OB_RX_FACT_FCS_ERROR		(1u << 2)
#define OB_RX_FACT_PAD_PRESENT		(1u << 3)
#define OB_RX_FACT_PHY_DROP		(1u << 4)
#define OB_RX_FACT_AMSDU		(1u << 5)
#define OB_RX_FACT_AGGTYPE		(1u << 6)
#define OB_RX_FACT_TSF_LOW		(1u << 7)
#define OB_RX_FACT_CHAN_RAW		(1u << 8)
#define OB_RX_FACT_BAND			(1u << 9)
#define OB_RX_FACT_TSF_HIGH		(1u << 10)
#define OB_RX_FACT_SEC_INDEX		(1u << 11)
#define OB_RX_FACT_DECRYPT		(1u << 12)
#define OB_RX_FACT_ANTENNA		(1u << 13)
#define OB_RX_FACT_JSSI			(1u << 14)
#define OB_RX_FACT_TSF48		(1u << 15)

enum ob_rx_confidence {
	OB_RX_CONF_UNKNOWN = 0,
	OB_RX_CONF_CORROBORATED = 1,
	OB_RX_CONF_VERIFIED = 2,
};

struct ob_rx_status {
	ob_rx_status_u32 observed_len;	/* bytes supplied to the decoder */
	ob_rx_status_u32 verified;	/* OB_RX_FACT_* proven by wlc_recv* */
	ob_rx_status_u32 corroborated;	/* OB_RX_FACT_* layout-corroborated */

	ob_rx_status_u16 rx_frame_size;
	ob_rx_status_u16 phy_rx_status_0;
	ob_rx_status_u16 phy_rx_status_1;
	ob_rx_status_u16 rx_status1;
	ob_rx_status_u16 rx_status2;
	ob_rx_status_u16 rx_tsf_low;
	ob_rx_status_u16 rx_chan;
	ob_rx_status_u32 rx_tsf_high;
	ob_rx_status_u64 tsf48;	/* merged from +0x14/+0x18 once decoded */

	ob_rx_status_u8 phy_type;	/* OB_RX_PHY0_TYPE_* */
	ob_rx_status_u8 fcs_error;
	ob_rx_status_u8 pad_present;
	ob_rx_status_u8 dec_attempted;
	ob_rx_status_u8 dec_error;
	ob_rx_status_u16 sec_index;
	ob_rx_status_u8 beacon_sent;
	ob_rx_status_u8 amsdu;
	ob_rx_status_u8 agg_type;
	ob_rx_status_u8 phy_status_valid;
	ob_rx_status_u8 rx_antenna;
	ob_rx_status_u8 band_5g;	/* derived from OB_RX_CHAN_BAND_MASK */
	/* Channel number is decoded separately by ob_rx_chan_decode(). */
	ob_rx_status_u8 jssi;
	ob_rx_status_u8 sq;
};

struct ob_rx_chan {
	ob_rx_status_u16 raw;
	ob_rx_status_u8 chan;
	ob_rx_status_u8 band_5g;
	ob_rx_status_u8 bw40;		/* legacy form only */
	ob_rx_status_u8 bw40_known;
};

/*
 * Decode RxChan for a given D11 core revision. Returns 0 when the value names
 * an existing 2.4 GHz (1..14) or 5 GHz (32..196) channel; -ERANGE when the
 * number does not exist or contradicts the band bit; -EINVAL on a NULL
 * argument. @out is always zeroed first, so a failure never yields a
 * half-decoded channel. 40 MHz width is only established for the legacy
 * encoding; in the normalized form it stays UNKNOWN (bw40_known == 0).
 */
int ob_rx_chan_decode(ob_rx_status_u32 d11_core_revision,
		      ob_rx_status_u16 raw_chan,
		      struct ob_rx_chan *out);

/*
 * Decode a rev42 DMA prefix. @prefix_len may be 24..40; fields beyond the
 * supplied bytes are left zero and their facts are not set. A shorter buffer
 * returns -EINVAL and leaves @out zeroed.
 */
int ob_rx_status_decode(const ob_rx_status_u8 *prefix,
			ob_rx_status_size prefix_len,
			struct ob_rx_status *out);

/*
 * Merge the split TSF the vendor reads at prefix +0x14 (low 16 bits) and
 * +0x18 (upper 32 bits): wlc_recv_prep_event_rx_frame_data 0x2ea46..0x2ea62
 * forms (hi & 0xffff0000) | lo and subtracts 0x10000 when lo is greater than
 * the low half of `hi`, i.e. when the high word already advanced past a wrap.
 * The result is a 48-bit counter in bits 47:0; bits above are always zero.
 * Both raw words are taken as read from the little-endian prefix.
 */
ob_rx_status_u64 ob_rx_tsf_merge(ob_rx_status_u16 tsf_low,
				  ob_rx_status_u32 tsf_high);

/*
 * Merge the TSF of an already decoded status and set OB_RX_FACT_TSF48. Needs
 * OB_RX_FACT_TSF_LOW and OB_RX_FACT_TSF_HIGH (a 40-byte prefix); returns
 * -EOPNOTSUPP otherwise and leaves @tsf untouched.
 */
int ob_rx_status_tsf48(struct ob_rx_status *status, ob_rx_status_u64 *tsf);

/*
 * Minimum-length gate of wlc_recv 0x4c482..0x4c4af, evaluated on the length
 * the packet has *after* the caller's pulls (the vendor's pkttake(pkt, 0)
 * result). Returns 1 when the frame passes, 0 when the vendor path would
 * reject it; a length below OB_RX_LEN_GATE_SUB always fails. `extra_tail` is
 * that function's local boolean (0 or 1); any non-zero value counts as 1,
 * matching the vendor's sign-extended and-mask. Despite the historical name,
 * it is a conditional header-field allowance after the MAC header, not a tail
 * allowance. This helper covers the management form only.
 */
int ob_rx_len_plausible(ob_rx_status_u32 pulled_len,
			ob_rx_status_u8 extra_tail);

/* Frame boundaries derived from RxFrameSize and the proven layout. */
struct ob_rx_frame_bounds {
	ob_rx_status_u32 rx_frame_size;
	ob_rx_status_u32 pad_len;	/* 2 when RxStatus1.PAD */
	ob_rx_status_u32 phy_header_len;	/* 6, proven by wlc_recvdata */
	ob_rx_status_u32 fcs_len;	/* 4, stripped by wlc_recvdata */
	ob_rx_status_u32 mpdu_len;	/* size - pad - 6 - 4 */
};

/*
 * Compute the 802.11 MPDU window inside the received frame. Requires
 * OB_RX_FACT_FRAME_SIZE and a size large enough for pad + PHY header + FCS.
 * -EOPNOTSUPP when the status is missing the proven length fact; -ERANGE when
 * the length cannot describe a complete MPDU.
 */
int ob_rx_frame_bounds(const struct ob_rx_status *status,
		       struct ob_rx_frame_bounds *bounds);

/*
 * Receive rate recovered from the PLCP bytes (vendor wlc_recv_compute_rspec
 * 0x168eac). The rate is not carried in the D11 prefix: wlc_recvdata obtains
 * the PHY header with osl_pktdata() (0x43d12) and calls compute_rspec with the
 * prefix and that pointer (0x43d20). wlc_recv makes the same call at 0x4cee7.
 * PhyRxStatus_0 at prefix +0x04 supplies only the modulation type (mask 0x3).
 */
enum ob_rx_rate_kind {
	OB_RX_RATE_UNKNOWN = 0,
	OB_RX_RATE_CCK = 1,
	OB_RX_RATE_OFDM = 2,
	OB_RX_RATE_HT = 3,
	OB_RX_RATE_VHT = 4,
};

struct ob_rx_rate {
	enum ob_rx_rate_kind kind;
	ob_rx_status_u16 legacy_rate_500kbps;	/* CCK and OFDM */
	ob_rx_status_u8 mcs;			/* HT: plcp[0] & 0x7f */
	ob_rx_status_u8 bw40;			/* HT: plcp[0] & 0x80 */
	ob_rx_status_u8 sgi;			/* HT: plcp[3] & 0x80 */
};

#define OB_RX_PLCP_LEN 6u

/*
 * Decode the 6-byte PHY header that follows the D11 RX prefix (and the
 * optional two pad bytes). `plcp` points at the PHY header; `plcp_len` must be
 * at least 4 for HT (the vendor reads bytes 0 and 3).
 *
 * OFDM uses the exact vendor table at .rodata 0x2cdbe0:
 *   index 0..7 -> 48, 24, 12, 6, 54, 36, 18, 9 Mbps.
 * CCK accepts only the four standard 802.11b signal bytes (0x0a, 0x14, 0x37,
 * 0x6e); the vendor divides any byte by five, but an unknown byte is refused
 * here rather than becoming a fabricated rate. VHT remains gated.
 */
int ob_rx_rate_decode(const struct ob_rx_status *status,
		      const ob_rx_status_u8 *plcp,
		      ob_rx_status_size plcp_len,
		      struct ob_rx_rate *rate);

/*
 * mac80211 rate representation (legacy index table is band dependent). For
 * HT, rate_idx is the MCS index and is limited to 0..31; the vendor field is
 * seven bits wide, so larger values are refused by the mapper.
 */
struct ob_rx_rate_80211 {
	ob_rx_status_u16 rate_idx;
	ob_rx_status_u8 is_ht;
	ob_rx_status_u8 bw40;
	ob_rx_status_u8 sgi;
};

/*
 * Map a decoded rate to a mac80211 rate index for the band implied by
 * status->band_5g. 5 GHz legacy rates use the OFDM-only index space.
 */
int ob_rx_rate_to_mac80211(const struct ob_rx_rate *rate,
			   ob_rx_status_u8 band_5g,
			   struct ob_rx_rate_80211 *out);

#endif /* _OB_RX_STATUS_H_ */
