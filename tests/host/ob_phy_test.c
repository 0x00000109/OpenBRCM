#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/ob_phy.h"
#include "../../src/ob_phy_lpf.h"

struct fake_phy { u16 value; int fail_read; int fail_write; int reads; int writes; };
static int read16(void *ctx, u16 reg, u16 *value)
{
	struct fake_phy *f = ctx;
	(void)reg;
	if (f->fail_read) return -5;
	f->reads++; *value = f->value; return 0;
}
static int write16(void *ctx, u16 reg, u16 value)
{
	struct fake_phy *f = ctx;
	(void)reg;
	if (f->fail_write) return -5;
	f->writes++; f->value = value; return 0;
}
static int failures;
static void check(const char *name, long got, long expected)
{
	if (got != expected) { printf("FAIL: %s got %ld expected %ld\n", name, got, expected); failures++; }
}

struct lpf_event { char kind; u16 reg; u16 value; u8 width_bits; };
struct fake_lpf {
	u16 r173a;
	u16 r1725;
	u16 r019e;
	/* 3736 pinned LPF events; the S1 mirror is a separate stage since
	 * 2026-10-28 and is not issued here. */
	struct lpf_event events[3736 + 64];
	unsigned int count;
	unsigned int fail_at;
	unsigned int mutate_019e_before_read_at;
	u16 mutate_019e_xor;
};

static int fake_lpf_record(struct fake_lpf *f, char kind, u16 reg, u16 value)
{
	if (f->count >= sizeof(f->events) / sizeof(f->events[0]))
		return -12;
	f->events[f->count++] = (struct lpf_event) {
		.kind = kind, .reg = reg, .value = value, .width_bits = 16,
	};
	return f->fail_at && f->count == f->fail_at ? -5 : 0;
}

static int fake_lpf_read16(void *ctx, u16 reg, u16 *value)
{
	struct fake_lpf *f = ctx;
	u16 sampled;
	int ret;

	if (!value)
		return -22;
	if (reg == 0x019e && f->mutate_019e_before_read_at == f->count + 1)
		f->r019e ^= f->mutate_019e_xor;
	switch (reg) {
	case 0x173a: sampled = f->r173a; break;
	case 0x1725: sampled = f->r1725; break;
	case 0x019e: sampled = f->r019e; break;
	default: sampled = 0; break;
	}
	ret = fake_lpf_record(f, 'R', reg, sampled);
	if (ret)
		return ret;
	*value = sampled;
	return 0;
}

static int fake_lpf_write16(void *ctx, u16 reg, u16 value)
{
	struct fake_lpf *f = ctx;
	int ret = fake_lpf_record(f, 'W', reg, value);

	if (ret)
		return ret;
	switch (reg) {
	/* These fake reads are explicit volatile snapshots, not a RAM shadow. */
	case 0x173a: break;
	case 0x1725: break;
	case 0x019e: f->r019e = value; break;
	default: break;
	}
	return 0;
}

static const struct lpf_event lpf_expected[] = {
	{ 'W', 0x0410, 0x0077, 16 }, { 'W', 0x173e, 0x0000, 16 },
	{ 'W', 0x1725, 0x0000, 16 }, { 'W', 0x1722, 0x0000, 16 },
	{ 'W', 0x1723, 0x0000, 16 }, { 'W', 0x1724, 0x0000, 16 },
	{ 'W', 0x1725, 0x0000, 16 }, { 'W', 0x1726, 0x0000, 16 },
	{ 'W', 0x1727, 0x0000, 16 }, { 'W', 0x1750, 0x0000, 16 },
	{ 'W', 0x1728, 0x0080, 16 }, { 'W', 0x1720, 0x0180, 16 },
	{ 'W', 0x1729, 0x0000, 16 }, { 'W', 0x1721, 0x5000, 16 },
	{ 'R', 0x173a, 0x0201, 16 }, { 'W', 0x173a, 0x0301, 16 },
	{ 'R', 0x1725, 0x0005, 16 }, { 'W', 0x1725, 0x0405, 16 },
	{ 'R', 0x019e, 0xa540, 16 }, { 'R', 0x019e, 0xa540, 16 },
	{ 'W', 0x019e, 0xa542, 16 },
};

static void lpf_fake_init(struct fake_lpf *f)
{
	memset(f, 0, sizeof(*f));
	f->r173a = 0x0201;
	f->r1725 = 0x0005;
	f->r019e = 0xa540;
}

