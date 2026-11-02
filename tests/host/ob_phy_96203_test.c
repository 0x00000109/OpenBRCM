/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ob_phy_96203.h"

#define abort() do { fprintf(stderr, "assertion at %s:%d\n", __FILE__, __LINE__); exit(1); } while (0)

#define MAX_LOG 2048

enum log_kind { LOG_READ, LOG_WRITE, LOG_DELAY, LOG_FGC };
struct record {
	enum log_kind kind;
	u8 event;
	u32 site;
	enum ob_phy_post_lpf_domain domain;
	u16 reg;
	u16 value;
};
struct fake {
	u16 phy[65536];
	u16 radio[65536];
	struct record log[MAX_LOG];
	size_t n;
	size_t fail_at;
	int poll_mode;
	u8 poll_attempt;
	u8 poll_core;
	u8 poll_read_in_attempt;
	u16 fgc;
};

static int append(struct fake *f, enum log_kind kind, u8 event, u32 site,
		  enum ob_phy_post_lpf_domain domain, u16 reg, u16 value)
{
	struct record *r;
	if (f->n >= MAX_LOG)
		return -EOVERFLOW;
	r = &f->log[f->n++];
	*r = (struct record){kind, event, site, domain, reg, value};
	return f->fail_at && f->n == f->fail_at ? -EIO : 0;
}

static int fake_read(void *ctx, u8 ev, u32 site,
		     enum ob_phy_post_lpf_domain d, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	int ret;
	if (site == 0x963d5) {
		f->poll_core = (u8)((reg >> 9) & 3);
		f->poll_attempt = 0;
		f->poll_read_in_attempt = 0;
	}
	if (site == 0x96464 || site == 0x964a2) {
		if (site == 0x96464) {
			f->poll_attempt++;
			f->poll_read_in_attempt = 1;
		} else {
			f->poll_read_in_attempt = 2;
		}
		if (f->poll_mode == 0) {
			*value = site == 0x96464 ? 2 : 1;
		} else if (f->poll_mode == 1) {
			*value = f->poll_attempt == 10 ?
				(site == 0x96464 ? 2 : 1) : 0;
		} else {
			*value = 0;
		}
	} else {
		*value = d == OB_PHY_POST_LPF_PHY ? f->phy[reg] : f->radio[reg];
	}
	ret = append(f, LOG_READ, ev, site, d, reg, *value);
	return ret;
}

static int fake_write(void *ctx, u8 ev, u32 site,
		      enum ob_phy_post_lpf_domain d, u16 reg, u16 value)
{
	struct fake *f = ctx;
	int ret = append(f, LOG_WRITE, ev, site, d, reg, value);
	if (ret)
		return ret;
	if (d == OB_PHY_POST_LPF_PHY)
		f->phy[reg] = value;
	else
		f->radio[reg] = value;
	return 0;
}

static int fake_delay(void *ctx, u8 ev, u32 site, u16 usec)
{
	return append(ctx, LOG_DELAY, ev, site, OB_PHY_POST_LPF_CONTROL, 0, usec);
}

static int fake_fgc(void *ctx, u8 ev, u32 site, bool enabled)
{
	struct fake *f = ctx;
	int ret = append(f, LOG_FGC, ev, site, OB_PHY_POST_LPF_CONTROL, 0,
			 enabled ? 1 : 0);
	if (!ret)
		f->fgc = enabled;
	return ret;
}

static const struct ob_phy_post_lpf_ops ops = {
	.read16 = fake_read, .write16 = fake_write,
	.delay_us = fake_delay, .phyclk_fgc = fake_fgc,
};

static struct ob_phy_96203_input input_for(u32 selector, u8 bound, u8 gate)
{
	return (struct ob_phy_96203_input) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .radio_class = 0,
		.acphychipid_present = true, .acphychipid = selector,
		.loop_bound_present = true, .loop_bound = bound,
		.second_loop_gate_present = true, .second_loop_gate = gate,
		.caller_gate_present = true, .pi_164_present = true, .pi_164 = 4,
	};
}

