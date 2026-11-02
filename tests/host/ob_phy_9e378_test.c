/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_9e378.h"
#include "../../src/ob_phy_9e378_vector.h"

#define MAX_TRACE 256u
#define MAX_WORDS 384u
#define MAX_REGS 256u

struct record {
	char kind;
	enum ob_phy_9e378_domain domain;
	u8 event;
	u32 site;
	u16 reg;
	u16 value;
	u8 width;
	u8 table;
	u16 offset;
	u16 count;
	u16 word_count;
	u16 words[MAX_WORDS];
};

struct reg_cell {
	enum ob_phy_9e378_domain domain;
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

static struct reg_cell *reg_find(struct fake *f,
				 enum ob_phy_9e378_domain domain, u16 reg)
{
	unsigned int i;

	for (i = 0; i < f->reg_count; i++)
		if (f->regs[i].domain == domain && f->regs[i].reg == reg)
			return &f->regs[i];
	if (f->reg_count == MAX_REGS)
		return NULL;
	f->regs[i].domain = domain;
	f->regs[i].reg = reg;
	if (domain == OB_PHY_9E378_PHY && reg == 0x019e)
		f->regs[i].value = 0xa540;
	else
		f->regs[i].value = (u16)((reg * 37u) ^
			(domain == OB_PHY_9E378_RADIO ? 0x5a5au : 0x1357u));
	f->reg_count++;
	return &f->regs[i];
}

static int record(struct fake *f, char kind,
		  enum ob_phy_9e378_domain domain, u8 event,
		  u32 site, u16 reg, u16 value, u8 width,
		  u8 table, u16 offset, u16 count,
		  const u16 *words, u16 word_count)
{
	struct record *r;

	if (f->trace_count == MAX_TRACE || word_count > MAX_WORDS)
		return -12;
	r = &f->trace[f->trace_count++];
	*r = (struct record) {
		.kind = kind, .domain = domain, .event = event, .site = site,
		.reg = reg, .value = value, .width = width, .table = table,
		.offset = offset, .count = count, .word_count = word_count,
	};
	if (words && word_count)
		memcpy(r->words, words, word_count * sizeof(words[0]));
	return f->fail_at == f->trace_count ? -5 : 0;
}

static int fake_read(void *ctx, u8 event, u32 site,
		     enum ob_phy_9e378_domain domain, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct reg_cell *cell = reg_find(f, domain, reg);
	int ret;

	if (!cell || !value)
		return -22;
	ret = record(f, 'R', domain, event, site, reg, cell->value, 16,
		     0, 0, 0, NULL, 0);
	if (!ret)
		*value = cell->value;
	return ret;
}

static int fake_write(void *ctx, u8 event, u32 site,
		      enum ob_phy_9e378_domain domain, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct reg_cell *cell;
	int ret = record(f, 'W', domain, event, site, reg, value, 16,
			 0, 0, 0, NULL, 0);

	if (ret)
		return ret;
	cell = reg_find(f, domain, reg);
	if (!cell)
		return -12;
	cell->value = value;
	return 0;
}

static int fake_table_write(void *ctx, u8 event, u32 site, u8 table,
			    u16 offset, u16 count, u8 width,
			    const u16 *words, u16 word_count)
{
	return record(ctx, 'T', OB_PHY_9E378_TABLE, event, site, 0, 0, width,
		      table, offset, count, words, word_count);
}

static const struct ob_phy_9e378_ops fake_ops = {
	.read16 = fake_read,
	.write16 = fake_write,
	.table_write = fake_table_write,
};

static struct ob_phy_9e378_fixture selected_fixture(
		const struct ob_phy_9e378_variant *v, u32 ext_value)
{
	return (struct ob_phy_9e378_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.chanspec_supplied = true, .requested_chanspec = 0x1001,
		.caller_gate_7b = 0, .caller_gate_79 = 1, .nested_32c = 1,
		.selectors_supplied = true, .extpagain2g = ext_value,
		.nested_411 = v->pdgain_write ? 9 : 17,
		.txchain_low2 = v->txchain_low2,
		.shared_80_supplied = true, .shared_80 = 0,
	};
}

static struct ob_phy_9e378_fixture skipped_fixture(void)
{
	return (struct ob_phy_9e378_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.caller_gate_7b = 0, .caller_gate_79 = 0, .nested_32c = 0,
	};
}

static const struct ob_phy_9e378_variant *variant_by_id(const char *id,
							 unsigned int *index)
{
	unsigned int i;

	for (i = 0; i < OB_PHY_9E378_VARIANT_COUNT; i++)
		if (!strcmp(ob_phy_9e378_variants[i].id, id)) {
			if (index)
				*index = i;
			return &ob_phy_9e378_variants[i];
		}
	return NULL;
}

static const char *domain_name(enum ob_phy_9e378_domain domain)
{
	switch (domain) {
	case OB_PHY_9E378_PHY: return "PHY";
	case OB_PHY_9E378_RADIO: return "RADIO";
	default: return "TABLE";
	}
}

static void dump_records(const struct fake *fake)
{
	unsigned int i;

	for (i = 0; i < fake->trace_count; i++) {
		const struct record *r = &fake->trace[i];
		unsigned int j;

		if (r->kind != 'T') {
			printf("%c,%s,%u,0x%05x,0x%04x,0x%08x,%u\n",
			       r->kind, domain_name(r->domain), r->event, r->site,
			       r->reg, r->value, r->width);
			continue;
		}
		printf("T,WRITE,%u,0x%05x,%u,0x%04x,%u,%u",
		       r->event, r->site, r->table, r->offset, r->count, r->width);
		if (r->width == 48)
			printf(",%u", r->word_count);
		for (j = 0; j < r->word_count; j++)
			printf(",0x%04x", r->words[j]);
		putchar('\n');
	}
}

static int dump_variant(const char *id)
{
	const struct ob_phy_9e378_variant *v = variant_by_id(id, NULL);
	struct ob_phy_9e378_fixture fixture;
	struct ob_phy_9e378_result result;
	struct fake fake = { 0 };
	int ret;

	if (!v)
		return 2;
	fixture = selected_fixture(v, v->extpagain2g_eq2 ? 2 : 3);
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake,
						   &fixture, &result);
	if (ret)
		return 3;
	dump_records(&fake);
	return 0;
}

