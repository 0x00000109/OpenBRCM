// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — silicon backplane bring-up.
 *
 * The register map and the exact values/sequences are recovered from the blob
 * (RE Stage 4/5) and cross-checked against Linux bcma. This file is the only
 * place that touches PMU/PLL/clock/OTP/SPROM directly.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <linux/bcma/bcma.h>
#include "ob_si.h"

/*
 * Opt-out switch for the NON-DESTRUCTIVE-TO-OTP diagnostic probe. It writes OTP
 * control registers but never programs OTP and never replaces the MAC. Disable
 * with `insmod openbrcm.ko otp_diag=0`.
 */
static bool otp_diag;
module_param(otp_diag, bool, 0444);
MODULE_PARM_DESC(otp_diag, "Run the OTP diagnostic probe at probe time (disabled: board uses external SPROM)");

/*
 * Read-only external-SPROM diagnostic. BC.M2.5e showed the BCM4352 reports
 * SROM_CONTROL.PRESENT=1 and the blob's srom_read() therefore takes the
 * external-SPROM path, never the OTP fallback. This probe reads the external
 * SPROM via the ChipCommon window exactly as bcma_sprom_read() does.
 */
static bool sprom_diag = true;
module_param(sprom_diag, bool, 0444);
MODULE_PARM_DESC(sprom_diag, "Run the read-only external SPROM diagnostic probe at probe time");
/* Alias so runbooks using the historical "srom_diag" spelling keep working. */
module_param_named(srom_diag, sprom_diag, bool, 0444);
MODULE_PARM_DESC(srom_diag, "Alias for sprom_diag");

/* CLKCTLST: HT clock available (matches BCMA_CLKCTLST_HAVEHT). */
#define OB_CLKCTLST_HAVEHT	0x00020000

/* ChipCommon is a fixed-function core; access it through bcma's window. */
u32 ob_si_cc_read(struct ob_hw *hw, u16 off)
{
	return bcma_read32(hw->cc, off);
}

void ob_si_cc_write(struct ob_hw *hw, u16 off, u32 val)
{
	bcma_write32(hw->cc, off, val);
}

/* 16-bit accessor: identical width to bcma_sprom_read()'s bcma_read16(). */
static u16 ob_si_cc_read16(struct ob_hw *hw, u16 off)
{
	return bcma_read16(hw->cc, off);
}

static u8 ob_sprom_crc(const u16 *sprom, size_t words);

/*
 * Read and validate the factory MAC from the external SPROM (base 0x800).
 *
 * Provenance: bcma_sprom_get() probes 220/230/234 words and validates the CRC8
 * plus revision (8..11) before trusting the image; the MAC byte offset then
 * depends on that revision: rev8 -> +0x8C, rev11 (BCM4352/BCM4360) -> +0x90.
 * Using 0x8C against a rev11 image reads the zero words just before the real
 * MAC — exactly the malformed 00:00:00:00:2c:fd bcma reports. The three words
 * are decoded big-endian (bcma: cpu_to_be16). Base is NOT OTPL-derived (0x840
 * is the on-chip OTP general-use region).
 */
