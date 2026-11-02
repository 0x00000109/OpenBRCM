/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_a6b0f.h"
#include "../../src/ob_phy_a6b0f_vector.h"
#include "../../src/ob_phy_9e378.h"
#include "../../src/ob_phy_9e378_vector.h"
#include "../../src/ob_phy_9eaf9.h"
#include "../../src/ob_phy_farrow_crs.h"
#include "../../src/ob_phy_post_farrow_continuation.h"

#define MAX_TRACE 5000u
#define MAX_REGS 256u
#define MAX_TABLE_WORDS 16u

enum fake_domain { FAKE_PHY, FAKE_RADIO, FAKE_AI_CORE, FAKE_CONTROL, FAKE_TABLE };

struct fake_record {
	char kind;
	enum fake_domain domain;
	u16 event;
	u32 site;
	u16 reg;
	u32 value;
	u8 width;
	u8 table_id;
	u16 offset;
	u16 count;
	u8 chain;
	u8 invocation;
	u8 ordinal;
	u16 payload_word_count;
	u16 words[MAX_TABLE_WORDS];
};

struct fake_reg { enum fake_domain domain; u16 reg; u32 value; };
struct fake_table_cell { u8 table_id; u16 offset; u16 value; };

struct fake_backend {
	struct fake_record trace[MAX_TRACE];
	struct fake_reg regs[MAX_REGS];
	struct fake_table_cell table_cells[256];
	unsigned int trace_count;
	unsigned int reg_count;
	unsigned int table_cell_count;
	unsigned int fail_at;
};

static unsigned int failures;
static u16 gain_dump_words[384];

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct fake_reg *reg_find(struct fake_backend *fake,
				 enum fake_domain domain, u16 reg)
{
	unsigned int i;

	for (i = 0; i < fake->reg_count; i++)
		if (fake->regs[i].domain == domain && fake->regs[i].reg == reg)
			return &fake->regs[i];
	if (fake->reg_count == MAX_REGS)
		return NULL;
	fake->regs[fake->reg_count].domain = domain;
	fake->regs[fake->reg_count].reg = reg;
	if (domain == FAKE_PHY && reg == 0x019e)
		fake->regs[fake->reg_count].value = 0xa540;
	else if (domain == FAKE_PHY && reg == 0x0140)
		fake->regs[fake->reg_count].value = 0x1237;
	else if (domain == FAKE_PHY && reg == 0x0001)
		fake->regs[fake->reg_count].value = 0x5101;
	else if (domain == FAKE_PHY && reg == 0x173a)
		fake->regs[fake->reg_count].value = 0x0201;
	else if (domain == FAKE_PHY && reg == 0x1725)
		fake->regs[fake->reg_count].value = 0x0005;
	else if (domain == FAKE_AI_CORE && reg == 0x0408)
		fake->regs[fake->reg_count].value = 0xa5a50001;
	else
		fake->regs[fake->reg_count].value =
			((u32)reg * 37u) ^ (domain == FAKE_RADIO ? 0x5a5au : 0x1357u);
	return &fake->regs[fake->reg_count++];
}

static int record(struct fake_backend *fake, char kind,
		  enum fake_domain domain, u16 event, u32 site,
		  u16 reg, u32 value, u8 width, struct fake_record **out)
{
	struct fake_record *record;

	if (fake->trace_count == MAX_TRACE)
		return -12;
	record = &fake->trace[fake->trace_count++];
	*record = (struct fake_record) {
		.kind = kind, .domain = domain, .event = event, .site = site,
		.reg = reg, .value = value, .width = width,
	};
	if (out)
		*out = record;
	return fake->fail_at == fake->trace_count ? -5 : 0;
}

static enum fake_domain map_domain(enum ob_phy_post_lpf_domain domain)
{
	switch (domain) {
	case OB_PHY_POST_LPF_PHY: return FAKE_PHY;
	case OB_PHY_POST_LPF_RADIO: return FAKE_RADIO;
	case OB_PHY_POST_LPF_CONTROL: return FAKE_CONTROL;
	default: return FAKE_CONTROL;
	}
}

