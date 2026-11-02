/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_phy_sub92500.h"
#include "../../src/ob_phy.h"

struct fake_shm {
	u16 value;
	u16 offset;
	u16 written;
	unsigned int reads;
	unsigned int writes;
	int fail_read;
	int fail_write;
};

static int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL %s got=%ld expected=%ld\n", name, got, expected);
		failures++;
	}
}

static int read_shm(void *ctx, u16 offset, u16 *value)
{
	struct fake_shm *f = ctx;

	f->reads++;
	f->offset = offset;
	if (f->fail_read)
		return -5;
	*value = f->value;
	return 0;
}

static int write_shm(void *ctx, u16 offset, u16 value)
{
	struct fake_shm *f = ctx;

	f->writes++;
	f->offset = offset;
	f->written = value;
	if (f->fail_write)
		return -5;
	f->value = value;
	return 0;
}

int main(void)
{
	const struct ob_phy_sub92500_ops ops = {
		.read_shm16 = read_shm, .write_shm16 = write_shm,
	};
	struct fake_shm f = { .value = 0xdeadu };
	struct ob_phy_init_lifecycle lifecycle = { 0 };
	bool activated = true;
	int ret;

	check("rev1 attach predicate", ob_phy_nested_912_for_revision(1), 1);
	check("rev0 attach predicate", ob_phy_nested_912_for_revision(0), 1);
	check("rev2 skips attach predicate", ob_phy_nested_912_for_revision(2), 0);
	ret = ob_phy_sub92500_run(&ops, &f, false, &activated);
	check("disabled helper succeeds", ret, 0);
	check("disabled helper result", activated, 0);
	check("disabled helper performs no read", f.reads, 0);

	activated = false;
	ret = ob_phy_sub92500_run(&ops, &f, true, &activated);
	check("sentinel helper succeeds", ret, 0);
	check("sentinel helper activates", activated, 1);
	check("sentinel read offset", f.offset, 0x184);
	check("sentinel write zero", f.written, 0);
	check("sentinel helper one read", f.reads, 1);
	check("sentinel helper one write", f.writes, 1);

	memset(&f, 0, sizeof(f));
	f.value = 0x1234;
	activated = true;
	ret = ob_phy_sub92500_run(&ops, &f, true, &activated);
	check("non-sentinel helper succeeds", ret, 0);
	check("non-sentinel remains inactive", activated, 0);
	check("non-sentinel does not write", f.writes, 0);

	memset(&f, 0, sizeof(f));
	f.fail_read = 1;
	activated = true;
	ret = ob_phy_sub92500_run(&ops, &f, true, &activated);
	check("read error propagated", ret, -5);
	check("read error prevents write", f.writes, 0);
	check("read error result false", activated, 0);

	memset(&f, 0, sizeof(f));
	f.value = 0xdeadu;
	f.fail_write = 1;
	activated = false;
	ret = ob_phy_sub92500_run(&ops, &f, true, &activated);
	check("write error propagated", ret, -5);
	check("write error does not report activation", activated, 0);

	check("null ops rejected", ob_phy_sub92500_run(NULL, &f, true,
							  &activated), -22);
	check("null result rejected", ob_phy_sub92500_run(&ops, &f, true,
							  NULL), -22);
	check("lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check("lifecycle pre-radio ready", ob_phy_init_advance(&lifecycle,
						 OB_PHY_INIT_PRE_RADIO_READY), 0);
	check("lifecycle begin radio", ob_phy_init_begin_radio(&lifecycle), 0);
	lifecycle.radio_prefix_complete = true;
	lifecycle.analog_tx_lpf_complete = true;
	check("lifecycle records selected row",
	      ob_phy_init_post_lpf_lookup_complete(&lifecycle, true, 1, 63,
						   0x1c8c), 0);
	check("lifecycle stops after rev1 helper",
	      ob_phy_init_block(&lifecycle,
			OB_PHY_BLOCK_REV1_AFTER_SUB92500), 0);
	check("rev1 continuation remains blocked",
	      lifecycle.phase, OB_PHY_INIT_BLOCKED);
	check("rev1 continuation cannot claim operational",
	      ob_phy_init_is_operational(&lifecycle), 0);
	if (failures) {
		printf("ob_phy_sub92500: %d failures\n", failures);
		return 1;
	}
	puts("ob_phy_sub92500: PASS (instruction-derived; synthetic SHM)");
	return 0;
}
