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
#define OB_CC_OTPS		0x0010	/* OTP status */
#define OB_CC_OTPC		0x0014	/* OTP control (blob 0x38d3) */
#define OB_CC_OTPP		0x0018	/* OTP prog/command (blob 0x38d3) */
#define OB_CC_OTPL		0x001c	/* OTP layout (wrap_type, region offset) */
#define OB_CC_CHIPCONTROL	0x0028
#define OB_CC_OTPC1		0x00f4	/* OTP control 1 (blob 0x38d3) */
#define OB_CC_SROM_CONTROL	0x0190
#define OB_CC_CLKCTLST		0x01e0
#define OB_CC_PMU_CTL		0x0600
#define OB_CC_PMU_CAP		0x0604
#define OB_CC_PMU_STAT		0x0608
#define OB_CC_PMU_OTPSTAT	0x060c	/* OTP power status (blob si_pmu_is_otp_powered) */
#define OB_CC_PMU_MINRES_MSK	0x0618
#define OB_CC_PMU_MAXRES_MSK	0x061c
#define OB_CC_PMU_PLLCTL_ADDR	0x0660
#define OB_CC_PMU_PLLCTL_DATA	0x0664

#define OB_CC_SPROM		0x0800	/* external SPROM / shadow window base */
#define OB_CC_CAP_SPROM		0x40000000	/* CC_CAP: SPROM present */

/*
 * CC_CAP OTP-size selector. Blob otp_init (table B, 0x3c3f) reads ChipCommon
 * CAP and extracts selector = (CAP & 0x00380000) >> 19; the table-A geometry
 * init (0x3dc7) uses the same mask on the cached CAP. BCM4352 CC_CAP=0x58680001
 * => selector 5.
 */
#define OB_CC_CAP_OTPSIZE_MASK	0x00380000
#define OB_CC_CAP_OTPSIZE_SHIFT	19

/* SROM_CONTROL (CC+0x190) fields (BCMA-authoritative) */
#define OB_SROM_PRESENT		0x00000001
#define OB_SROM_SIZE_MASK	0x00000006
#define OB_SROM_SIZE_SHIFT	1
#define OB_SROM_OTPSEL		0x00000010
#define OB_SROM_OTP_PRESENT	0x00000020
#define OB_SROM_SIZE_1K		0
#define OB_SROM_SIZE_4K		1
#define OB_SROM_SIZE_16K	2

/* OTPS (CC+0x10) general-use-programmed indication */
#define OB_OTPS_GU_PROG_IND	0x00000f00
#define OB_OTPS_GU_PROG_HW	0x00000100

/* PMU_CTL bits (blob-derived) */
#define OB_PMU_CTL_PLLRST	0x00000004

/*
 * SPROM layouts (SSB/BCMA). rev4 IL0MAC at +0x4C; rev8 at +0x8C; rev11 moved
 * the first MAC to +0x90 (BCM4352/BCM4360 rev11 extraction). The reader selects
 * the offset from the validated revision — it must NOT use 0x8C for rev11.
 */
#define OB_SPROM_MAC_OFFSET	0x4c	/* rev4 IL0MAC */
#define OB_SPROM8_IL0MAC	0x008c	/* rev8 IL0MAC */
#define OB_SPROM11_IL0MAC	0x0090	/* rev11 IL0MAC (BCM4352/BCM4360) */
#define OB_SPROM8_REVISION	0x007e	/* legacy 64-word tail revision word */

/* bcma_sprom_get() probe sizes, in 16-bit words (ssb_regs.h). */
#define OB_SPROM_WORDS_R4	220
#define OB_SPROM_WORDS_R10	230
#define OB_SPROM_WORDS_R11	234

/*
 * OTP power (BCM4352-specific, blob si_pmu_otp_power 0x12a8d case 0x4352):
 * set PMU_MINRES_MSK bit 0x100 and poll PMU_OTPSTAT bit 0x100.
 */
#define OB_OTP_PWR_BIT		0x00000100

/*
 * OTPP (CC+0x18) command/status bits, recovered from the blob bit reader
 * (0x38d3): command = 0x80000000 | (row << 8) | col, polled while bit 31 is set.
 * Opcodes are bits [27:24] (Broadcom 40nm OTP: READ=0, PROG_ENABLE=1,
 * PROG_DISABLE=2, VERIFY=3, WORD_VERIFY_1=4, ...).
 */
#define OB_OTP_CMD_START	0x80000000	/* bit 31: start / busy */
#define OB_OTP_READERR		0x10000000	/* bit 28: read error */
#define OB_OTP_VALUE		0x20000000	/* bit 29: data bit */
#define OB_OTP_OPCODE_MASK	0x0f000000
#define OB_OTP_OPCODE_SHIFT	24
#define OB_OTP_OP_READ		0
#define OB_OTP_OP_WORD_VERIFY_1	4	/* blob 0x3fa7 writes 0x84000000 */
#define OB_OTP_BUSY_MAX		10000000	/* blob poll bound (0x989680) */

/*
 * OTPL (CC+0x1c) layout fields. CC_CAP selector-derived geometry is separate;
 * these decode the on-chip region placement. GURGN_OFFSET is bcma-authoritative
 * (BCMA_CC_OTPL_GURGN_OFFSET); the other fields are the Broadcom header layout.
 */
#define OB_OTPL_GURGN_OFFSET_MASK	0x00000fff
#define OB_OTPL_ROW_SIZE_MASK		0x0000f000
#define OB_OTPL_ROW_SIZE_SHIFT		12
#define OB_OTPL_WRAP_TYPE_MASK		0x00070000
#define OB_OTPL_WRAP_TYPE_SHIFT		16
#define OB_OTPL_WRAP_TYPE_40NM		1
#define OB_OTPL_WRAP_REV_MASK		0x00780000
#define OB_OTPL_WRAP_REV_SHIFT		19

int  ob_si_probe(struct ob_hw *hw);
int  ob_si_sprom_evidence(struct ob_hw *hw);
int  ob_si_prepare_board_data_for_d3a1(struct ob_hw *hw);
int  ob_si_powerup(struct ob_hw *hw);
void ob_si_dump(struct ob_hw *hw);
u32  ob_si_cc_read(struct ob_hw *hw, u16 off);
void ob_si_cc_write(struct ob_hw *hw, u16 off, u32 val);
int  ob_si_read_mac(struct ob_hw *hw, u8 mac[6]);
int  ob_si_otp_diag(struct ob_hw *hw);

#endif /* _OB_SI_H_ */
