/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bcm43xx_chan_tuning.h - per-channel tuning rows for the BCM4352 AC PHY.
 *
 * Recovered from Broadcom wlc_hybrid.o_shipped 6.30.223.271
 * (SHA-256 352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743).
 * Layout (channel/freq header, radio u16 block, phy_bw[6]) is instruction-level
 * CONFIRMED. Field names are address-only; radio-register semantics are
 * UNKNOWN. "phy_bw" is the documented PHY 0x371..0x376 slice; its functional
 * role is INFERRED, not CONFIRMED.
 */
#ifndef _BCM43XX_CHAN_TUNING_H_
#define _BCM43XX_CHAN_TUNING_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef BUILD_BUG_ON
#define BUILD_BUG_ON(cond) _Static_assert(!(cond), #cond)
#endif
#endif

#define BCM43XX_CHAN_TUNING_ROWS		77
#define BCM43XX_CHAN_TUNING_STRIDE_2069	0x5e
#define BCM43XX_CHAN_TUNING_STRIDE_GE25	0x60
#define BCM43XX_CHAN_TUNING_STRIDE_LEGACY	0x74
#define BCM43XX_CHAN_TUNING_BW_COUNT		6

/* chanspec fields (b43/brcmsmac encoding). */
#define BCM43XX_CHANSPEC_CHANNEL_MASK		0x00ff
#define BCM43XX_CHANSPEC_BW_MASK		0x3800
#define BCM43XX_CHANSPEC_BW20			0x1000
#define BCM43XX_CHANSPEC_BW40			0x1800
#define BCM43XX_CHANSPEC_BW80			0x2000
#define BCM43XX_CHANSPEC_BAND_MASK		0xc000
#define BCM43XX_REFCLK_40MHZ			40000000u
#define BCM43XX_MODE2_REV_MASK		0x6f00000000ULL

/* PHY register addresses 0x371..0x376; BW role is INFERRED. */
enum bcm43xx_phy_bw_reg {
	BCM43XX_PHY_BW1A = 0x371,
	BCM43XX_PHY_BW2  = 0x372,
	BCM43XX_PHY_BW3  = 0x373,
	BCM43XX_PHY_BW4  = 0x374,
	BCM43XX_PHY_BW5  = 0x375,
	BCM43XX_PHY_BW6  = 0x376,
};

/* Reference frequencies recovered from the tables. */
#define BCM43XX_FREQ_2484_MHZ	2484u
#define BCM43XX_FREQ_4920_MHZ	4920u
#define BCM43XX_FREQ_5170_MHZ	5170u

struct bcm43xx_chan_tuning_hdr {
	u8  channel;
	u8  pad_01;
	u16 freq_mhz;
} __packed;

struct bcm43xx_chan_tuning_row_2069 {
	u8  channel;
	u8  pad_01;
	u16 freq_mhz;
	u16 radio_reg_0x8e8_at_0x04;
	u16 radio_reg_0x8e9_at_0x06;
	u16 radio_reg_0x8e5_at_0x08;
	u16 radio_reg_0x8e4_at_0x0a;
	u16 radio_reg_0x8ee_at_0x0c;
	u16 radio_reg_0x8ef_at_0x0e;
	u16 radio_reg_0x8cc_at_0x10;
	u16 radio_reg_0x8cd_at_0x12;
	u16 radio_reg_0x8ed_at_0x14;
	u16 radio_reg_0x8f3_at_0x16;
	u16 radio_reg_0x8de_at_0x18;
	u16 radio_reg_0x125_at_0x1a;
	u16 radio_reg_0x8e3_at_0x1c;
	u16 radio_reg_0x8e2_at_0x1e;
	u16 radio_reg_0x8df_at_0x20;
	u16 radio_reg_0x88c_at_0x22;
	u16 radio_reg_0x88d_at_0x24;
	u16 radio_reg_0x88e_at_0x26;
	u16 radio_reg_0x8e1_at_0x28;
	u16 radio_reg_0x8e0_at_0x2a;
	u16 radio_reg_0x8d1_at_0x2c;
	u16 radio_reg_0x8d2_at_0x2e;
	u16 radio_reg_0x8d4_at_0x30;
	u16 radio_reg_0x8cf_at_0x32;
	u16 radio_reg_0x8d0_at_0x34;
	u16 radio_reg_0x89a_at_0x36;
	u16 radio_reg_0x8d3_at_0x38;
	u16 radio_reg_0x124_at_0x3a;
	u16 radio_reg_0x02f_at_0x3c;
	u16 radio_reg_0x062_at_0x3e;
	u16 radio_reg_0x065_at_0x40;
	u16 radio_reg_0x06f_at_0x42;
	u16 radio_reg_0x12c_at_0x44;
	u16 radio_reg_0x12d_at_0x46;
	u16 radio_reg_0x12b_at_0x48;
	u16 radio_reg_0x037_at_0x4a;
	u16 radio_reg_0x063_at_0x4c;
	u16 radio_reg_0x069_at_0x4e;
	u16 radio_reg_0x8e8_at_0x50;
	u16 phy_bw[BCM43XX_CHAN_TUNING_BW_COUNT];
} __packed;  /* 94 bytes */