static int fake_read16(void *ctx, u8 event, u32 site,
		       enum ob_phy_post_lpf_domain domain, u16 reg, u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	u16 sampled;
	int ret;

	if (!value || domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	entry = reg_find(fake, map_domain(domain), reg);
	if (!entry)
		return -12;
	sampled = (u16)entry->value;
	if (event == 0 && domain == OB_PHY_POST_LPF_PHY) {
		if (reg == 0x173a)
			sampled = 0x0201;
		else if (reg == 0x1725)
			sampled = 0x0005;
	}
	ret = record(fake, 'R', map_domain(domain), event, site, reg, sampled, 16, NULL);
	if (ret)
		return ret;
	*value = sampled;
	return 0;
}

static int fake_write16(void *ctx, u8 event, u32 site,
			enum ob_phy_post_lpf_domain domain, u16 reg, u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret;

	if (domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	ret = record(fake, 'W', map_domain(domain), event, site, reg, value, 16, NULL);
	if (ret)
		return ret;
	entry = reg_find(fake, map_domain(domain), reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_read_ai_core32(void *ctx, u8 event, u32 site,
			       u16 reg, u32 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry = reg_find(fake, FAKE_AI_CORE, reg);
	int ret;

	if (!value || !entry)
		return -22;
	ret = record(fake, 'R', FAKE_AI_CORE, event, site, reg, entry->value, 32, NULL);
	if (ret)
		return ret;
	*value = entry->value;
	return 0;
}

static int fake_write_ai_core32(void *ctx, u8 event, u32 site,
				u16 reg, u32 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret = record(fake, 'W', FAKE_AI_CORE, event, site, reg, value, 32, NULL);

	if (ret)
		return ret;
	entry = reg_find(fake, FAKE_AI_CORE, reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_delay(void *ctx, u8 event, u32 site, u16 usec)
{
	return record(ctx, 'D', FAKE_CONTROL, event, site, 0, usec, 0, NULL);
}

static int fake_phyclk(void *ctx, u8 event, u32 site, bool enabled)
{
	return record(ctx, 'F', FAKE_CONTROL, event, site, 0,
		      enabled ? 1u : 0u, 0, NULL);
}

static int fake_table_read(void *ctx, u8 event, u32 site, u8 table,
			   u16 offset, u16 count, u8 width, u16 *values)
{
	struct fake_backend *fake = ctx;
	struct fake_record *record_ptr = NULL;
	struct fake_table_cell *cell = NULL;
	u16 sample;
	unsigned int i;
	int ret;

	if (!values || count != 1 || width != 16)
		return -22;
	/* The first read uses an explicit synthetic seed; later reads observe the
	 * persistent fake table state, including prior writes to the same entry. */
	for (i = 0; i < fake->table_cell_count; i++)
		if (fake->table_cells[i].table_id == table &&
		    fake->table_cells[i].offset == offset) {
			cell = &fake->table_cells[i];
			break;
		}
	if (!cell) {
		if (fake->table_cell_count ==
		    sizeof(fake->table_cells) / sizeof(fake->table_cells[0]))
			return -12;
		cell = &fake->table_cells[fake->table_cell_count++];
		cell->table_id = table;
		cell->offset = offset;
		cell->value = (u16)((offset * 37u) + (table * 0x5a5au));
	}
	sample = cell->value;
	ret = record(fake, 'T', FAKE_TABLE, event, site, offset, sample, width,
		     &record_ptr);
	if (!record_ptr)
		return ret;
	record_ptr->table_id = table;
	record_ptr->offset = offset;
	record_ptr->count = count;
	record_ptr->payload_word_count = count;
	record_ptr->words[0] = sample;
	if (ret)
		return ret;
	values[0] = sample;
	return 0;
}

static int fake_table_write(void *ctx, u8 event, u32 site, u8 table,
			    u16 offset, u16 count, u8 width,
			    const u16 *values)
{
	struct fake_backend *fake = ctx;
	struct fake_record *record_ptr = NULL;
	unsigned int i, j;
	int ret;

	if (!values || !count || count > MAX_TABLE_WORDS || width != 16)
		return -22;
	ret = record(fake, 't', FAKE_TABLE, event, site, offset, table, width,
		     &record_ptr);
	if (!record_ptr)
		return ret;
	record_ptr->table_id = table;
	record_ptr->offset = offset;
	record_ptr->count = count;
	record_ptr->payload_word_count = count;
	for (i = 0; i < count; i++)
		record_ptr->words[i] = values[i];
	if (ret)
		return ret;
	for (i = 0; i < count; i++) {
		struct fake_table_cell *cell = NULL;

		for (j = 0; j < fake->table_cell_count; j++)
			if (fake->table_cells[j].table_id == table &&
			    fake->table_cells[j].offset == offset + i) {
				cell = &fake->table_cells[j];
				break;
			}
		if (!cell) {
			if (fake->table_cell_count ==
			    sizeof(fake->table_cells) / sizeof(fake->table_cells[0]))
				return -12;
			cell = &fake->table_cells[fake->table_cell_count++];
			cell->table_id = table;
			cell->offset = offset + i;
		}
		cell->value = values[i];
	}
	return 0;
}

static const struct ob_phy_a6b0f_ops fake_ops = {
	.prefix = {
		.phy = { .read16 = fake_read16, .write16 = fake_write16,
			 .delay_us = fake_delay, .phyclk_fgc = fake_phyclk },
		.read_ai_core32 = fake_read_ai_core32,
		.write_ai_core32 = fake_write_ai_core32,
	},
	.table_read = fake_table_read,
	.table_write = fake_table_write,
};

static int fake_9e378_read16(void *ctx, u8 event, u32 site,
			     enum ob_phy_9e378_domain domain, u16 reg,
			     u16 *value)
{
	if (domain == OB_PHY_9E378_PHY)
		return fake_read16(ctx, event, site, OB_PHY_POST_LPF_PHY, reg, value);
	if (domain == OB_PHY_9E378_RADIO)
		return fake_read16(ctx, event, site, OB_PHY_POST_LPF_RADIO, reg, value);
	return -22;
}

static int fake_9e378_write16(void *ctx, u8 event, u32 site,
			      enum ob_phy_9e378_domain domain, u16 reg,
			      u16 value)
{
	if (domain == OB_PHY_9E378_PHY)
		return fake_write16(ctx, event, site, OB_PHY_POST_LPF_PHY, reg, value);
	if (domain == OB_PHY_9E378_RADIO)
		return fake_write16(ctx, event, site, OB_PHY_POST_LPF_RADIO, reg, value);
	return -22;
}

static int fake_9e378_table_write(void *ctx, u8 event, u32 site, u8 table,
				  u16 offset, u16 count, u8 width,
				  const u16 *words, u16 word_count)
{
	struct fake_backend *fake = ctx;
	struct fake_record *record_ptr = NULL;
	unsigned int i;
	int ret;

	if (!words || !count || (width == 48 && word_count != 384) ||
	    (width == 16 && word_count != count) ||
	    (width != 16 && width != 48) || word_count > 384)
		return -22;
	ret = record(fake, 't', FAKE_TABLE, event, site, offset, table, width,
		     &record_ptr);
	if (!record_ptr)
		return ret;
	record_ptr->table_id = table;
	record_ptr->offset = offset;
	record_ptr->count = count;
	record_ptr->payload_word_count = word_count;
	if (width == 48) {
		memcpy(gain_dump_words, words, word_count * sizeof(words[0]));
	} else {
		for (i = 0; i < word_count; i++)
			record_ptr->words[i] = words[i];
	}
	return ret;
}

static const struct ob_phy_9e378_ops fake_9e378_ops = {
	.read16 = fake_9e378_read16,
	.write16 = fake_9e378_write16,
	.table_write = fake_9e378_table_write,
};

static int fake_9eaf9_read16(void *ctx, u16 event, u32 site, u16 reg,
			     u16 *value)
{
	return fake_read16(ctx, (u8)event, site, OB_PHY_POST_LPF_PHY, reg, value);
}

static int fake_9eaf9_write16(void *ctx, u16 event, u32 site, u16 reg,
			      u16 value)
{
	return fake_write16(ctx, (u8)event, site, OB_PHY_POST_LPF_PHY, reg,
			    value);
}

static int fake_9eaf9_table_read(void *ctx, u16 event, u32 site, u8 table,
				 u16 offset, u8 count, u8 width,
				 u8 chain, u8 invocation, u8 ordinal,
				 u16 supplied, u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_record *r = NULL;
	int ret;

	if (!value || count != 1 || width != 16 || table != 7 || chain > 1 ||
	    invocation != OB_PHY_9EAF9_INVOCATION_CONTROLLED_1001 ||
	    ordinal < 1 || ordinal > 4)
		return -22;
	ret = record(fake, 'T', FAKE_TABLE, (u8)event, site, offset, supplied,
		     width, &r);
	if (!r)
		return ret;
	r->table_id = table;
	r->offset = offset;
	r->count = count;
	r->payload_word_count = 1;
	r->words[0] = supplied;
	if (ret)
		return ret;
	*value = supplied;
	return 0;
}

static int fake_9eaf9_table_write(void *ctx, u16 event, u32 site, u8 table,
				  u16 offset, u8 count, u8 width,
				  u8 word_width, const u8 *order,
				  u8 order_count, const u16 *words,
				  u16 word_count)
{
	struct fake_backend *fake = ctx;
	struct fake_record *r = NULL;
	unsigned int i;
	int ret;

	if (!words || !count || (width == 16 && (word_width != 16 ||
	    word_count != count || order_count != 0)) ||
	    (width == 48 && (table != 0x14 || offset != 0x33 || count != 1 ||
	    word_width != 16 || word_count != 3 || order_count != 3 || !order ||
	    order[0] != 0 || order[1] != 1 || order[2] != 2)) ||
	    (width != 16 && width != 48) || word_count > MAX_TABLE_WORDS)
		return -22;
	ret = record(fake, 't', FAKE_TABLE, (u8)event, site, offset, table,
		     width, &r);
	if (!r)
		return ret;
	r->table_id = table;
	r->offset = offset;
	r->count = count;
	r->payload_word_count = word_count;
	for (i = 0; i < word_count; i++)
		r->words[i] = words[i];
	return ret;
}

static const struct ob_phy_9eaf9_ops fake_9eaf9_ops = {
	.read16 = fake_9eaf9_read16,
	.write16 = fake_9eaf9_write16,
	.table_read16 = fake_9eaf9_table_read,
	.table_write = fake_9eaf9_table_write,
};

static int fake_farrow_read16(void *ctx, u16 event, u32 site, u16 reg,
			     u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry = reg_find(fake, FAKE_PHY, reg);
	int ret;

	if (!entry || !value)
		return -22;
	ret = record(fake, 'R', FAKE_PHY, event, site, reg, entry->value, 16, NULL);
	if (!ret)
		*value = (u16)entry->value;
	return ret;
}

static int fake_farrow_write16(void *ctx, u16 event, u32 site, u16 reg,
			      u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret;

	ret = record(fake, 'W', FAKE_PHY, event, site, reg, value, 16, NULL);
	if (ret)
		return ret;
	entry = reg_find(fake, FAKE_PHY, reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_farrow_table_read16(void *ctx, u16 event, u32 site,
				    u8 table, u16 offset, u8 count,
				    u8 width, u8 chain, u8 invocation,
				    u8 ordinal, u16 supplied, u16 *observed)
{
	struct fake_backend *fake = ctx;
	struct fake_record *r = NULL;
	int ret;

	if (!observed || table != 7 || count != 1 || width != 16 ||
	    invocation != 1 || chain > 1 || ordinal != chain + 1)
		return -22;
	ret = record(fake, 'T', FAKE_TABLE, event, site, offset, supplied,
		     width, &r);
	if (!r)
		return ret;
	r->table_id = table;
	r->offset = offset;
	r->count = count;
	r->chain = chain;
	r->invocation = invocation;
	r->ordinal = ordinal;
	r->payload_word_count = 1;
	r->words[0] = supplied;
	if (!ret)
		*observed = supplied;
	return ret;
}

static int fake_farrow_table_write16(void *ctx, u16 event, u32 site,
				     u8 table, u16 offset, u8 count,
				     u8 width, u8 chain, u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_record *r = NULL;
	struct fake_table_cell *cell = NULL;
	unsigned int i;
	int ret;

	if (table != 7 || count != 1 || width != 16 || chain > 1)
		return -22;
	ret = record(fake, 't', FAKE_TABLE, event, site, offset, table,
		     width, &r);
	if (!r)
		return ret;
	r->table_id = table;
	r->offset = offset;
	r->count = count;
	r->chain = chain;
	r->payload_word_count = 1;
	r->words[0] = value;
	if (ret)
		return ret;
	for (i = 0; i < fake->table_cell_count; i++)
		if (fake->table_cells[i].table_id == table &&
		    fake->table_cells[i].offset == offset) {
			cell = &fake->table_cells[i];
			break;
		}
	if (!cell) {
		if (fake->table_cell_count ==
		    sizeof(fake->table_cells) / sizeof(fake->table_cells[0]))
			return -12;
		cell = &fake->table_cells[fake->table_cell_count++];
		cell->table_id = table;
		cell->offset = offset;
	}
	cell->value = value;
	return 0;
}

static int fake_farrow_api(void *ctx, u16 event, u32 site,
			   enum ob_phy_farrow_api_call operation,
			   u32 max_wait)
{
	return record(ctx, 'A', FAKE_CONTROL, event, site, (u16)operation,
		      max_wait, 0, NULL);
}

static const struct ob_phy_farrow_crs_ops fake_farrow_ops = {
	.read16 = fake_farrow_read16,
	.write16 = fake_farrow_write16,
	.table_read16 = fake_farrow_table_read16,
	.table_write16 = fake_farrow_table_write16,
	.api_call = fake_farrow_api,
};

static int fake_post_farrow_read16(void *ctx, u8 event, u32 site, u16 reg,
				  u16 *value)
{
	return fake_read16(ctx, event, site, OB_PHY_POST_LPF_PHY, reg, value);
}

static int fake_post_farrow_write16(void *ctx, u8 event, u32 site, u16 reg,
				   u16 value)
{
	return fake_write16(ctx, event, site, OB_PHY_POST_LPF_PHY, reg, value);
}

static const struct ob_phy_post_farrow_hw_ops fake_post_farrow_ops = {
	.read16 = fake_post_farrow_read16,
	.write16 = fake_post_farrow_write16,
};

static struct ob_phy_a6b0f_fixture fixture(u32 flags)
{
	return (struct ob_phy_a6b0f_fixture) {
		.initial_prefix = {
			.si_core_type = 1,
			.phy = {
				.explicitly_synthetic = true,
				.chip_id = 0x4352, .chip_revision = 3,
				.phy_revision = 0x0a, .radio_id = 0x2069,
				.radio_revision = 4, .radio_class = 0,
				.requested_chanspec = 0x1001,
				.lookup_key = 1, .row_index = 63,
				.lookup_matched = true, .pi_16e_class = 0,
				.pi_164_phy_revision = 0x0a,
				.pi_168_radio_class = 2, .nested_912 = 0,
			},
			.entry = { .nested_32c = 1, .nested_32e = 0xff,
				   .nested_912 = 0 },
		},
		.nested_34c = 0, .nested_342 = 0,
		.boardflags2_supplied = true, .boardflags2 = flags,
	};
}

static struct ob_phy_9e378_fixture fixture_9e378(
		const struct ob_phy_9e378_variant *v)
{
	return (struct ob_phy_9e378_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.chanspec_supplied = true, .requested_chanspec = 0x1001,
		.caller_gate_7b = 0, .caller_gate_79 = 1, .nested_32c = 1,
		.selectors_supplied = true,
		.extpagain2g = v->extpagain2g_eq2 ? 2 : 3,
		.nested_411 = v->pdgain_write ? 9 : 17,
		.txchain_low2 = v->txchain_low2,
		.shared_80_supplied = true, .shared_80 = 0,
	};
}

static struct ob_phy_9eaf9_fixture fixture_9eaf9(void)
{
	return (struct ob_phy_9eaf9_fixture) {
		.explicitly_synthetic = true,
		.policy = OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.chanspec_supplied = true, .controlled_chanspec = 0x1001,
		.caller_gate_inputs_supplied = true,
		.caller_local_7a = 0, .nested_32c = 1,
		.word2_present = true, .synthetic_word2 = 0,
		.runtime_samples = {
			{ .valid = true, .logical_event = 40, .source_site = 0x9da4b,
			  .table_id = 7, .offset = 0x14a, .element_count = 1,
			  .element_width_bits = 16, .chain_index = 0, .invocation_id = 1,
			  .sample_ordinal = 1, .value = 0x1234 },
			{ .valid = true, .logical_event = 41, .source_site = 0x9da73,
			  .table_id = 7, .offset = 0x36a, .element_count = 1,
			  .element_width_bits = 16, .chain_index = 0, .invocation_id = 1,
			  .sample_ordinal = 2, .value = 0x5678 },
			{ .valid = true, .logical_event = 44, .source_site = 0x9da4b,
			  .table_id = 7, .offset = 0x15a, .element_count = 1,
			  .element_width_bits = 16, .chain_index = 1, .invocation_id = 1,
			  .sample_ordinal = 3, .value = 0x9abc },
			{ .valid = true, .logical_event = 45, .source_site = 0x9da73,
			  .table_id = 7, .offset = 0x37a, .element_count = 1,
			  .element_width_bits = 16, .chain_index = 1, .invocation_id = 1,
			  .sample_ordinal = 4, .value = 0xdef0 },
		},
	};
}

static const char *domain_name(enum fake_domain domain)
{
	switch (domain) {
	case FAKE_PHY: return "PHY";
	case FAKE_RADIO: return "RADIO";
	case FAKE_AI_CORE: return "AI_CORE";
	case FAKE_TABLE: return "TABLE";
	default: return "CONTROL";
	}
}

static void dump_record(const struct fake_record *r)
{
	unsigned int i;

	if (r->domain != FAKE_TABLE) {
		printf("%c,%s,%u,0x%05x,0x%04x,0x%08x,%u\n",
		       r->kind, domain_name(r->domain), r->event, r->site,
		       r->reg, r->value, r->width);
		return;
	}
	printf("T,%s,%u,0x%05x,%u,0x%04x,%u,%u",
	       r->kind == 'T' ? "READ" : "WRITE", r->event, r->site,
	       r->table_id, r->offset, r->count, r->width);
	if (r->width == 48) {
		printf(",%u", r->payload_word_count);
		for (i = 0; i < r->payload_word_count; i++)
			printf(",0x%04x", r->payload_word_count == 384 ?
			       gain_dump_words[i] : r->words[i]);
	} else {
		for (i = 0; i < r->count; i++)
			printf(",0x%04x", r->words[i]);
	}
	putchar('\n');
}

static void test_selected_branches_and_boundaries(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture scenario = fixture(0x00300002);
	struct ob_phy_a6b0f_result result;
	int ret;

	scenario.a0be4_fixture_completed = true;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("selected A6B0F returns", ret, 0);
	check("A6B0F logical count with flags set", result.logical_event_count, 216);
	check("A6B0F callback count with flags set", result.callback_count, 181);
	check("A6B0F callback trace count", fake.trace_count, 181);
	check("A6B0F flag branch selected", result.boardflags2_mask_set, true);
	check("first accepted callback is PHY read", fake.trace[0].kind, 'R');
	check("first accepted source site", fake.trace[0].site, 0xa6b3c);
	check("first child callback follows parent event 3864", fake.trace[3].site, 0xa6052);
	check("branch precedes final restore", fake.trace[fake.trace_count - 3].site, 0xa6f35);
	check("last callback is final parent restore write", fake.trace[fake.trace_count - 1].site, 0xa7075);
	check("completion does not claim hardware ready", result.hardware_ready, false);

	memset(&fake, 0, sizeof(fake));
	scenario = fixture(0x00200002);
	scenario.a0be4_fixture_completed = true;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("clear-mask fixture returns", ret, 0);
	check("clear-mask helper logical count", result.logical_event_count, 215);
	check("clear-mask callback count", result.callback_count, 180);
	{
		unsigned int i;
		for (i = 0; i < fake.trace_count; i++)
			if (fake.trace[i].site == 0xa6f35)
				break;
		check("clear-mask skips table write", i, fake.trace_count);
	}
}

static void test_invalid_inputs_rejected_before_callback(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture scenario = fixture(0x00300002);
	struct ob_phy_a6b0f_result result;
	struct ob_phy_a6b0f_ops missing = fake_ops;
	int ret;

	scenario.a0be4_fixture_completed = true;
	scenario.boardflags2_supplied = false;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("missing boardflags rejected", ret, -22);
	check("missing boardflags has no access", fake.trace_count, 0);
	scenario = fixture(0x00300002);
	scenario.a0be4_fixture_completed = true;
	missing.table_read = NULL;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		&missing, &fake, &scenario, &result);
	check("missing TABLE callback rejected", ret, -22);
	check("missing TABLE callback has no access", fake.trace_count, 0);
	scenario = fixture(0x00300002);
	scenario.a0be4_fixture_completed = true;
	scenario.initial_prefix.phy.chip_id = 0x4353;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("wrong identity rejected", ret, -95);
	check("wrong identity has no access", fake.trace_count, 0);
}

static void test_exhaustive_fail_stop(void)
{
	unsigned int fail_at;

	for (fail_at = 1; fail_at <= 181; fail_at++) {
		struct fake_backend fake = { .fail_at = fail_at };
		struct ob_phy_a6b0f_fixture scenario = fixture(0x00300002);
		struct ob_phy_a6b0f_result result;
		int ret;

		scenario.a0be4_fixture_completed = true;
		memset(&result, 0xa5, sizeof(result));
		ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
			&fake_ops, &fake, &scenario, &result);
		check("A6 injected error propagated", ret, -5);
		check("A6 stops after failed callback", fake.trace_count, fail_at);
		check("A6 failure does not publish completion", result.kind,
		      OB_PHY_A6B0F_RESULT_INVALID);
		check("A6 failure does not publish readiness", result.hardware_ready, false);
	}
}

static void test_composed_prefix(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture scenario = fixture(0x00300002);
	struct ob_phy_a6b0f_composed_result result;
	int ret;

	ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("composed initial sequence returns", ret, 0);
	check("existing prefix unchanged", result.through_a0be4.callback_count,
	      3912);
	check("new A6B0F callback count", result.a6b0f.callback_count, 181);
	check("complete composed callback count", result.callback_count, 4093);
	check("corrected selected logical prefix count", result.logical_event_count, 4078);
	check("fake trace callback count", fake.trace_count, 4093);
	check("first A6B0F callback at exact join", fake.trace[3912].site, 0xa6b3c);
	check("composed final event is @a7075", fake.trace[4092].site, 0xa7075);
	check("terminal caller rejoin", result.terminal_return_site, 0xaa41f);
	check("no readiness is published", result.hardware_ready, false);
}

static void test_composition_stops_at_each_helper_boundary(void)
{
	static const unsigned int fail_points[] = { 1, 3737, 3846, 3913 };
	unsigned int i;

	for (i = 0; i < sizeof(fail_points) / sizeof(fail_points[0]); i++) {
		struct fake_backend fake = { .fail_at = fail_points[i] };
		struct ob_phy_a6b0f_fixture scenario = fixture(0x00300002);
		struct ob_phy_a6b0f_composed_result result;
		int ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
			&fake_ops, &fake, &scenario, &result);

		check("composed first error is unchanged", ret, -5);
		check("composition stops at failing callback", fake.trace_count,
		      fail_points[i]);
		check("composed failure publishes no result", result.callback_count, 0);
		check("composed failure publishes no readiness", result.hardware_ready,
		      false);
	}
}

static void test_composed_9e378_extension(void)
{
	static const u32 flags[] = { 0x00200002, 0x00300002 };
	unsigned int scenario_index;

	for (scenario_index = 0; scenario_index < 2; scenario_index++) {
		struct fake_backend fake = { 0 };
		struct ob_phy_a6b0f_fixture prefix_fixture = fixture(flags[scenario_index]);
		struct ob_phy_a6b0f_composed_result prefix_result;
		struct ob_phy_9e378_result helper_result;
		const struct ob_phy_9e378_variant *variant =
			&ob_phy_9e378_variants[scenario_index ? 15 : 0];
		struct ob_phy_9e378_fixture helper_fixture = fixture_9e378(variant);
		unsigned int expected_prefix = scenario_index ? 4093u : 4092u;
		int ret;

		ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
			&fake_ops, &fake, &prefix_fixture, &prefix_result);
		check("9e378 composition prefix succeeds", ret, 0);
		check("9e378 composition preserves prefix callbacks",
		      fake.trace_count, expected_prefix);
		ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, &fake,
							     &helper_fixture,
							     &helper_result);
		check("9e378 composition suffix succeeds", ret, 0);
		check("9e378 composed first new callback", fake.trace[expected_prefix].site,
		      0x9e395);
		check("9e378 composed final callback", fake.trace[fake.trace_count - 1].site,
		      0x9eae7);
		check("9e378 composed callback suffix", helper_result.callback_count,
		      variant->callback_count);
		check("9e378 composed stage does not claim readiness",
		      helper_result.hardware_ready, false);
	}
}

static void test_composed_9eaf9_extension(void)
{
	static const u32 flags[] = { 0x00200002, 0x00300002 };
	unsigned int scenario_index;

	for (scenario_index = 0; scenario_index < 2; scenario_index++) {
		struct fake_backend fake = { 0 };
		struct ob_phy_a6b0f_fixture prefix_fixture = fixture(flags[scenario_index]);
		struct ob_phy_a6b0f_composed_result prefix_result;
		struct ob_phy_9e378_result middle_result;
		struct ob_phy_9eaf9_fixture tail_fixture = fixture_9eaf9();
		struct ob_phy_9eaf9_result tail_result;
		const struct ob_phy_9e378_variant *variant =
			&ob_phy_9e378_variants[scenario_index ? 15 : 0];
		struct ob_phy_9e378_fixture middle_fixture = fixture_9e378(variant);
		unsigned int prefix_count = scenario_index ? 4093u : 4092u;
		unsigned int expected = prefix_count + variant->callback_count + 107u;
		int ret;

		ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
			&fake_ops, &fake, &prefix_fixture, &prefix_result);
		check("9eaf9 composed predecessor returns", ret, 0);
		ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, &fake,
								   &middle_fixture,
								   &middle_result);
		check("9eaf9 composed 9e378 returns", ret, 0);
		ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_9eaf9_ops, &fake,
								    &tail_fixture,
								    &tail_result);
		check("9eaf9 composed helper returns", ret, 0);
		check("9eaf9 composed predecessor exact count", prefix_result.callback_count,
		      prefix_count);
		check("9eaf9 composed middle exact count", middle_result.callback_count,
		      variant->callback_count);
		check("9eaf9 composed tail exact count", tail_result.callback_count, 107);
		check("9eaf9 full controlled composition count", fake.trace_count, expected);
		check("9eaf9 first callback after accepted predecessor",
		      fake.trace[prefix_count + variant->callback_count].site, 0x9eb45);
		check("9eaf9 terminal callback", fake.trace[fake.trace_count - 1].site,
		      0x9fb5b);
		check("9eaf9 terminal event register", fake.trace[fake.trace_count - 1].reg,
		      0x019e);
		check("9eaf9 composed helper remains synthetic", tail_result.kind,
		      OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE);
		check("9eaf9 composed word2 remains historical unknown",
		      tail_result.historical_word2_resolved, false);
		check("9eaf9 composition does not publish readiness",
		      tail_result.hardware_ready, false);
	}
}

