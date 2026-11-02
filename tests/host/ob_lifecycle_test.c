/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "../../src/ob_lifecycle.h"

static int failures;

static void chk(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s: got %d expected %d\n", name, got, expected);
		failures++;
	}
}

static void chku(const char *name, unsigned int got, unsigned int expected)
{
	if (got != expected) {
		printf("FAIL: %s: got 0x%08x expected 0x%08x\n", name, got,
		       expected);
		failures++;
	}
}

static void test_order(void)
{
	enum ob_d11_lifecycle_state s = OB_D11_CORE_READY;

	chk("attach identity", ob_d11_lifecycle_advance(&s,
		OB_D11_ATTACH_IDENTITY_READY), 1);
	chk("attach identity final", s, OB_D11_ATTACH_IDENTITY_READY);
	chk("ucode", ob_d11_lifecycle_advance(&s, OB_D11_UCODE_LOADED), 1);
	chk("psm", ob_d11_lifecycle_advance(&s, OB_D11_PSM_STARTED), 1);
	chk("startup suspend", ob_d11_lifecycle_advance(&s,
		OB_D11_STARTUP_SUSPEND_CONFIRMED), 1);
	chk("common", ob_d11_lifecycle_advance(&s,
		OB_D11_COMMON_INITVALS_APPLIED), 1);
	chk("ready", ob_d11_lifecycle_advance(&s,
		OB_D11_POST_UCODE_READY), 1);
	chk("final state", s, OB_D11_POST_UCODE_READY);
}

/*
 * M1-6 bracket precondition: the vendor rule is EN_MAC == 0, not equality with
 * the startup word.  0x84020402 is what the bench actually read (startup word
 * plus bit 31 set on the read); 0xffffffff is a dead bus, never a state.
 */
static void test_mac_bracket_precondition(void)
{
	chk("startup word",
	    ob_d11_mac_already_suspended(OB_D11_STARTUP_QUIESCED_MACCONTROL), 1);
	chk("bit31 status read", ob_d11_mac_already_suspended(0x84020402u), 1);
	chk("all clear", ob_d11_mac_already_suspended(0x00000000u), 1);
	chk("en_mac set", ob_d11_mac_already_suspended(0x04020403u), 0);
	chk("all ones", ob_d11_mac_already_suspended(0xffffffffu), 0);
}

static void test_fail_closed_order(void)
{
	enum ob_d11_lifecycle_state s = OB_D11_CORE_READY;

	chk("reject common early", ob_d11_lifecycle_advance(&s,
		OB_D11_COMMON_INITVALS_APPLIED), 0);
	chk("reject ucode before attach identity", ob_d11_lifecycle_advance(&s,
		OB_D11_UCODE_LOADED), 0);
	chk("state unchanged", s, OB_D11_CORE_READY);
	chk("reject ready early", ob_d11_lifecycle_advance(&s,
		OB_D11_POST_UCODE_READY), 0);
	chk("reject rollback", ob_d11_lifecycle_advance(&s,
		OB_D11_STATE_NONE), 0);

	s = OB_D11_ATTACH_IDENTITY_READY;
	chk("reject pre-radio before post-ucode", ob_d11_lifecycle_advance(&s,
		OB_D11_POST_UCODE_READY), 0);
}

/* ------------------------------------------------------------------ */
/* M1-6D wake model: bitmap, transition-only bit 26, non-failing wait. */
/* ------------------------------------------------------------------ */

struct wake_fake {
	int reads;
	int writes;
	int shm_reads;
	int delays;
	u32 mctrl;
	u32 last_delay;
	int fail_read;
	int fail_write;
	u16 shm[4];
	int shm_len;
	int shm_pos;
};

static int wake_fake_read32(void *ctx, u16 off, u32 *value)
{
	struct wake_fake *f = ctx;

	(void)off;
	f->reads++;
	if (f->fail_read)
		return -5;
	*value = f->mctrl;
	return 0;
}

static int wake_fake_write32(void *ctx, u16 off, u32 value)
{
	struct wake_fake *f = ctx;

	(void)off;
	f->writes++;
	if (f->fail_write)
		return -5;
	f->mctrl = value;
	return 0;
}

static int wake_fake_shm16(void *ctx, u16 off, u16 *value)
{
	struct wake_fake *f = ctx;
	int pos;

	(void)off;
	f->shm_reads++;
	pos = f->shm_pos;
	if (pos >= f->shm_len)
		pos = f->shm_len - 1;
	if (pos < 0) {
		*value = 0;
		return 0;
	}
	*value = f->shm[pos];
	if (f->shm_pos < f->shm_len)
		f->shm_pos++;
	return 0;
}

static void wake_fake_delay(void *ctx, u32 usec)
{
	struct wake_fake *f = ctx;

	f->delays++;
	f->last_delay = usec;
}

static const struct ob_d11_wake_io wake_io = {
	.read32 = wake_fake_read32,
	.write32 = wake_fake_write32,
	.read_shm16 = wake_fake_shm16,
	.delay_us = wake_fake_delay,
};