int ob_si_read_mac(struct ob_hw *hw, u8 mac[6])
{
	static const u16 sizes[] = { OB_SPROM_WORDS_R4, OB_SPROM_WORDS_R10,
				     OB_SPROM_WORDS_R11 };
	u16 *sp;
	size_t words = 0;
	u8 rev = 0, crc = 0, ecrc = 0;
	u16 off, raw[3];
	int i, k;

	sp = kcalloc(OB_SPROM_WORDS_R11, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		words = sizes[i];
		for (k = 0; k < words; k++)
			sp[k] = ob_si_cc_read16(hw, OB_CC_SPROM + k * 2);
		crc = ob_sprom_crc(sp, words);
		rev = sp[words - 1] & 0xff;
		ecrc = sp[words - 1] >> 8;
		dev_info(hw->dev,
			 "SPROM rev=%u words=%zu crc stored=%02x calculated=%02x %s\n",
			 rev, words, ecrc, crc,
			 crc == ecrc ? "MATCH" : "MISMATCH");
		if (crc == ecrc && rev >= 8 && rev <= 11)
			break;
	}
	if (i == ARRAY_SIZE(sizes)) {
		dev_warn(hw->dev,
			 "SPROM validation failed (no valid CRC/revision)\n");
		kfree(sp);
		return -ENODATA;
	}

	/* rev11 moved the first MAC to +0x90; rev8 uses +0x8C. */
	off = (rev == 11) ? OB_SPROM11_IL0MAC : OB_SPROM8_IL0MAC;
	raw[0] = sp[(off + 0) / 2];
	raw[1] = sp[(off + 2) / 2];
	raw[2] = sp[(off + 4) / 2];
	mac[0] = (raw[0] >> 8) & 0xff; mac[1] = raw[0] & 0xff;
	mac[2] = (raw[1] >> 8) & 0xff; mac[3] = raw[1] & 0xff;
	mac[4] = (raw[2] >> 8) & 0xff; mac[5] = raw[2] & 0xff;
	kfree(sp);

	dev_info(hw->dev, "IL0MAC offset=0x%x\n", off);
	dev_info(hw->dev, "raw=%04x %04x %04x\n", raw[0], raw[1], raw[2]);
	dev_info(hw->dev, "MAC=%pM valid=%d\n", mac, is_valid_ether_addr(mac));

	if (!is_valid_ether_addr(mac))
		return -EINVAL;
	return 0;
}

void ob_si_dump(struct ob_hw *hw)
{
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u32 otps = ob_si_cc_read(hw, OB_CC_OTPS);
	u32 otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	u32 srom_ctl = ob_si_cc_read(hw, OB_CC_SROM_CONTROL);
	u32 clkctl = ob_si_cc_read(hw, OB_CC_CLKCTLST);
	u32 pmu_cap = ob_si_cc_read(hw, OB_CC_PMU_CAP);
	u32 pmu_ctl = ob_si_cc_read(hw, OB_CC_PMU_CTL);

	dev_info(hw->dev, "cc: cap=%08x otps=%08x otpl=%08x srom_ctl=%08x\n",
		 cap, otps, otpl, srom_ctl);
	dev_info(hw->dev, "pmu: cap=%08x ctl=%08x clkctlst=%08x\n",
		 pmu_cap, pmu_ctl, clkctl);
	dev_info(hw->dev, "sprom present=%d otp present=%d otpsel=%d\n",
		 !!(srom_ctl & OB_SROM_PRESENT),
		 !!(srom_ctl & OB_SROM_OTP_PRESENT),
		 !!(srom_ctl & OB_SROM_OTPSEL));
}

/*
 * Read-only diagnostics of the bcma host/core topology. All of these are plain
 * struct reads (no MMIO), used purely to prove the PCIe2 vs legacy-PCI topology
 * before any power operation. Safe to call at any time.
 */
static void ob_si_dump_bus(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;
	struct bcma_device *d11 = hw->core;

	dev_info(hw->dev,
		 "bus: hosttype=%d host_is_pcie2=%d host_pci=%px\n",
		 bus->hosttype, bus->host_is_pcie2, bus->host_pci);
	dev_info(hw->dev,
		 "bus: drv_pci[0].core=%px drv_pci[1].core=%px drv_pcie2.core=%px\n",
		 bus->drv_pci[0].core, bus->drv_pci[1].core, bus->drv_pcie2.core);
	dev_info(hw->dev,
		 "d11: core_index=%u addr=0x%x wrap=0x%x rev=%u\n",
		 d11->core_index, d11->addr, d11->wrap, d11->id.rev);
}

/*
 * External SPROM base: the fixed ChipCommon window 0x800. It is NOT the
 * OTPL-derived 0x840 — 0x840 is the on-chip OTP general-use region offset
 * (0x800 + (OTPL.GURGN_OFFSET >> 3), OTPL=0x00115200 -> 0x200 bits -> 0x40 B).
 */
static u32 ob_si_sprom_base(struct ob_hw *hw)
{
	(void)hw;
	return OB_CC_SPROM;
}

/* Read-only snapshot of the external SPROM window (rev4 MAC location shown). */
static void ob_si_dump_sprom(struct ob_hw *hw, const char *tag)
{
	u32 base = ob_si_sprom_base(hw);

	dev_info(hw->dev,
		 "powerup[%s]: external SPROM@0x%x word0=%08x rev4mac=%08x %08x %08x\n",
		 tag, base,
		 ob_si_cc_read(hw, base),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 4),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 8));
}

