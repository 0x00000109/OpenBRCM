/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_D11_TX_FRAMEID_H_
#define _OB_D11_TX_FRAMEID_H_

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
typedef u8 ob_d11_fid_u8;
typedef u16 ob_d11_fid_u16;
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t ob_d11_fid_u8;
typedef uint16_t ob_d11_fid_u16;
#endif

#define OB_D11_TX_FRAMEID_ACPHY_TYPE 0x0bu

enum ob_d11_tx_frameid_counter {
	OB_D11_TX_FRAMEID_NORMAL_COUNTER = 0,
	OB_D11_TX_FRAMEID_SELECTOR4_COUNTER = 1,
};

struct ob_d11_tx_frameid_result {
	ob_d11_fid_u16 frame_id;
	ob_d11_fid_u16 next_counter;
	enum ob_d11_tx_frameid_counter counter_kind;
};

/*
 * Instruction-derived `sub_29210` frame-ID transform for AC-PHY packets.
 * `old_header_id` is the existing TX-header word at +0x0c. The normal path
 * consumes WLC+0x422; selector 4 consumes WLC+0x424. These counters are
 * short, wrapping software sequence sources, not unique packet tickets.
 *
 * This helper only computes the header word and next u16 counter. The caller
 * owns/persists the selected counter and places frame_id in the TX header.
 * It does not associate status records with packets or enable hardware TX.
 */
static inline int
ob_d11_tx_frameid_acphy(ob_d11_fid_u8 phy_type,
			ob_d11_fid_u16 old_header_id,
			ob_d11_fid_u16 counter,
			struct ob_d11_tx_frameid_result *result)
{
	ob_d11_fid_u16 selector;

	if (!result)
		return -EINVAL;
	result->frame_id = 0;
	result->next_counter = 0;
	result->counter_kind = OB_D11_TX_FRAMEID_NORMAL_COUNTER;
	if (phy_type != OB_D11_TX_FRAMEID_ACPHY_TYPE)
		return -EOPNOTSUPP;

	selector = old_header_id & 7u;
	if (selector == 4u) {
		result->frame_id =
			(ob_d11_fid_u16)((counter << 5) & 0x7fe0u) |
			(old_header_id & 0x8018u) | 4u;
		result->counter_kind = OB_D11_TX_FRAMEID_SELECTOR4_COUNTER;
	} else {
		result->frame_id =
			(ob_d11_fid_u16)((counter << 9) & 0x7fe0u) |
			(old_header_id & 0x801fu);
	}
	result->next_counter = (ob_d11_fid_u16)(counter + 1u);
	return 0;
}

#endif /* _OB_D11_TX_FRAMEID_H_ */