static void wake_fake_init(struct wake_fake *f, u32 mctrl, u16 shm)
{
	*f = (struct wake_fake) { .mctrl = mctrl, .shm_len = 1 };
	f->shm[0] = shm;
}

/*
 * Suspend override on a clean byte: one MACCONTROL write with bit 26, the
 * vendor's initial delay, and a single SHM read that ends the wait on a value
 * that is not 4.
 */
static void test_wake_override_suspend_transition(void)
{
	struct wake_fake f;
	u32 ov = 0;
	int ret;

	wake_fake_init(&f, 0x00020402u, 0x0002u);
	ret = ob_d11_wake_override_set_script(&wake_io, &f, &ov,
					      OB_D11_WAKE_OVERRIDE_SUSPEND);
	chk("suspend set rc", ret, 0);
	chku("suspend override", ov, OB_D11_WAKE_OVERRIDE_SUSPEND);
	chku("bit26 set once", f.mctrl, 0x04020402u);
	chk("one mctrl read", f.reads, 1);
	chk("one mctrl write", f.writes, 1);
	chk("one shm read", f.shm_reads, 1);
	chk("one delay", f.delays, 1);
	chku("initial delay", f.last_delay, OB_D11_WAKE_FIRST_US);
}

/*
 * The vendor's 0x0639d7 branch: a nonzero bitmap makes a further set
 * bookkeeping-only.  This is why phyreg_enter is MMIO-free inside the bracket.
 */
static void test_wake_override_set_is_bookkeeping_when_active(void)
{
	struct wake_fake f;
	u32 ov = OB_D11_WAKE_OVERRIDE_SUSPEND;
	int ret;

	wake_fake_init(&f, 0x84020402u, 0x0000u);
	ret = ob_d11_wake_override_set_script(&wake_io, &f, &ov,
					      OB_D11_WAKE_OVERRIDE_PHYREG);
	chk("bookkeeping rc", ret, 0);
	chku("bitmap 4|2", ov, OB_D11_WAKE_OVERRIDE_SUSPEND |
				 OB_D11_WAKE_OVERRIDE_PHYREG);
	chk("no mctrl read", f.reads, 0);
	chk("no mctrl write", f.writes, 0);
	chk("no shm read", f.shm_reads, 0);
	chk("no delay", f.delays, 0);
}

/*
 * wlc_bmac_wait_for_wake keeps polling WHILE the word reads 4 and ends on the
 * first other value (vendor trace: 21 reads of 4, ends on 0x2).
 */
static void test_wait_spins_while_pending_then_stops(void)
{
	struct wake_fake f;

	wake_fake_init(&f, 0, 0);
	f.shm[0] = 0x0004u;
	f.shm[1] = 0x0004u;
	f.shm[2] = 0x0002u;
	f.shm_len = 3;
	ob_d11_wait_for_wake_script(&wake_io, &f);
	chk("three shm reads", f.shm_reads, 3);
	chk("initial plus two poll delays", f.delays, 3);
	chku("poll delay", f.last_delay, OB_D11_WAKE_POLL_US);
}

/* The wait is bounded and never fails, even if 4 never clears. */
static void test_wait_is_bounded_and_never_fails(void)
{
	struct wake_fake f;

	wake_fake_init(&f, 0, 0x0004u);
	ob_d11_wait_for_wake_script(&wake_io, &f);
	chku("poll bound", f.shm_reads, OB_D11_WAKE_POLLS);
	chk("delays = initial + bound - 1", f.delays,
	    (int)OB_D11_WAKE_POLLS);
	chku("last delay is a poll delay", f.last_delay,
	     OB_D11_WAKE_POLL_US);
}

/*
 * The bracket case: with the suspend override active, phyreg enter/exit are
 * pure refcount bookkeeping - no MACCONTROL write and no SHM read, exactly the
 * vendor trace at 0x0925fe.
 */
static void test_phyreg_enter_exit_mmio_free_behind_suspend(void)
{
	struct wake_fake f;
	u32 ov = OB_D11_WAKE_OVERRIDE_SUSPEND;
	u8 refcount = 0;
	int ret;

	wake_fake_init(&f, 0x84020402u, 0x0000u);
	ret = ob_d11_phyreg_enter_script(&wake_io, &f, &ov, &refcount);
	chk("enter rc", ret, 0);
	chk("enter refcount", refcount, 1);
	chku("enter bitmap", ov, 6u);
	chk("enter no read", f.reads, 0);
	chk("enter no write", f.writes, 0);
	chk("enter no shm", f.shm_reads, 0);
	chk("enter no delay", f.delays, 0);

	ret = ob_d11_phyreg_exit_script(&wake_io, &f, &ov, &refcount);
	chk("exit rc", ret, 0);
	chk("exit refcount", refcount, 0);
	chku("exit bitmap leaves suspend", ov, OB_D11_WAKE_OVERRIDE_SUSPEND);
	chk("exit no read", f.reads, 0);
	chk("exit no write", f.writes, 0);
	chk("exit no shm", f.shm_reads, 0);
}

