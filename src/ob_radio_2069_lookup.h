/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RADIO_2069_LOOKUP_H_
#define _OB_RADIO_2069_LOOKUP_H_

#include <linux/types.h>

/*
 * Clean-room sub_8e9b1 lookup contract for BCM2069 rev4, class 0.
 *
 * Accepted historical packet:
 * docs/research/packets/historical-binary-identity-recovery-and-chanspec-
 * resume-20261008.md. The admitted RE.DB table is 77 rows with a 0x74-byte
 * stride. Its indexed bytes place key 1 at row 63, offset 0x1c8c; an older
 * packet's offset 0x1c5c is an arithmetic transcription error. This helper
 * returns the proven key match and row identity; the +2 word is published
 * only for key 1, whose full row is retained in the accepted evidence.
 */
#define OB_RADIO2069_REV4_ROW_COUNT	77u
#define OB_RADIO2069_REV4_ROW_STRIDE	0x74u
#define OB_RADIO2069_REV4_TABLE_SIZE	0x22e4u

struct ob_radio2069_lookup_result {
	bool found;
	u8 key;
	u8 row_index;
	u16 row_offset;
	bool row_word2_valid;
	u16 row_word2;
};

static const u8 ob_radio2069_rev4_class0_keys[OB_RADIO2069_REV4_ROW_COUNT] = {
	184, 185, 187, 188, 189, 192, 196, 207, 208, 209, 210, 212, 216,
	34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 62, 64,
	100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124,
	126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 145, 146, 147,
	148, 149, 151, 153, 155, 157, 159, 161, 165,
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};

static inline int ob_radio2069_rev4_lookup_key(
	u8 key, struct ob_radio2069_lookup_result *result)
{
	unsigned int i;

	if (!result)
		return -22;
	*result = (struct ob_radio2069_lookup_result){ .key = key };
	for (i = 0; i < OB_RADIO2069_REV4_ROW_COUNT; i++) {
		if (ob_radio2069_rev4_class0_keys[i] != key)
			continue;
		result->found = true;
		result->row_index = (u8)i;
		result->row_offset = (u16)(i * OB_RADIO2069_REV4_ROW_STRIDE);
		if (key == 1u) {
			result->row_word2_valid = true;
			result->row_word2 = 0x096cu;
		}
		return 0;
	}
	return -2;
}

/* Decode only the OpenBRCM-implemented 2.4-GHz, 20-MHz chanspec form.
 * The channel comes from the caller's current request; no test channel is
 * substituted when input is malformed or unsupported. */
static inline int ob_radio2069_rev4_lookup_2g20(
	u16 chanspec, struct ob_radio2069_lookup_result *result)
{
	u8 channel;

	if (!result)
		return -22;
	*result = (struct ob_radio2069_lookup_result){ 0 };
	if ((chanspec & 0xff00u) != 0x1000u)
		return -95;
	channel = (u8)chanspec;
	if (channel < 1u || channel > 14u ||
	    chanspec != (u16)(0x1000u | channel))
		return -95;
	return ob_radio2069_rev4_lookup_key(channel, result);
}

#endif /* _OB_RADIO_2069_LOOKUP_H_ */
