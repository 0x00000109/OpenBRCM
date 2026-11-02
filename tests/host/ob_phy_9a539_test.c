/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ob_phy_9a539.h"

#define MAX_LOG 512
enum kind { SUSPEND, READ, WRITE, ACI, ENABLE, TABLE, RADIO_READ, RADIO_WRITE };
struct rec {
	enum kind kind; u32 site; u16 reg, value;
	u8 argument, chain, offset, count, width, length, payload[8];
};
struct fake { struct rec log[MAX_LOG]; unsigned n, fail;
	u16 regs[0x0c00], radio_regs[0x2000]; };
static int push(struct fake *f, enum kind k, u32 site, u16 reg, u16 value)
{
	assert(f->n < MAX_LOG);
	f->log[f->n++] = (struct rec){ .kind = k, .site = site,
		.reg = reg, .value = value };
	return f->fail == f->n ? -73 : 0;
}
static int suspend_mac(void *p, u32 s) { return push(p, SUSPEND, s, 0, 0); }
static int enable_mac(void *p, u32 s) { return push(p, ENABLE, s, 0, 0); }
static int aci(void *p, u32 s, u8 mode) { return push(p, ACI, s, 0, mode); }
static int rd(void *p, u32 s, u16 r, u16 *v)
{
	struct fake *f = p;
	int e = push(f, READ, s, r, f->regs[r]);
	if (!e) *v = f->regs[r];
	return e;
}
static int wr(void *p, u32 s, u16 r, u16 v)
{
	struct fake *f = p; int e = push(f, WRITE, s, r, v);
	if (!e) f->regs[r] = v;
	return e;
}
static int rrd(void *p, u32 s, u16 r, u16 *v)
{
	struct fake *f = p;
	int e = push(f, RADIO_READ, s, r, f->radio_regs[r]);
	if (!e) *v = f->radio_regs[r];
	return e;
}
static int rwr(void *p, u32 s, u16 r, u16 v)
{
	struct fake *f = p; int e = push(f, RADIO_WRITE, s, r, v);
	if (!e) f->radio_regs[r] = v;
	return e;
}
static int table(void *p, u32 s, u8 arg, u8 chain, u8 id, u8 off,
		 u8 count, u8 width, const u8 *payload, u8 length)
{
	struct fake *f = p;
	int e = push(f, TABLE, s, id, off);
	struct rec *r = &f->log[f->n - 1];
	r->argument = arg; r->chain = chain; r->offset = off;
	r->count = count; r->width = width; r->length = length;
	assert(length <= sizeof(r->payload));
	memcpy(r->payload, payload, length);
	return e;
}
static const struct ob_phy_9a539_ops ops = {
	.mac_suspend_and_wait = suspend_mac, .mac_enable = enable_mac,
	.aci_update = aci, .phy_read16 = rd, .phy_write16 = wr,
	.table_write = table, .radio_read16 = rrd, .radio_write16 = rwr,
};
static struct ob_phy_9a539_input input(u16 chanspec)
{
	struct ob_phy_9a539_input i = { .explicitly_synthetic = true,
		.policy = OB_9A539_SYNTHETIC_PARAMETERIZED, .chip_id = 0x4352,
		.chip_revision = 3, .phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.argument_present = true, .argument = 0, .chanspec_present = true,
		.chanspec = chanspec, .nested_8a8_present = false };
	return i;
}
static void setup_nonzero_software(
	struct ob_phy_post_farrow_software_input *s, u8 tail)
{
	memset(s, 0, sizeof(*s));
	s->explicitly_synthetic = true;
	s->entry_after_farrow_join = true;
	s->chip_id = 0x4352; s->chip_revision = 3; s->phy_revision = 0x0a;
	s->radio_id = 0x2069; s->radio_revision = 4;
	s->phy_flags_19c = 0x0206;
	s->initial_chanspec = 0xc001;
	s->pi_cache_240_present = true; s->pi_cache_240 = 0;
	s->nested_341_present = true; s->nested_341 = 1;
	s->nested_912_present = true; s->nested_912 = 0;
	s->nested_8b0_present = true; s->nested_8b0 = 0;
	s->nested_914_present = true; s->nested_914 = -1;
	s->nested_916_present = true; s->nested_916 = -1;
	s->nested_65f[0] = 1; s->nested_65f[1] = 2;
	s->nested_65f[2] = 3; s->nested_65f[3] = 4;
	s->nested_65f[4] = 5; s->nested_65f[5] = 6;
	s->nested_65f[6] = 1; s->nested_65f[7] = 8;
	s->nested_65f[8] = tail;
}
static struct ob_phy_9a539_input nonzero_input(u8 entry_tail, u8 inner_tail,
					      bool inner_config_nonzero)
{
	struct ob_phy_9a539_input i = input(0xc001);
	i.argument = entry_tail;
	i.entry_software_state_present = true;
	i.entry_software_bytes_present = true;
	i.entry_software_flags_present = true;
	setup_nonzero_software(&i.entry_software_state, entry_tail);
	i.inner_software_state_present = true;
	i.inner_software_bytes_present = true;
	i.inner_software_flags_present = true;
	setup_nonzero_software(&i.inner_software_state, inner_tail);
	if (inner_config_nonzero)
		i.inner_software_state.nested_8b0 = 1;
	i.active_core_mask_present = true; i.active_core_mask = 0;
	i.phy_mode_present = true; i.phy_mode = 0;
	i.nested_42_present = true; i.nested_42 = 0;
	i.nested_8a8_state_present = true;
	i.nested_compare_state_present = true;
	i.nested_65c = 1; i.nested_658 = 3;
	i.nested_659 = 4;
	i.nested_65a = 5; i.nested_65b = 6;
	i.nested_table_counts_present = true;
	i.nested_64b_table_count = 6;
	i.nested_64c_table_count = 7;
	return i;
}

