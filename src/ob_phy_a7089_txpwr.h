/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_A7089_TXPWR_H_
#define _OB_PHY_A7089_TXPWR_H_

#include "ob_phy_96203.h"
#include "ob_phy_txpwr_index.h"

#define OB_A7089_RXGAINERR_BYTES 0x19u
#define OB_A7089_SUB8F086_BYTES 0x122u

enum ob_phy_a7089_txpwr_status {
	OB_A7089_TXPWR_INVALID = 0,
	OB_A7089_TXPWR_SYNTHETIC_COMPLETE,
	OB_A7089_TXPWR_FAILED_BACKEND,
	OB_A7089_TXPWR_UNSUPPORTED,
};

enum ob_phy_a7089_mac_state {
	OB_A7089_MAC_UNKNOWN = 0,
	OB_A7089_MAC_ENABLED,
	OB_A7089_MAC_SUSPENDED,
};

struct ob_phy_a7089_platform_ops {
	/* Delegated wlapi_suspend_mac_and_wait callback. The runner keeps the
	 * existing sub_99528 boundary and does not use this callback for the
	 * rxcore setter's separately gated MAC path. */
	int (*suspend_mac_and_wait)(void *ctx, u8 logical_event, u32 source_site);
	/* Delegated wlapi_enable_mac counterpart of the callback above. The
	 * pinned wlc_bmac_suspend_mac_and_wait/wlc_bmac_enable_mac pair is
	 * reference counted on hw+0x16c: the suspend nests when the MAC is
	 * already suspended by an outer owner and the enable only re-enables
	 * at the outermost decrement. A binding must preserve that nesting
	 * rule; a callback that rejects an already-suspended MAC is not
	 * equivalent. Required by wlc_phy_rxcore_setstate_acphy's nonzero arm
	 * only; its absence keeps that arm fail-closed. */
	int (*enable_mac)(void *ctx, u8 logical_event, u32 source_site);
};

struct ob_phy_a7089_txpwr_input {
	/* Reset-CCA and sub_96203 caller state; every field remains explicit. */
	struct ob_phy_96203_input post_resetcca;
	/* Points at nested+0x10. Bytes are loaded and sign-extended per loop. */
	u8 *nested_power_indices;
	u8 nested_power_indices_length;
	/* Explicit mutable bytes for nested+0x45a, written by txpwrctrl enable. */
	u8 *nested_txpwrctrl_values;
	u8 nested_txpwrctrl_values_length;
	/* TSSI threshold input: u16(nested+0x342). */
	bool nested_342_present;
	u16 nested_342;

