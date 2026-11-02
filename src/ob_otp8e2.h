/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_OTP8E2_H_
#define _OB_OTP8E2_H_

#include <linux/errno.h>
#include <linux/types.h>

/* Historical type-1 OTP geometry for the BCM4352 selector-5 target route. */
#define OB_OTP8E2_WORD_INDEX	0x10u
#define OB_OTP8E2_COLUMNS	0x20u

struct ob_otp8e2_ops {
	int (*read_word16)(void *ctx, u16 word_index, u16 columns, u16 *word);
	void (*observation)(void *ctx, u16 word, u8 value, bool jbe_le3);
	void (*failure)(void *ctx, int ret);
};

/*
 * Acquire the historical logical word once, then expose only its software
 * result. On read failure no value is reported; in particular, a partially
 * filled output slot cannot masquerade as OTP data. All 16-bit words,
 * including 0xffff, are accepted when the backend reports success.
 */
static inline int ob_otp8e2_observe_word10(
	const struct ob_otp8e2_ops *ops, void *ctx)
{
	u16 word = 0;
	u8 value;
	int ret;

	if (!ops || !ops->read_word16 || !ops->observation || !ops->failure)
		return -EINVAL;

	ret = ops->read_word16(ctx, OB_OTP8E2_WORD_INDEX,
			       OB_OTP8E2_COLUMNS, &word);
	if (ret) {
		ops->failure(ctx, ret);
		return ret;
	}

	value = (word >> 8) & 0x1f;
	ops->observation(ctx, word, value, value <= 3);
	return 0;
}

#endif /* _OB_OTP8E2_H_ */
