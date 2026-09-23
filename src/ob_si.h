/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — silicon backplane (SI/PMU/PLL/OTP/SPROM).
 *
 * Register offsets and bring-up semantics are taken from the OpenBRCM RE
 * specification: Stage 4 (MMIO map) and Stage 5 (sequences/values), cross-checked
 * against Linux bcma names. The values themselves are blob-derived (C2).
 */
#ifndef _OB_SI_H_
#define _OB_SI_H_

#include <linux/types.h>
#include "ob_core.h"

/* ChipCommon / PMU registers (offsets relative to the ChipCommon core). */
#define OB_CC_CHIPID		0x0000
#define OB_CC_CAP		0x0004
#define OB_CC_OTPS		0x0010
#define OB_CC_OTPC		0x0014
#define OB_CC_OTPD		0x0018
#define OB_CC_OTPL		0x001c
#define OB_CC_CHIPCONTROL	0x0028
#define OB_CC_SROM_CONTROL	0x0190
#define OB_CC_CLKCTLST		0x01e0
#define OB_CC_PMU_CTL		0x0600
#define OB_CC_PMU_CAP		0x0604
#define OB_CC_PMU_STAT		0x0608
#define OB_CC_PMU_MINRES_MSK	0x0618
#define OB_CC_PMU_MAXRES_MSK	0x061c
#define OB_CC_PMU_PLLCTL_ADDR	0x0660
#define OB_CC_PMU_PLLCTL_DATA	0x0664

#define OB_CC_SPROM		0x0800	/* SPROM shadow base */
#define OB_CC_CAP_SPROM		0x40000000

/* SROM_CONTROL bits */
#define OB_SROM_OTPSEL		0x00000010
#define OB_SROM_OTP_PRESENT	0x00000020
#define OB_SROM_PRESENT		0x00000001

/* PMU_CTL bits (blob-derived) */
#define OB_PMU_CTL_PLLRST	0x00000004

/* SPROM layout: MAC address */
#define OB_SPROM_MAC_OFFSET	0x4c

int  ob_si_probe(struct ob_hw *hw);
int  ob_si_powerup(struct ob_hw *hw);
void ob_si_dump(struct ob_hw *hw);
u32  ob_si_cc_read(struct ob_hw *hw, u16 off);
void ob_si_cc_write(struct ob_hw *hw, u16 off, u32 val);
int  ob_si_read_mac(struct ob_hw *hw, u8 mac[6]);

#endif /* _OB_SI_H_ */
