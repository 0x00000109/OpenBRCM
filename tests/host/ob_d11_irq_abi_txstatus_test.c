/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "../../src/ob_d11_irq_abi_txstatus.h"

static unsigned failures;

static void check_u(const char *name, unsigned got, unsigned expected)
{
	if (got != expected) {
		printf("FAIL: %s got=%u expected=%u\n", name, got, expected);
		failures++;
	}
}

static void check_b(const char *name, bool got, bool expected)
{
	check_u(name, got ? 1u : 0u, expected ? 1u : 0u);
}

int main(void)
{
	struct ob_d11_irq_abi_txstatus_fields fields;
	unsigned i;

	check_b("rev42 TX DPC flag clear",
		ob_d11_irq_abi_rev42_txstatus_dpc_selected(0), false);
	check_b("rev42 TX DPC flag set",
		ob_d11_irq_abi_rev42_txstatus_dpc_selected(0x20000000u), true);
	check_b("other DPC flag is not TX status",
		ob_d11_irq_abi_rev42_txstatus_dpc_selected(0x00008000u), false);
	check_b("mixed flags include TX status",
		ob_d11_irq_abi_rev42_txstatus_dpc_selected(0x20008000u), true);

	fields = ob_d11_irq_abi_decode_rev42_status_sample(0);
	check_b("empty sample absent", fields.present, false);
	check_u("empty status word", fields.status_word1, 0);
	check_u("empty status slot selector", fields.status_slot_selector, 0);

	fields = ob_d11_irq_abi_decode_rev42_status_sample(0xabc00000u);
	check_b("valid bit clear is absent", fields.present, false);
	check_u("upper halfword retained when absent", fields.status_word1, 0xabc0);
	check_u("status slot selector low three bits",
		fields.status_slot_selector, 0);

	for (i = 0; i < 8; i++) {
		ob_d11_irq_abi_u32 sample = (0xabc00000u | 1u) | (i << 16);

		fields = ob_d11_irq_abi_decode_rev42_status_sample(sample);
		check_b("present bit", fields.present, true);
		check_u("status halfword", fields.status_word1,
			(unsigned)((sample >> 16) & 0xffffu));
		check_u("all status slot selectors",
			fields.status_slot_selector, i);
	}

	fields = ob_d11_irq_abi_decode_rev42_status_sample(0xffff0001u);
	check_b("all-ones upper half is still present", fields.present, true);
	check_u("upper halfword width", fields.status_word1, 0xffff);
	check_u("upper halfword masked to 3-bit selector",
		fields.status_slot_selector, 7);

	if (failures) {
		printf("D11 rev42 txstatus ABI tests: %u failures\n", failures);
		return 1;
	}
	puts("D11 rev42 txstatus ABI tests: PASS (37 assertions)");
	return 0;
}