struct bcm43xx_chan_tuning_row_ge25 {
	u8  channel;
	u8  pad_01;
	u16 freq_mhz;
	u16 radio_reg_0x8e8_at_0x04;
	u16 radio_reg_0x8e9_at_0x06;
	u16 radio_reg_0x8e5_at_0x08;
	u16 radio_reg_0x8e4_at_0x0a;
	u16 radio_reg_0x8ee_at_0x0c;
	u16 radio_reg_0x8ef_at_0x0e;
	u16 radio_reg_0x8cc_at_0x10;
	u16 radio_reg_0x8cd_at_0x12;
	u16 radio_reg_0x8ed_at_0x14;
	u16 radio_reg_0x8f3_at_0x16;
	u16 radio_reg_0x91b_at_0x18;
	u16 radio_reg_0x8de_at_0x1a;
	u16 radio_reg_0x125_at_0x1c;
	u16 radio_reg_0x8e3_at_0x1e;
	u16 radio_reg_0x8e2_at_0x20;
	u16 radio_reg_0x8df_at_0x22;
	u16 radio_reg_0x88c_at_0x24;
	u16 radio_reg_0x88d_at_0x26;
	u16 radio_reg_0x88e_at_0x28;
	u16 radio_reg_0x8e1_at_0x2a;
	u16 radio_reg_0x8e0_at_0x2c;
	u16 radio_reg_0x8d1_at_0x2e;
	u16 radio_reg_0x8d2_at_0x30;
	u16 radio_reg_0x8d4_at_0x32;
	u16 radio_reg_0x8cf_at_0x34;
	u16 radio_reg_0x8d0_at_0x36;
	u16 radio_reg_0x89a_at_0x38;
	u16 radio_reg_0x8d3_at_0x3a;
	u16 radio_reg_0x124_at_0x3c;
	u16 radio_reg_0x02f_at_0x3e;
	u16 radio_reg_0x062_at_0x40;
	u16 radio_reg_0x065_at_0x42;
	u16 radio_reg_0x06f_at_0x44;
	u16 radio_reg_0x12c_at_0x46;
	u16 radio_reg_0x12d_at_0x48;
	u16 radio_reg_0x12b_at_0x4a;
	u16 radio_reg_0x037_at_0x4c;
	u16 radio_reg_0x063_at_0x4e;
	u16 radio_reg_0x069_at_0x50;
	u16 radio_reg_0x074_at_0x52;
	u16 phy_bw[BCM43XX_CHAN_TUNING_BW_COUNT];
} __packed;  /* 96 bytes */

struct bcm43xx_chan_tuning_row_legacy {
	u8  channel;
	u8  pad_01;
	u16 freq_mhz;
	u16 radio_reg_0x8e8_at_0x04;
	u16 radio_reg_0x8e9_at_0x06;
	u16 radio_reg_0x8e5_at_0x08;
	u16 radio_reg_0x8e4_at_0x0a;
	u16 radio_reg_0x8ee_at_0x0c;
	u16 radio_reg_0x8ef_at_0x0e;
	u16 radio_reg_0x8cc_at_0x10;
	u16 radio_reg_0x8cd_at_0x12;
	u16 radio_reg_0x8ed_at_0x14;
	u16 radio_reg_0x8f3_at_0x16;
	u16 radio_reg_0x8de_at_0x18;
	u16 radio_reg_0x125_at_0x1a;
	u16 radio_reg_0x8e3_at_0x1c;
	u16 radio_reg_0x8e2_at_0x1e;
	u16 radio_reg_0x8df_at_0x20;
	u16 radio_reg_0x88c_at_0x22;
	u16 radio_reg_0x88d_at_0x24;
	u16 radio_reg_0x88e_at_0x26;
	u16 radio_reg_0x8e1_at_0x28;
	u16 radio_reg_0x8e0_at_0x2a;
	u16 radio_reg_0x8d1_at_0x2c;
	u16 radio_reg_0x8d2_at_0x2e;
	u16 radio_reg_0x8d4_at_0x30;
	u16 radio_reg_0x8cf_at_0x32;
	u16 radio_reg_0x8d0_at_0x34;
	u16 radio_reg_0x89a_at_0x36;
	u16 radio_reg_0x09c_at_0x38;
	u16 radio_reg_0x09d_at_0x3a;
	u16 radio_reg_0x09e_at_0x3c;
	u16 radio_reg_0x09f_at_0x3e;
	u16 radio_reg_0x0a1_at_0x40;
	u16 radio_reg_0x0a2_at_0x42;
	u16 radio_reg_0x0a3_at_0x44;
	u16 radio_reg_0x0a4_at_0x46;
	u16 radio_reg_0x124_at_0x48;
	u16 radio_reg_0x62f_at_0x4a;
	u16 radio_reg_0x662_at_0x4c;
	u16 radio_reg_0x665_at_0x4e;
	u16 radio_reg_0x66f_at_0x50;
	u16 radio_reg_0x12c_at_0x52;
	u16 radio_reg_0x12d_at_0x54;
	u16 radio_reg_0x72b_at_0x56;
	u16 radio_reg_0x637_at_0x58;
	u16 radio_reg_0x663_at_0x5a;
	u16 radio_reg_0x669_at_0x5c;
	u16 radio_reg_0x674_at_0x5e;
	u16 radio_reg_0x89b_at_0x60;
	u16 radio_reg_0x157_at_0x62;
	u16 radio_reg_0x158_at_0x64;
	u16 radio_reg_0x735_at_0x66;
	u16 phy_bw[BCM43XX_CHAN_TUNING_BW_COUNT];
} __packed;  /* 116 bytes */

