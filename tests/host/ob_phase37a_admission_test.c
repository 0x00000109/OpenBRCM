// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 37A admission tests A01..A04, A14, A16.
 *
 * These execute the REAL pure admission decisions compiled into the module:
 *
 *   ob_isolated_default_load_refused()  Phase 37A default-load gate
 *   ob_aiw_legacy_retired()             Phase 36Z retired-mode refusal
 *   ob_isolated_mode_select22/23()      the 22+1 way isolated-mode policy
 *   ob_chip_profile_resolve()           the supported BCM4352 profile
 *
 * No bcma_device, no kernel API, no MMIO. A01 and A14 are the pure half of the
 * default-load proof; test_phase37a_source.py pins the wiring of the same
 * predicate inside the real ob_probe() before allocation and before every
 * dispatch branch.
 *
 * Every check prints one machine-readable RESULT line.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../../src/ob_ai_wrapper_only.h"
#include "../../src/ob_ucode.h"
#include "../../src/ob_profile.h"

static int failures;
static int checks;

static void report(const char *id, const char *fn, bool ok, int got, int want,
		   const char *note)
{
	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s func=%s ret=%d expected_ret=%d reads=0 trace=[] note=%s\n",
	       id, ok ? "PASS" : "FAIL", fn, got, want, note);
}

/* ---- 23 real flags in declaration order -------------------------------- */

enum {
	F_FW, F_UCODE, F_INITVALS, F_DMA, F_D11TAIL, F_BSINIT, F_RADIOID,
	F_DIAG, F_ATTACH, F_SBRESET, F_AI_PROBE, F_AI_ENABLE, F_R2069,
	F_D11TIMING, F_PRERADIO, F_CLASS0_0413, F_POST0413, F_POSTPHASE1,
	F_POSTPHASE2, F_POSTBMAC, F_RADIO_ON, F_POSTLPF, F_AI_WRAPPER,
	F_COUNT
};

static enum ob_isolated_mode sel23(const bool *f)
{
	return ob_isolated_mode_select23(
		f[F_FW], f[F_UCODE], f[F_INITVALS], f[F_DMA], f[F_D11TAIL],
		f[F_BSINIT], f[F_RADIOID], f[F_DIAG], f[F_ATTACH], f[F_SBRESET],
		f[F_AI_PROBE], f[F_AI_ENABLE], f[F_R2069], f[F_D11TIMING],
		f[F_PRERADIO], f[F_CLASS0_0413], f[F_POST0413],
		f[F_POSTPHASE1], f[F_POSTPHASE2], f[F_POSTBMAC], f[F_RADIO_ON],
		f[F_POSTLPF], f[F_AI_WRAPPER]);
}

static enum ob_isolated_mode sel22(const bool *f)
{
	return ob_isolated_mode_select22(
		f[F_FW], f[F_UCODE], f[F_INITVALS], f[F_DMA], f[F_D11TAIL],
		f[F_BSINIT], f[F_RADIOID], f[F_DIAG], f[F_ATTACH], f[F_SBRESET],
		f[F_AI_PROBE], f[F_AI_ENABLE], f[F_R2069], f[F_D11TIMING],
		f[F_PRERADIO], f[F_CLASS0_0413], f[F_POST0413],
		f[F_POSTPHASE1], f[F_POSTPHASE2], f[F_POSTBMAC], f[F_RADIO_ON],
		f[F_POSTLPF]);
}

static void flags_clear(bool *f)
{
	memset(f, 0, F_COUNT * sizeof(bool));
}

static struct ob_chip_identity bcm4352_identity(void)
{
	struct ob_chip_identity id;

	memset(&id, 0, sizeof(id));
	id.pci_present = true;
	id.bcma_present = true;
	id.d11_present = true;
	id.pci_vendor = 0x14e4;
	id.pci_device = 0x43b1;
	id.pci_revision = 0x03;
	id.chip_id = 0x4352;
	id.chip_revision = 3;
	id.d11_manufacturer = 0x04bf;
	id.d11_core_id = 0x0812;
	id.d11_revision = 42;
	id.d11_class = 0;
	return id;
}

/* ---- A01: no flags is refused before hardware --------------------------- */

static void a01_no_flags_refused(void)
{
	struct ob_chip_identity id = bcm4352_identity();
	const struct ob_chip_profile *profile = NULL;
	bool refused, wrapper_admitted, sprom_admitted, sflags_admitted;
	enum ob_chip_profile_status st;
	bool ok;

	refused = ob_isolated_default_load_refused(OB_ISOLATED_NONE, false,
						   false);
	wrapper_admitted = !ob_isolated_default_load_refused(
		OB_ISOLATED_REV42_AI_WRAPPER_ONLY, false, false);
	sprom_admitted = !ob_isolated_default_load_refused(
		OB_ISOLATED_NONE, true, false);
	sflags_admitted = !ob_isolated_default_load_refused(
		OB_ISOLATED_NONE, false, true);
	st = ob_chip_profile_resolve(&id, &profile);
	ok = refused && wrapper_admitted && sprom_admitted && sflags_admitted &&
	     st == OB_CHIP_PROFILE_OK && profile != NULL;

	report("A01", "ob_isolated_default_load_refused", ok, refused ? 1 : 0, 1,
	       "supported BCM4352 + no isolated flag is refused; wrapper and the two read-only diagnostics remain admitted");
}