static void seed(struct fake *f)
{
	u32 i;
	memset(f, 0, sizeof(*f));
	for (i = 0; i < 65536; i++) {
		f->phy[i] = (u16)(i * 37u + 0x1234u);
		f->radio[i] = (u16)(i * 17u + 0x3456u);
	}
}

static const struct record *find_site(const struct fake *f, u32 site,
				      enum log_kind kind, size_t start)
{
	size_t i;
	for (i = start; i < f->n; i++)
		if (f->log[i].site == site && f->log[i].kind == kind)
			return &f->log[i];
	return NULL;
}

static void test_selected_and_alt_maps(void)
{
	struct fake f;
	struct ob_phy_96203_result r;
	struct ob_phy_96203_input in = input_for(0x4352, 0, 4);
	seed(&f);
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) ||
	    r.status != OB_96203_SYNTHETIC_COMPLETE ||
	    r.terminal_boundary != 0x96da0 || !f.n ||
	    f.log[0].site != 0x96250 || f.log[0].reg != 0x08ea ||
	    f.log[0].kind != LOG_READ || f.log[1].kind != LOG_WRITE ||
	    r.outer_cores_completed != 0 || r.inner_cores_completed != 0)
		abort();
	in = input_for(0x9999, 0, 4);
	seed(&f);
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) || f.log[0].reg != 0x08f2)
		abort();
	in.second_loop_gate = 3;
	seed(&f);
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) || f.n != 8 ||
	    !find_site(&f, 0x965bf, LOG_READ, 0) ||
	    !find_site(&f, 0x96d8d, LOG_READ, 0) ||
	    r.outer_cores_completed || r.inner_cores_completed)
		abort();
}

static void test_loops_polls_and_restore(void)
{
	struct fake f;
	struct ob_phy_96203_result r;
	struct ob_phy_96203_input in = input_for(0x4352, 2, 3);
	const struct record *a, *b, *c, *radio_restore;
	size_t i;
	unsigned pairs_read = 0, pairs_write = 0, delay10 = 0;
	seed(&f);
	f.poll_mode = 1;
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) ||
	    r.outer_cores_completed != 2 || r.inner_cores_completed != 2 ||
	    r.poll_success_mask != 3 || r.poll_success_attempt[0] != 10 ||
	    r.poll_success_attempt[1] != 10 || r.poll_exhausted_mask ||
	    r.terminal_boundary != 0x96da0)
		abort();
	radio_restore = find_site(&f, 0x964ff, LOG_WRITE, 0);
	a = find_site(&f, 0x9650e, LOG_WRITE, 0);
	b = find_site(&f, 0x9651d, LOG_WRITE, 0);
	c = find_site(&f, 0x9652c, LOG_WRITE, 0);
	if (!radio_restore || !a || !b || !c ||
	    !(radio_restore < a && a < b && b < c) ||
	    radio_restore->domain != OB_PHY_POST_LPF_RADIO ||
	    a->domain != OB_PHY_POST_LPF_PHY || b->domain != OB_PHY_POST_LPF_PHY ||
	    c->domain != OB_PHY_POST_LPF_PHY)
		abort();
	for (i = 0; i < f.n; i++) {
		if (f.log[i].site == 0x9642a && f.log[i].kind == LOG_DELAY &&
		    f.log[i].value == 10) delay10++;
		if (f.log[i].site >= 0x9668a && f.log[i].site <= 0x96d45) {
			if (f.log[i].kind == LOG_READ) pairs_read++;
			if (f.log[i].kind == LOG_WRITE) pairs_write++;
		}
	}
	if (delay10 != 20 || pairs_read != 28 || pairs_write != 28)
		abort();
	seed(&f); f.poll_mode = 0;
	in.loop_bound = 1;
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) ||
	    r.poll_success_mask != 1 || r.poll_success_attempt[0] != 1 ||
	    r.poll_exhausted_mask != 0)
		abort();

	seed(&f); f.poll_mode = 2;
	in.loop_bound = 2;
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) ||
	    r.poll_exhausted_mask != 3 || r.poll_success_mask != 0 ||
	    r.outer_cores_completed != 2)
		abort();
	in.second_loop_gate = 4;
	seed(&f); f.poll_mode = 0;
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) ||
	    r.inner_cores_completed != 0 || find_site(&f, 0x965bf, LOG_READ, 0) ||
	    find_site(&f, 0x96d8d, LOG_READ, 0))
		abort();
}

