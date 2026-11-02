// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for rev42 firmware validation (M3.4D1).
 *
 * Pure parser/iterator tests plus, when the real vendor-derived files are
 * installed, a check of the exact record/word contents against the blob.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/ob_fw.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

/* Build one vendor IV record: offset, width, value (little-endian). */
static void put_iv(u8 *p, u16 off, u16 width, u32 val)
{
	p[0] = off & 0xff;
	p[1] = off >> 8;
	p[2] = width & 0xff;
	p[3] = width >> 8;
	p[4] = val & 0xff;
	p[5] = (val >> 8) & 0xff;
	p[6] = (val >> 16) & 0xff;
	p[7] = (val >> 24) & 0xff;
}

static void test_ucode(void)
{
	/* Flat 32-bit little-endian word stream, no header. */
	u8 w[12] = { 0x4e, 0x10, 0x00, 0x03,	/* 0x0300104e */
		     0x60, 0xbc, 0x01, 0x00,	/* 0x0001bc60 */
		     0x25, 0x0e, 0xf0, 0x02 };	/* 0x02f00e25 */
	u32 v;

	chk("ucode size ok", (long)ob_fw_ucode_size_ok(OB_FW_UCODE42_SIZE), 1);
	chk("ucode size bad", (long)ob_fw_ucode_size_ok(OB_FW_UCODE42_SIZE + 1),
	    0);
	chk("ucode empty bad", (long)ob_fw_ucode_size_ok(0), 0);
	chk("ucode words", (long)ob_fw_ucode_words(OB_FW_UCODE42_SIZE), 10850);
	chk("ucode words/4", (long)ob_fw_ucode_words(12), 3);

	ob_fw_ucode_word(w, 0, &v);
	chk("ucode w0", (long)v, 0x0300104e);
	ob_fw_ucode_word(w, 1, &v);
	chk("ucode w1", (long)v, 0x0001bc60);
	ob_fw_ucode_word(w, 2, &v);
	chk("ucode w2", (long)v, 0x02f00e25);
}

/* First 8 words and last 4 words of the vendor d11ucode42 image (blob
 * slice .rodata 0xb1670). The iterator must reproduce them byte-exactly.
 */
static const u8 ucode_head[32] = {
	0x4e, 0x10, 0x00, 0x03, 0x60, 0xbc, 0x01, 0x00,
	0x25, 0x0e, 0xf0, 0x02, 0xde, 0xbf, 0x03, 0x00,
	0x10, 0x00, 0xf0, 0x02, 0x5e, 0xa8, 0x03, 0x00,
	0x10, 0x00, 0xf0, 0x02, 0x5e, 0xa6, 0x03, 0x00,
};
static const u8 ucode_tail[16] = {
	0x09, 0x01, 0xf0, 0x02, 0x5e, 0x02, 0x00, 0x00,
	0x00, 0x00, 0xf0, 0x02, 0xde, 0x02, 0x00, 0x00,
};

static void test_ucode_against_blob(void)
{
	static const u32 head_exp[8] = {
		0x0300104e, 0x0001bc60, 0x02f00e25, 0x0003bfde,
		0x02f00010, 0x0003a85e, 0x02f00010, 0x0003a65e,
	};
	static const u32 tail_exp[4] = {
		0x02f00109, 0x0000025e, 0x02f00000, 0x000002de,
	};
	u32 i, v;

	for (i = 0; i < 8; i++) {
		ob_fw_ucode_word(ucode_head, i, &v);
		chk("ucode blob head word", (long)v, (long)head_exp[i]);
	}
	for (i = 0; i < 4; i++) {
		ob_fw_ucode_word(ucode_tail, i, &v);
		chk("ucode blob tail word", (long)v, (long)tail_exp[i]);
	}
}

static void test_iv_valid(void)
{
	/* 16-bit, 32-bit, 16-bit, terminator. */
	u8 t[4 * 8];
	struct ob_fw_iv_stats st;
	struct ob_fw_iv iv;

	put_iv(t + 0, 0x160, 4, 0x03010005);
	put_iv(t + 8, 0x686, 2, 0x09d0);
	put_iv(t + 16, 0x124, 4, 0x4);
	put_iv(t + 24, 0xffff, 0, 0);

	chk("iv parse", ob_fw_iv_parse(t, sizeof(t), &st), 0);
	chk("iv records", (long)st.records, 3);
	chk("iv w16", (long)st.w16, 1);
	chk("iv w32", (long)st.w32, 2);
	chk("iv term", (long)st.terminator_index, 3);

	chk("iv at 0", ob_fw_iv_at(t, sizeof(t), 0, &iv), 0);
	chk("iv0 off", (long)iv.offset, 0x160);
	chk("iv0 width", (long)iv.width, 4);
	chk("iv0 value", (long)iv.value, 0x03010005);
	chk("iv at 1", ob_fw_iv_at(t, sizeof(t), 1, &iv), 0);
	chk("iv1 width", (long)iv.width, 2);
	chk("iv1 value", (long)iv.value, 0x09d0);
	chk("iv oob", ob_fw_iv_at(t, sizeof(t), 4, &iv), -EINVAL);
}