/*
 * M2.5 — instrumented power-up for the BCM4352.
 *
 * Steps that are delegated to the in-kernel bcma bus driver (authoritative,
 * GPL-exported) are used directly: core enable/reset (bcma_core_enable is the
 * kernel implementation of the recovered ai_core_reset semantics) and HT clock
 * forcing (bcma_core_set_clockmode implements FORCEHT + HAVEHT polling with a
 * timeout). Steps whose values are not provenance-backed for 0x4352 are NOT
 * guessed; they are reported as UNKNOWN.
 */
int ob_si_powerup(struct ob_hw *hw)
{
	u32 clk;
	int err;

	dev_info(hw->dev, "powerup: chip 0x%04x rev %u\n",
		 hw->chip_id, hw->chip_rev);

	/* Stage: initial state */
	ob_si_dump(hw);
	ob_si_dump_sprom(hw, "before");
	dev_info(hw->dev, "powerup: d11 core enabled=%d\n",
		 bcma_core_is_enabled(hw->core));

	/* Stage 0: topology diagnostics (read-only) */
	ob_si_dump_bus(hw);

	/*
	 * Stage A: host PCIe bring-up.
	 *
	 * bcma_core_pci_power_save() is the legacy PCI-core power-save path and
	 * only guards bus->hosttype, NOT bus->host_is_pcie2. A modern BCM4352
	 * board has no legacy PCI core, so bus->drv_pci[0].core is NULL and that
	 * function dereferences NULL at pc->core->id.rev (fault address 0xc).
	 *
	 * Use the public host abstraction instead: bcma_host_pci_up() checks
	 * hosttype and dispatches on host_is_pcie2 to bcma_core_pcie2_up() — the
	 * correct path for this bus. The legacy call is removed entirely.
	 */
	dev_info(hw->dev, "powerup[A]: host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2);
	bcma_host_pci_up(hw->bus);
	dev_info(hw->dev,
		 "powerup[A]: host up done drv_pcie2.core=%px drv_pci[0].core=%px\n",
		 hw->bus->drv_pcie2.core, hw->bus->drv_pci[0].core);

	/* Stage B: D11 core enable/reset, separate from host bring-up */
	dev_info(hw->dev, "powerup[B]: d11 enabled(before)=%d\n",
		 bcma_core_is_enabled(hw->core));
	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		dev_info(hw->dev,
			 "powerup[B]: bcma_core_enable(d11) err=%d enabled(after)=%d\n",
			 err, bcma_core_is_enabled(hw->core));
	}

	/* Stage C: force HT clock and wait for it (bounded inside bcma) */
	dev_info(hw->dev, "powerup[C]: set clockmode FAST\n");
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	clk = ob_si_cc_read(hw, OB_CC_CLKCTLST);
	dev_info(hw->dev, "powerup[C]: clkctlst=0x%08x HAVEHT=%d\n",
		 clk, !!(clk & OB_CLKCTLST_HAVEHT));

	/*
	 * Board data source. Runtime (M2.5) proved SROM_CONTROL selects the
	 * external SPROM on this board (PRESENT=1, OTPSEL=0) and that the OTP HW
	 * region is not programmed (OTPS.GU_PROG_HW=0). The factory MAC is read
	 * and validated from the external SPROM in ob_si_read_mac(); OTP is not
	 * required for it. On-chip OTP is reachable only through the opt-in
	 * otp_diag path (default off). si_pmu_otp_power is a no-op for 0x4352
	 * (RE Stage 4/5, C2) — no register write is needed here.
	 */
	dev_info(hw->dev,
		 "powerup: board data from external SPROM (OTP not required; otp_diag opt-in)\n");
	ob_si_dump_sprom(hw, "after");

	return 0;
}

/* OTP geometry derived from the CC_CAP OTP-size selector. */
struct ob_otp_geometry {
	u32 rows;
	u32 cols;
	u32 words;
};

/*
 * OTP-size selector -> geometry. Recovered from the blob's per-chip geometry
 * init (0x3d6a, the OTPP[18:16]==0 arm: 0x3edf sets rows=0x60/cols=0x40 and
 * 0x3ef1 sets words=0x180 for selector 5) and independently corroborated by
 * the historical Broadcom IPX OTP geometry table. Selector 5 is the BCM4352
 * case: rows=96, cols=64, 384 16-bit words (96*64/16). The raw bit reader
 * divides the bit offset by cols to obtain row/col, so only cols matters as
 * the divisor.
 */
