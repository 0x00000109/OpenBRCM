// SPDX-License-Identifier: GPL-2.0-only
/* D11 rev42 transmit-status record decoding; no MMIO and no skb ownership. */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_tx_status_decode.h"

int ob_tx_status_decode_rev42(const struct ob_tx_status_regs *regs,
			      struct ob_tx_status_decoded *out)
{
	struct ob_tx_status_decoded clean;
	ob_tx_dec_u32 status;

	if (!out)
		return -EINVAL;
	memset(&clean, 0, sizeof(clean));
	*out = clean;
	if (!regs)
		return -EINVAL;

	if (!(regs->frmtxstatus & OB_TX_DEC_FRMTXSTATUS_VALID))
		return 0;

	clean.valid = 1;
	status = regs->frmtxstatus & OB_TX_DEC_STATUS_MASK;
	clean.status_word = (ob_tx_dec_u16)status;
	clean.frame_id = (ob_tx_dec_u16)
		((regs->frmtxstatus & OB_TX_DEC_FID_MASK) >> OB_TX_DEC_FID_SHIFT);
	clean.slot = (ob_tx_dec_u8)(clean.frame_id & OB_TX_DEC_SLOT_MASK);

	clean.intermediate = !!(status & OB_TX_DEC_INTERMEDIATE);
	clean.pm_indicated = !!(status & OB_TX_DEC_PM_INDICATED);
	clean.suppress_code = (ob_tx_dec_u8)
		((status >> OB_TX_DEC_SUPPRESS_SHIFT) & OB_TX_DEC_SUPPRESS_MASK);

	/*
	 * rev42 ACK predicate, instruction-identical to 0x643c7..0x643f0 and
	 * confirmed by the consumer mapping in 0x25481..0x254b0.
	 */
	clean.acked = ((((status & OB_TX_DEC_ACK_FIELD_MASK) >>
			 OB_TX_DEC_ACK_FIELD_SHIFT) >=
			OB_TX_DEC_ACK_FIELD_MIN) ||
		       (status & OB_TX_DEC_ACK_TOP_BIT)) ? 1 : 0;

	clean.sequence = (ob_tx_dec_u16)
		(regs->frmtxstatus2 & OB_TX_DEC_SEQ_MASK);
	clean.ptx = (ob_tx_dec_u8)
		((regs->frmtxstatus2 & OB_TX_DEC_PTX_MASK) >>
		 OB_TX_DEC_PTX_SHIFT);

	if (regs->have_aux) {
		clean.attempts = (ob_tx_dec_u16)
			((regs->aux1 & 0xffu) + (regs->aux0 & 0xffu) +
			 ((regs->aux0 >> 16) & 0xffu) +
			 ((regs->aux1 >> 16) & 0xffu));
		clean.attempts_known = 1;
	}

	clean.proven = OB_TX_STATUS_EV_FRAME_ID | OB_TX_STATUS_EV_ACK;
	if (clean.attempts_known)
		clean.proven |= OB_TX_STATUS_EV_RETRIES;

	*out = clean;
	return 0;
}

int ob_tx_status_input_from_decoded(const struct ob_tx_status_decoded *decoded,
				    ob_tx_dec_u16 expected_frame_id,
				    struct ob_tx_status_input *input,
				    struct ob_tx_status_evidence *evidence)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;

	if (!input || !evidence)
		return -EINVAL;
	memset(&in, 0, sizeof(in));
	memset(&ev, 0, sizeof(ev));
	*input = in;
	*evidence = ev;
	if (!decoded)
		return -EINVAL;
	if (!decoded->valid)
		return -ENODATA;

	in.record_present = 1;
	in.expected_frame_id = expected_frame_id;
	in.reported_frame_id = decoded->frame_id;
	in.status_slot_selector = decoded->slot;

	ev.proven = decoded->proven &
		(OB_TX_STATUS_EV_FRAME_ID | OB_TX_STATUS_EV_ACK |
		 OB_TX_STATUS_EV_RETRIES);
	if (!(ev.proven & OB_TX_STATUS_EV_ACK))
		return -ENODATA;
	ev.ack = decoded->acked;

	if ((ev.proven & OB_TX_STATUS_EV_RETRIES) && decoded->attempts > 0) {
		ob_tx_dec_u32 retries = decoded->attempts - 1u;

		if (retries > 0xffu) {
			/* The neutral report stores u8 retries; stay unknown. */
			ev.proven &= ~OB_TX_STATUS_EV_RETRIES;
		} else {
			in.retries = (ob_tx_status_u8)retries;
		}
	} else {
		ev.proven &= ~OB_TX_STATUS_EV_RETRIES;
	}

	*input = in;
	*evidence = ev;
	return 0;
}
