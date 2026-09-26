/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 rev42 firmware acquisition and validation (M3.4D1).
 *
 * Provenance A: the exact firmware objects extracted from the same Broadcom
 * vendor blob we reverse-engineer (d11ucode42 / d11ac1initvals42 /
 * d11ac1bsinitvals42). No b43 IV conversion, no firmware upload. See
 * docs/firmware_reconciliation.md.
 *
 * The parsing rules below are recovered from the vendor consumer
 * `wlc_bmac_write_inits` (blob 0x60fce -> 0x60f67), which walks 8-byte
 * little-endian records {u16 offset, u16 width, u32 value} terminated by
 * offset == 0xffff.
 */
#ifndef _OB_FW_H_
#define _OB_FW_H_

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
#else
#include <errno.h>
#include <stddef.h>
#endif

/* ---- exact vendor-derived firmware files ---- */
#define OB_FW_UCODE42_NAME		"brcm/bcm4352-d11ucode42.bin"
/* Documented fallback: the currently installed name of the same image. */
#define OB_FW_UCODE42_NAME_LEGACY	"brcm/bcm43xx-ucode.fw"
#define OB_FW_AC1INITVALS42_NAME	"brcm/bcm4352-d11ac1initvals42.bin"
#define OB_FW_AC1BSINITVALS42_NAME	"brcm/bcm4352-d11ac1bsinitvals42.bin"

#define OB_FW_UCODE42_SIZE		43400u
#define OB_FW_AC1INITVALS42_SIZE	4888u
#define OB_FW_AC1BSINITVALS42_SIZE	592u

/*
 * FNV-1a-64 guards computed from the vendor blob slices. Strict validation:
 * a present-but-different file (even of the right size) fails probe instead of
 * ever being uploaded. Host-side SHA256 is recorded in the M3.4C.1 report.
 */
#define OB_FW_UCODE42_FNV		0x7d364f6207b3298bULL
#define OB_FW_AC1INITVALS42_FNV		0xa7cfdfdbcc9d58f1ULL
#define OB_FW_AC1BSINITVALS42_FNV	0xfa86a2510d2e0e3eULL

/* ---- vendor initvals record ---- */
struct ob_fw_iv {
	u16 offset;
	u16 width;	/* OB_FW_IV_WIDTH16 (value in low 16 bits) or WIDTH32 */
	u32 value;
};

#define OB_FW_IV_RECORD_SIZE	8u
#define OB_FW_IV_TERMINATOR	0xffffu
#define OB_FW_IV_WIDTH16	2u
#define OB_FW_IV_WIDTH32	4u
/* Proven D11 register window the vendor applier can address. */
#define OB_FW_IV_MAX_OFFSET	0x0fffu

/* Number of leading/trailing records or words logged by the dry run. */
#define OB_FW_TRACE_N		10u

struct ob_fw_iv_stats {
	u32 records;		/* data records before the terminator */
	u32 w16;
	u32 w32;
	u32 terminator_index;	/* index of the 0xffff terminator record */
};

/* ---- pure helpers (host-testable) ---- */