static struct ob_phy_9a539_input nonzero_lowband_input(void)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	unsigned n;

	i.chanspec = 0x1001;
	i.entry_software_state.initial_chanspec = 0x1001;
	i.inner_software_state.initial_chanspec = 0x1001;
	i.low_band_rows_present = true;
	i.low_band_row_source =
		OB_9A539_LOW_BAND_ROWS_EXPLICIT_SYNTHETIC_FIXTURE;
	for (n = 0; n < 13; n++) {
		i.low_band_row_2cc700[n] = (u8)(0x30 + n);
		i.low_band_row_2cc730[n] = (u8)(3 + n);
		i.low_band_row_2cc710[n * 2] = (u8)(0x20 + n);
		i.low_band_row_2cc710[n * 2 + 1] = (u8)(0x40 + n);
	}
	return i;
}

static void nonzero_lowband_failure_sweep(void)
{
	unsigned n;
	for (n = 1; n <= 126; n++) {
		struct fake f = { .fail = n };
		struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_lowband_input();

		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}

static void nonzero_lowband_path(bool print_trace)
{
	struct fake f = {0};
	struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_lowband_input();
	unsigned n;
	int ret;

	f.regs[0x019e] = 0xa5a5;
	ret = ob_phy_9a539_test_only(&ops, &f, &i, &r);
	if (ret)
		fprintf(stderr, "low-band runner ret=%d status=%d site=%x callbacks=%u\n",
			ret, r.status, r.first_unsupported_site, f.n);
	assert(ret == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.n == 126 && r.callback_count == 126);
	assert(r.logical_event_count == 68);
	assert(r.low_band_row_selected && r.low_band_row_index == 1);
	assert(r.low_band_row_count == 12);
	assert(r.low_band_phy_0299_value == 4);
	assert(r.low_band_phy_03c1_value == 0x4121);
	assert(f.log[120].kind == WRITE && f.log[120].site == 0x9a82d &&
	       f.log[120].reg == 0x0299 && f.log[120].value == 0x4404);
	assert(f.log[121].kind == WRITE && f.log[121].site == 0x9a840 &&
	       f.log[121].reg == 0x03c1 && f.log[121].value == 0x4121);
	assert(f.log[122].kind == ACI && f.log[122].site == 0x9a848);
	assert(f.log[125].kind == ENABLE && f.log[125].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5);
	assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}

static void nonzero_lowband_boundary_rows(void)
{
	struct fake f = {0};
	struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_lowband_input();

	/* Clamp nested+0x669 to 24, then select final valid byte/word row 12. */
	i.inner_software_state.nested_65f[1] = 0xff;
	for (unsigned n = 0; n < 13; n++)
		i.low_band_row_2cc700[n] = n == 12 ? 0xff : (u8)n;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.low_band_row_index == 12 && r.low_band_row_count == 12);
	assert(r.low_band_phy_0299_value == i.low_band_row_2cc730[12]);
	assert(r.low_band_phy_03c1_value == 0x4c2c);
	assert(f.n == 126 && f.log[120].value ==
	       (u16)(0x4400u | i.low_band_row_2cc730[12]));
}