static const struct ob_otp_geometry ob_otp_geom_tbl[] = {
	[1] = {  32, 64, 128 },
	[2] = {  64, 64, 256 },
	[5] = {  96, 64, 384 },	/* BCM4352: CC_CAP=0x58680001 -> selector 5 */
	[7] = {  16, 64,  64 },
};

static const struct ob_otp_geometry *ob_otp_geometry(struct ob_hw *hw,
						     u32 *selector)
{
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u32 sel = (cap & OB_CC_CAP_OTPSIZE_MASK) >> OB_CC_CAP_OTPSIZE_SHIFT;

	if (selector)
		*selector = sel;
	if (sel >= ARRAY_SIZE(ob_otp_geom_tbl))
		return NULL;
	if (!ob_otp_geom_tbl[sel].rows || !ob_otp_geom_tbl[sel].cols)
		return NULL;
	return &ob_otp_geom_tbl[sel];
}

struct ob_otp_bit_dbg {
	u32 bitaddr, row, col, cmd, val, otpl;
	bool busy, readerr, value, timeout;
};

/*
 * Raw OTP bit read — faithful port of blob 0x38d3 (table A, 40nm OTP):
 *   OTPC=0; OTPC1=0; read OTPL; row = bitaddr / cols; col = bitaddr % cols;
 *   OTPP = START | (row << 8) | col (opcode READ=0); poll OTPP while START
 *   (bounded); READERR on bit 28; data on bit 29. No WAKEUP/STBY and no init
 *   opcode is issued by the blob for this wrapper. Writes only OTP control
 *   registers, never a programming-enable bit.
 */
static int ob_otp_read_bit(struct ob_hw *hw, u32 cols, u32 bitaddr,
			   u32 *bit, struct ob_otp_bit_dbg *dbg)
{
	u32 row, col, cmd, v, otpl;
	bool timeout = false, readerr = false;
	int j;

	row = bitaddr / cols;
	col = bitaddr % cols;
	cmd = OB_OTP_CMD_START | ((row & 0xff) << 8) | (col & 0xff);

	ob_si_cc_write(hw, OB_CC_OTPC, 0);
	ob_si_cc_write(hw, OB_CC_OTPC1, 0);
	otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	ob_si_cc_write(hw, OB_CC_OTPP, cmd);

	v = ob_si_cc_read(hw, OB_CC_OTPP);
	for (j = 0; (v & OB_OTP_CMD_START) != 0; j++) {
		if (j >= OB_OTP_BUSY_MAX) {
			timeout = true;
			break;
		}
		v = ob_si_cc_read(hw, OB_CC_OTPP);
	}
	if (!timeout)
		readerr = (v & OB_OTP_READERR) != 0;

	if (dbg) {
		dbg->bitaddr = bitaddr;
		dbg->row = row;
		dbg->col = col;
		dbg->cmd = cmd;
		dbg->val = v;
		dbg->otpl = otpl;
		dbg->busy = (v & OB_OTP_CMD_START) != 0;
		dbg->readerr = readerr;
		dbg->timeout = timeout;
		dbg->value = (!timeout && !readerr) &&
			     ((v & OB_OTP_VALUE) != 0);
	}

	if (timeout)
		return -ETIMEDOUT;
	if (readerr)
		return -EIO;
	*bit = (v & OB_OTP_VALUE) ? 1 : 0;
	return 0;
}

static int ob_otp_read_word16(struct ob_hw *hw, u32 cols, u32 word, u16 *out)
{
	u32 acc = 0;
	int i, err;

	for (i = 0; i < 16; i++) {
		struct ob_otp_bit_dbg dbg;
		u32 bit = 0;

		err = ob_otp_read_bit(hw, cols, word * 16 + i, &bit, &dbg);
		if (err) {
			dev_warn(hw->dev,
				 "otpdiag: bit FAIL word=%#x bit=%d off=%u row=%u col=%u cmd(OTPP@0x18)=%08x OTPL(0x1c)=%08x OTPP=%08x BUSY=%d READERR=%d VALUE=%d timeout=%d err=%d\n",
				 word, i, dbg.bitaddr, dbg.row, dbg.col, dbg.cmd,
				 dbg.otpl, dbg.val, dbg.busy, dbg.readerr,
				 dbg.value, dbg.timeout, err);
			return err;
		}
		acc |= bit << i;
	}
	*out = acc & 0xffff;
	return 0;
}