static void test_composed_9eaf9_fail_stop(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture prefix_fixture = fixture(0x00200002);
	struct ob_phy_a6b0f_composed_result prefix_result;
	struct ob_phy_9e378_result middle_result;
	struct ob_phy_9eaf9_fixture tail_fixture = fixture_9eaf9();
	struct ob_phy_9eaf9_result tail_result;
	const struct ob_phy_9e378_variant *variant = &ob_phy_9e378_variants[0];
	struct ob_phy_9e378_fixture middle_fixture = fixture_9e378(variant);
	unsigned int fail_at = 4092u + variant->callback_count + 1u;
	int ret;

	fake.fail_at = fail_at;
	ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, &fake, &prefix_fixture, &prefix_result);
	if (!ret)
		ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, &fake,
								    &middle_fixture,
								    &middle_result);
	if (!ret)
		ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_9eaf9_ops, &fake,
								     &tail_fixture,
								     &tail_result);
	check("composed 9eaf9 injected first callback error", ret, -5);
	check("composed 9eaf9 stops exactly at injected callback",
	      fake.trace_count, fail_at);
	check("composed 9eaf9 does not publish success",
	      tail_result.kind, OB_PHY_9EAF9_FAILED_BACKEND_OPERATION);
	check("composed 9eaf9 failure does not publish readiness",
	      tail_result.hardware_ready, false);
}