static void nonzero_lowband_outer_selector(bool print_trace)
{
	struct fake f = {0};
	struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_lowband_input();
	unsigned n, k;

	f.regs[0x019e] = 0xa5a5;
	i.active_core_mask = 1;
	i.per_core_matrix_present = true;
	i.per_core_limits_present = true;
	for (k = 0; k < 6; k++) i.per_core_limits[k] = 1;
	for (n = 0; n < 2; n++) {
		u8 *row = i.per_core_matrix + n * 0x78;
		row[0x0f + n * 3] = (u8)(2 + n);
		for (k = 0; k < 6; k++)
			row[0x56 + k * 0x0a] = (u8)(1 + n * 6 + k);
	}
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE &&
	       r.terminal_boundary == 0x9a88a);
	assert(r.low_band_row_count == 12);
	/* 0x45 - clamp(.700[index 1] == 0x31, 12) = 0x39. */
	assert(r.per_core_called_mask == 1 &&
	       r.per_core_selector0_argument[0] == 0x39);
	assert(f.n == 129 && r.callback_count == 129);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		for (n = 0; n < f.n; n++) {
			unsigned b;
			if (f.log[n].kind != TABLE) continue;
			printf("TABLE %u %u %u %u %u %u %u", n,
			       f.log[n].argument, f.log[n].chain, f.log[n].offset,
			       f.log[n].count, f.log[n].width, f.log[n].length);
			for (b = 0; b < f.log[n].length; b++)
				printf(" %02x", f.log[n].payload[b]);
			printf("\n");
		}
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
		printf("SELECTORS %02x %02x\n", r.per_core_selector0_argument[0],
		       r.per_core_selector0_argument[1]);
	} else {
		unsigned fail;
		for (fail = 1; fail <= 129; fail++) {
			struct fake failed = { .fail = fail };
			struct ob_phy_9a539_result failed_result;
			struct ob_phy_9a539_input failed_input =
				nonzero_lowband_input();
			unsigned core, limit;

			failed.regs[0x019e] = 0xa5a5;
			failed_input.active_core_mask = 1;
			failed_input.per_core_matrix_present = true;
			failed_input.per_core_limits_present = true;
			for (limit = 0; limit < 6; limit++)
				failed_input.per_core_limits[limit] = 1;
			for (core = 0; core < 2; core++) {
				u8 *row = failed_input.per_core_matrix + core * 0x78;
				row[0x0f + core * 3] = (u8)(2 + core);
				for (limit = 0; limit < 6; limit++)
					row[0x56 + limit * 0x0a] =
						(u8)(1 + core * 6 + limit);
			}
			assert(ob_phy_9a539_test_only(&ops, &failed,
						      &failed_input,
						      &failed_result) == -73);
			assert(failed.n == fail && failed_result.callback_count == fail);
			assert(failed_result.status == OB_9A539_FAILED_BACKEND_OPERATION);
			assert(!failed_result.hardware_ready && !failed_result.phy_ready &&
			       !failed_result.channel_ready);
		}
	}
}
static struct ob_phy_9a539_input nonzero_998cc1_input(u8 nested_66c)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	/* The first three comparisons skip 99658/99658/998cc(1). The
	 * fourth selects 998cc(1): nested+65a differs from nested+66c. */
	i.nested_65a = 4;
	i.inner_software_state.nested_65f[4] = nested_66c;
	i.a121_state_present = true;
	i.nested_910 = 0; i.nested_911 = 0; i.nested_340 = 0;
	return i;
}
static struct ob_phy_9a539_input nonzero_99658arg1_input(void)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	memset(i.per_core_matrix, 0xa5, sizeof(i.per_core_matrix));
	i.per_core_matrix_present = true;
	i.nested_658 = 2;
	i.arg1_table_source_present = true;
	memcpy(i.arg1_table_source,
	       (const u8[]){0x11, 0x12, 0x13, 0x14, 0x15, 0x16}, 6);
	i.a121_state_present = true;
	i.nested_910 = 0; i.nested_911 = 0; i.nested_340 = 0;
	return i;
}
static struct ob_phy_9a539_input nonzero_99658arg2_input(void)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	memset(i.per_core_matrix, 0xa5, sizeof(i.per_core_matrix));
	i.per_core_matrix_present = true;
	i.nested_659 = 3;
	i.nested_340_present = true; i.nested_340 = 1;
	i.nested_3e0_present = true; i.nested_3e0 = 0;
	i.arg2_table_source_present = true;
	memcpy(i.arg2_table_source,
	       (const u8[]){0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27}, 7);
	i.a121_state_present = true;
	i.nested_910 = 0; i.nested_911 = 0;
	return i;
}
static struct ob_phy_9a539_input nonzero_998cc2_input(u8 nested_66d)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	i.nested_65b = (u8)(nested_66d ^ 1u);
	i.arg2_998cc_source_present = true;
	memcpy(i.arg2_998cc_source,
	       (const u8[]){0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16}, 7);
	i.inner_software_state.nested_65f[5] = nested_66d;
	i.a121_state_present = true;
	i.nested_910 = 0; i.nested_911 = 0; i.nested_340 = 0;
	return i;
}
static struct ob_phy_9a539_input nonzero_all_children_input(void)
{
	struct ob_phy_9a539_input i = nonzero_99658arg2_input();
	i.nested_65c = 2; /* selects both 99658 caller comparisons */
	i.nested_658 = 1;
	i.arg1_table_source_present = true;
	memcpy(i.arg1_table_source,
	       (const u8[]){0x31, 0x32, 0x33, 0x34, 0x35, 0x36}, 6);
	i.nested_65a = 2;
	i.nested_65b = 2;
	i.arg2_998cc_source_present = true;
	memcpy(i.arg2_998cc_source,
	       (const u8[]){0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47}, 7);
	return i;
}
static struct ob_phy_9a539_input nonzero_per_core_input(u8 mask)
{
	struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
	unsigned core, k;
	i.active_core_mask = mask;
	i.per_core_matrix_present = true;
	i.per_core_limits_present = true;
	for (k = 0; k < 6; k++) i.per_core_limits[k] = 1;
	for (core = 0; core < 2; core++) {
		u8 *row = i.per_core_matrix + core * 0x78;
		row[0x0f + core * 3] = (u8)(2 + core);
		for (k = 0; k < 6; k++)
			row[0x56 + k * 0x0a] = (u8)(1 + core * 6 + k);
	}
	return i;
}
static void normal_path(u16 chanspec, unsigned callbacks, unsigned logical,
		bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = input(chanspec);
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	if (callbacks) {
		assert(f.n == callbacks && r.callback_count == callbacks);
		assert(r.logical_event_count == logical);
	}
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.log[0].kind == SUSPEND && f.log[0].site == 0x9a58f);
	assert(f.log[1].kind == READ && f.log[1].site == 0x9a59c && f.log[1].reg == 0x019e);
	assert(f.log[f.n - 1].kind == ENABLE && f.log[f.n - 1].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5);
	assert(r.phy_ready == false && r.channel_ready == false);
	if (print_trace) {
		unsigned n;
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("RESULT %u %u %x\n", r.logical_event_count,
		       r.callback_count, r.terminal_boundary);
	}
}
static void failure_sweep(u16 chanspec, unsigned total)
{
	unsigned n;
	for (n = 1; n <= total; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = input(chanspec);
		f.regs[0x019e] = 0x1234;
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(r.callback_count == n && !r.hardware_ready && !r.phy_ready);
	}
}
static void nonzero_path(u8 entry_tail, u8 inner_tail,
			 bool inner_config_nonzero,
			 bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_input(entry_tail, inner_tail,
						    inner_config_nonzero);
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(f.n == 124 && r.callback_count == 124);
	assert(r.logical_event_count == 66 && r.terminal_boundary == 0x9a88a);
	assert(r.entry_nested_670_present && r.entry_nested_670 == entry_tail);
	assert(r.inner_nested_670_present && r.inner_nested_670 == inner_tail);
	assert(r.sub_909fd_invocations == 2);
	assert(f.log[0].kind == SUSPEND && f.log[0].site == 0x9a58f);
	assert(f.log[1].kind == READ && f.log[1].site == 0x9a59c);
	assert(f.log[4].kind == WRITE && f.log[4].site == 0x90ba0 &&
	       f.log[4].reg == 0x0304 && f.log[4].value == 0x5f62);
	assert(f.log[8].site == 0x8f64c && f.log[8].reg == 0x0324);
	assert(f.log[104].site == 0x8f86f && f.log[104].reg == 0x0324);
	assert(f.log[120].kind == ACI && f.log[120].site == 0x9a848);
	assert(f.log[123].kind == ENABLE && f.log[123].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5 && !r.mac_suspended);
	assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	if (print_trace) {
		unsigned n;
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_998cc1_path(u8 nested_66c, bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_998cc1_input(nested_66c);
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.n == 130 && r.callback_count == 130);
	assert(r.logical_event_count == 70);
	assert(f.log[4].kind == WRITE && f.log[4].site == 0x90ba0);
	assert(f.log[8].kind == TABLE && f.log[8].site == 0x99981);
	assert(f.log[8].argument == 1 && f.log[8].chain == 0);
	assert(f.log[8].reg == 0x0b && f.log[8].offset == 8);
	assert(f.log[8].count == 6 && f.log[8].width == 8 &&
	       f.log[8].length == 6);
	if (nested_66c == 5)
		assert(memcmp(f.log[8].payload,
			      (const u8[]){0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f}, 6) == 0);
	else if (nested_66c == 3)
		assert(memcmp(f.log[8].payload,
			      (const u8[]){0x0b, 0x0c, 0x7f, 0x7f, 0x7f, 0x7f}, 6) == 0);
	/* The TABLE child sets the caller flag, so sub_9a121(mask=0) runs
	 * before the existing sub_8f41b continuation, on the same backend. */
	assert(f.log[9].kind == READ && f.log[9].site == 0x9a1af &&
	       f.log[9].reg == 0x019e);
	assert(f.log[13].kind == WRITE && f.log[13].site == 0x9a525);
	assert(f.log[14].kind == READ && f.log[14].site == 0x8f64c);
	assert(f.log[129].kind == ENABLE && f.log[129].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5);
	assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	if (print_trace) {
		unsigned n;
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("PAYLOAD 8");
		for (n = 0; n < f.log[8].length; n++)
			printf(" %02x", f.log[8].payload[n]);
		printf("\nRESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_99658arg1_path(bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_99658arg1_input();
	static const u8 expect[4][6] = {
		{0x12, 0x12, 0x13, 0x13, 0x13, 0x13},
		{1, 1, 2, 2, 2, 2},
		{0x12, 0x12, 0x13, 0x13, 0x13, 0x13},
		{1, 1, 2, 2, 2, 2},
	};
	unsigned n;
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.n == 133 && r.callback_count == 133);
	assert(r.logical_event_count == 73 && r.sub_99658_arg1_called);
	assert(r.nested_658_after == 3 && r.sub_9a121_called);
	assert(r.coefficient_matrix_after_present);
	assert(memcmp(r.coefficient_matrix_after + 0x24,
		      (const u8[]){0x12, 0x12, 0x13, 0x13, 0x13, 0x13}, 6) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0x60,
		      (const u8[]){1, 1, 2, 2, 2, 2}, 6) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0x9c,
		      (const u8[]){0x12, 0x12, 0x13, 0x13, 0x13, 0x13}, 6) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0xd8,
		      (const u8[]){1, 1, 2, 2, 2, 2}, 6) == 0);
	for (n = 0; n < 4; n++) {
		assert(f.log[8 + n].kind == TABLE);
		assert(f.log[8 + n].site == (n & 1 ? 0x998aa : 0x99875));
		assert(f.log[8 + n].argument == 1 && f.log[8 + n].chain == n / 2);
		assert(f.log[8 + n].reg == (u16)(n % 2 ? 0x45 + 0x20 * (n / 2) :
						   0x44 + 0x20 * (n / 2)));
		assert(f.log[8 + n].offset == 8 && f.log[8 + n].count == 6 &&
		       f.log[8 + n].width == 8 && f.log[8 + n].length == 6);
		assert(memcmp(f.log[8 + n].payload, expect[n], 6) == 0);
	}
	assert(f.log[12].site == 0x9a1af && f.log[16].site == 0x9a525);
	assert(f.regs[0x019e] == 0xa5a5);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		for (n = 0; n < 4; n++) {
			unsigned j;
			printf("PAYLOAD %u", 8 + n);
			for (j = 0; j < f.log[8 + n].length; j++)
				printf(" %02x", f.log[8 + n].payload[j]);
			printf("\n");
		}
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_99658arg2_path(bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_99658arg2_input();
	static const u8 payload_a[7] = {0x22, 0x22, 0x23, 0x23,
					 0x23, 0x23, 0x23};
	static const u8 payload_b[7] = {1, 1, 2, 2, 2, 2, 2};
	unsigned n;
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.n == 133 && r.callback_count == 133);
	assert(r.logical_event_count == 73 && r.sub_99658_arg2_called);
	assert(r.coefficient_matrix_after_present);
	assert(memcmp(r.coefficient_matrix_after + 0x2e,
		      payload_a, 7) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0x6a,
		      payload_b, 7) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0xa6,
		      payload_a, 7) == 0);
	assert(memcmp(r.coefficient_matrix_after + 0xe2,
		      payload_b, 7) == 0);
	assert(r.nested_659_after == 4 && r.sub_9a121_called);
	for (n = 0; n < 4; n++) {
		const u8 *want = (n & 1) ? payload_b : payload_a;
		u8 length = (n & 1) ? 7 : 6;
		assert(f.log[8 + n].kind == TABLE);
		assert(f.log[8 + n].site == (n & 1 ? 0x998aa : 0x99875));
		assert(f.log[8 + n].argument == 2 && f.log[8 + n].chain == n / 2);
		assert(f.log[8 + n].reg == (u16)(n % 2 ? 0x45 + 0x20 * (n / 2) :
						   0x44 + 0x20 * (n / 2)));
		assert(f.log[8 + n].offset == 16 && f.log[8 + n].count == 7 &&
		       f.log[8 + n].width == 8 && f.log[8 + n].length == 7);
		assert(memcmp(f.log[8 + n].payload, want, length) == 0);
	}
	assert(f.log[12].site == 0x9a1af && f.log[16].site == 0x9a525);
	assert(f.regs[0x019e] == 0xa5a5);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		for (n = 0; n < 4; n++) {
			unsigned j;
			printf("PAYLOAD %u", 8 + n);
			for (j = 0; j < f.log[8 + n].length; j++)
				printf(" %02x", f.log[8 + n].payload[j]);
			printf("\n");
		}
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_998cc2_path(u8 nested_66d, bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_998cc2_input(nested_66d);
	u8 expected_payload[7] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
	u8 start = nested_66d < 6 ? (u8)(6 - nested_66d) : 0;
	unsigned n;
	f.regs[0x019e] = 0xa5a5;
	for (n = start; n < 7; n++) expected_payload[n] = 0x7f;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(f.n == 130 && r.callback_count == 130);
	assert(r.logical_event_count == 70 && r.sub_998cc_arg2_called);
	assert(r.nested_65b_after == nested_66d && r.sub_9a121_called);
	assert(f.log[8].kind == TABLE && f.log[8].site == 0x99981);
	assert(f.log[8].argument == 2 && f.log[8].reg == 0x0b);
	assert(f.log[8].offset == 16 && f.log[8].count == 7 &&
	       f.log[8].width == 8 && f.log[8].length == 7);
	assert(memcmp(f.log[8].payload, expected_payload, 7) == 0);
	assert(f.log[9].site == 0x9a1af && f.log[13].site == 0x9a525);
	assert(f.regs[0x019e] == 0xa5a5);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("PAYLOAD 8");
		for (n = 0; n < f.log[8].length; n++)
			printf(" %02x", f.log[8].payload[n]);
		printf("\nRESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_failure_sweep(void)
{
	unsigned n;
	for (n = 1; n <= 124; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_input(1, 1, false);
		f.regs[0x019e] = 0xa5a5;
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(r.mac_suspended == (n > 1));
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}
static void nonzero_998cc1_failure_sweep(void)
{
	unsigned n;
	for (n = 1; n <= 130; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_998cc1_input(5);
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}
static void nonzero_99658arg1_failure_sweep(void)
{
	unsigned n;
	for (n = 1; n <= 133; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_99658arg1_input();
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}
static void nonzero_arg2_failure_sweeps(void)
{
	unsigned n;
	for (n = 1; n <= 133; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_99658arg2_input();
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
	for (n = 1; n <= 130; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_998cc2_input(3);
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}
static void nonzero_all_children_path(bool print_trace)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_all_children_input();
	unsigned n;
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE &&
	       r.terminal_boundary == 0x9a88a);
	assert(f.n == 139 && r.callback_count == 139 &&
	       r.logical_event_count == 79);
	assert(r.sub_99658_arg1_called && r.sub_99658_arg2_called &&
	       r.sub_998cc_arg1_called && r.sub_998cc_arg2_called &&
	       r.sub_9a121_called);
	assert(r.coefficient_matrix_after_present);
	for (n = 0; n < 4; n++)
		assert(f.log[8 + n].kind == TABLE && f.log[8 + n].argument == 1);
	for (n = 0; n < 4; n++)
		assert(f.log[12 + n].kind == TABLE && f.log[12 + n].argument == 2);
	assert(f.log[16].kind == TABLE && f.log[16].site == 0x99981 &&
	       f.log[16].argument == 1);
	assert(f.log[17].kind == TABLE && f.log[17].site == 0x99981 &&
	       f.log[17].argument == 2);
	assert(f.log[18].kind == READ && f.log[18].site == 0x9a1af);
	assert(f.log[138].kind == ENABLE && f.log[138].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5);
	if (print_trace) {
		for (n = 0; n < f.n; n++)
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
	}
}
static void nonzero_all_children_failure_sweep(void)
{
	unsigned n;
	for (n = 1; n <= 139; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_all_children_input();
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}
static void nonzero_per_core_path(u8 mask, unsigned callbacks,
				  unsigned logical)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_per_core_input(mask);
	static const u16 expected_reg[2][2] = {{0x06dc,0x06dd},{0x08dc,0x08dd}};
	static const u16 expected_write[2][2] = {{0x0234,0x0658},{0x0590,0x0cb8}};
	static const u8 expected_table[2][2] = {{0x1a,0xc1},{0xc8,0xc2}};
	unsigned n, out_index = 8;
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE &&
	       r.terminal_boundary == 0x9a88a);
	assert(f.n == callbacks && r.callback_count == callbacks &&
	       r.logical_event_count == logical);
	assert(r.per_core_called_mask == mask);
	for (n = 0; n < 2; n++)
		if (mask & (1u << n))
			assert(r.per_core_selector0_argument[n] == 0x45);
	for (n = 0; n < 2; n++) {
		if (!(mask & (1u << n))) continue;
		assert(f.log[out_index].kind == WRITE &&
		       f.log[out_index].site == 0x99fef &&
	       f.log[out_index].reg == expected_reg[n][0] &&
	       f.log[out_index].value == expected_write[n][0]);
		assert(f.log[out_index + 1].kind == WRITE &&
	       f.log[out_index + 1].site == 0x99fff &&
	       f.log[out_index + 1].reg == expected_reg[n][1] &&
	       f.log[out_index + 1].value == expected_write[n][1]);
		assert(f.log[out_index + 2].kind == TABLE &&
	       f.log[out_index + 2].site == 0x9a04f &&
	       f.log[out_index + 2].argument == 0 &&
	       f.log[out_index + 2].chain == n &&
	       f.log[out_index + 2].reg == 0xf9 + n &&
	       f.log[out_index + 2].offset == 7 &&
	       f.log[out_index + 2].count == 1 &&
	       f.log[out_index + 2].width == 16 &&
	       f.log[out_index + 2].length == 2 &&
	       memcmp(f.log[out_index + 2].payload,
	      expected_table[n], 2) == 0);
		out_index += 3;
	}
	assert(f.log[out_index].kind == READ && f.log[out_index].site == 0x8f64c);
	assert(f.log[f.n - 1].kind == ENABLE && f.log[f.n - 1].site == 0x9a877);
	assert(f.regs[0x019e] == 0xa5a5 && !r.hardware_ready && !r.phy_ready);
}
static void nonzero_per_core_trace(u8 mask, u8 cap)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_per_core_input(mask);
	unsigned n, b;
	i.inner_software_state.nested_65f[0] = cap;
	f.regs[0x019e] = 0xa5a5;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	for (n = 0; n < f.n; n++)
		printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
		       f.log[n].site, f.log[n].reg, f.log[n].value);
	for (n = 0; n < f.n; n++) {
		if (f.log[n].kind != TABLE) continue;
		printf("TABLE %u %u %u %u %u %u %u", n,
		       f.log[n].argument, f.log[n].chain, f.log[n].offset,
		       f.log[n].count, f.log[n].width, f.log[n].length);
		for (b = 0; b < f.log[n].length; b++)
			printf(" %02x", f.log[n].payload[b]);
		printf("\n");
	}
	printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
	       (unsigned)r.callback_count, r.terminal_boundary);
	printf("SELECTORS %02x %02x\n", r.per_core_selector0_argument[0],
	       r.per_core_selector0_argument[1]);
}
static void nonzero_per_core_failure_sweep(u8 mask, unsigned callbacks)
{
	unsigned n;
	for (n = 1; n <= callbacks; n++) {
		struct fake f = { .fail = n }; struct ob_phy_9a539_result r;
		struct ob_phy_9a539_input i = nonzero_per_core_input(mask);
		assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -73);
		assert(f.n == n && r.callback_count == n);
		assert(r.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	}
}

static void table_to_active_core_matrix_path(bool print_trace)
{
	struct fake f = {0};
	struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = nonzero_99658arg1_input();
	unsigned k, n, callbacks;
	int ret;

	i.active_core_mask = 1;
	i.per_core_limits_present = true;
	for (k = 0; k < 6; k++) i.per_core_limits[k] = 1;
	f.regs[0x019e] = 0xa5a5;
	ret = ob_phy_9a539_test_only(&ops, &f, &i, &r);
	assert(ret == 0);
	assert(r.status == OB_9A539_SYNTHETIC_COMPLETE);
	assert(r.terminal_boundary == 0x9a88a);
	assert(r.sub_99658_arg1_called && r.sub_9a121_called);
	assert(r.per_core_called_mask == 1);
	assert(r.per_core_selector0_argument[0] == 0x45);
	assert(r.coefficient_matrix_after[0x24] == 0x12);
	assert(r.coefficient_matrix_after[0x25] == 0x12);
	assert(r.coefficient_matrix_after[0x26] == 0x13);
	assert(r.coefficient_matrix_after[0x27] == 0x13);
	assert(r.coefficient_matrix_after[0x60] == 1);
	assert(r.coefficient_matrix_after[0x61] == 1);
	assert(r.coefficient_matrix_after[0x62] == 2);
	assert(r.coefficient_matrix_after[0x63] == 2);
	assert(f.regs[0x019e] == 0xa5a5);
	assert(!r.hardware_ready && !r.phy_ready && !r.channel_ready);
	callbacks = f.n;
	assert(callbacks == r.callback_count);
	assert(f.log[8].kind == TABLE && f.log[8].site == 0x99875);
	assert(f.log[12].site == 0x9a1af);
	assert(f.log[callbacks - 1].kind == ENABLE &&
	       f.log[callbacks - 1].site == 0x9a877);
	if (print_trace) {
		for (n = 0; n < f.n; n++) {
			unsigned b;
			printf("%u %x %x %x\n", (unsigned)f.log[n].kind,
			       f.log[n].site, f.log[n].reg, f.log[n].value);
			if (f.log[n].kind != TABLE) continue;
			printf("TABLE %u %u %u %u %u %u %u %u", n,
			       f.log[n].argument, f.log[n].chain, f.log[n].reg,
			       f.log[n].offset, f.log[n].count, f.log[n].width,
			       f.log[n].length);
			for (b = 0; b < f.log[n].length; b++)
				printf(" %02x", f.log[n].payload[b]);
			printf("\n");
		}
		printf("RESULT %u %u %x\n", (unsigned)r.logical_event_count,
		       (unsigned)r.callback_count, r.terminal_boundary);
		printf("MATRIX");
		for (n = 0x24; n < 0x2a; n++)
			printf(" %02x", r.coefficient_matrix_after[n]);
		for (n = 0x60; n < 0x66; n++)
			printf(" %02x", r.coefficient_matrix_after[n]);
		printf("\nSELECTOR %02x\n", r.per_core_selector0_argument[0]);
	}
	for (n = 1; n <= callbacks; n++) {
		struct fake failed = { .fail = n };
		struct ob_phy_9a539_result failed_result;
		failed.regs[0x019e] = 0xa5a5;
		ret = ob_phy_9a539_test_only(&ops, &failed, &i, &failed_result);
		assert(ret == -73);
		assert(failed.n == n);
		assert(failed_result.status == OB_9A539_FAILED_BACKEND_OPERATION);
		assert(failed_result.callback_count == n);
		assert(!failed_result.hardware_ready && !failed_result.phy_ready);
	}
}
int main(int argc, char **argv)
{
	struct fake f = {0}; struct ob_phy_9a539_result r;
	struct ob_phy_9a539_input i = input(0x1001);
	unsigned n;
	if (argc == 3 && !strcmp(argv[1], "--trace")) {
		normal_path((u16)strtoul(argv[2], NULL, 0), 0, 0, true);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--trace-nonzero")) {
			nonzero_path((u8)strtoul(argv[2], NULL, 0),
				     (u8)strtoul(argv[2], NULL, 0), false, true);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--trace-nonzero-998cc1")) {
		nonzero_998cc1_path((u8)strtoul(argv[2], NULL, 0), true);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-nonzero-99658arg1")) {
		nonzero_99658arg1_path(true);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-nonzero-99658arg2")) {
		nonzero_99658arg2_path(true);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--trace-nonzero-998cc2")) {
		nonzero_998cc2_path((u8)strtoul(argv[2], NULL, 0), true);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "--trace-nonzero-per-core")) {
		nonzero_per_core_trace((u8)strtoul(argv[2], NULL, 0), 1);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "--trace-nonzero-per-core-cap")) {
		nonzero_per_core_trace((u8)strtoul(argv[2], NULL, 0),
				      (u8)strtoul(argv[3], NULL, 0));
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-table-active")) {
		table_to_active_core_matrix_path(true);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-nonzero-all-children")) {
		nonzero_all_children_path(true);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-nonzero-lowband")) {
		nonzero_lowband_path(true);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-nonzero-lowband-core")) {
		nonzero_lowband_outer_selector(true);
		return 0;
	}
	normal_path(0x1001, 30, 20, false);
	normal_path(0xc001, 28, 18, false);
	f.regs[0x019e] = 0; i.argument = 1;
	/* Nonzero execution must require its full explicit reaching-state model. */
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EINVAL);
	assert(r.status == OB_9A539_INVALID && f.n == 0);
	f = (struct fake){0}; i = nonzero_input(1, 1, false);
	i.inner_software_bytes_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EINVAL);
	assert(f.n == 0);
	nonzero_path(1, 1, false, false);
	nonzero_998cc1_path(5, false);
	nonzero_998cc1_path(3, false);
	nonzero_99658arg1_path(false);
	nonzero_99658arg2_path(false);
	nonzero_998cc2_path(3, false);
	nonzero_998cc2_path(0, false);
	nonzero_998cc2_path(6, false);
	nonzero_998cc2_path(7, false);
	nonzero_all_children_path(false);
	nonzero_all_children_failure_sweep();
	nonzero_lowband_path(false);
	nonzero_lowband_boundary_rows();
	nonzero_lowband_outer_selector(false);
	nonzero_lowband_failure_sweep();
	f = (struct fake){0};
	i = nonzero_lowband_input();
	i.low_band_row_source = OB_9A539_LOW_BAND_ROWS_UNSPECIFIED;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.first_unsupported_site == 0x9a687 && f.n == 0);
	nonzero_per_core_path(1, 127, 69);
	nonzero_per_core_path(2, 127, 69);
	nonzero_per_core_path(3, 130, 72);
	table_to_active_core_matrix_path(false);
	nonzero_per_core_failure_sweep(1, 127);
	nonzero_per_core_failure_sweep(2, 127);
	nonzero_per_core_failure_sweep(3, 130);
	nonzero_path(0x80, 0x80, false, false);
	/* The callee's second sub_909fd may change +0x670, but the captured
	 * entry argument remains the original nonzero branch selector. */
	nonzero_path(1, 0x80, true, false);
	f = (struct fake){0}; i = nonzero_input(1, 1, false);
	f.regs[0x019e] = 0x8123;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(f.regs[0x019e] == 0x8123 && r.saved_phy_019e == 0x8123);
	f = (struct fake){0}; i = nonzero_input(1, 1, false);
	i.active_core_mask = 1;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.status == OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION && f.n == 0);
	assert(r.first_unsupported_site == 0x9a7a5);
	f = (struct fake){0}; i = nonzero_input(1, 1, false);
	i.nested_658++;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.status == OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION && f.n == 0);
	assert(r.first_unsupported_site == 0x9a6e7);
	f = (struct fake){0}; i = nonzero_99658arg1_input();
	i.arg1_table_source_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.status == OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION && f.n == 0);
	assert(r.first_unsupported_site == 0x9a6e7);
	f = (struct fake){0}; i = nonzero_99658arg2_input();
	i.arg2_table_source_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.first_unsupported_site == 0x9a71a && f.n == 0);
	f = (struct fake){0}; i = nonzero_998cc2_input(3);
	i.arg2_998cc_source_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.first_unsupported_site == 0x9a75c && f.n == 0);
	f = (struct fake){0}; i = nonzero_99658arg1_input();
	i.a121_state_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.status == OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION && f.n == 0);
	assert(r.first_unsupported_site == 0x9a76a);
	f = (struct fake){0}; i = nonzero_input(1, 1, false);
	i.chanspec = 0x1001;
	i.entry_software_state.initial_chanspec = 0x1001;
	i.inner_software_state.initial_chanspec = 0x1001;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.first_unsupported_site == 0x9a687 && f.n == 0);
	nonzero_failure_sweep();
	nonzero_998cc1_failure_sweep();
	nonzero_99658arg1_failure_sweep();
	nonzero_arg2_failure_sweeps();
	f = (struct fake){0}; i = input(0x1001);
	i.policy = OB_9A539_HISTORICAL_STRICT;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EOPNOTSUPP);
	assert(r.status == OB_9A539_BLOCKED_HISTORICAL_INPUT && f.n == 0);
	f = (struct fake){0}; i = input(0x1001);
	i.nested_8a8_present = true; i.nested_8a8_byte18 = 0; i.nested_8a8_byte47 = 1;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	for (n = 0; n < f.n && f.log[n].kind != ACI; n++)
		;
	assert(n < f.n && f.log[n].value == 1);
	f = (struct fake){0}; i = input(0x1001); i.argument_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EINVAL && f.n == 0);
	f = (struct fake){0}; i = input(0x1001); i.chip_revision++;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -ENODEV && f.n == 0);
	f = (struct fake){0}; i = input(0x1001); i.chanspec_present = false;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == -EINVAL && f.n == 0);
	f = (struct fake){0}; i = input(0x1001);
	assert(ob_phy_9a539_test_only(NULL, &f, &i, &r) == -EINVAL && f.n == 0);
	f = (struct fake){0}; i = input(0x1001); f.regs[0x019e] = 0x8000;
	assert(ob_phy_9a539_test_only(&ops, &f, &i, &r) == 0);
	assert(f.regs[0x019e] == 0x8000);
	failure_sweep(0x1001, 30); failure_sweep(0xc001, 28);
	puts("sub_9a539 host test: PASS"); return 0;
}
