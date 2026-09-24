// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — D11 rev42 firmware acquisition and validation (M3.4D1).
 *
 * Acquire the vendor-derived rev42 images through request_firmware(), validate
 * size + FNV-1a-64 + structure, and emit a dry-run plan. NO hardware writes:
 * no D11 SHM/IHR, MACCONTROL, PSM, PHY, radio, RX or TX access.
 *
 * Provenance: docs/firmware_reconciliation.md. Clean-room; no code from the
 * proprietary blob or upstream drivers.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include "ob_core.h"

static bool ob_fw_dryrun = true;
module_param_named(fw_dryrun, ob_fw_dryrun, bool, 0644);
MODULE_PARM_DESC(fw_dryrun,
		 "log first/last rev42 firmware records (M3.4D1 dry run)");

/*
 * Request and strictly validate one image. Returns 0 and stores @fw on
 * success, -ENOENT when the file (and any fallback) is absent, or a negative
 * error when a present file is malformed/mismatched. The caller owns @fw.
 */
static int ob_fw_request(struct ob_hw *hw, const char *name, const char *legacy,
			 size_t exp_size, u64 exp_fnv,
			 const struct firmware **fw, const char **used)
{
	const struct firmware *f = NULL;
	const char *which = name;
	int ret;

	ret = request_firmware(&f, name, hw->dev);
	if (ret == -ENOENT && legacy) {
		which = legacy;
		ret = request_firmware(&f, legacy, hw->dev);
	}
	if (ret)
		return ret;

	if (f->size != exp_size) {
		dev_err(hw->dev,
			OB_DRV_NAME ": fw %s bad size %zu (expected %zu)\n",
			which, f->size, exp_size);
		release_firmware(f);
		return -EINVAL;
	}
	if (ob_fw_hash64(f->data, f->size) != exp_fnv) {
		dev_err(hw->dev,
			OB_DRV_NAME ": fw %s hash mismatch (refusing it)\n",
			which);
		release_firmware(f);
		return -EILSEQ;
	}

	*fw = f;
	if (used)
		*used = which;
	return 0;
}

/*
 * Acquire + validate only the rev42 ucode image for the isolated hardware test
 * (M3.4D2A). Same strictness as ob_fw_probe(): exact size + FNV-1a-64.
 */
int ob_fw_request_ucode(struct ob_hw *hw, const struct firmware **fw,
			const char **used)
{
	return ob_fw_request(hw, OB_FW_UCODE42_NAME, OB_FW_UCODE42_NAME_LEGACY,
			     OB_FW_UCODE42_SIZE, OB_FW_UCODE42_FNV, fw, used);
}

/*
 * Acquire + size/FNV-validate the rev42 common initvals table for the isolated
 * common-initvals test (M3.4D2B). Same strictness as ob_fw_probe().
 */
int ob_fw_request_initvals(struct ob_hw *hw, const struct firmware **fw)
{
	return ob_fw_request(hw, OB_FW_AC1INITVALS42_NAME, NULL,
			     OB_FW_AC1INITVALS42_SIZE,
			     OB_FW_AC1INITVALS42_FNV, fw, NULL);
}

static void ob_fw_trace_ucode(struct ob_hw *hw, const u8 *data, u32 words)
{
	u32 i, v, start;

	if (!ob_fw_dryrun)
		return;

	dev_info(hw->dev, OB_DRV_NAME ": fw dry-run d11ucode42 first %u words:\n",
		 min(words, OB_FW_TRACE_N));
	for (i = 0; i < words && i < OB_FW_TRACE_N; i++) {
		ob_fw_ucode_word(data, i, &v);
		dev_info(hw->dev, OB_DRV_NAME ":   w[%u]=0x%08x\n", i, v);
	}
	start = (words > OB_FW_TRACE_N) ? words - OB_FW_TRACE_N : 0;
	dev_info(hw->dev, OB_DRV_NAME ": fw dry-run d11ucode42 last %u words:\n",
		 min(words, OB_FW_TRACE_N));
	for (i = start; i < words; i++) {
		ob_fw_ucode_word(data, i, &v);
		dev_info(hw->dev, OB_DRV_NAME ":   w[%u]=0x%08x\n", i, v);
	}
}

static void ob_fw_trace_iv(struct ob_hw *hw, const char *name, const u8 *data,
			   size_t size)
{
	u32 n = ob_fw_iv_record_index(size), i, start;
	struct ob_fw_iv iv;

	if (!ob_fw_dryrun || n == 0)
		return;

	dev_info(hw->dev, OB_DRV_NAME ": fw dry-run %s first %u records:\n",
		 name, min(n, OB_FW_TRACE_N));
	for (i = 0; i < n && i < OB_FW_TRACE_N; i++) {
		if (ob_fw_iv_at(data, size, i, &iv))
			break;
		dev_info(hw->dev,
			 OB_DRV_NAME ":   [%u] off=0x%03x width=%u value=0x%x\n",
			 i, iv.offset, iv.width, iv.value);
	}
	start = (n > OB_FW_TRACE_N) ? n - OB_FW_TRACE_N : 0;
	dev_info(hw->dev, OB_DRV_NAME ": fw dry-run %s last %u records:\n",
		 name, min(n, OB_FW_TRACE_N));
	for (i = start; i < n; i++) {
		if (ob_fw_iv_at(data, size, i, &iv))
			break;
		dev_info(hw->dev,
			 OB_DRV_NAME ":   [%u] off=0x%03x width=%u value=0x%x\n",
			 i, iv.offset, iv.width, iv.value);
	}
}