static int dump_composed(u32 flags)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture scenario = fixture(flags);
	struct ob_phy_a6b0f_composed_result result;
	unsigned int i;
	unsigned int expected = (flags & 0x00100000) ? 4093u : 4092u;
	int ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, &fake, &scenario, &result);

	if (ret || result.callback_count != expected || fake.trace_count != expected ||
	    result.terminal_return_site != 0xaa41f)
		return 1;
	for (i = 0; i < fake.trace_count; i++)
		dump_record(&fake.trace[i]);
	return 0;
}

static int dump_composed_9e378(u32 flags, const char *variant_id)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture prefix_fixture = fixture(flags);
	struct ob_phy_a6b0f_composed_result prefix_result;
	struct ob_phy_9e378_result helper_result;
	const struct ob_phy_9e378_variant *variant = NULL;
	struct ob_phy_9e378_fixture helper_fixture;
	unsigned int expected_prefix = (flags & 0x00100000) ? 4093u : 4092u;
	unsigned int i;
	int ret;

	for (i = 0; i < OB_PHY_9E378_VARIANT_COUNT; i++)
		if (!strcmp(ob_phy_9e378_variants[i].id, variant_id)) {
			variant = &ob_phy_9e378_variants[i];
			break;
		}
	if (!variant)
		return 2;
	ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, &fake, &prefix_fixture, &prefix_result);
	if (ret || fake.trace_count != expected_prefix ||
	    prefix_result.terminal_return_site != 0xaa41f)
		return 3;
	helper_fixture = fixture_9e378(variant);
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, &fake,
						   &helper_fixture, &helper_result);
	if (ret || helper_result.callback_count != variant->callback_count ||
	    fake.trace_count != expected_prefix + variant->callback_count ||
	    fake.trace[expected_prefix].site != 0x9e395 ||
	    fake.trace[fake.trace_count - 1].site != 0x9eae7 ||
	    helper_result.hardware_ready)
		return 4;
	for (i = 0; i < fake.trace_count; i++)
		dump_record(&fake.trace[i]);
	return 0;
}

