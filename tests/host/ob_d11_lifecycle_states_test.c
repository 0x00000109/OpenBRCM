// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — M4 D11 lifecycle state tests.
 *
 * Host-only: no kernel API, no MMIO, no module load.  These tests pin the
 * Phase 27 §4 transition contract and the Phase 30 failure-evidence rules:
 *
 *   - existing enum values are preserved;
 *   - only the eight forward edges are legal, including the three tail states;
 *   - skipped, repeated and backward transitions are refused;
 *   - invalid enum values are refused (fail closed);
 *   - a failure leaves the last proven state and records its stage;
 *   - FIFO/T1/DMA/T2/validation failures leave the hardware state unknown and
 *     cannot be reset;
 *   - a D2B-gate failure is not unknown and may be reset;
 *   - two devices keep independent state and fatal evidence.
 */
#include <stdio.h>
#include "ob_lifecycle.h"

static int failures;
static int checks;

static void chk(const char *what, long got, long exp)
{
	checks++;
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void chk_state(const char *what, struct ob_d11_lifecycle *lc,
		      enum ob_d11_lifecycle_state exp)
{
	chk(what, (long)lc->state, (long)exp);
}

static void test_enum_values_preserved(void)
{
	chk("NONE", OB_D11_STATE_NONE, 0);
	chk("CORE_READY", OB_D11_CORE_READY, 1);
	chk("ATTACH_IDENTITY_READY", OB_D11_ATTACH_IDENTITY_READY, 2);
	chk("UCODE_LOADED", OB_D11_UCODE_LOADED, 3);
	chk("PSM_STARTED", OB_D11_PSM_STARTED, 4);
	chk("STARTUP_SUSPEND_CONFIRMED",
	    OB_D11_STARTUP_SUSPEND_CONFIRMED, 5);
	chk("COMMON_INITVALS_APPLIED", OB_D11_COMMON_INITVALS_APPLIED, 6);
	chk("POST_UCODE_READY", OB_D11_POST_UCODE_READY, 7);
	chk("TAIL_FIXUP_APPLIED", OB_D11_TAIL_FIXUP_APPLIED, 8);
	chk("DMA_PROGRAMMED", OB_D11_DMA_PROGRAMMED, 9);
	chk("TAIL_COMPLETE", OB_D11_TAIL_COMPLETE, 10);
}

/*
 * The complete matrix: eight forward edges and nothing else.  This is the
 * same matrix the pre/post differential compares for states 0..7.
 */
static void test_exact_transition_matrix(void)
{
	static const int legal[11][11] = {
		[1][2] = 1, [2][3] = 1, [3][4] = 1, [4][5] = 1,
		[5][6] = 1, [6][7] = 1, [7][8] = 1, [8][9] = 1,
		[9][10] = 1,
	};
	int from, to;

	for (from = 0; from <= 10; from++) {
		for (to = 0; to <= 10; to++) {
			chk("exact matrix",
			    ob_d11_lifecycle_step_allowed(
				    (enum ob_d11_lifecycle_state)from,
				    (enum ob_d11_lifecycle_state)to),
			    legal[from][to]);
		}
	}
}

static void test_valid_sequential_transitions(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	chk_state("init", &lc, OB_D11_STATE_NONE);
	lc.state = OB_D11_CORE_READY;

	chk("publish attach", ob_d11_lifecycle_publish(&lc,
		OB_D11_ATTACH_IDENTITY_READY), 1);
	chk("publish ucode", ob_d11_lifecycle_publish(&lc,
		OB_D11_UCODE_LOADED), 1);
	chk("publish psm", ob_d11_lifecycle_publish(&lc,
		OB_D11_PSM_STARTED), 1);
	chk("publish suspend", ob_d11_lifecycle_publish(&lc,
		OB_D11_STARTUP_SUSPEND_CONFIRMED), 1);
	chk("publish common", ob_d11_lifecycle_publish(&lc,
		OB_D11_COMMON_INITVALS_APPLIED), 1);
	chk("publish post-ucode", ob_d11_lifecycle_publish(&lc,
		OB_D11_POST_UCODE_READY), 1);
	chk("publish fixup", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 1);
	chk("publish dma", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 1);
	chk("publish complete", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_COMPLETE), 1);
	chk_state("final", &lc, OB_D11_TAIL_COMPLETE);
	chk("no failure evidence", lc.failure_stage, OB_D11_FAIL_NONE);
	chk("not fatal", lc.fatal_evidence, 0);
}