static void test_iv_bad(void)
{
	u8 t[5 * 8];
	struct ob_fw_iv_stats st;

	/* truncated (not a multiple of 8) */
	put_iv(t, 0x160, 4, 1);
	chk("iv truncated", ob_fw_iv_parse(t, 7, &st), -EINVAL);
	chk("iv zero", ob_fw_iv_parse(t, 0, &st), -EINVAL);

	/* missing terminator */
	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0x164, 4, 1);
	chk("iv no term", ob_fw_iv_parse(t, 16, &st), -ENODATA);

	/* illegal width */
	put_iv(t + 0, 0x160, 1, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	chk("iv bad width", ob_fw_iv_parse(t, 16, &st), -EINVAL);

	/* out-of-range offset */
	put_iv(t + 0, 0x1000, 4, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	chk("iv bad offset", ob_fw_iv_parse(t, 16, &st), -EINVAL);

	/* data after terminator */
	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	put_iv(t + 16, 0x164, 4, 1);
	chk("iv after term", ob_fw_iv_parse(t, 24, &st), -EINVAL);

	/* immediate terminator only */
	put_iv(t + 0, 0xffff, 0, 0);
	chk("iv term only", ob_fw_iv_parse(t, 8, &st), 0);
	chk("iv term only records", (long)st.records, 0);
}

static void test_hash(void)
{
	static const u8 s[3] = { 'a', 'b', 'c' };

	chk("fnv empty",
	    (long)(ob_fw_hash64((const u8 *)"", 0) == 0xcbf29ce484222325ULL),
	    1);
	/* FNV-1a-64("abc") */
	chk("fnv abc",
	    (long)(ob_fw_hash64(s, 3) == 0xe71fa2190541574bULL), 1);
}

/* Optional: validate the real installed files if present. */
static void test_real_files(void)
{
	struct {
		const char *path;
		size_t size;
		u64 fnv;
		u32 records, w16, w32, term;
	} ivs[] = {
		{ "/lib/firmware/brcm/bcm4352-d11ac1initvals42.bin",
		  OB_FW_AC1INITVALS42_SIZE, OB_FW_AC1INITVALS42_FNV,
		  610, 113, 497, 610 },
		{ "/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin",
		  OB_FW_AC1BSINITVALS42_SIZE, OB_FW_AC1BSINITVALS42_FNV,
		  73, 39, 34, 73 },
	};
	const char *const upaths[] = {
		"/lib/firmware/brcm/bcm43xx-ucode.fw",
		"/lib/firmware/brcm/bcm4352-d11ucode42.bin",
	};
	unsigned int i;
	u8 *buf;
	long n;
	FILE *f;
	struct ob_fw_iv_stats st;

	for (i = 0; i < 2; i++) {
		f = fopen(ivs[i].path, "rb");
		if (!f) {
			printf("skip: %s not installed\n", ivs[i].path);
			continue;
		}
		buf = malloc(ivs[i].size);
		n = fread(buf, 1, ivs[i].size, f);
		fclose(f);
		chk("real iv size", n, (long)ivs[i].size);
		chk("real iv fnv", (long)ob_fw_hash64(buf, n),
		    (long)ivs[i].fnv);
		chk("real iv parse", ob_fw_iv_parse(buf, ivs[i].size, &st), 0);
		chk("real iv records", (long)st.records,
		    (long)ivs[i].records);
		chk("real iv w16", (long)st.w16, (long)ivs[i].w16);
		chk("real iv w32", (long)st.w32, (long)ivs[i].w32);
		chk("real iv term", (long)st.terminator_index,
		    (long)ivs[i].term);
		free(buf);
	}

	for (i = 0; i < 2; i++) {
		f = fopen(upaths[i], "rb");
		if (!f)
			continue;
		buf = malloc(OB_FW_UCODE42_SIZE);
		n = fread(buf, 1, OB_FW_UCODE42_SIZE, f);
		fclose(f);
		chk("real ucode size", n, (long)OB_FW_UCODE42_SIZE);
		chk("real ucode fnv", (long)ob_fw_hash64(buf, n),
		    (long)OB_FW_UCODE42_FNV);
		free(buf);
	}
}

int main(void)
{
	test_ucode();
	test_ucode_against_blob();
	test_iv_valid();
	test_iv_bad();
	test_hash();
	test_real_files();

	if (failures) {
		printf("openbrcm fw tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm fw tests: PASS\n");
	return 0;
}