static int dump_composed_9eaf9(u32 flags)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a6b0f_fixture prefix_fixture = fixture(flags);
	struct ob_phy_a6b0f_composed_result prefix_result;
	struct ob_phy_9e378_result middle_result;
	struct ob_phy_9eaf9_fixture tail_fixture = fixture_9eaf9();
	struct ob_phy_9eaf9_result tail_result;
	const struct ob_phy_9e378_variant *variant =
		(flags & 0x00100000) ? &ob_phy_9e378_variants[15] :
		&ob_phy_9e378_variants[0];
	struct ob_phy_9e378_fixture middle_fixture = fixture_9e378(variant);
	unsigned int expected_prefix = (flags & 0x00100000) ? 4093u : 4092u;
	unsigned int i;
	int ret;

	ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, &fake, &prefix_fixture, &prefix_result);
	if (ret || prefix_result.callback_count != expected_prefix ||
	    prefix_result.terminal_return_site != 0xaa41f)
		return 1;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, &fake,
							    &middle_fixture,
							    &middle_result);
	if (ret || middle_result.callback_count != variant->callback_count ||
	    middle_result.hardware_ready)
		return 2;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_9eaf9_ops, &fake,
							     &tail_fixture, &tail_result);
	if (ret || tail_result.kind != OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE ||
	    tail_result.callback_count != 107 || tail_result.hardware_ready ||
	    tail_result.historical_word2_resolved ||
	    fake.trace_count != expected_prefix + variant->callback_count + 107u)
		return 3;
	for (i = 0; i < fake.trace_count; i++)
		dump_record(&fake.trace[i]);
	return 0;
}