static void test_invalid_skipping(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	chk("skip fixup to DMA", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 0);
	chk("skip to complete", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_COMPLETE), 0);
	chk_state("unchanged after skips", &lc, OB_D11_POST_UCODE_READY);

	lc.state = OB_D11_COMMON_INITVALS_APPLIED;
	chk("skip post-ucode", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 0);
	chk_state("unchanged before post-ucode", &lc,
		  OB_D11_COMMON_INITVALS_APPLIED);

	lc.state = OB_D11_TAIL_FIXUP_APPLIED;
	chk("skip DMA", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_COMPLETE), 0);
	chk_state("unchanged before DMA", &lc, OB_D11_TAIL_FIXUP_APPLIED);
}

static void test_invalid_enum_values(void)
{
	struct ob_d11_lifecycle lc;
	enum ob_d11_lifecycle_state bad[] = {
		(enum ob_d11_lifecycle_state)-1,
		(enum ob_d11_lifecycle_state)11,
		(enum ob_d11_lifecycle_state)99,
	};
	unsigned int i;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		chk("invalid next refused", ob_d11_lifecycle_step_allowed(
			OB_D11_POST_UCODE_READY, bad[i]), 0);
		chk("invalid state refused", ob_d11_lifecycle_step_allowed(
			bad[i], OB_D11_POST_UCODE_READY), 0);
		chk("invalid publish refused",
		    ob_d11_lifecycle_publish(&lc, bad[i]), 0);
		chk("invalid tail target",
		    (long)ob_d11_lifecycle_state_for_tail_stop(
			    (enum ob_d11_tail_stop)bad[i]),
		    (long)OB_D11_STATE_NONE);
	}
	chk("invalid failure stage refused", ob_d11_lifecycle_fail(
		&lc, (enum ob_d11_stage_failure)99, -22), 0);
	chk("invalid failure is unknown",
	    ob_d11_failure_hardware_state_unknown(
		    (enum ob_d11_stage_failure)99), 1);
	chk("invalid tail state", ob_d11_lifecycle_is_tail_state(
		(enum ob_d11_lifecycle_state)99), 0);
	chk_state("state unchanged after invalid", &lc,
		  OB_D11_POST_UCODE_READY);
}

static void test_repeated_transitions(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	chk("first fixup", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 1);
	chk("repeat fixup", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 0);
	chk("first DMA", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 1);
	chk("repeat DMA", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 0);
	chk("backward to fixup", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 0);
	chk("backward to post-ucode", ob_d11_lifecycle_publish(&lc,
		OB_D11_POST_UCODE_READY), 0);
	chk_state("state after repeats", &lc, OB_D11_DMA_PROGRAMMED);
}

static void test_failure_after_fifo_fixup(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	chk("record fixup failure", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_FIXUP, -110), 1);
	chk_state("state kept", &lc, OB_D11_POST_UCODE_READY);
	chk("stage", lc.failure_stage, OB_D11_FAIL_FIXUP);
	chk("errno", lc.failure_errno, -110);
	chk("unknown", lc.hardware_state_unknown, 1);
	chk("fatal", lc.fatal_evidence, 1);
	chk("reboot", lc.reboot_required, 1);
	chk("progression blocked", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 0);
	chk("reset refused", ob_d11_lifecycle_reset(&lc), 0);
	chk("later failure cannot overwrite", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_D2B_GATE, -5), 0);
	chk("first evidence preserved", lc.failure_stage,
	    OB_D11_FAIL_FIXUP);
	chk("first errno preserved", lc.failure_errno, -110);
	chk_state("evidence state preserved", &lc,
		  OB_D11_POST_UCODE_READY);
}