	/* Optional explicit caller state for sub_a7089's post-TSSI continuation.
	 * PI+0x0fa2 and the chanspec word are runtime inputs, not defaults. */
	bool sub995ab_tail_present;
	bool pi_fa2_present;
	u8 pi_fa2;
	bool pi_17e_present;
	u16 pi_17e;
	u8 *nested_456;
	u8 nested_456_length;
	bool phy_context_31_present;
	u8 phy_context_31;
	/* Caller-saved r12b loaded from PI+0x0fa0 at @0xaa5c4, before the first
	 * arg-0 txpwrctrl child overwrites that PI field. */
	bool saved_pi_fa0_present;
	u8 saved_pi_fa0;
	bool mac_state_present;
	enum ob_phy_a7089_mac_state mac_state_at_99528;
	/* Optional continuation after the second txpwrctrl return. Bit 0 is the
	 * explicit synthetic nullness result from the earlier sub_8e9b1 class-0
	 * channel-row lookup into caller slot -0x40. Other slots belong to other
	 * class branches and are rejected for this BCM4352 profile. The runner does
	 * not synthesize or claim the lookup's real row selection. */
	bool post_aa63e_inputs_present;
	bool post_aa63e_lookup_mask_present;
	/* Selected sub_8e9b1 row output. Exactly one bit is set on its successful
	 * return: bits 0..3 represent [rbp-0x40], [rbp-0x48], [rbp-0x50],
	 * [rbp-0x58]. This runner currently admits only bit 0 for the selected
	 * class-0 synthetic profile; other slots need their upstream table route. */
	u8 post_aa63e_lookup_nonnull_mask;
	/* Entry snapshot loaded from nested+0x32c at sub_a7089 @0xa70ca.
	 * Distinct from the later live byte used at the resetCCA caller gate. */
	bool post_aa63e_nested_32c_snapshot_present;
	u8 post_aa63e_nested_32c_snapshot;
	bool post_aa63e_pi_a7_present;
	u8 post_aa63e_pi_a7;
	bool post_aa63e_pi_a4_present;
	u8 post_aa63e_pi_a4;
	/* Explicit shared-state byte consumed by rxcore_setstate's PHY[0x0401]
	 * restoration. It is not inferred from the caller comparator inputs. */
	bool post_aa63e_pi_a6_present;
	u8 post_aa63e_pi_a6;
	/* Explicit software state consumed by wlc_phy_get_rxgainerr_phy. The
	 * vector maps PI+0x1e3..PI+0x1fb and carries no hardware-default claim. */
	bool post_aa63e_rxgainerr_state_present;
	u32 post_aa63e_phy_type; /* PI+0x160; selected BCM4352 AC type is 0x0b. */
	u8 pi_rxgainerr_1e3_through_1fb[OB_A7089_RXGAINERR_BYTES];
	/* State consumed by the shared software-only sub_8f086 implementation. */
	bool post_aa63e_sub8f086_state_present;
	u8 nested_coefficients_450_through_571[OB_A7089_SUB8F086_BYTES];
	/* sub_8f086 adds core*0x78 to nested before reading its candidate row. */
	u8 nested_candidate_counts_64a_through_64f[6];
	bool nested_candidate_counts_6c2_through_6c7_present;
	u8 nested_candidate_counts_6c2_through_6c7[6];
	u8 nested_initial_650_through_655[6];
	/* Mutable view begins at nested+0x211; selected stores reach +0x213. */
	u8 *nested_gainerr_state_211;
	u8 nested_gainerr_state_211_length;
	/* wlc_phy_stay_in_carriersearch_acphy(PI,0) @0xaa757 consumes and
	 * decrements the low u16 at nested+0x00c. The pointer preserves mutable
	 * caller state; no default counter is inferred by this runner. */
	bool post_aa63e_carriersearch_state_present;
	u16 *nested_carriersearch_count;
	/* The selected helper always writes the explicit nested+0x904 word to
	 * PHY[0x0339] after its per-core branch. */
	bool post_aa63e_nested_904_present;
	u16 post_aa63e_nested_904;
	/* Read by sub_979bc only when PI+0x164 is zero; the callee uses the same
	 * unstrided nested+0x902 word for each bounded core iteration. */
	bool post_aa63e_nested_902_present;
	u16 post_aa63e_nested_902;
};

struct ob_phy_a7089_txpwr_result {
	enum ob_phy_a7089_txpwr_status status;
	struct ob_phy_96203_result post_resetcca;
	struct ob_phy_96203_result post_aa63e_resetcca;
	struct ob_phy_txpwr_index_result helper[2];
	u32 logical_event_count;
	u32 callback_count;
	/* Last executed historical site or the exact next unsupported site. */
	u32 terminal_boundary;
	u32 failed_site;
	u32 tail_callback_start;
	u32 tail_logical_event_count;
	u32 tail_callback_count;
	u32 unsupported_site;
	u32 txpwrctrl_logical_event_count;
	u32 txpwrctrl_callback_count;
	u8 txpwrctrl_nonzero_argument;
	s8 power_index[2];
	u8 core_selector[2];
	u8 helper_calls_completed;
	u8 txpwrctrl_core_updates;
	u8 pi_fa0_after_txpwrctrl;
	u8 nested_456_after[4];
	u8 nested_456_mutations;
	u8 tssi_threshold;
	u16 tssi_write_reg;
	u16 tssi_write_value;
	bool phy_040a_selected;
	bool txpwrctrl_arg0_written;
	bool txpwrctrl_per_core_reads;
	bool txpwrctrl_nonzero_path;
	bool txpwrctrl_nonzero_store;
	bool post_aa63e_entered;
	bool post_aa63e_radio_poll_selected;
	bool post_aa63e_poll_satisfied;
	bool post_aa63e_poll_exhausted;
	bool post_aa63e_nested_gate_nonzero;
	bool post_aa63e_rxcore_setstate_called;
	u8 post_aa63e_rxcore_argument;
	/* shared+0xa7 store at @0x97cfa: issued on both arms, before the branch. */
	bool post_aa63e_rxcore_a7_store;
	/* shared+0x31 == 0 arm: software-only, no PHY access and no MAC call. */
	bool post_aa63e_rxcore_software_only;
	/* shared+0x31 != 0 arm: delegated MAC suspend, PHY sequence, MAC enable. */
	bool post_aa63e_rxcore_mac_sequence;
	bool post_aa63e_rxcore_mac_suspended;
	bool post_aa63e_rxcore_mac_restored;
	/* A fail-stop between suspend and enable leaves the MAC suspended; a
	 * second run is refused until the owner restores the hardware. */
	bool post_aa63e_rxcore_reentry_forbidden;
	u16 post_aa63e_rxcore_saved_0400;
	u16 post_aa63e_rxcore_saved_0401;
	u32 post_aa63e_rfseq_poll_reads[2];
	u32 post_aa63e_rfseq_poll_delays[2];
	bool post_aa63e_rfseq_poll_exhausted[2];
	bool post_aa63e_rxgainerr_completed;
	bool post_aa63e_0400_selected;
	bool post_aa63e_carriersearch_entered;
	bool post_aa63e_carriersearch_effects_selected;
	bool post_aa63e_carriersearch_completed;
	u8 post_aa63e_lookup_nonnull_mask;
	u8 post_aa63e_poll_attempts;
	u8 post_aa63e_rxgainerr_band;
	u8 post_aa63e_rxgainerr_return;
	u8 post_aa63e_cores_completed;
	s16 post_aa63e_rxgainerr_values[2];
	u8 post_aa63e_sub8f086_returns[2];
	u8 post_aa63e_sub8f086_output[2][6];
	u16 post_aa63e_carriersearch_count_after;
	u8 post_aa63e_classifier_mask;
	u8 nested_gainerr_state_after[5];
	bool tssi_threshold_valid;
	bool tssi_phy_write_complete;
	bool sub995ab_entered;
	bool sub99528_called;
	bool sub99528_return_valid;
	s32 sub99528_return;
	bool mac_suspend_attempted;
	enum ob_phy_a7089_mac_state mac_state;
	bool tail_completed;
	bool hardware_ready;
};