static void test_caller_and_validation(void)
{
	struct fake f;
	struct ob_phy_96203_result r;
	struct ob_phy_96203_input in = input_for(0x4352, 0, 4);
	seed(&f);
	if (ob_phy_96203_caller_test_only(&ops, &f, &in, &r) ||
	    r.status != OB_96203_SKIPPED_BY_CALLER || f.n ||
	    r.terminal_boundary != 0xaa54f)
		abort();
	in.nested_32c = 1;
	if (ob_phy_96203_caller_test_only(&ops, &f, &in, &r) ||
	    r.status != OB_96203_SYNTHETIC_COMPLETE || !r.caller_selected)
		abort();
	in.reset_marker = 1;
	if (ob_phy_96203_caller_test_only(&ops, &f, &in, &r) ||
	    r.status != OB_96203_SYNTHETIC_COMPLETE || f.log[0].site != 0x974fa ||
	    f.log[1].site != 0x97507 || f.log[2].site != 0x9751f ||
	    f.log[5].site != 0x9754e || f.log[5].kind != LOG_FGC ||
	    f.log[6].site != 0x97555 || f.log[6].value != 2 ||
	    f.log[7].site != 0xaa542 || f.log[7].value != 1 ||
	    r.terminal_boundary != 0xaa54f)
		abort();
	in.pi_164 = 5;
	seed(&f);
	if (ob_phy_96203_caller_test_only(&ops, &f, &in, &r) ||
	    f.log[0].site != 0x9742b || f.log[0].kind != LOG_FGC)
		abort();
	seed(&f); f.fail_at = 2;
	if (ob_phy_96203_caller_test_only(&ops, &f, &in, &r) != -EIO ||
	    r.status != OB_96203_FAILED_BACKEND_OPERATION || f.n != 2 ||
	    !r.clock_fgc_enabled || r.hardware_ready || r.phy_ready ||
	    r.channel_ready)
		abort();
	in.loop_bound = 3;
	seed(&f);
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) != -EINVAL || f.n)
		abort();
	in.loop_bound = 0;
	in.acphychipid_present = false;
	seed(&f);
	if (ob_phy_96203_test_only(&ops, &f, &in, &r) != -EINVAL || f.n)
		abort();
}

static void test_fail_stop_exhaustive(void)
{
	struct fake f;
	struct ob_phy_96203_result r;
	struct ob_phy_96203_input in = input_for(0x4352, 2, 3);
	size_t total, fail;
	seed(&f); f.poll_mode = 2;
	if (ob_phy_96203_test_only(&ops, &f, &in, &r)) abort();
	total = f.n;
	for (fail = 1; fail <= total; fail++) {
		seed(&f); f.poll_mode = 2; f.fail_at = fail;
		if (ob_phy_96203_test_only(&ops, &f, &in, &r) != -EIO ||
		    r.status != OB_96203_FAILED_BACKEND_OPERATION || f.n != fail ||
		    r.callback_count != fail || r.hardware_ready || r.phy_ready ||
		    r.channel_ready)
			abort();
	}
}

static struct ob_phy_96203_live_input live_for(u8 gate, bool gate_owned)
{
	return (struct ob_phy_96203_live_input) {
		.identity_valid = true,
		.acphychipid = 0x4352,
		.acphychipid_owned = true,
		.loop_bound = 2,
		.second_loop_gate_owned = gate_owned,
		.second_loop_gate = gate,
	};
}