static struct ob_phy_farrow_fixture fixture_farrow(void)
{
	return (struct ob_phy_farrow_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0,
		.chanspec = 0x1001, .pi_16e = 0, .pi_168 = 2,
		.pi_164 = 0x0a, .nested_348 = 0, .nested_912 = 0,
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
			  .sample_ordinal = 1, .value = 0x62a1 },
			{ .valid = true, .table_id = 7, .offset = 0x03dd,
			  .element_count = 1, .element_width_bits = 16,
			  .chain_index = 1, .invocation_id = 1,
			  .sample_ordinal = 2, .value = 0x91d4 },
		},
	};
}

static int run_composed_farrow(struct fake_backend *fake, u32 flags,
			       struct ob_phy_farrow_crs_result *farrow_result,
			       bool include_post_farrow, u32 nested_8b0)
{
	struct ob_phy_a6b0f_fixture prefix_fixture = fixture(flags);
	struct ob_phy_a6b0f_composed_result prefix_result;
	struct ob_phy_9e378_result middle_result;
	struct ob_phy_9eaf9_fixture tail_fixture = fixture_9eaf9();
	struct ob_phy_9eaf9_result tail_result;
	struct ob_phy_farrow_fixture farrow_fixture = fixture_farrow();
	struct ob_phy_post_farrow_software_input software_input = { 0 };
	struct ob_phy_post_farrow_state software_state;
	struct ob_phy_post_farrow_software_result software_result;
	struct ob_phy_desense_prefix_result desense_result;
	const struct ob_phy_9e378_variant *variant =
		(flags & 0x00100000) ? &ob_phy_9e378_variants[15] :
		&ob_phy_9e378_variants[0];
	struct ob_phy_9e378_fixture middle_fixture = fixture_9e378(variant);
	unsigned int expected_prefix = (flags & 0x00100000) ? 4093u : 4092u;
	unsigned int before_farrow;
	u16 prior_019e;
	u16 prior_0601;
	u16 expected_after_9eaf9;
	int ret;

	ret = ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
		&fake_ops, fake, &prefix_fixture, &prefix_result);
	if (ret || prefix_result.callback_count != expected_prefix ||
	    prefix_result.terminal_return_site != 0xaa41f)
		return 1;
	ret = ob_phy_acphy_9e378_fixture_test_only(&fake_9e378_ops, fake,
							    &middle_fixture,
							    &middle_result);
	if (ret || middle_result.callback_count != variant->callback_count ||
	    middle_result.hardware_ready)
		return 2;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_9eaf9_ops, fake,
							     &tail_fixture,
							     &tail_result);
	if (ret || tail_result.kind != OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE ||
	    tail_result.callback_count != 107 || tail_result.hardware_ready ||
	    tail_result.historical_word2_resolved)
		return 3;
	before_farrow = fake->trace_count;
	expected_after_9eaf9 = expected_prefix + variant->callback_count + 107u;
	if (before_farrow != expected_after_9eaf9)
		return 4;
	prior_019e = (u16)reg_find(fake, FAKE_PHY, 0x019e)->value;
	prior_0601 = (u16)reg_find(fake, FAKE_PHY, 0x0601)->value;
	ret = ob_phy_acphy_farrow_crs_fixture_test_only(&fake_farrow_ops, fake,
								&farrow_fixture,
								farrow_result);
	if (ret || farrow_result->kind !=
	    OB_PHY_FARROW_CRS_HOST_SIMULATION_COMPLETE ||
	    farrow_result->logical_events_completed != 53 ||
	    farrow_result->callback_records != 73 ||
	    farrow_result->terminal_return_site != 0x0a5fad ||
	    farrow_result->historically_exact || farrow_result->hardware_ready ||
	    farrow_result->phy_ready || farrow_result->channel_ready)
		return 5;
	if (fake->trace_count != before_farrow + 73u ||
	    fake->trace[before_farrow].event != 4264 ||
	    fake->trace[before_farrow].site != 0x0a4d36 ||
	    fake->trace[before_farrow].reg != 0x019a ||
	    fake->trace[fake->trace_count - 1].event != 4316 ||
	    fake->trace[fake->trace_count - 1].site != 0x92e53 ||
	    reg_find(fake, FAKE_PHY, 0x019e)->value != prior_019e ||
	    reg_find(fake, FAKE_PHY, 0x1601)->value != prior_0601)
		return 6;
	if (!include_post_farrow)
		return 0;

	/* Explicit synthetic caller state. This models the software cache/fork
	 * seam and carries the same persistent fake PHY register state forward. */
	software_input.explicitly_synthetic = true;
	software_input.entry_after_farrow_join = true;
	software_input.chip_id = 0x4352;
	software_input.chip_revision = 3;
	software_input.phy_revision = 0x0a;
	software_input.radio_id = 0x2069;
	software_input.radio_revision = 4;
	software_input.phy_flags_19c = 0;
	software_input.initial_chanspec = 0x1001;
	software_input.pi_cache_240_present = true;
	software_input.pi_cache_240 = 0x55;
	software_input.nested_341_present = true;
	software_input.nested_341 = 1;
	software_input.nested_912_present = true;
	software_input.nested_912 = 0;
	software_input.nested_8b0_present = true;
	software_input.nested_8b0 = nested_8b0;
	software_input.nested_914_present = true;
	software_input.nested_914 = -1;
	software_input.nested_916_present = true;
	software_input.nested_916 = -1;
	software_input.cache_record_result_supplied = true;
	software_input.cache_record_result_present = true;
	software_input.cache_record_payload_present = true;
	memset(software_input.nested_65f, 0x31,
	       sizeof(software_input.nested_65f));
	memset(software_input.nested_8b4, 0x22,
	       sizeof(software_input.nested_8b4));
	memset(software_input.cache_record_payload, 0x41,
	       sizeof(software_input.cache_record_payload));
	ret = ob_phy_post_farrow_continue_software_test_only(
		&software_input, &software_state, &software_result);
	if (ret || software_result.kind != OB_PHY_POST_FARROW_SW_COMPLETE ||
	    !software_result.caller_joined || !software_result.sub_909fd_called ||
	    software_result.configuration_arm != (nested_8b0 == 0 ?
		OB_PHY_POST_FARROW_CONFIG_ZERO : OB_PHY_POST_FARROW_CONFIG_NONZERO) ||
	    software_result.hardware_callbacks != 0 || software_result.hardware_ready)
		return 7;
	ret = ob_phy_desense_9af05_prefix_test_only(
		&fake_post_farrow_ops, fake, &software_state, &desense_result);
	if (ret || desense_result.kind != OB_PHY_DESENSE_PREFIX_COMPLETE ||
	    desense_result.logical_events != 4 ||
	    desense_result.callback_records != 6 ||
	    desense_result.terminal_boundary != 0x9b123 ||
	    desense_result.desense_helper_complete || desense_result.hardware_ready ||
	    fake->trace_count != before_farrow + 73u + 6u)
		return 8;
	return 0;
}

