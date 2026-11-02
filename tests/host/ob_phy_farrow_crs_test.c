/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ob_phy_farrow_crs.h"

#define MAX_TRACE 80u
#define MAX_REGS 64u

struct record {
	char kind;
	u16 event;
	u32 site;
	u16 reg;
	u16 value;
	u8 table_id;
	u16 offset;
	u8 count;
	u8 width;
	u8 chain;
	u8 invocation;
	u8 ordinal;
	enum ob_phy_farrow_api_call api;
	u32 max_wait;
};

struct reg_cell {
	u16 reg;
	u16 value;
};

struct fake {
	struct record trace[MAX_TRACE];
	struct reg_cell regs[MAX_REGS];
	u16 trace_count;
	u16 reg_count;
	u16 fail_at;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct reg_cell *find_reg(struct fake *f, u16 reg)
{
	u16 i;

	for (i = 0; i < f->reg_count; i++)
		if (f->regs[i].reg == reg)
			return &f->regs[i];
	if (f->reg_count == MAX_REGS)
		return NULL;
	f->regs[i].reg = reg;
	f->regs[i].value = reg == 0x019e ? 0xa540 :
		(reg == 0x0601 ? 0x2468 : (u16)((reg * 37u) ^ 0x1357u));
	f->reg_count++;
	return &f->regs[i];
}

static u16 get_reg(struct fake *f, u16 reg)
{
	struct reg_cell *cell = find_reg(f, reg);

	return cell ? cell->value : 0;
}

static int append(struct fake *f, const struct record *r)
{
	if (f->trace_count == MAX_TRACE)
		return -ENOSPC;
	f->trace[f->trace_count++] = *r;
	return f->fail_at == f->trace_count ? -EIO : 0;
}

static int fake_read(void *ctx, u16 event, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct reg_cell *cell = find_reg(f, reg);
	struct record r = { .kind = 'R', .event = event, .site = site,
		.reg = reg, .width = 16 };
	int ret;

	if (!cell || !value)
		return -EINVAL;
	r.value = cell->value;
	ret = append(f, &r);
	if (!ret)
		*value = cell->value;
	return ret;
}

static int fake_write(void *ctx, u16 event, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct reg_cell *cell = find_reg(f, reg);
	struct record r = { .kind = 'W', .event = event, .site = site,
		.reg = reg, .value = value, .width = 16 };
	int ret;

	if (!cell)
		return -ENOSPC;
	ret = append(f, &r);
	if (!ret)
		cell->value = value;
	return ret;
}

static int fake_table_read(void *ctx, u16 event, u32 site, u8 table_id,
			   u16 offset, u8 count, u8 width, u8 chain,
			   u8 invocation, u8 ordinal, u16 supplied,
			   u16 *observed)
{
	struct fake *f = ctx;
	struct record r = { .kind = 'T', .event = event, .site = site,
		.table_id = table_id, .offset = offset, .count = count,
		.width = width, .chain = chain, .invocation = invocation,
		.ordinal = ordinal, .value = supplied };
	int ret;

	if (!observed)
		return -EINVAL;
	ret = append(f, &r);
	if (!ret)
		*observed = supplied;
	return ret;
}

static int fake_table_write(void *ctx, u16 event, u32 site, u8 table_id,
			    u16 offset, u8 count, u8 width, u8 chain,
			    u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = 't', .event = event, .site = site,
		.table_id = table_id, .offset = offset, .count = count,
		.width = width, .chain = chain, .value = value };

	return append(f, &r);
}

static int fake_api(void *ctx, u16 event, u32 site,
		    enum ob_phy_farrow_api_call api, u32 max_wait)
{
	struct fake *f = ctx;
	struct record r = { .kind = 'A', .event = event, .site = site,
		.api = api, .max_wait = max_wait };

	return append(f, &r);
}

static const struct ob_phy_farrow_crs_ops fake_ops = {
	.read16 = fake_read,
	.write16 = fake_write,
	.table_read16 = fake_table_read,
	.table_write16 = fake_table_write,
	.api_call = fake_api,
};

static struct ob_phy_farrow_fixture fixture(void)
{
	return (struct ob_phy_farrow_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.phy_revision = 0x0a,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_class = 0,
		.chanspec = 0x1001,
		.pi_16e = 0,
		.pi_168 = 2,
		.pi_164 = 0x0a,
		.nested_348 = 0,
		.nested_912 = 0,
		.shared_a5 = 3,
		.qword_40_nonnull = true,
		.qword_40_row = {
			.present = true, .channel_key = 1, .row_index = 63,
			.frequency_word = 0x096c,
			.phy_tail = { 0x03c9, 0x03c5, 0x03c1,
				      0x043a, 0x043f, 0x0443 },
		},
		.rx_farrow_row = {
			.present = true, .channel_key = 1, .row_index = 109,
			.words = { 0xcccd, 0x0069, 0x1400, 0x00a7 },
		},
		.tx_farrow_row = {
			.present = true, .channel_key = 1, .row_index = 109,
			.words = { 0x91c3, 0x005a, 0x91c3, 0x005a },
		},
		.table_samples = {
			{ .valid = true, .table_id = 7, .offset = 0x03cd,
			  .element_count = 1, .element_width_bits = 16,
			  .chain_index = 0, .invocation_id = 1,
			  .sample_ordinal = 1, .value = 0x1234 },
			{ .valid = true, .table_id = 7, .offset = 0x03dd,
			  .element_count = 1, .element_width_bits = 16,
			  .chain_index = 1, .invocation_id = 1,
			  .sample_ordinal = 2, .value = 0x5678 },
		},
	};
}