/*
 * M3.4D1 entry point. Present-but-invalid firmware is a hard error so it can
 * never be uploaded; absent firmware is reported and leaves the currently
 * validated SPROM/MAC/DMA/IRQ behaviour unchanged.
 */
int ob_fw_probe(struct ob_hw *hw)
{
	const struct firmware *ucode = NULL, *iv = NULL, *biv = NULL;
	const char *ucode_name = NULL;
	bool missing = false;
	int ret;

	/* --- d11ucode42 --- */
	ret = ob_fw_request(hw, OB_FW_UCODE42_NAME, OB_FW_UCODE42_NAME_LEGACY,
			    OB_FW_UCODE42_SIZE, OB_FW_UCODE42_FNV,
			    &ucode, &ucode_name);
	if (ret == -ENOENT) {
		missing = true;
		dev_warn(hw->dev,
			 OB_DRV_NAME ": rev42 ucode missing; M3.4D2 unavailable\n");
	} else if (ret) {
		goto err;
	} else if (!ob_fw_ucode_size_ok(ucode->size) || (ucode->size % 4)) {
		dev_err(hw->dev, OB_DRV_NAME ": fw %s not a flat word stream\n",
			ucode_name);
		ret = -EINVAL;
		goto err;
	} else {
		hw->fw.ucode_words = ob_fw_ucode_words(ucode->size);
		hw->fw.ucode_valid = true;
		hw->fw.ucode_name = ucode_name;
		dev_info(hw->dev,
			 OB_DRV_NAME ": fw %s size=%zu words=%u validated\n",
			 ucode_name, ucode->size, hw->fw.ucode_words);
	}

	/* --- ac1initvals42 --- */
	ret = ob_fw_request(hw, OB_FW_AC1INITVALS42_NAME, NULL,
			    OB_FW_AC1INITVALS42_SIZE, OB_FW_AC1INITVALS42_FNV,
			    &iv, NULL);
	if (ret == -ENOENT) {
		missing = true;
		dev_warn(hw->dev, OB_DRV_NAME ": ac1initvals42 missing\n");
	} else if (ret) {
		goto err;
	} else if (ob_fw_iv_parse(iv->data, iv->size, &hw->fw.initvals)) {
		dev_err(hw->dev,
			OB_DRV_NAME ": fw %s invalid IV table\n",
			OB_FW_AC1INITVALS42_NAME);
		ret = -EINVAL;
		goto err;
	} else {
		hw->fw.initvals_valid = true;
		dev_info(hw->dev,
			 OB_DRV_NAME ": fw %s size=%zu records=%u w16=%u w32=%u terminator=%u validated\n",
			 OB_FW_AC1INITVALS42_NAME, iv->size,
			 hw->fw.initvals.records, hw->fw.initvals.w16,
			 hw->fw.initvals.w32, hw->fw.initvals.terminator_index);
	}

	/* --- ac1bsinitvals42 --- */
	ret = ob_fw_request(hw, OB_FW_AC1BSINITVALS42_NAME, NULL,
			    OB_FW_AC1BSINITVALS42_SIZE,
			    OB_FW_AC1BSINITVALS42_FNV, &biv, NULL);
	if (ret == -ENOENT) {
		missing = true;
		dev_warn(hw->dev, OB_DRV_NAME ": ac1bsinitvals42 missing\n");
	} else if (ret) {
		goto err;
	} else if (ob_fw_iv_parse(biv->data, biv->size, &hw->fw.bsinitvals)) {
		dev_err(hw->dev,
			OB_DRV_NAME ": fw %s invalid IV table\n",
			OB_FW_AC1BSINITVALS42_NAME);
		ret = -EINVAL;
		goto err;
	} else {
		hw->fw.bsinitvals_valid = true;
		dev_info(hw->dev,
			 OB_DRV_NAME ": fw %s size=%zu records=%u w16=%u w32=%u terminator=%u validated\n",
			 OB_FW_AC1BSINITVALS42_NAME, biv->size,
			 hw->fw.bsinitvals.records, hw->fw.bsinitvals.w16,
			 hw->fw.bsinitvals.w32,
			 hw->fw.bsinitvals.terminator_index);
	}

	/* --- dry-run plan (no writes) --- */
	if (ucode)
		ob_fw_trace_ucode(hw, ucode->data, hw->fw.ucode_words);
	if (iv)
		ob_fw_trace_iv(hw, "ac1initvals42", iv->data, iv->size);
	if (biv)
		ob_fw_trace_iv(hw, "ac1bsinitvals42", biv->data, biv->size);

	if (!missing)
		dev_info(hw->dev,
			 OB_DRV_NAME ": fw all rev42 firmware validated; hardware untouched\n");

	ret = 0;

err:
	release_firmware(ucode);
	release_firmware(iv);
	release_firmware(biv);
	return ret;
}