static inline u16 ob_fw_le16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static inline u32 ob_fw_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline u64 ob_fw_hash64(const u8 *data, size_t n)
{
	u64 h = 0xcbf29ce484222325ULL;
	size_t i;

	for (i = 0; i < n; i++) {
		h ^= data[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

/*
 * The vendor consumer writes the image as a flat stream of 32-bit words with
 * no container/header and no byte swap (blob 0x60744 stores *(__le32 *) raw;
 * brcmsmac corroborates via `bcma_write32(objdata, le32_to_cpu(ucode[i]))`).
 */
static inline bool ob_fw_ucode_size_ok(size_t size)
{
	return size == OB_FW_UCODE42_SIZE;
}

static inline u32 ob_fw_ucode_words(size_t size)
{
	return (u32)(size / 4);
}

static inline void ob_fw_ucode_word(const u8 *data, u32 i, u32 *out)
{
	*out = ob_fw_le32(data + (size_t)i * 4);
}

static inline u32 ob_fw_iv_record_index(size_t size)
{
	return (u32)(size / OB_FW_IV_RECORD_SIZE);
}

/*
 * Parse a vendor initvals table into record counts.
 *
 * Returns 0 on success, -EINVAL on malformed input (bad size, illegal width,
 * offset outside the D11 window, or records after the terminator) and
 * -ENODATA when the 0xffff terminator is missing.
 */
static inline int ob_fw_iv_parse(const u8 *data, size_t size,
				 struct ob_fw_iv_stats *st)
{
	size_t n, i;

	if (!data || !st || size == 0 ||
	    (size % OB_FW_IV_RECORD_SIZE) != 0)
		return -EINVAL;

	n = size / OB_FW_IV_RECORD_SIZE;
	st->records = 0;
	st->w16 = 0;
	st->w32 = 0;
	st->terminator_index = 0;

	for (i = 0; i < n; i++) {
		const u8 *r = data + i * OB_FW_IV_RECORD_SIZE;
		u16 offset = ob_fw_le16(r);
		u16 width = ob_fw_le16(r + 2);

		if (offset == OB_FW_IV_TERMINATOR) {
			/* The vendor table ends here: nothing may follow. */
			if (i != n - 1)
				return -EINVAL;
			st->terminator_index = (u32)i;
			return 0;
		}
		if (width != OB_FW_IV_WIDTH16 && width != OB_FW_IV_WIDTH32)
			return -EINVAL;
		if (offset > OB_FW_IV_MAX_OFFSET)
			return -EINVAL;

		if (width == OB_FW_IV_WIDTH32)
			st->w32++;
		else
			st->w16++;
		st->records++;
	}
	return -ENODATA;
}

/*
 * Decode record @i for the dry-run iterator. Only a 16-bit value is meaningful
 * when width == OB_FW_IV_WIDTH16 (the vendor applier reads a halfword there).
 */
static inline int ob_fw_iv_at(const u8 *data, size_t size, u32 i,
			      struct ob_fw_iv *iv)
{
	const u8 *r;

	if (!data || !iv ||
	    (size_t)i * OB_FW_IV_RECORD_SIZE + OB_FW_IV_RECORD_SIZE > size)
		return -EINVAL;

	r = data + (size_t)i * OB_FW_IV_RECORD_SIZE;
	iv->offset = ob_fw_le16(r);
	iv->width = ob_fw_le16(r + 2);
	if (iv->width == OB_FW_IV_WIDTH16)
		iv->value = ob_fw_le16(r + 4);
	else
		iv->value = ob_fw_le32(r + 4);
	return 0;
}

#ifdef __KERNEL__
struct ob_hw;
struct firmware;

/**
 * struct ob_fw - rev42 firmware validation state (M3.4D1)
 * @ucode_valid:	d11ucode42 validated
 * @initvals_valid:	ac1initvals42 validated
 * @bsinitvals_valid:	ac1bsinitvals42 validated
 * @ucode_words:	validated ucode word count (10850)
 * @initvals:		ac1initvals42 record statistics
 * @bsinitvals:		ac1bsinitvals42 record statistics
 * @ucode_name:		name actually used for the ucode image
 */
struct ob_fw {
	bool			ucode_valid;
	bool			initvals_valid;
	bool			bsinitvals_valid;
	u32			ucode_words;
	struct ob_fw_iv_stats	initvals;
	struct ob_fw_iv_stats	bsinitvals;
	const char		*ucode_name;
};

int ob_fw_probe(struct ob_hw *hw);

/*
 * Acquire + size/FNV-validate only the rev42 ucode image (primary name with the
 * documented legacy fallback). The caller owns *fw and must release_firmware().
 * Used by the isolated ucode_test_only path (M3.4D2A).
 */
int ob_fw_request_ucode(struct ob_hw *hw, const struct firmware **fw,
			const char **used);

/*
 * Acquire + size/FNV-validate the rev42 common initvals table
 * (bcm4352-d11ac1initvals42.bin). The caller owns *fw and must
 * release_firmware(). Used by the isolated initvals_test_only path (M3.4D2B).
 */
int ob_fw_request_initvals(struct ob_hw *hw, const struct firmware **fw);

/*
 * Acquire + size/FNV-validate the rev42 band-switch initvals table
 * (bcm4352-d11ac1bsinitvals42.bin). The caller owns *fw and must
 * release_firmware(). Used by the isolated bsinitvals_test_only path (M3.4D3B).
 */
int ob_fw_request_bsinitvals(struct ob_hw *hw, const struct firmware **fw);
#endif /* __KERNEL__ */

#endif /* _OB_FW_H_ */