static int run(struct fake *f, struct ob_phy_farrow_fixture *fixture,
	       const struct ob_phy_farrow_crs_ops *ops,
	       struct ob_phy_farrow_crs_result *result)
{
	return ob_phy_acphy_farrow_crs_fixture_test_only(ops, f, fixture, result);
}

static void test_selected_trace_and_register_state(u16 initial_019e,
						   u16 sample0, u16 sample1)
{
	struct ob_phy_farrow_fixture f = fixture();
	struct ob_phy_farrow_crs_result result;
	struct fake fake = { 0 };
	int ret;

	f.table_samples[0].value = sample0;
	f.table_samples[1].value = sample1;
	find_reg(&fake, 0x019e)->value = initial_019e;
	ret = run(&fake, &f, &fake_ops, &result);
	check("selected return", ret, 0);
	check("selected result", result.kind,
	      OB_PHY_FARROW_CRS_HOST_SIMULATION_COMPLETE);
	check("logical events", result.logical_events_completed,
	      OB_PHY_FARROW_CRS_LOGICAL_EVENTS);
	check("callback records", result.callback_records,
	      OB_PHY_FARROW_CRS_CALLBACKS);
	check("trace callback count", fake.trace_count,
	      OB_PHY_FARROW_CRS_CALLBACKS);
	check("first global event id", fake.trace[0].event, 4264);
	check("first event site", fake.trace[0].site, 0x0a4d36);
	check("first register", fake.trace[0].reg, 0x019a);
	check("first payload", fake.trace[0].value, 0xcccd);
	check("first callback kind", fake.trace[0].kind, 'W');
	check("first API boundary", fake.trace[37].site, 0x92e22);
	check("bounded suspend maximum", fake.trace[37].max_wait, 148580);
	check("last global event id", fake.trace[72].event, 4316);
	check("last callback is enable MAC", fake.trace[72].api,
	      OB_PHY_FARROW_ENABLE_MAC);
	check("last callback site", fake.trace[72].site, 0x92e53);
	check("terminal return boundary", result.terminal_return_site, 0x0a5fad);
	check("saved PHY 0601 copied", fake.trace[13].value, 0x2468);
	check("TABLE read snapshot 0", fake.trace[17].value, sample0);
	check("TABLE read snapshot 1", fake.trace[19].value, sample1);
	check("PHY 019e bit restored", get_reg(&fake, 0x019e) & 2,
	      initial_019e & 2);
	check("historical exact remains false", result.historically_exact, 0);
	check("hardware ready remains false", result.hardware_ready, 0);
	check("PHY ready remains false", result.phy_ready, 0);
	check("channel ready remains false", result.channel_ready, 0);
}

static void test_skip_and_prevalidation(void)
{
	struct ob_phy_farrow_fixture f = fixture();
	struct ob_phy_farrow_crs_result result;
	struct fake fake = { 0 };
	int ret;

	f.qword_40_nonnull = false;
	f.qword_40_row.present = false;
	ret = run(&fake, &f, NULL, &result);
	check("caller skip return", ret, 0);
	check("caller skip result", result.kind,
	      OB_PHY_FARROW_CRS_SKIPPED_BY_CALLER);
	check("caller skip callbacks", fake.trace_count, 0);

	f = fixture();
	f.qword_48_nonnull = true;
	ret = run(&fake, &f, &fake_ops, &result);
	check("unsupported output slot", ret, -EOPNOTSUPP);
	check("unsupported slot before access", fake.trace_count, 0);

	f = fixture();
	f.table_samples[0].offset++;
	ret = run(&fake, &f, &fake_ops, &result);
	check("bad keyed table snapshot", ret, -EINVAL);
	check("bad snapshot before access", fake.trace_count, 0);

	f = fixture();
	f.chip_revision++;
	ret = run(&fake, &f, &fake_ops, &result);
	check("bad target profile", ret, -EINVAL);
	check("bad profile before access", fake.trace_count, 0);
	check("bad target result is invalid", result.kind,
	      OB_PHY_FARROW_CRS_INVALID);

	f = fixture();
	f.chanspec++;
	ret = run(&fake, &f, &fake_ops, &result);
	check("incompatible chanspec", ret, -EINVAL);
	check("incompatible chanspec before access", fake.trace_count, 0);

	f = fixture();
	f.qword_40_row.present = false;
	ret = run(&fake, &f, &fake_ops, &result);
	check("missing qword row object", ret, -EINVAL);
	check("missing row before access", fake.trace_count, 0);

	f = fixture();
	ret = run(&fake, &f, NULL, &result);
	check("missing callback set", ret, -EINVAL);
	check("missing callbacks before access", fake.trace_count, 0);
}

