/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — W2 firmware-chain kernel adapter.
 *
 * The public kernel entry points are deliberately narrow: one identity gate,
 * one production chain and one isolated test chain.  No caller may issue D2A
 * or the common table while bypassing the pure contract in
 * ob_fw_chain_contract.h.
 */
#ifndef _OB_FW_CHAIN_H_
#define _OB_FW_CHAIN_H_

#include "ob_fw_chain_contract.h"

#ifdef __KERNEL__
struct ob_hw;

/*
 * Validate chip/D11 identity and, when runtime_required, the runtime PHY/radio
 * identity published by ob_acphy_attach_identity().  Returns 0 before any
 * firmware request or chain MMIO; an error is a pre-mutation refusal.
 */
int ob_fw_chain_identity_gate(struct ob_hw *hw, bool runtime_required);

/*
 * Run the single D2A -> common-initvals -> lifecycle chain.
 *
 * policy.runtime_identity_required is true for the normal path and for the
 * isolated fw_chain_test_only run, false for the existing isolated D2B/D3A0/
 * D3B callers.  policy.publish_lifecycle is true only for the normal/test path;
 * isolated callers must not advance production lifecycle state.
 *
 * On failure with result->retained set, hw->ucode_retain is latched and the
 * caller must keep the device bound: this function issues no cleanup, reset,
 * retry or second upload.
 */
int ob_fw_chain_run(struct ob_hw *hw, const char *tag,
		    const struct ob_fw_chain_policy *policy,
		    struct ob_fw_chain_result *result);

/*
 * Isolated W2 test: requires ATTACH_IDENTITY_READY, runs the exact production
 * chain and stops at POST_UCODE_READY.  No PHY/radio/DMA/IRQ/mac80211.
 */
int ob_fw_chain_test(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_FW_CHAIN_H_ */