static unsigned phy_writes(const struct fake *f, u16 reg)
{
	unsigned n = 0;
	size_t i;

	for (i = 0; i < f->n; i++)
		if (f->log[i].kind == LOG_WRITE &&
		    f->log[i].domain == OB_PHY_POST_LPF_PHY &&
		    f->log[i].reg == reg)
			n++;
	return n;
}

/* How many of this register's writes carry one of the two allowed values.
 * A mask that is already set in the saved word makes the RMW value equal to
 * the restored one, so the check cannot classify by value alone. */
static unsigned phy_write_values(const struct fake *f, u16 reg, u16 a, u16 b)
{
	unsigned n = 0;
	size_t i;

	for (i = 0; i < f->n; i++) {
		const struct record *q = &f->log[i];
		if (q->kind == LOG_WRITE && q->domain == OB_PHY_POST_LPF_PHY &&
		    q->reg == reg && (q->value == a || q->value == b))
			n++;
	}
	return n;
}

/*
 * W96 live entry. Three properties, in this order: an unowned PI+0x16c
 * refuses with zero callbacks; an owned gate runs the block for both chains
 * with the measured masked writes and restores; a backend error stops at the
 * failing callback and issues nothing after it.
 */
static void test_live_entry(void)
{
	struct fake f;
	struct ob_phy_96203_result r;
	struct ob_phy_96203_live_input in = live_for(4, false);
	const u16 v739 = (u16)(0x739 * 37u + 0x1234u);
	const u16 v73a = (u16)(0x73a * 37u + 0x1234u);
	const u16 v725 = (u16)(0x725 * 37u + 0x1234u);
	const u16 v939 = (u16)(0x939 * 37u + 0x1234u);
	const u16 v93a = (u16)(0x93a * 37u + 0x1234u);
	const u16 v925 = (u16)(0x925 * 37u + 0x1234u);

	seed(&f);
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) != -EINVAL || f.n ||
	    r.status != OB_96203_INVALID)
		abort();

	in = live_for(4, true);
	seed(&f);
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) ||
	    r.status != OB_96203_LIVE_COMPLETE ||
	    r.outer_cores_completed != 2 || r.inner_cores_completed != 0 ||
	    r.terminal_boundary != 0x96da0 || !f.n ||
	    f.log[0].site != 0x96250 || f.log[0].reg != 0x08ea ||
	    f.log[0].kind != LOG_READ)
		abort();
	/*
	 * Per chain the block emits five PHY writes: 0x739 twice (RMW 0x0080,
	 * then restore), 0x725 twice (RMW 0x0004, then restore) and 0x73a once
	 * (restore). Ten in total for two chains.
	 */
	if (phy_writes(&f, 0x0739) != 2 ||
	    phy_write_values(&f, 0x0739, (u16)(v739 | 0x0080),
			     (u16)(v739 | 0x0080)) != 1 ||
	    phy_write_values(&f, 0x0739, v739, v739) != 1 ||
	    phy_writes(&f, 0x073a) != 1 ||
	    phy_write_values(&f, 0x073a, v73a, v73a) != 1 ||
	    phy_writes(&f, 0x0725) != 2 ||
	    phy_write_values(&f, 0x0725, (u16)(v725 | 0x0004), v725) != 2 ||
	    phy_writes(&f, 0x0939) != 2 ||
	    phy_write_values(&f, 0x0939, (u16)(v939 | 0x0080),
			     (u16)(v939 | 0x0080)) != 1 ||
	    phy_write_values(&f, 0x0939, v939, v939) != 1 ||
	    phy_writes(&f, 0x093a) != 1 ||
	    phy_write_values(&f, 0x093a, v93a, v93a) != 1 ||
	    phy_writes(&f, 0x0925) != 2 ||
	    phy_write_values(&f, 0x0925, (u16)(v925 | 0x0004), v925) != 2 ||
	    phy_writes(&f, 0x0739) + phy_writes(&f, 0x073a) +
	    phy_writes(&f, 0x0725) + phy_writes(&f, 0x0939) +
	    phy_writes(&f, 0x093a) + phy_writes(&f, 0x0925) != 10) {
		fprintf(stderr, "counts: 739 %u/%u/%u 73a %u/%u 725 %u/%u "
			"939 %u/%u/%u 93a %u/%u 925 %u/%u\n",
			phy_writes(&f, 0x0739),
			phy_write_values(&f, 0x0739, (u16)(v739 | 0x80),
					 (u16)(v739 | 0x80)),
			phy_write_values(&f, 0x0739, v739, v739),
			phy_writes(&f, 0x073a), phy_write_values(&f, 0x073a, v73a, v73a),
			phy_writes(&f, 0x0725),
			phy_write_values(&f, 0x0725, (u16)(v725 | 4), v725),
			phy_writes(&f, 0x0939),
			phy_write_values(&f, 0x0939, (u16)(v939 | 0x80),
					 (u16)(v939 | 0x80)),
			phy_write_values(&f, 0x0939, v939, v939),
			phy_writes(&f, 0x093a), phy_write_values(&f, 0x093a, v93a, v93a),
			phy_writes(&f, 0x0925),
			phy_write_values(&f, 0x0925, (u16)(v925 | 4), v925));
		abort();
	}
	if (f.phy[0x739] != v739 || f.phy[0x73a] != v73a ||
	    f.phy[0x725] != v725 || f.phy[0x939] != v939 ||
	    f.phy[0x93a] != v93a || f.phy[0x925] != v925) {
		fprintf(stderr,
			"restore mismatch: 739 %04x/%04x 73a %04x/%04x "
			"725 %04x/%04x 939 %04x/%04x 93a %04x/%04x 925 %04x/%04x\n",
			f.phy[0x739], v739, f.phy[0x73a], v73a,
			f.phy[0x725], v725, f.phy[0x939], v939,
			f.phy[0x93a], v93a, f.phy[0x925], v925);
		abort();
	}

	in.loop_bound = 0;
	seed(&f);
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) != -EINVAL || f.n)
		abort();
	in = live_for(4, true);
	in.loop_bound = 3;
	seed(&f);
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) != -EINVAL || f.n)
		abort();
	in = live_for(4, true);
	in.identity_valid = false;
	seed(&f);
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) != -EINVAL || f.n)
		abort();

	in = live_for(4, true);
	seed(&f);
	f.fail_at = 4;
	if (ob_phy_96203_live_run(&ops, &f, &in, &r) != -EIO ||
	    r.status != OB_96203_FAILED_BACKEND_OPERATION || f.n != 4 ||
	    r.callback_count != 4)
		abort();
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--trace")) {
		struct fake f;
		struct ob_phy_96203_result r;
		struct ob_phy_96203_input in;
		size_t i;
		if (argc != 5 && argc != 6) return 2;
		in = input_for((u32)strtoul(argv[2], NULL, 0),
			       (u8)strtoul(argv[3], NULL, 0),
			       (u8)strtoul(argv[4], NULL, 0));
		seed(&f);
		f.poll_mode = argc == 6 ? (int)strtol(argv[5], NULL, 0) : 0;
		if (ob_phy_96203_test_only(&ops, &f, &in, &r)) return 3;
		for (i = 0; i < f.n; i++)
			printf("%u %u %x %u %x %x\n", (unsigned)f.log[i].kind,
			       f.log[i].event, f.log[i].site,
			       (unsigned)f.log[i].domain, f.log[i].reg,
			       f.log[i].value);
		printf("RESULT %u %u %u %u\n", r.logical_event_count,
		       r.callback_count, r.terminal_boundary, r.poll_success_mask);
		return 0;
	}
	test_selected_and_alt_maps();
	test_loops_polls_and_restore();
	test_caller_and_validation();
	test_live_entry();
	test_fail_stop_exhaustive();
	puts("ob_phy_96203_test: PASS");
	return 0;
}