static void test_failure_before_dma(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_TAIL_FIXUP_APPLIED;
	chk("record T1 failure", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_T1, -5), 1);
	chk_state("state before DMA kept", &lc,
		  OB_D11_TAIL_FIXUP_APPLIED);
	chk("T1 unknown", lc.hardware_state_unknown, 1);
	chk("T1 reboot", lc.reboot_required, 1);
	chk("T1 blocks DMA", ob_d11_lifecycle_publish(&lc,
		OB_D11_DMA_PROGRAMMED), 0);
}

static void test_failure_after_dma(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_DMA_PROGRAMMED;
	chk("record DMA failure", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_DMA, -5), 1);
	chk_state("state after DMA kept", &lc, OB_D11_DMA_PROGRAMMED);
	chk("DMA unknown", lc.hardware_state_unknown, 1);

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_DMA_PROGRAMMED;
	chk("record T2 failure", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_T2, -5), 1);
	chk("T2 unknown", lc.hardware_state_unknown, 1);
	chk("T2 blocks complete", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_COMPLETE), 0);
	chk_state("T2 state kept", &lc, OB_D11_DMA_PROGRAMMED);
}

static void test_reset_versus_fatal_evidence(void)
{
	struct ob_d11_lifecycle lc;

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	chk("D2B failure recorded", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_D2B_GATE, -5), 1);
	chk("D2B not unknown", lc.hardware_state_unknown, 0);
	chk("D2B not fatal", lc.fatal_evidence, 0);
	chk("D2B does not need reboot", lc.reboot_required, 0);
	chk("D2B blocks progression", ob_d11_lifecycle_publish(&lc,
		OB_D11_TAIL_FIXUP_APPLIED), 0);
	chk("D2B reset allowed", ob_d11_lifecycle_reset(&lc), 1);
	chk_state("reset state", &lc, OB_D11_STATE_NONE);
	chk("reset clears stage", lc.failure_stage, OB_D11_FAIL_NONE);
	chk("reset clears errno", lc.failure_errno, 0);
	chk("reset clears unknown", lc.hardware_state_unknown, 0);
	chk("reset clears fatal", lc.fatal_evidence, 0);

	chk("null reset", ob_d11_lifecycle_reset(NULL), 0);
	chk("null failure", ob_d11_lifecycle_fail(NULL,
		OB_D11_FAIL_FIXUP, -5), 0);
	chk("null publish", ob_d11_lifecycle_publish(NULL,
		OB_D11_TAIL_FIXUP_APPLIED), 0);
	chk("none failure refused", ob_d11_lifecycle_fail(&lc,
		OB_D11_FAIL_NONE, 0), 0);
}

static void test_two_devices_independent(void)
{
	struct ob_d11_lifecycle a, b;

	ob_d11_lifecycle_init(&a);
	ob_d11_lifecycle_init(&b);
	a.state = OB_D11_POST_UCODE_READY;
	b.state = OB_D11_POST_UCODE_READY;

	chk("A fixup", ob_d11_lifecycle_publish(&a,
		OB_D11_TAIL_FIXUP_APPLIED), 1);
	chk("B still post-ucode", b.state, OB_D11_POST_UCODE_READY);
	chk("A failure", ob_d11_lifecycle_fail(&a,
		OB_D11_FAIL_FIXUP, -110), 1);
	chk("B unaffected by A failure", ob_d11_lifecycle_publish(&b,
		OB_D11_TAIL_FIXUP_APPLIED), 1);
	chk("B DMA", ob_d11_lifecycle_publish(&b,
		OB_D11_DMA_PROGRAMMED), 1);
	chk("A reset refused", ob_d11_lifecycle_reset(&a), 0);
	chk("B reset allowed", ob_d11_lifecycle_reset(&b), 1);
	chk_state("A keeps evidence", &a, OB_D11_TAIL_FIXUP_APPLIED);
	chk_state("B reset", &b, OB_D11_STATE_NONE);
	chk("A fatal intact", a.fatal_evidence, 1);
	chk("B fatal clear", b.fatal_evidence, 0);
}

