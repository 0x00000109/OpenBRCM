/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 rev42 transmit-status record decoding (Phase 36).
 *
 * The pinned vendor object reads four consecutive status words for rev > 0x27
 * (wlc_bmac_txstatus 0x642c4..0x644cf):
 *
 *   0x170  frmtxstatus    bit0 valid; bits 15:0 status; bits 31:16 frame ID
 *   0x174  frmtxstatus2   bits 15:0 sequence; bits 23:16 PTX
 *   0x178  aux0           four independent 8-bit attempt counters
 *   0x17c  aux1           (low and high halves both summed)
 *
 * It then fills a local record whose fields are consumed by wlc_dotxstatus
 * (0x3b7f4) and mapped to the historical tx_status layout by
 * wlc_txs_alias_to_old_fmt (0x2546e). The consumer mapping is the independent
 * cross-check for the ACK/flag positions:
 *
 *   record +5  intermediate      -> old status bit 6 (0x40)
 *   record +6  PM indicated      -> old status bit 7 (0x80)
 *   record +8  suppress code     -> old status bits 4:2
 *   record +9  ACK               -> old status bit 1 (TX_STATUS_ACK_RCV)
 *   record +12 attempt count     -> old status bits 15:12 (retry count)
 *
 * The ACK predicate is therefore decoded even though it is not a single
 * status bit for this revision:
 *
 *   acked = ((status & 0x7f00) >> 8) > 1 || (status & 0x8000)
 *
 * This module performs no MMIO, owns no skb and has no production caller.
 */
#ifndef _OB_TX_STATUS_DECODE_H_
#define _OB_TX_STATUS_DECODE_H_

#include "ob_tx_status.h"

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_tx_dec_u8;
typedef u16 ob_tx_dec_u16;
typedef u32 ob_tx_dec_u32;
#else
#include <stdint.h>
typedef uint8_t ob_tx_dec_u8;
typedef uint16_t ob_tx_dec_u16;
typedef uint32_t ob_tx_dec_u32;
#endif

/* Proven register/record bit definitions for rev42. */
#define OB_TX_DEC_FRMTXSTATUS_VALID	0x00000001u
#define OB_TX_DEC_STATUS_MASK		0x0000ffffu
#define OB_TX_DEC_FID_SHIFT		16
#define OB_TX_DEC_FID_MASK		0xffff0000u
#define OB_TX_DEC_SLOT_MASK		7u
#define OB_TX_DEC_INTERMEDIATE		(1u << 2)
#define OB_TX_DEC_PM_INDICATED		(1u << 3)
#define OB_TX_DEC_SUPPRESS_MASK		0x000fu
#define OB_TX_DEC_SUPPRESS_SHIFT	4
#define OB_TX_DEC_ACK_FIELD_MASK	0x7f00u
#define OB_TX_DEC_ACK_FIELD_SHIFT	8
#define OB_TX_DEC_ACK_FIELD_MIN		2u
#define OB_TX_DEC_ACK_TOP_BIT		0x8000u
#define OB_TX_DEC_SEQ_MASK		0x0000ffffu
#define OB_TX_DEC_PTX_MASK		0x00ff0000u
#define OB_TX_DEC_PTX_SHIFT		16

struct ob_tx_status_regs {
	ob_tx_dec_u32 frmtxstatus;
	ob_tx_dec_u32 frmtxstatus2;
	/* Only needed for the attempt counter; ignored when absent. */
	ob_tx_dec_u32 aux0;
	ob_tx_dec_u32 aux1;
	ob_tx_dec_u8 have_aux;
};

struct ob_tx_status_decoded {
	ob_tx_dec_u8 valid;
	ob_tx_dec_u16 frame_id;
	ob_tx_dec_u8 slot;
	ob_tx_dec_u16 status_word;
	ob_tx_dec_u8 acked;
	ob_tx_dec_u8 intermediate;
	ob_tx_dec_u8 pm_indicated;
	ob_tx_dec_u8 suppress_code;
	ob_tx_dec_u8 attempts_known;
	ob_tx_dec_u16 attempts;
	ob_tx_dec_u16 sequence;
	ob_tx_dec_u8 ptx;
	/* OB_TX_STATUS_EV_* facts this record actually establishes. */
	ob_tx_dec_u32 proven;
};

/*
 * Decode one rev42 status record. Returns 0 in all cases (including an invalid
 * record, which only clears `valid`); -EINVAL on a NULL argument.
 */
int ob_tx_status_decode_rev42(const struct ob_tx_status_regs *regs,
			      struct ob_tx_status_decoded *out);

/*
 * Convert a decoded record into the neutral translator input. `expected_frame_id`
 * is the ID the driver placed in the TX header (wlc_get_txh_info equivalent).
 * EV_FRAME_ID and EV_ACK are always set for a valid record; EV_RETRIES is set
 * only when the attempt counters were supplied. `retries` is attempts - 1,
 * the exact relationship used by wlc_dotxstatus (0x3bc61..0x3bc86).
 */
int ob_tx_status_input_from_decoded(const struct ob_tx_status_decoded *decoded,
				    ob_tx_dec_u16 expected_frame_id,
				    struct ob_tx_status_input *input,
				    struct ob_tx_status_evidence *evidence);

#endif /* _OB_TX_STATUS_DECODE_H_ */