/*
 * Blob 0x3fa3..0x3fdd: OTPP = 0x84000000, i.e. START | opcode 4. Opcode 4 is
 * WORD_VERIFY_1 (NOT an init), and the blob only issues it on the geometry arm
 * where the OTPL wrap_type != 1. For the BCM4352 (40nm, wrap_type == 1) the
 * blob skips it entirely, so we do too.
 */
static int ob_otp_word_verify1(struct ob_hw *hw, u32 otpl)
{
	u32 wrap_type = (otpl & OB_OTPL_WRAP_TYPE_MASK) >> OB_OTPL_WRAP_TYPE_SHIFT;
	u32 cmd = OB_OTP_CMD_START | (OB_OTP_OP_WORD_VERIFY_1 << OB_OTP_OPCODE_SHIFT);
	u32 v;
	int j;

	if (wrap_type == OB_OTPL_WRAP_TYPE_40NM)
		return 0;	/* 40nm/BCM4352: not part of the read sequence */

	ob_si_cc_write(hw, OB_CC_OTPP, cmd);
	v = ob_si_cc_read(hw, OB_CC_OTPP);
	for (j = 0; (v & OB_OTP_CMD_START) != 0; j++) {
		if (j >= OB_OTP_BUSY_MAX) {
			dev_warn(hw->dev,
				 "otpdiag[verify1]: opcode 4 timeout OTPP=%08x\n", v);
			return -ETIMEDOUT;
		}
		v = ob_si_cc_read(hw, OB_CC_OTPP);
	}
	dev_info(hw->dev, "otpdiag[verify1]: opcode 4 WORD_VERIFY_1 done OTPP=%08x\n", v);
	return 0;
}

/*
 * NON-DESTRUCTIVE-TO-OTP diagnostic probe (M2.5c). It writes OTP *control*
 * registers (OTPC/OTP_CFG/OTPD) but never a programming bit and never programs
 * OTP. It does not replace the mac80211 hardware address; it only reports.
 */