/* Without another override the enter writes bit 26 and waits, but cannot fail. */
static void test_phyreg_enter_without_override_writes_and_proceeds(void)
{
	struct wake_fake f;
	u32 ov = 0;
	u8 refcount = 0;
	int ret;

	wake_fake_init(&f, 0x00020402u, 0x0002u);
	ret = ob_d11_phyreg_enter_script(&wake_io, &f, &ov, &refcount);
	chk("enter rc", ret, 0);
	chk("enter refcount", refcount, 1);
	chku("enter bitmap", ov, OB_D11_WAKE_OVERRIDE_PHYREG);
	chku("enter wrote bit26", f.mctrl, 0x04020402u);
	chk("enter one write", f.writes, 1);
	chk("enter one delay", f.delays, 1);
	chk("enter one shm read", f.shm_reads, 1);
}

/* The 1 -> 0 transition of the last override clears bit 26. */
static void test_phyreg_exit_clears_bit26_on_last_override(void)
{
	struct wake_fake f;
	u32 ov = OB_D11_WAKE_OVERRIDE_PHYREG;
	u8 refcount = 1;
	int ret;

	wake_fake_init(&f, 0x04020402u, 0x0000u);
	ret = ob_d11_phyreg_exit_script(&wake_io, &f, &ov, &refcount);
	chk("exit rc", ret, 0);
	chk("exit refcount", refcount, 0);
	chku("exit bitmap empty", ov, 0u);
	chk("exit one write", f.writes, 1);
	chku("exit cleared bit26", f.mctrl, 0x00020402u);
}

/* Nested enter/exit and the empty-exit refusal. */
static void test_phyreg_refcount_edges(void)
{
	struct wake_fake f;
	u32 ov = OB_D11_WAKE_OVERRIDE_SUSPEND;
	u8 refcount = 0;

	wake_fake_init(&f, 0x84020402u, 0x0000u);
	chk("enter 1", ob_d11_phyreg_enter_script(&wake_io, &f, &ov,
						  &refcount), 0);
	chk("enter 2", ob_d11_phyreg_enter_script(&wake_io, &f, &ov,
						  &refcount), 0);
	chk("nested refcount", refcount, 2);
	chk("nested exit 1", ob_d11_phyreg_exit_script(&wake_io, &f, &ov,
						       &refcount), 0);
	chk("nested refcount after one exit", refcount, 1);
	chk("nested exit 2", ob_d11_phyreg_exit_script(&wake_io, &f, &ov,
						       &refcount), 0);
	chk("exit at zero refused", ob_d11_phyreg_exit_script(&wake_io, &f,
							      &ov, &refcount), -22);
	chk("refcount stays zero", refcount, 0);
	chk("no mmio in the whole sequence", f.reads + f.writes + f.shm_reads,
	    0);
}

/* A failed transition write rolls the bitmap back and does not leak a wait. */
static void test_wake_set_failure_rolls_back(void)
{
	struct wake_fake f;
	u32 ov = 0;
	u8 refcount = 0;
	int ret;

	wake_fake_init(&f, 0x00020402u, 0x0002u);
	f.fail_write = 1;
	ret = ob_d11_wake_override_set_script(&wake_io, &f, &ov,
					      OB_D11_WAKE_OVERRIDE_SUSPEND);
	chk("set failure rc", ret, -5);
	chku("bitmap rolled back", ov, 0u);
	chk("no shm read after failure", f.shm_reads, 0);

	f.fail_write = 0;
	f.reads = f.writes = f.shm_reads = f.delays = 0;
	f.fail_read = 1;
	ret = ob_d11_phyreg_enter_script(&wake_io, &f, &ov, &refcount);
	chk("enter read failure rc", ret, -5);
	chk("enter failure leaves refcount", refcount, 0);
	chku("enter failure leaves bitmap", ov, 0u);
	chk("enter failure did not write", f.writes, 0);

	chk("invalid mask refused", ob_d11_wake_override_set_script(
		&wake_io, &f, &ov, 0x8u), -22);
}

int main(void)
{
	test_order();
	test_mac_bracket_precondition();
	test_fail_closed_order();
	test_wake_override_suspend_transition();
	test_wake_override_set_is_bookkeeping_when_active();
	test_wait_spins_while_pending_then_stops();
	test_wait_is_bounded_and_never_fails();
	test_phyreg_enter_exit_mmio_free_behind_suspend();
	test_phyreg_enter_without_override_writes_and_proceeds();
	test_phyreg_exit_clears_bit26_on_last_override();
	test_phyreg_refcount_edges();
	test_wake_set_failure_rolls_back();
	if (failures)
		return 1;
	printf("ob_lifecycle_test: PASS\n");
	return 0;
}