static void test_variants_and_fail_stop(void)
{
	unsigned int i;

	for (i = 0; i < OB_PHY_9E378_VARIANT_COUNT; i++) {
		const struct ob_phy_9e378_variant *v = &ob_phy_9e378_variants[i];
		struct ob_phy_9e378_fixture fixture = selected_fixture(v,
			v->extpagain2g_eq2 ? 2 : 3);
		struct ob_phy_9e378_result result;
		struct fake fake = { 0 };
		unsigned int fail_at;
		int ret = ob_phy_acphy_9e378_fixture_test_only(
			&fake_ops, &fake, &fixture, &result);

		check("variant runner succeeds", ret, 0);
		check("selected result", result.kind,
		      OB_PHY_9E378_RESULT_SYNTHETIC_VARIANT_COMPLETE);
		check("selected variant index", result.variant_index, i);
		check("logical event count", result.logical_event_count,
		      v->logical_event_count);
		check("callback count", result.callback_count,
		      v->callback_count);
		check("record count", fake.trace_count, v->callback_count);
		check("completion not hardware readiness", result.hardware_ready, 0);
		check("first operation is entry snapshot", fake.trace[0].site, 0x9e395);
		check("last operation is normal restore", fake.trace[fake.trace_count - 1].site,
		      0x9eae7);

		for (fail_at = 1; fail_at <= v->callback_count; fail_at++) {
			struct fake failed = { .fail_at = fail_at };
			struct ob_phy_9e378_result failed_result;
			int error = ob_phy_acphy_9e378_fixture_test_only(
				&fake_ops, &failed, &fixture, &failed_result);

			check("injected callback error is unchanged", error, -5);
			check("no callback after injected error", failed.trace_count,
			      fail_at);
			check("failure publishes no completion", failed_result.kind,
			      OB_PHY_9E378_RESULT_INVALID);
			check("failure never claims readiness", failed_result.hardware_ready, 0);
		}
	}
}