static int run_complete_lpf(struct fake_lpf *f, u16 *saved)
{
	const struct ob_phy_access_ops ops = {
		.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
	};

	return ob_phy_acphy_lpf_complete_test_only(&ops, f, 0x4352, 0x0a,
						  saved);
}

static int emit_complete_lpf_trace(void)
{
	struct fake_lpf f;
	u16 saved = 0xffff;
	unsigned int i;
	int ret;

	lpf_fake_init(&f);
	ret = run_complete_lpf(&f, &saved);
	if (ret || f.count != 3736)
		return 1;
	printf("{\"scenario\":\"SYNTHETIC_FIXED_SAMPLES\",\"events\":[");
	for (i = 0; i < f.count; i++) {
		if (i)
			putchar(',');
		printf("{\"kind\":\"%c\",\"register\":%u,\"value\":%u}",
		       f.events[i].kind, f.events[i].reg, f.events[i].value);
	}
	printf("],\"saved_019e\":%u,\"callback_count\":%u}\n", saved, f.count);
	return 0;
}

static void test_lpf_complete(void)
{
	const struct ob_phy_access_ops ops = {
		.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
	};
	struct fake_lpf f;
	u16 saved;
	unsigned int i;
	int ret;

	lpf_fake_init(&f);
	saved = 0xffff;
	ret = run_complete_lpf(&f, &saved);
	check("complete LPF returns success", ret, 0);
	check("complete LPF callback count", f.count, 3736);
	check("complete LPF publishes saved state", saved, 0xa540);
	check("descriptor 0 table-id setup", f.events[21].reg, 0x000d);
	check("descriptor 0 table-id value", f.events[21].value, 0x0001);
	check("descriptor 0 offset setup", f.events[22].reg, 0x000e);
	check("descriptor 0 first data port", f.events[23].reg, 0x000f);
	check("descriptor 0 first data value", f.events[23].value, 0x0000);
	check("last descriptor last table event reg", f.events[3732].reg, 0x000f);
	check("last descriptor last table event value", f.events[3732].value, 0x068c);
	check("restore read precedes restore write", f.events[3733].kind, 'R');
	check("restore read register", f.events[3733].reg, 0x019e);
	check("restore write register", f.events[3734].reg, 0x019e);
	check("restore write bit 1 clear", f.events[3734].value, 0xa540);
	check("terminal helper write register", f.events[3735].reg, 0x1645);
	check("terminal helper write value", f.events[3735].value, 0x025c);
	check("restored register matches saved bit", f.r019e, 0xa540);

	/* Set-bit snapshot also remains set after the selected-bit restore. */
	lpf_fake_init(&f);
	f.r019e = 0xa542;
	saved = 0xffff;
	ret = run_complete_lpf(&f, &saved);
	check("set-bit LPF returns success", ret, 0);
	check("set-bit original snapshot returned", saved, 0xa542);
	check("set-bit restored rather than cleared", f.r019e, 0xa542);

	/* A later unrelated-bit change is retained by the tail masked merge. */
	lpf_fake_init(&f);
	f.mutate_019e_before_read_at = 3734;
	f.mutate_019e_xor = 0x8000;
	saved = 0xffff;
	ret = run_complete_lpf(&f, &saved);
	check("unrelated-bit scenario succeeds", ret, 0);
	check("tail preserves current unrelated bit", f.r019e, 0x2540);

	/* Every callback index is a fail-stop point, including both RMW accesses. */
	for (i = 1; i <= 3736; i++) {
		lpf_fake_init(&f);
		f.fail_at = i;
		saved = 0xdead;
		ret = run_complete_lpf(&f, &saved);
		if (ret != -5 || f.count != i || saved != 0xdead) {
			char label[80];
			snprintf(label, sizeof(label), "failure injection callback %u", i);
			check(label, (ret == -5 && f.count == i && saved == 0xdead), 1);
			break;
		}
	}

	{
		const struct ob_phy_access_ops missing = {
			.read16 = NULL, .write16 = fake_lpf_write16,
		};
		lpf_fake_init(&f);
		saved = 0xdead;
		check("complete LPF missing read rejected",
		      ob_phy_acphy_lpf_complete_test_only(&missing, &f, 0x4352,
							 0x0a, &saved), -22);
		check("invalid complete LPF performs no callbacks", f.count, 0);
		check("invalid complete LPF preserves output", saved, 0xdead);
		check("complete LPF wrong chip rejected",
		      ob_phy_acphy_lpf_complete_test_only(&ops, &f, 0x4353,
							 0x0a, &saved), -95);
		check("wrong-chip complete LPF performs no callbacks", f.count, 0);
		check("complete LPF wrong revision rejected",
		      ob_phy_acphy_lpf_complete_test_only(&ops, &f, 0x4352,
							 3, &saved), -95);
		check("wrong-revision complete LPF performs no callbacks", f.count, 0);
	}
}