/* Synthetic composition of sub_a7089's resetCCA/delay/sub_96203 prefix,
 * repeated by-index calls, the arg-0 txpwrctrl helper, and its software TSSI
 * threshold. Shared callback context preserves PHY/TABLE state. Execution
 * includes the selected TX-power-control and TSSI PHY write. When the
 * optional sub995ab_tail_present input is set, it also executes the proven
 * software transformation and caller join through the second txpwrctrl
 * return. The optional post_aa63e fixture evaluates the supported class-0
 * lookup result and bounded sub_93e47 poll, consumes the entry snapshot at
 * @0xaa664, and reuses resetCCA plus the software rxgainerr/sub_8f086 path for
 * the zero-snapshot arm. It then executes the supported arg-0
 * wlc_phy_stay_in_carriersearch_acphy continuation through caller return
 * @0xaa781 when the explicit nested counter and selected profile inputs are
	 * supported. A nonzero entry snapshot executes PHY_READ16 @0xaa672 and
	 * evaluates the historical comparison. On mismatch it executes
	 * `wlc_phy_rxcore_setstate_acphy`: the shared+0xa7 byte is stored on
	 * both arms, shared+0x31 == 0 is software-only (no PHY access, no MAC
	 * callback), and shared+0x31 != 0 delegates the reference-counted MAC
	 * suspend, runs the recovered register sequence with both bounded
	 * RF-sequence children, and delegates the MAC enable. The nonzero arm
	 * is fail-closed when either MAC callback is absent; a failure between
	 * suspend and enable leaves the MAC suspended and forbids re-entry
	 * without fabricating a resume. The MAC-delegating sub_99528 arm still
	 * stops before sub_9949f @0x99593. */
int ob_phy_a7089_txpwr_test_only(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result);

/*
 * W2 export: the same reconstructed wlc_phy_rxcore_setstate_acphy body, typed
 * for a caller that owns only the window.  The 43-operation ledger lives in the
 * static body above; this wrapper exists so the W2 stage binds it instead of
 * duplicating it.
 *
 * phy_context_31 selects the vendor arm.  A zero value is the software-only arm
 * (no PHY access, no MAC callback); W2 passes 1 because the pinned capture
 * contains the PHY[0x0401]/[0x0400] reads that only the nonzero arm issues.
 * pi_a6 is the shared+0xa6 byte the restore at 0x97de4 writes into bits 0..2.
 */
struct ob_phy_a7089_rxcore_export {
	bool called;
	bool software_only;
	bool mac_sequence;
	bool mac_restored;
	bool reentry_forbidden;
	u32 callback_count;
	u32 logical_event_count;
	u16 saved_0401;
	u16 saved_0400;
	u32 poll_reads[2];
	u32 poll_delays[2];
	bool poll_exhausted[2];
};

int ob_phy_a7089_rxcore_setstate_export(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	u8 phy_context_31, u8 pi_a6, u8 state,
	struct ob_phy_a7089_rxcore_export *out);

#endif /* _OB_PHY_A7089_TXPWR_H_ */