static void test_exhaustive_fail_stop(void)
{
	unsigned int index;

	for (index = 1; index <= OB_PHY_FARROW_CRS_CALLBACKS; index++) {
		struct ob_phy_farrow_fixture f = fixture();
		struct ob_phy_farrow_crs_result result;
		struct fake fake = { .fail_at = (u16)index };
		int ret = run(&fake, &f, &fake_ops, &result);

		if (ret != -EIO || fake.trace_count != index ||
		    result.kind != OB_PHY_FARROW_CRS_FAILED_BACKEND ||
		    result.callback_records != index || result.hardware_ready ||
		    result.phy_ready || result.channel_ready ||
		    result.logical_events_completed >= OB_PHY_FARROW_CRS_LOGICAL_EVENTS) {
			printf("FAIL: fail-stop position %u ret=%d callbacks=%u\n",
			       index, ret, fake.trace_count);
			failures++;
		}
	}
}

static void test_restore_both_original_bit_states(void)
{
	test_selected_trace_and_register_state(0xa540, 0x1234, 0x5678);
	test_selected_trace_and_register_state(0xa542, 0xabcd, 0x9876);
}

static const char *api_name(enum ob_phy_farrow_api_call api)
{
	switch (api) {
	case OB_PHY_FARROW_SUSPEND_MAC_AND_WAIT: return "SUSPEND_MAC_AND_WAIT";
	case OB_PHY_FARROW_PHYREG_ENTER: return "PHYREG_ENTER";
	case OB_PHY_FARROW_PHYREG_EXIT: return "PHYREG_EXIT";
	case OB_PHY_FARROW_ENABLE_MAC: return "ENABLE_MAC";
	}
	return "INVALID";
}

static void dump_json(const struct fake *f,
		      const struct ob_phy_farrow_crs_result *result)
{
	u16 i;
	printf("{\"result\":%d,\"logical_events\":%u,\"callbacks\":%u,\"historically_exact\":false,\"hardware_ready\":false,\"records\":[",
	       result->kind, result->logical_events_completed,
	       result->callback_records);
	for (i = 0; i < f->trace_count; i++) {
		const struct record *r = &f->trace[i];
		if (i)
			putchar(',');
		if (r->kind == 'R' || r->kind == 'W') {
			printf("{\"kind\":\"%c\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"PHY\",\"register\":\"0x%04x\",\"width_bits\":16,\"value\":\"0x%04x\"}",
			       r->kind, r->event, r->site, r->reg, r->value);
		} else if (r->kind == 'T') {
			printf("{\"kind\":\"TABLE_READ\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"TABLE\",\"table_id\":%u,\"offset\":\"0x%03x\",\"element_count\":%u,\"element_width_bits\":%u,\"chain_index\":%u,\"invocation_id\":%u,\"sample_ordinal\":%u,\"value\":\"0x%04x\"}",
			       r->event, r->site, r->table_id, r->offset,
			       r->count, r->width, r->chain, r->invocation,
			       r->ordinal, r->value);
		} else if (r->kind == 't') {
			printf("{\"kind\":\"TABLE_WRITE\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"TABLE\",\"table_id\":%u,\"offset\":\"0x%03x\",\"element_count\":%u,\"element_width_bits\":%u,\"chain_index\":%u,\"value\":\"0x%04x\"}",
			       r->event, r->site, r->table_id, r->offset,
			       r->count, r->width, r->chain, r->value);
		} else {
			printf("{\"kind\":\"API_CALL\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"operation\":\"%s\",\"max_declared_wait_us\":%u}",
			       r->event, r->site, api_name(r->api), r->max_wait);
		}
	}
	puts("]}");
}

int main(int argc, char **argv)
{
	if (argc == 6 && !strcmp(argv[1], "--dump")) {
		struct ob_phy_farrow_fixture f = fixture();
		struct ob_phy_farrow_crs_result result;
		struct fake fake = { 0 };
		int ret;
		find_reg(&fake, 0x0601)->value = (u16)strtoul(argv[2], NULL, 0);
		find_reg(&fake, 0x019e)->value = (u16)strtoul(argv[3], NULL, 0);
		f.table_samples[0].value = (u16)strtoul(argv[4], NULL, 0);
		f.table_samples[1].value = (u16)strtoul(argv[5], NULL, 0);
		ret = run(&fake, &f, &fake_ops, &result);
		if (ret)
			return 2;
		dump_json(&fake, &result);
		return 0;
	}
	test_restore_both_original_bit_states();
	test_skip_and_prevalidation();
	test_exhaustive_fail_stop();
	if (failures) {
		printf("%u Farrow/CRS C checks failed\n", failures);
		return 1;
	}
	puts("Farrow/CRS fake-backend tests passed; failure positions=73");
	return 0;
}