int ob_si_otp_diag(struct ob_hw *hw)
{
	const struct ob_otp_geometry *geom;
	u32 otpc, otpc1, otpc1_save, otpc_save;
	u32 pmu_stat, resmin, selector = 0, cap;
	u32 otpl, wrap_type, wrap_rev, row_size, hwbase;
	u16 word0 = 0;
	bool was_powered;
	int i, err, ret = 0;

	dev_info(hw->dev, "otpdiag: ===== NON-DESTRUCTIVE-TO-OTP diagnostic probe =====\n");

	/* ---- A. before state ---- */
	ob_si_dump(hw);
	otpc = ob_si_cc_read(hw, OB_CC_OTPC);
	otpc1 = ob_si_cc_read(hw, OB_CC_OTPC1);
	otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
	resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
	wrap_type = (otpl & OB_OTPL_WRAP_TYPE_MASK) >> OB_OTPL_WRAP_TYPE_SHIFT;
	wrap_rev = (otpl & OB_OTPL_WRAP_REV_MASK) >> OB_OTPL_WRAP_REV_SHIFT;
	row_size = (otpl & OB_OTPL_ROW_SIZE_MASK) >> OB_OTPL_ROW_SIZE_SHIFT;
	hwbase = (otpl & OB_OTPL_GURGN_OFFSET_MASK) >> 4;
	dev_info(hw->dev,
		 "otpdiag[before]: otpc=%08x otpc1=%08x otpl=%08x pmu_stat=%08x minres=%08x\n",
		 otpc, otpc1, otpl, pmu_stat, resmin);
	dev_info(hw->dev,
		 "otpdiag[otpl]: wrap_type=%u wrap_rev=%u row_size=%u hw_off_bits=%u hwbase(words)=%u\n",
		 wrap_type, wrap_rev, row_size,
		 otpl & OB_OTPL_GURGN_OFFSET_MASK, hwbase);
	ob_si_dump_sprom(hw, "otp:before");

	otpc_save = otpc;
	otpc1_save = otpc1;
	was_powered = (pmu_stat & OB_OTP_PWR_BIT) != 0;

	/* ---- B. power OTP (BCM4352 C2 sequence), bounded ---- */
	if (!was_powered) {
		resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
		ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK, resmin | OB_OTP_PWR_BIT);
		udelay(1000);
		for (i = 0; i < 2000; i++) {
			pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
			if (pmu_stat & OB_OTP_PWR_BIT)
				break;
			udelay(10);
		}
	}
	dev_info(hw->dev,
		 "otpdiag[power]: was_powered=%d now_powered=%d pmu_stat=%08x minres=%08x\n",
		 was_powered, !!(pmu_stat & OB_OTP_PWR_BIT), pmu_stat,
		 ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK));
	if (!(pmu_stat & OB_OTP_PWR_BIT)) {
		dev_warn(hw->dev, "otpdiag: OTP did not report powered; abort\n");
		ret = -ETIMEDOUT;
		goto restore;
	}

	/* ---- C. re-read traditional shadow (diagnostic only) ---- */
	ob_si_dump_sprom(hw, "otp:after-power");

	/*
	 * ---- D. geometry from the CC_CAP OTP-size selector ----
	 * ChipCommon+0x10 is BCMA_CC_OTPS (OTP status/flags), NOT a geometry
	 * divisor; the old readl(CC+0x10) derivation was wrong. The blob derives
	 * the geometry from the capability selector instead (otp_init 0x3c3f /
	 * 0x3dc7). No OTP word is read to discover the divisor (no circularity).
	 */
	cap = ob_si_cc_read(hw, OB_CC_CAP);
	geom = ob_otp_geometry(hw, &selector);
	if (!geom) {
		dev_warn(hw->dev,
			 "otpdiag[geom]: cap=%08x selector=%u has no recovered geometry; abort (no guessing)\n",
			 cap, selector);
		ret = -EOPNOTSUPP;
		goto restore;
	}
	dev_info(hw->dev,
		 "otpdiag[geom]: cap=%08x selector=%u rows=%u cols=%u words=%u divisor(cols)=%u\n",
		 cap, selector, geom->rows, geom->cols, geom->words, geom->cols);

	/* ---- D2. Blob opcode-4 WORD_VERIFY_1 (only when wrap_type != 40nm) ---- */
	err = ob_otp_word_verify1(hw, otpl);
	if (err)
		dev_warn(hw->dev,
			 "otpdiag: WORD_VERIFY_1 did not complete (%d); continuing\n",
			 err);

	/*
	 * ---- E. SINGLE-WORD diagnostic: absolute OTP word 0x46 only. ----
	 * srom_read(region 1) maps buffer word i -> absolute OTP word hwbase+i,
	 * so SPROM byte 0x4c (buffer word 0x26) is absolute word hwbase+0x26 =
	 * 0x20+0x26 = 0x46. Words 0x47/0x48 and the MAC decode are deferred.
	 */
	{
		u32 bitaddr = (hwbase + 0x26) * 16;
		u32 row0 = bitaddr / geom->cols;
		u32 col0 = bitaddr % geom->cols;
		u32 cmd0 = OB_OTP_CMD_START | ((row0 & 0xff) << 8) |
			   (col0 & 0xff);

		/* Requirement: log pre-read control state and the first command. */
		dev_info(hw->dev,
			 "otpdiag[pre-read]: otpc=%08x otpc1=%08x otpl=%08x otps=%08x otpp=%08x\n",
			 ob_si_cc_read(hw, OB_CC_OTPC),
			 ob_si_cc_read(hw, OB_CC_OTPC1),
			 ob_si_cc_read(hw, OB_CC_OTPL),
			 ob_si_cc_read(hw, OB_CC_OTPS),
			 ob_si_cc_read(hw, OB_CC_OTPP));
		dev_info(hw->dev,
			 "otpdiag[addr]: word=%#x bit=0 offset=%u row=%u col=%u cmd=%08x opcode=%u\n",
			 hwbase + 0x26, bitaddr, row0, col0, cmd0,
			 (cmd0 & OB_OTP_OPCODE_MASK) >> OB_OTP_OPCODE_SHIFT);
	}

	err = ob_otp_read_word16(hw, geom->cols, hwbase + 0x26, &word0);
	if (err) {
		dev_warn(hw->dev,
			 "otpdiag: absolute OTP word %#x (buffer %#x) read failed (%d); STOP\n",
			 hwbase + 0x26, 0x26, err);
		ret = err;
		goto restore;
	}

	/* Success: report the raw 16-bit value and STOP (no 0x47/0x48 yet). */
	dev_info(hw->dev, "otpdiag[word %#x]: raw=%04x (all 16 bits, no READERR)\n",
		 hwbase + 0x26, word0);
	ret = 0;
	goto restore;