static void test_skip_and_rejections(void)
{
	struct ob_phy_9e378_fixture fixture = skipped_fixture();
	struct ob_phy_9e378_result result;
	struct ob_phy_9e378_ops missing = fake_ops;
	struct fake fake = { 0 };
	int ret;

	missing.read16 = NULL;
	ret = ob_phy_acphy_9e378_fixture_test_only(&missing, &fake, &fixture,
							   &result);
	check("caller skip succeeds without operation callbacks", ret, 0);
	check("caller skip result", result.kind, OB_PHY_9E378_RESULT_SKIPPED);
	check("caller skip has no callbacks", fake.trace_count, 0);
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.selectors_supplied = false;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake, &fixture,
							   &result);
	check("missing selectors rejected", ret, -22);
	check("missing selectors rejected before access", fake.trace_count, 0);
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.chanspec_supplied = false;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake, &fixture,
							   &result);
	check("missing controlled chanspec rejected", ret, -22);
	check("missing chanspec rejected before access", fake.trace_count, 0);
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.txchain_low2 = 4;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake, &fixture,
							   &result);
	check("invalid txchain selector rejected", ret, -22);
	check("invalid selector rejected before access", fake.trace_count, 0);
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.caller_gate_79 = 0;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake, &fixture,
							   &result);
	check("inconsistent reaching caller state rejected", ret, -22);
	check("inconsistent state rejected before access", fake.trace_count, 0);
	missing = fake_ops;
	missing.read16 = NULL;
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	ret = ob_phy_acphy_9e378_fixture_test_only(&missing, &fake, &fixture,
							   &result);
	check("missing read callback rejected before selected access", ret, -22);
	check("missing read callback has no access", fake.trace_count, 0);
	missing = fake_ops;
	missing.write16 = NULL;
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	ret = ob_phy_acphy_9e378_fixture_test_only(&missing, &fake, &fixture,
							   &result);
	check("missing write callback rejected before selected access", ret, -22);
	check("missing write callback has no access", fake.trace_count, 0);
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.chip_revision = 2;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake, &fixture,
							   &result);
	check("invalid target identity rejected before access", ret, -95);
	check("invalid target identity has no access", fake.trace_count, 0);
	missing = fake_ops;
	missing.table_write = NULL;
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	ret = ob_phy_acphy_9e378_fixture_test_only(&missing, &fake, &fixture,
							   &result);
	check("missing table callback rejected", ret, -22);
	check("missing table callback rejected before access", fake.trace_count, 0);
}

static void test_saved_phy_019e_bit_restore(void)
{
	static const u16 initial_values[] = { 0xa540, 0xb542 };
	unsigned int i;

	for (i = 0; i < sizeof(initial_values) / sizeof(initial_values[0]); i++) {
		const struct ob_phy_9e378_variant *v = &ob_phy_9e378_variants[0];
	struct ob_phy_9e378_fixture fixture = selected_fixture(v, 2);
	struct ob_phy_9e378_result result;
	struct fake fake = { 0 };
	struct reg_cell *entry;
	int ret;

		fake.regs[0] = (struct reg_cell) {
			.domain = OB_PHY_9E378_PHY, .reg = 0x019e,
			.value = initial_values[i],
		};
		fake.reg_count = 1;
		ret = ob_phy_acphy_9e378_fixture_test_only(
			&fake_ops, &fake, &fixture, &result);
		check("snapshot restore scenario succeeds", ret, 0);
		entry = reg_find(&fake, OB_PHY_9E378_PHY, 0x019e);
		check("saved PHY 0x019e bit and unrelated bits restored",
		      entry ? entry->value : -1, initial_values[i]);
	}
}

static void test_selector_equivalence_and_payload_endpoints(void)
{
	const struct ob_phy_9e378_variant *ne_variant = NULL;
	struct ob_phy_9e378_fixture fixture;
	struct ob_phy_9e378_result result;
	struct fake fake = { 0 };
	unsigned int i;
	int ret;

	for (i = 0; i < OB_PHY_9E378_VARIANT_COUNT; i++)
		if (!ob_phy_9e378_variants[i].extpagain2g_eq2 &&
		    !ob_phy_9e378_variants[i].pdgain_write &&
		    ob_phy_9e378_variants[i].txchain_low2 == 0)
			ne_variant = &ob_phy_9e378_variants[i];
	check("NE2 variant exists", ne_variant != NULL, true);
	if (!ne_variant)
		return;
	fixture = selected_fixture(ne_variant, 1);
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake,
						   &fixture, &result);
	check("extpagain2g=1 selects the NE2 equivalence arm", ret, 0);
	check("extpagain2g=1 chooses the NE2 variant", result.variant_index,
	      (long)(ne_variant - ob_phy_9e378_variants));
	memset(&fake, 0, sizeof(fake));
	fixture = selected_fixture(&ob_phy_9e378_variants[0], 2);
	fixture.nested_411 = 16;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_ops, &fake,
						   &fixture, &result);
	check("nested+0x411=16 selects write arm", ret, 0);
	check("nested+0x411=16 selects pdgain write variant", result.variant_index, 0);
	if (fake.trace_count > 6) {
		const struct record *gain = &fake.trace[6];
		check("width-48 source table count", gain->count, 128);
		check("width-48 source payload words", gain->word_count, 384);
		check("width-48 payload first word", gain->words[0], 0x0044);
		check("width-48 payload last word", gain->words[383], 0xf70f);
	}
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--dump"))
		return dump_variant(argv[2]);
	test_variants_and_fail_stop();
	test_skip_and_rejections();
	test_saved_phy_019e_bit_restore();
	test_selector_equivalence_and_payload_endpoints();
	if (failures) {
		printf("ob_phy_9e378_test: %u failure(s)\n", failures);
		return 1;
	}
	puts("ob_phy_9e378_test: all fake-backend checks passed");
	return 0;
}
