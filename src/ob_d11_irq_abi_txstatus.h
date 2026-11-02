/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_D11_IRQ_ABI_TXSTATUS_H_
#define _OB_D11_IRQ_ABI_TXSTATUS_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 ob_d11_irq_abi_u32;
typedef u16 ob_d11_irq_abi_u16;
typedef u8 ob_d11_irq_abi_u8;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t ob_d11_irq_abi_u32;
typedef uint16_t ob_d11_irq_abi_u16;
typedef uint8_t ob_d11_irq_abi_u8;
#endif

/*
 * Source-only extraction from the pinned vendor's rev > 0x27 path in
 * wlc_bmac_txstatus. The caller tests bit 0 of the sampled word before
 * invoking wlc_dotxstatus. The first halfword in the vendor status record
 * (status + 2) is the sample's bits 31:16; wlc_dotxstatus then selects its
 * slot with (status[1] & 7).
 *
 * This deliberately models only these proven software fields. It does not
 * name the hardware register, define FIFO interrupt/ack behavior, or establish
 * a host DMA-cookie/packet identity.
 */
#define OB_D11_IRQ_ABI_REV42_TXSTATUS_DPC_FLAG 0x20000000u

struct ob_d11_irq_abi_txstatus_fields {
	bool present;
	ob_d11_irq_abi_u16 status_word1;
	ob_d11_irq_abi_u8 status_slot_selector;
};

static inline bool
ob_d11_irq_abi_rev42_txstatus_dpc_selected(ob_d11_irq_abi_u32 dpc_flags)
{
	return !!(dpc_flags & OB_D11_IRQ_ABI_REV42_TXSTATUS_DPC_FLAG);
}

static inline struct ob_d11_irq_abi_txstatus_fields
ob_d11_irq_abi_decode_rev42_status_sample(ob_d11_irq_abi_u32 sample)
{
	struct ob_d11_irq_abi_txstatus_fields fields;

	fields.present = !!(sample & 1u);
	fields.status_word1 = (ob_d11_irq_abi_u16)(sample >> 16);
	fields.status_slot_selector =
		(ob_d11_irq_abi_u8)(fields.status_word1 & 7u);
	return fields;
}

#endif /* _OB_D11_IRQ_ABI_TXSTATUS_H_ */