restore:
	/* ---- I. symmetric power-down + control restore, only if we changed it ---- */
	if (!was_powered) {
		resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
		ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK, resmin & ~OB_OTP_PWR_BIT);
	}
	ob_si_cc_write(hw, OB_CC_OTPC, otpc_save);
	ob_si_cc_write(hw, OB_CC_OTPC1, otpc1_save);
	dev_info(hw->dev, "otpdiag: ===== done (ret=%d) =====\n", ret);
	return ret;
}

/*
 * BCMA's SPROM CRC8 (polynomial x^8 + x^7 + x^6 + x^4 + x^2 + 1). The table and
 * algorithm are reproduced from Linux drivers/bcma/sprom.c so the external
 * SPROM is validated exactly as the in-kernel bus driver validates it.
 */
static u8 ob_sprom_crc8(u8 crc, u8 data)
{
	static const u8 t[256] = {
	0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
	0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
	0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
	0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
	0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
	0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
	0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
	0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
	0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
	0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
	0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
	0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
	0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
	0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
	0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
	0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
	0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
	0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
	0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
	0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
	0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
	0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
	0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
	0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
	0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
	0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
	0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
	0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
	0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
	0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
	0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
	0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
	};

	return t[crc ^ data];
}

static u8 ob_sprom_crc(const u16 *sprom, size_t words)
{
	u8 crc = 0xFF;
	int word;

	for (word = 0; word < words - 1; word++) {
		crc = ob_sprom_crc8(crc, sprom[word] & 0x00FF);
		crc = ob_sprom_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = ob_sprom_crc8(crc, sprom[words - 1] & 0x00FF);
	return crc ^ 0xFF;
}

/* Log the SPROM data the in-kernel bcma bus driver already populated. */
static void ob_si_dump_bus_sprom(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;

	dev_info(hw->dev,
		 "bcmasp: revision=%u board_rev=%u board_type=%u board_num=%u vendor=%u type=%u\n",
		 bus->sprom.revision, bus->sprom.board_rev, bus->sprom.board_type,
		 bus->sprom.board_num, bus->boardinfo.vendor,
		 bus->boardinfo.type);
	/* Shown for comparison only; NOT used for the hardware address. */
	dev_info(hw->dev, "bcmasp(bcma-parsed,not used): il0mac=%pM et0mac=%pM\n",
		 bus->sprom.il0mac, bus->sprom.et0mac);
}

static void ob_si_dump_sprom_window(struct ob_hw *hw, u16 start,
				    unsigned int bytes)
{
	unsigned int off;

	for (off = 0; off < bytes; off += 16) {
		char line[160];
		int n = 0, k;

		n += scnprintf(line + n, sizeof(line) - n, "spromdiag[0x%x]:",
			       OB_CC_SPROM + start + off);
		for (k = 0; k < 8; k++)
			n += scnprintf(line + n, sizeof(line) - n, " %04x",
				       ob_si_cc_read16(hw, OB_CC_SPROM + start +
						       off + k * 2));
		dev_info(hw->dev, "%s\n", line);
	}
}

/*
 * Read-only external-SPROM diagnostic (M2.5f). Reads through the ChipCommon
 * window with 16-bit accesses exactly like bcma_sprom_read(); writes nothing.
 * Validates CRC + revision the same way bcma_sprom_valid() does.
 */
static int ob_si_sprom_diag(struct ob_hw *hw)
{
	u32 srom_ctl = ob_si_cc_read(hw, OB_CC_SROM_CONTROL);
	u32 otps = ob_si_cc_read(hw, OB_CC_OTPS);
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u16 revw;
	bool valid = false;
	u16 *sp;
	int i;

	dev_info(hw->dev, "spromdiag: ===== read-only external SPROM probe =====\n");
	dev_info(hw->dev, "spromdiag[ctl]: cap=%08x srom_control=%08x otps=%08x\n",
		 cap, srom_ctl, otps);
	dev_info(hw->dev,
		 "spromdiag[ctl]: present=%d size=%u otpsel=%d otp_present=%d cap_sprom=%d\n",
		 !!(srom_ctl & OB_SROM_PRESENT),
		 (srom_ctl & OB_SROM_SIZE_MASK) >> OB_SROM_SIZE_SHIFT,
		 !!(srom_ctl & OB_SROM_OTPSEL),
		 !!(srom_ctl & OB_SROM_OTP_PRESENT),
		 !!(cap & OB_CC_CAP_SPROM));
	dev_info(hw->dev,
		 "spromdiag[otps]: GU_PROG_IND=%u GU_PROG_HW=%d protect=%u\n",
		 (otps & OB_OTPS_GU_PROG_IND) >> 8,
		 !!(otps & OB_OTPS_GU_PROG_HW), otps & 0x7);

	/* Window 0x800..0x8a0, 16-bit reads, exactly like bcma_sprom_read(). */
	ob_si_dump_sprom_window(hw, 0, 0xa0);

	/*
	 * The rev@0x7e word is the legacy 64-word-tail convention; the rev8/rev11
	 * revision actually lives in the last word of the probed size (reported in
	 * the bcma-style block below and by ob_si_read_mac()). MAC extraction is
	 * revision-aware in ob_si_read_mac() (rev8 +0x8C, rev11 +0x90), so it is
	 * not duplicated here.
	 */
	revw = ob_si_cc_read16(hw, OB_CC_SPROM + OB_SPROM8_REVISION);
	dev_info(hw->dev, "spromdiag[rev@0x%x]: raw=%04x rev=%u crc8=%02x\n",
		 OB_SPROM8_REVISION, revw, revw & 0xff, revw >> 8);

	/* BCMA-style full-size read + CRC/revision validation. */
	sp = kcalloc(OB_SPROM_WORDS_R11, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	for (i = 0; i < 3; i++) {
		size_t words = (i == 0) ? OB_SPROM_WORDS_R4 :
			       (i == 1) ? OB_SPROM_WORDS_R10 :
					  OB_SPROM_WORDS_R11;
		u8 crc, erev, ecrc;
		int k;

		for (k = 0; k < words; k++)
			sp[k] = ob_si_cc_read16(hw, OB_CC_SPROM + k * 2);
		crc = ob_sprom_crc(sp, words);
		erev = sp[words - 1] & 0xff;
		ecrc = sp[words - 1] >> 8;
		dev_info(hw->dev,
			 "spromdiag[bcma]: words=%zu rev=%u crc=%02x calc=%02x %s rev_ok=%d\n",
			 words, erev, ecrc, crc, crc == ecrc ? "MATCH" : "MISMATCH",
			 erev >= 8 && erev <= 11);
		if (crc == ecrc && erev >= 8 && erev <= 11)
			valid = true;
	}
	kfree(sp);

	dev_info(hw->dev,
		 "spromdiag: result external_sprom_valid=%d (bcma CRC+rev)\n",
		 valid);
	dev_info(hw->dev, "spromdiag: ===== done =====\n");
	return valid ? 0 : -ENODATA;
}

int ob_si_probe(struct ob_hw *hw)
{
	/* ChipCommon core provides the CC/PMU/OTP/SPROM window. */
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "no ChipCommon core\n");
		return -ENODEV;
	}

	ob_si_powerup(hw);

	/* Read-only: what bcma already parsed from the SPROM at bus scan time. */
	ob_si_dump_bus_sprom(hw);

	/*
	 * Authoritative board MAC: validated rev8/rev11 external SPROM read.
	 * bcma's own bus->sprom.il0mac is deliberately NOT used here — for a
	 * rev11 image it is malformed (rev8 offset applied to rev11 data).
	 */
	hw->mac_valid = (ob_si_read_mac(hw, hw->mac) == 0);

	if (sprom_diag)
		ob_si_sprom_diag(hw);

	if (otp_diag)
		ob_si_otp_diag(hw);

	return 0;
}