static void test_lpf_register_prefix(void)
{
	const struct ob_phy_access_ops ops = {
		.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
	};
	const struct ob_phy_access_ops missing_read = {
		.read16 = NULL, .write16 = fake_lpf_write16,
	};
	const struct ob_phy_access_ops missing_write = {
		.read16 = fake_lpf_read16, .write16 = NULL,
	};
	struct fake_lpf f;
	u16 saved_019e = 0xffff;
	static const unsigned int failpoints[] = { 1, 10, 19, 21 };
	unsigned int i;
	int ret;

	lpf_fake_init(&f);
	ret = ob_phy_acphy_lpf_register_prefix_test_only(&ops, &f, 0x4352, 0x0a,
							&saved_019e);
	check("LPF prefix succeeds", ret, 0);
	check("LPF prefix event count", f.count,
	      sizeof(lpf_expected) / sizeof(lpf_expected[0]));
	check("LPF prefix returns saved PHY 019e", saved_019e, 0xa540);
	check("LPF prefix sets only historical bit 1", f.r019e, 0xa542);
	for (i = 0; i < sizeof(lpf_expected) / sizeof(lpf_expected[0]) && i < f.count; i++) {
		char label[48];
		snprintf(label, sizeof(label), "LPF event %u kind", i + 1);
		check(label, f.events[i].kind, lpf_expected[i].kind);
		snprintf(label, sizeof(label), "LPF event %u register", i + 1);
		check(label, f.events[i].reg, lpf_expected[i].reg);
		snprintf(label, sizeof(label), "LPF event %u width", i + 1);
		check(label, f.events[i].width_bits, lpf_expected[i].width_bits);
		snprintf(label, sizeof(label), "LPF event %u value", i + 1);
		check(label, f.events[i].value, lpf_expected[i].value);
	}
	check("LPF exact first event register", f.events[0].reg, 0x0410);
	check("LPF exact first event value", f.events[0].value, 0x0077);
	check("rev3-only 1728 value not selected", f.events[10].value, 0x0080);
	check("rev3-only 1720 value not selected", f.events[11].value, 0x0180);
	check("LPF prelude stops before table port", f.events[f.count - 1].reg, 0x019e);
	for (i = 0; i < f.count; i++)
		if (f.events[i].kind == 'W' && (f.events[i].reg == 0x000d ||
							 f.events[i].reg == 0x000e ||
							 f.events[i].reg == 0x000f))
			check("no descriptor table write before declared stop", 1, 0);

	/* Reject missing dependencies and unselected PHY revision before any access. */
	lpf_fake_init(&f);
	check("LPF missing backend rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(NULL, &f, 0x4352, 0x0a, &saved_019e), -22);
	check("LPF missing output rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(&ops, &f, 0x4352, 0x0a, NULL), -22);
	check("LPF other chip rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(&ops, &f, 0x4353, 0x0a, &saved_019e), -95);
	check("LPF other PHY revision rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(&ops, &f, 0x4352, 3, &saved_019e), -95);
	check("LPF missing read callback rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(&missing_read, &f, 0x4352, 0x0a, &saved_019e), -22);
	check("LPF missing write callback rejected",
	      ob_phy_acphy_lpf_register_prefix_test_only(&missing_write, &f, 0x4352, 0x0a, &saved_019e), -22);
	check("LPF invalid entry does no I/O", f.count, 0);

	/* Failure at first, middle, and last backend operation is terminal. */
	for (i = 0; i < sizeof(failpoints) / sizeof(failpoints[0]); i++) {
		unsigned int fail_at = failpoints[i];

		lpf_fake_init(&f);
		f.fail_at = fail_at;
		saved_019e = 0xffff;
		ret = ob_phy_acphy_lpf_register_prefix_test_only(&ops, &f, 0x4352, 0x0a,
									&saved_019e);
		check("LPF injected failure propagated", ret, -5);
		check("LPF no transaction follows failure", f.count, fail_at);
		if (fail_at >= 19)
			check("failed LPF prefix does not publish saved state", saved_019e, 0xffff);
	}
}

static void prepare_radio_prefix(struct ob_phy_init_lifecycle *lifecycle)
{
	memset(lifecycle, 0, sizeof(*lifecycle));
	(void)ob_phy_init_begin(lifecycle);
	(void)ob_phy_init_advance(lifecycle, OB_PHY_INIT_PRE_RADIO_READY);
	(void)ob_phy_init_begin_radio(lifecycle);
	(void)ob_phy_init_radio_prefix_complete(lifecycle);
}

static void test_lpf_lifecycle_stage(void)
{
	static struct fake_lpf rev10_trace;
	static const struct ob_phy_lpf_target target = {
		.identity_valid = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_core_id = 0x812,
		.d11_revision = 42,
		.phy_type = 0x0b,
		.phy_revision = 0x0a,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_revision_class = 0,
	};
	struct ob_phy_lpf_target wrong_target = target;
	struct ob_phy_init_lifecycle lifecycle;
	struct fake_lpf f;
	u16 saved = 0xdead;
	int ret;

	wrong_target.phy_revision = (u8)(0xcb01u & 0x000fu);
	check("PHY rev1 selects shared LPF instruction arm",
	      ob_phy_acphy_lpf_input_status(&wrong_target, true, 0),
	      OB_PHY_LPF_INPUTS_SUPPORTED);
	check("PHY rev1 still requires validated boardflags3",
	      ob_phy_acphy_lpf_input_status(&wrong_target, false, 0),
	      OB_PHY_LPF_INPUT_BOARDFLAGS3);
	wrong_target = target;
	check("missing cached rev11 boardflags3 remains unavailable",
	      ob_phy_acphy_lpf_input_status(&target, false, 0),
	      OB_PHY_LPF_INPUT_BOARDFLAGS3);
	check("boardflags3 nested bit selects unsupported LPF arm",
	      ob_phy_acphy_lpf_input_status(&target, true, 1u << 7),
	      OB_PHY_LPF_INPUT_BOARDFLAGS3_BRANCH);
	check("complete synthetic LPF inputs classified separately",
	      ob_phy_acphy_lpf_input_status(&target, true, 0),
	      OB_PHY_LPF_INPUTS_SUPPORTED);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &target, true, 0, &lifecycle, &saved);
	check("LPF lifecycle stage succeeds", ret, 0);
	check("LPF lifecycle full ordered trace", f.count, 3736);
	check("LPF lifecycle publishes saved state after success", saved, 0xa540);
	check("LPF lifecycle success token", lifecycle.analog_tx_lpf_complete, 1);
	check("LPF lifecycle leaves radio sequence incomplete",
	      lifecycle.radio_sequence_complete, 0);
	check("LPF lifecycle remains in progress", lifecycle.phase,
	      OB_PHY_INIT_RADIO_IN_PROGRESS);
	check("LPF result cannot falsely advance radio completion",
	      ob_phy_init_advance(&lifecycle, OB_PHY_INIT_RADIO_COMPLETE), -95);
	lpf_fake_init(&rev10_trace);
	{
		u16 rev10_saved = 0xdead;

		check("rev10 reference trace succeeds",
		      ob_phy_acphy_lpf_complete_test_only(
			      & (const struct ob_phy_access_ops){
				      .read16 = fake_lpf_read16,
				      .write16 = fake_lpf_write16,
			      }, &rev10_trace, 0x4352, 0x0a,
			      &rev10_saved), 0);
	}

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	saved = 0xdead;
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &wrong_target, true, 0, &lifecycle, &saved);
	check("PHY rev1 LPF full trace succeeds", ret, 0);
	check("PHY rev1 LPF full trace length", f.count, rev10_trace.count);
	if (f.count == rev10_trace.count) {
		unsigned int i;

		for (i = 0; i < rev10_trace.count; i++) {
			if (f.events[i].kind != rev10_trace.events[i].kind ||
			    f.events[i].reg != rev10_trace.events[i].reg ||
			    f.events[i].value != rev10_trace.events[i].value) {
				check("PHY rev1/rev10 LPF trace equivalence", 0, 1);
				break;
			}
		}
	} else {
		check("PHY rev1/rev10 LPF trace length equivalence", 0, 1);
	}
	check("PHY rev1 LPF restores original bit", f.r019e, 0xa540);
	check("PHY rev1 LPF publishes completion token",
	      lifecycle.analog_tx_lpf_complete, 1);
	check("PHY rev1 LPF cannot claim radio complete",
	      ob_phy_init_advance(&lifecycle, OB_PHY_INIT_RADIO_COMPLETE), -95);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	f.fail_at = 100;
	saved = 0xdead;
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &wrong_target, true, 0, &lifecycle, &saved);
	check("PHY rev1 LPF failure propagated", ret, -5);
	check("PHY rev1 LPF failure stops callbacks", f.count, 100);
	check("PHY rev1 LPF failure preserves output", saved, 0xdead);
	check("PHY rev1 LPF failure cannot publish completion",
	      lifecycle.analog_tx_lpf_complete, 0);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	wrong_target.phy_revision = 3;
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &wrong_target, true, 0, &lifecycle, &saved);
	check("wrong LPF target rejected", ret, -95);
	check("wrong LPF target has no hardware access", f.count, 0);
	check("wrong LPF target leaves completion token clear",
	      lifecycle.analog_tx_lpf_complete, 0);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &target, false, 0, &lifecycle, &saved);
	check("invalid SPROM boardflags3 rejected", ret, -95);
	check("invalid SPROM boardflags3 has no access", f.count, 0);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &target, true, 1u << 7, &lifecycle, &saved);
	check("unsupported boardflags3 bit-7 branch rejected", ret, -95);
	check("unsupported boardflags3 bit-7 branch has no access", f.count, 0);

	memset(&lifecycle, 0, sizeof(lifecycle));
	(void)ob_phy_init_begin(&lifecycle);
	(void)ob_phy_init_advance(&lifecycle, OB_PHY_INIT_PRE_RADIO_READY);
	(void)ob_phy_init_begin_radio(&lifecycle);
	lpf_fake_init(&f);
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &target, true, 0, &lifecycle, &saved);
	check("LPF cannot run before radio prefix success", ret, -95);
	check("missing radio prefix produces no accesses", f.count, 0);

	prepare_radio_prefix(&lifecycle);
	lpf_fake_init(&f);
	f.fail_at = 3736;
	saved = 0xdead;
	ret = ob_phy_acphy_lpf_stage_run(
		& (const struct ob_phy_access_ops){
			.read16 = fake_lpf_read16, .write16 = fake_lpf_write16,
		}, &f, &target, true, 0, &lifecycle, &saved);
	check("LPF terminal write failure propagated", ret, -5);
	check("LPF failed stage does not publish output", saved, 0xdead);
	check("LPF failed stage does not publish token",
	      lifecycle.analog_tx_lpf_complete, 0);
}