static void test_tail_stage_mapping(void)
{
	struct ob_d11_lifecycle lc;

	chk("DISABLED maps to post-ucode",
	    (long)ob_d11_lifecycle_state_for_tail_stop(
		    OB_D11_TAIL_STOP_DISABLED),
	    (long)OB_D11_POST_UCODE_READY);
	chk("AFTER_FIXUP maps to fixup state",
	    (long)ob_d11_lifecycle_state_for_tail_stop(
		    OB_D11_TAIL_STOP_AFTER_FIXUP),
	    (long)OB_D11_TAIL_FIXUP_APPLIED);
	chk("AFTER_DMA maps to DMA state",
	    (long)ob_d11_lifecycle_state_for_tail_stop(
		    OB_D11_TAIL_STOP_AFTER_DMA),
	    (long)OB_D11_DMA_PROGRAMMED);
	chk("COMPLETE maps to complete state",
	    (long)ob_d11_lifecycle_state_for_tail_stop(
		    OB_D11_TAIL_STOP_COMPLETE),
	    (long)OB_D11_TAIL_COMPLETE);

	chk("fixup state is tail", ob_d11_lifecycle_is_tail_state(
		OB_D11_TAIL_FIXUP_APPLIED), 1);
	chk("DMA state is tail", ob_d11_lifecycle_is_tail_state(
		OB_D11_DMA_PROGRAMMED), 1);
	chk("complete state is tail", ob_d11_lifecycle_is_tail_state(
		OB_D11_TAIL_COMPLETE), 1);
	chk("post-ucode is not tail", ob_d11_lifecycle_is_tail_state(
		OB_D11_POST_UCODE_READY), 0);

	ob_d11_lifecycle_init(&lc);
	lc.state = OB_D11_POST_UCODE_READY;
	chk("publish AFTER_FIXUP target", ob_d11_lifecycle_publish(&lc,
		ob_d11_lifecycle_state_for_tail_stop(
			OB_D11_TAIL_STOP_AFTER_FIXUP)), 1);
	chk("publish AFTER_DMA target", ob_d11_lifecycle_publish(&lc,
		ob_d11_lifecycle_state_for_tail_stop(
			OB_D11_TAIL_STOP_AFTER_DMA)), 1);
	chk("publish COMPLETE target", ob_d11_lifecycle_publish(&lc,
		ob_d11_lifecycle_state_for_tail_stop(
			OB_D11_TAIL_STOP_COMPLETE)), 1);
	chk_state("tail complete", &lc, OB_D11_TAIL_COMPLETE);
}

static void test_failure_stage_classification(void)
{
	enum ob_d11_stage_failure s;

	for (s = OB_D11_FAIL_NONE; s <= OB_D11_FAIL_DEVICE_LOST; s++)
		chk("failure stage valid", ob_d11_failure_stage_valid(s), 1);

	chk("NONE known", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_NONE), 0);
	chk("D2B known", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_D2B_GATE), 0);
	chk("FIXUP unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_FIXUP), 1);
	chk("T1 unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_T1), 1);
	chk("DMA unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_DMA), 1);
	chk("T2 unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_T2), 1);
	chk("VALIDATE unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_VALIDATE), 1);
	chk("DEVICE_LOST unknown", ob_d11_failure_hardware_state_unknown(
		OB_D11_FAIL_DEVICE_LOST), 1);
}

int main(void)
{
	test_enum_values_preserved();
	test_exact_transition_matrix();
	test_valid_sequential_transitions();
	test_invalid_skipping();
	test_invalid_enum_values();
	test_repeated_transitions();
	test_failure_after_fifo_fixup();
	test_failure_before_dma();
	test_failure_after_dma();
	test_reset_versus_fatal_evidence();
	test_two_devices_independent();
	test_tail_stage_mapping();
	test_failure_stage_classification();

	if (failures) {
		printf("ob_d11_lifecycle_states_test: %d/%d checks FAILED\n",
		       failures, checks);
		return 1;
	}
	printf("ob_d11_lifecycle_states_test: %d checks PASS\n", checks);
	return 0;
}