/* ---- A02: retired legacy mode remains rejected -------------------------- */

static void a02_legacy_rejected(void)
{
	bool retired = ob_aiw_legacy_retired(true);
	bool live = ob_aiw_legacy_retired(false);

	report("A02", "ob_aiw_legacy_retired", retired && !live, retired ? 1 : 0,
	       1,
	       "rev42_ai_probe_only=1 is still an unconditional -EOPNOTSUPP refusal");
}

/* ---- A03: wrapper-only remains selectable ------------------------------- */

static void a03_wrapper_selectable(void)
{
	bool f[F_COUNT];
	enum ob_isolated_mode m;

	flags_clear(f);
	f[F_AI_WRAPPER] = true;
	m = sel23(f);
	report("A03", "ob_isolated_mode_select23",
	       m == OB_ISOLATED_REV42_AI_WRAPPER_ONLY &&
	       !ob_isolated_default_load_refused(m, false, false),
	       (int)m, (int)OB_ISOLATED_REV42_AI_WRAPPER_ONLY,
	       "rev42_ai_wrapper_only alone still selects the wrapper-only mode and passes the default-load gate");
}

/* ---- A04: conflicting isolated parameters reject ------------------------ */

static void a04_conflicts_rejected(void)
{
	bool f[F_COUNT];
	enum ob_isolated_mode w_enable, w_radio, fw_ucode, w_legacy;
	bool ok;

	flags_clear(f);
	f[F_AI_WRAPPER] = true;
	f[F_AI_ENABLE] = true;
	w_enable = sel23(f);

	flags_clear(f);
	f[F_AI_WRAPPER] = true;
	f[F_RADIO_ON] = true;
	w_radio = sel23(f);

	flags_clear(f);
	f[F_FW] = true;
	f[F_UCODE] = true;
	fw_ucode = sel23(f);

	flags_clear(f);
	f[F_AI_WRAPPER] = true;
	f[F_AI_PROBE] = true;
	w_legacy = sel23(f);

	ok = w_enable == OB_ISOLATED_CONFLICT &&
	     w_radio == OB_ISOLATED_CONFLICT &&
	     fw_ucode == OB_ISOLATED_CONFLICT &&
	     w_legacy == OB_ISOLATED_CONFLICT;
	report("A04", "ob_isolated_mode_select23", ok,
	       (int)OB_ISOLATED_CONFLICT, (int)OB_ISOLATED_CONFLICT,
	       "wrapper+enable, wrapper+radio_on, fw+ucode and wrapper+legacy all remain CONFLICT");
}

/* ---- A14: the pure default-load decision still refuses ------------------ */

static void a14_default_production_blocked(void)
{
	bool refused = ob_isolated_default_load_refused(OB_ISOLATED_NONE, false,
							false);
	bool ok = refused;

	report("A14", "ob_isolated_default_load_refused", ok, refused ? 1 : 0, 1,
	       "pure default-load decision refuses a bare load; test_phase37a_source.py pins its position in ob_probe() before allocation and ob_si_probe()");
}

/* ---- A16: the other 22 isolated modes are unchanged --------------------- */

static void a16_existing_modes_unchanged(void)
{
	static const enum ob_isolated_mode expected[22] = {
		OB_ISOLATED_FW_VALIDATE,
		OB_ISOLATED_UCODE_TEST,
		OB_ISOLATED_INITVALS_TEST,
		OB_ISOLATED_DMA_TEST,
		OB_ISOLATED_D3A1_TEST,
		OB_ISOLATED_D3B_TEST,
		OB_ISOLATED_RADIO_ID_PROBE,
		OB_ISOLATED_REV42_DIAG,
		OB_ISOLATED_REV42_ATTACH,
		OB_ISOLATED_REV42_SB_RESET,
		OB_ISOLATED_REV42_AI_PROBE,
		OB_ISOLATED_REV42_AI_ENABLE,
		OB_ISOLATED_RADIO2069_REV4_PROBE,
		OB_ISOLATED_D11_TRANSACTION_TIMING,
		OB_ISOLATED_ACPHY_PRE_RADIO_TEST,
		OB_ISOLATED_ACPHY_CLASS0_0413_TEST,
		OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST,
		OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST,
		OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST,
		OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST,
		OB_ISOLATED_ACPHY_RADIO_ON_TEST,
		OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST,
	};
	bool f[F_COUNT];
	int i;
	bool ok = true;
	int first_bad = -1;

	for (i = 0; i < 22; i++) {
		enum ob_isolated_mode m23, m22;

		flags_clear(f);
		f[i] = true;
		m23 = sel23(f);
		m22 = sel22(f);
		if (m23 != expected[i] || m22 != expected[i]) {
			ok = false;
			if (first_bad < 0)
				first_bad = i;
		}
	}

	report("A16", "ob_isolated_mode_select22/select23", ok,
	       first_bad, first_bad < 0 ? -1 : (int)expected[first_bad],
	       "each of the 22 pre-existing isolated flags still selects its exact mode through select22 and select23 with the 23rd flag clear");
}

int main(void)
{
	printf("# Phase 37A admission tests (real driver decisions)\n");
	a01_no_flags_refused();
	a02_legacy_rejected();
	a03_wrapper_selectable();
	a04_conflicts_rejected();
	a14_default_production_blocked();
	a16_existing_modes_unchanged();
	printf("# checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}