static inline void bcm43xx_chan_tuning_build_asserts(void)
{
	BUILD_BUG_ON(sizeof(struct bcm43xx_chan_tuning_row_2069) != 94);
	BUILD_BUG_ON(sizeof(struct bcm43xx_chan_tuning_row_ge25) != 96);
	BUILD_BUG_ON(sizeof(struct bcm43xx_chan_tuning_row_legacy) != 116);
	BUILD_BUG_ON(sizeof(struct bcm43xx_chan_tuning_hdr) != 4);
}

struct bcm43xx_chan_tuning_table {
	const void *rows;
	u16 count;
	u16 stride;
};

/* Descriptors are indexed by (radio_class, radio_rev), not by channel band. */
struct bcm43xx_chan_tuning_tables {
	struct bcm43xx_chan_tuning_table mode0[6];	/* class 0, rev 3..8, stride 0x74 */
	struct bcm43xx_chan_tuning_table mode1[11];	/* class 1 normal refclk */
	struct bcm43xx_chan_tuning_table mode1_40[11];	/* class 1, 40 MHz refclk */
	struct bcm43xx_chan_tuning_table mode2_other_ref;	/* 0x9b3e70, stride 0x5e */
	struct bcm43xx_chan_tuning_table mode2_40mhz_ref;	/* 0x9b2220, stride 0x5e */
};

struct bcm43xx_chan_tuning_env {
	u8  radio_class;	/* pi+0x16e: radio revision class (rev >> 4), 0/1/2 */
	u8  radio_rev;	/* pi+0x16c: full 2069 radio revision; class 0: 3..8,
			 * class 1: 0x10..0x1a, class 2 mask 0x6f00000000:
			 * 0x20,0x21,0x22,0x23,0x25,0x26
			 */
	u32 refclk_hz;	/* pi+0xc24; 40000000 selects the _40 tables */
};

static inline u16 bcm43xx_chan_tuning_channel(u16 chanspec)
{
	return chanspec & BCM43XX_CHANSPEC_CHANNEL_MASK;
}

/*
 * Mirrors sub_48e9b1: select the mode (radio-class) table, compare the row channel
 * with chanspec & 0xff, return the row frequency.  expected_freq_mhz == 0
 * means channel-only lookup (the blob behavior); a non-zero value adds an
 * explicit frequency check.  Exactly one of the four row outputs is set.
 */
int bcm43xx_get_chan_tuning_row(const struct bcm43xx_chan_tuning_env *env,
				const struct bcm43xx_chan_tuning_tables *tables,
				u16 chanspec, u16 expected_freq_mhz,
				u32 *code, const void **row74,
				const void **row5e, const void **row60,
				const void **row5e_mode2);

enum bcm43xx_chan_tuning_kind {
	BCM43XX_ROW_2069,
	BCM43XX_ROW_GE25,
	BCM43XX_ROW_LEGACY,
};

struct bcm43xx_chan_tuning_io {
	void *ctx;
	void (*write_radio_reg)(void *ctx, u16 reg, u16 value);
	void (*phy_reg_write)(void *ctx, u16 reg, u16 value);
	u32 chip_id;	/* acphychipid; {0x4352,0x4360,0xa9c4,0xaa06} use the alt reg */
};

/* Mirrors the radio loop and the six phy_bw writes of sub_4a7089/sub_4a4adc. */
int bcm43xx_apply_chan_tuning(enum bcm43xx_chan_tuning_kind kind,
			      const void *row,
			      const struct bcm43xx_chan_tuning_io *io);

#endif /* _BCM43XX_CHAN_TUNING_H_ */