static int dump_composed_farrow(u32 flags, bool include_post_farrow,
				u32 nested_8b0)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_farrow_crs_result result;
	unsigned int i;
	int ret = run_composed_farrow(&fake, flags, &result,
				      include_post_farrow, nested_8b0);

	if (ret)
		return ret;
	for (i = 0; i < fake.trace_count; i++)
		dump_record(&fake.trace[i]);
	return 0;
}

static void test_composed_farrow_extension(void)
{
	struct fake_backend fake_clear = { 0 };
	struct fake_backend fake_set = { 0 };
	struct ob_phy_farrow_crs_result clear_result;
	struct ob_phy_farrow_crs_result set_result;
	int ret;

	ret = run_composed_farrow(&fake_clear, 0x00200002, &clear_result,
				  true, 0);
	check("clear-mask Farrow composition", ret, 0);
	check("clear-mask composed callback count", fake_clear.trace_count, 4356);
	check("clear-mask first post-Farrow callback",
	      fake_clear.trace[4350].site, 0x9b0cd);
	check("clear-mask final prefix callback",
	      fake_clear.trace[4355].site, 0x9b116);
	ret = run_composed_farrow(&fake_set, 0x00300002, &set_result,
				  true, 7);
	check("set-mask Farrow composition", ret, 0);
	check("set-mask composed callback count", fake_set.trace_count, 4384);
	check("set-mask first post-Farrow callback",
	      fake_set.trace[4378].site, 0x9b0cd);
	check("set-mask final prefix callback",
	      fake_set.trace[4383].site, 0x9b116);
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--dump-composed") == 0)
		return dump_composed(0x00300002);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-clear") == 0)
		return dump_composed(0x00200002);
	if (argc == 3 && strcmp(argv[1], "--dump-composed-9e378-clear") == 0)
		return dump_composed_9e378(0x00200002, argv[2]);
	if (argc == 3 && strcmp(argv[1], "--dump-composed-9e378-set") == 0)
		return dump_composed_9e378(0x00300002, argv[2]);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-9eaf9-clear") == 0)
		return dump_composed_9eaf9(0x00200002);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-9eaf9-set") == 0)
		return dump_composed_9eaf9(0x00300002);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-farrow-clear") == 0)
		return dump_composed_farrow(0x00200002, false, 0);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-farrow-set") == 0)
		return dump_composed_farrow(0x00300002, false, 0);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-farrow-prefix-clear") == 0)
		return dump_composed_farrow(0x00200002, false, 0);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-farrow-prefix-set") == 0)
		return dump_composed_farrow(0x00300002, false, 0);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-postfarrow-clear") == 0)
		return dump_composed_farrow(0x00200002, true, 0);
	if (argc == 2 && strcmp(argv[1], "--dump-composed-postfarrow-set") == 0)
		return dump_composed_farrow(0x00300002, true, 7);
	if (argc != 1)
		return 2;
	test_selected_branches_and_boundaries();
	test_invalid_inputs_rejected_before_callback();
	test_exhaustive_fail_stop();
	test_composed_prefix();
	test_composition_stops_at_each_helper_boundary();
	test_composed_9e378_extension();
	test_composed_9eaf9_extension();
	test_composed_9eaf9_fail_stop();
	test_composed_farrow_extension();
	if (failures) {
		printf("ob_phy_a6b0f_test: %u failures\n", failures);
		return 1;
	}
	puts("ob_phy_a6b0f_test: PASS");
	return 0;
}
