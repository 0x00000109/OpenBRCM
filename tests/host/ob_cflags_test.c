// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include "../../src/ob_guard.h"

struct fake {
	u32 value;
	u32 reads;
	u32 writes;
	u32 last_off;
	int read_error;
	int write_error;
	bool lost;
	bool mismatch;
};

static int read32(void *ctx, u16 off, u32 *value)
{
	struct fake *f = ctx;
	f->reads++;
	f->last_off = off;
	if (f->read_error)
		return f->read_error;
	*value = f->mismatch && f->reads > 1 ? f->value ^ 0x40 : f->value;
	return 0;
}

static int write32(void *ctx, u16 off, u32 value)
{
	struct fake *f = ctx;
	f->writes++;
	f->last_off = off;
	if (f->write_error)
		return f->write_error;
	f->value = value;
	return 0;
}

static bool lost(void *ctx)
{
	return ((struct fake *)ctx)->lost;
}

static const struct ob_checked_rmw32_io ops = {
	.read32 = read32,
	.write32 = write32,
	.device_lost = lost,
};

static int failures;
static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL %s: got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static void test_success_and_preserve(void)
{
	struct fake f = { .value = 0xa5a50080 };
	u32 next = 0;
	int ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x40, &next);

	check("success", ret, 0);
	check("new value", next, 0xa5a50040);
	check("stored value", f.value, 0xa5a50040);
	check("read count", f.reads, 2);
	check("write count", f.writes, 1);
	check("offset", f.last_off, 0x408);
}

static void test_reject_and_read_failure(void)
{
	struct fake f = { .value = 1 };
	u32 next = 0;
	int ret;

	ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x140, &next);
	check("value outside mask", ret, -22);
	check("outside mask no read", f.reads, 0);
	check("outside mask no write", f.writes, 0);
	f.read_error = -5;
	ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x40, &next);
	check("read failure", ret, -5);
	check("read failure no write", f.writes, 0);
}

static void test_loss_and_write_failure(void)
{
	struct fake f = { .value = 0 };
	u32 next = 0;
	int ret;

	f.lost = true;
	ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x40, &next);
	check("loss before access", ret, -19);
	check("loss no access", f.reads + f.writes, 0);
	f.lost = false;
	f.write_error = -5;
	ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x40, &next);
	check("write failure", ret, -5);
	check("write failure no verify", f.reads, 1);
}

static void test_readback_mismatch(void)
{
	struct fake f = { .value = 0, .mismatch = true };
	u32 next = 0;
	int ret = ob_checked_rmw32(&ops, &f, 0x408, 0xc0, 0x40, &next);

	check("readback mismatch", ret, -5);
	check("mismatch wrote once", f.writes, 1);
}

int main(void)
{
	test_success_and_preserve();
	test_reject_and_read_failure();
	test_loss_and_write_failure();
	test_readback_mismatch();
	if (failures)
		return 1;
	puts("ob_cflags_test: PASS");
	return 0;
}