int main(int argc, char **argv)
{
	const struct ob_phy_access_ops ops = { .read16 = read16, .write16 = write16 };
	struct fake_phy f = { .value = 0xa5a5 };
	if (argc == 2 && strcmp(argv[1], "--emit-lpf-trace") == 0)
		return emit_complete_lpf_trace();
	check("clear", ob_phy_rmw16(&ops, &f, 0x019e, 2, 0), 0);
	check("clear preserves", f.value, 0xa5a5 & (u16)~2u);
	check("set", ob_phy_rmw16(&ops, &f, 0x019e, 1, 1), 0);
	check("set preserves", f.value, (0xa5a5 & (u16)~2u) | 1u);
	f.fail_read = 1; check("read failure", ob_phy_rmw16(&ops, &f, 0x019e, 2, 2), -5);
	f.fail_read = 0; f.fail_write = 1; check("write failure", ob_phy_rmw16(&ops, &f, 0x019e, 1, 0), -5);
	f.fail_write = 0; f.value = 0xffff;
	check("ffff payload succeeds", ob_phy_rmw16(&ops, &f, 0x0003, 0x0100, 0), 0);
	check("ffff reg3 clear", f.value, 0xfeff);
	check("missing ops", ob_phy_rmw16(NULL, &f, 0x019e, 1, 1), -22);
	test_lpf_register_prefix();
	test_lpf_complete();
	test_lpf_lifecycle_stage();
	printf("ob_phy_test: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
