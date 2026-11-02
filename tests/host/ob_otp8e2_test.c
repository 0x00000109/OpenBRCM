/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_otp8e2.h"

struct fake_otp {
	u16 word;
	u16 seen_word_index;
	u16 seen_columns;
	unsigned int reads;
	unsigned int observations;
	unsigned int failures;
	int read_ret;
	int failure_ret;
	u16 observed_word;
	u8 observed_value;
	bool observed_jbe;
};

static int fake_read_word16(void *ctx, u16 word_index, u16 columns, u16 *word)
{
	struct fake_otp *fake = ctx;

	fake->reads++;
	fake->seen_word_index = word_index;
	fake->seen_columns = columns;
	/* Even a failing backend may have touched the output slot. */
	*word = fake->word;
	return fake->read_ret;
}

static void fake_observation(void *ctx, u16 word, u8 value, bool jbe_le3)
{
	struct fake_otp *fake = ctx;

	fake->observations++;
	fake->observed_word = word;
	fake->observed_value = value;
	fake->observed_jbe = jbe_le3;
}

static void fake_failure(void *ctx, int ret)
{
	struct fake_otp *fake = ctx;

	fake->failures++;
	fake->failure_ret = ret;
}

static const struct ob_otp8e2_ops fake_ops = {
	.read_word16 = fake_read_word16,
	.observation = fake_observation,
	.failure = fake_failure,
};

static int check_case(u16 word, u8 expected_value, bool expected_jbe)
{
	struct fake_otp fake = { .word = word };
	int ret = ob_otp8e2_observe_word10(&fake_ops, &fake);

	if (ret || fake.reads != 1 || fake.seen_word_index != 0x10 ||
	    fake.seen_columns != 32 || fake.observations != 1 ||
	    fake.failures != 0 || fake.observed_word != word ||
	    fake.observed_value != expected_value ||
	    fake.observed_jbe != expected_jbe) {
		fprintf(stderr,
			"FAIL word=%04x ret=%d reads=%u index=%x columns=%u obs=%u fail=%u value=%02x jbe=%u\n",
			word, ret, fake.reads, fake.seen_word_index,
			fake.seen_columns, fake.observations, fake.failures,
			fake.observed_value, fake.observed_jbe);
		return 1;
	}
	return 0;
}

int main(void)
{
	struct fake_otp failed = {
		.word = 0x1f00,
		.read_ret = -EIO,
	};
	int failures = 0;

	failures += check_case(0x0000, 0x00, true);
	failures += check_case(0x0300, 0x03, true);
	failures += check_case(0x0400, 0x04, false);
	failures += check_case(0x1f00, 0x1f, false);
	failures += check_case(0xffff, 0x1f, false);

	if (ob_otp8e2_observe_word10(&fake_ops, &failed) != -EIO ||
	    failed.reads != 1 || failed.observations != 0 ||
	    failed.failures != 1 || failed.failure_ret != -EIO) {
		fprintf(stderr,
			"FAIL error masqueraded as data reads=%u obs=%u fail=%u ret=%d\n",
			failed.reads, failed.observations, failed.failures,
			failed.failure_ret);
		failures++;
	}

	if (failures)
		return 1;
	puts("ob_otp8e2_test: PASS");
	return 0;
}
