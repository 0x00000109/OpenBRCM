// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the pure math (RE Stage 6).
 *
 * These assert the same invariants that the reverse-engineering audit used
 * (audit-0001.md, execution oracle): channel<->frequency round trips and the
 * 802.11n/ac MCS rate anchors.
 */
#include <stdio.h>
#include "../../src/ob_channel.h"
#include "../../src/ob_rate.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

int main(void)
{
	int ch;

	/* Channels (all bands) */
	for (ch = 1; ch <= 13; ch++)
		chk("2.4GHz freq", ob_channel_to_freq(ch, OB_BAND_2GHZ), 2407 + 5 * ch);
	chk("ch14", ob_channel_to_freq(14, OB_BAND_2GHZ), 2484);
	chk("2.4GHz invalid", ob_channel_to_freq(200, OB_BAND_2GHZ), -1);

	chk("5GHz ch36", ob_channel_to_freq(36, OB_BAND_5GHZ), 5180);
	chk("5GHz ch100", ob_channel_to_freq(100, OB_BAND_5GHZ), 5500);

	/* Round trips */
	for (ch = 1; ch <= 14; ch++)
		chk("2g roundtrip", ob_freq_to_channel(ob_channel_to_freq(ch, OB_BAND_2GHZ), OB_BAND_2GHZ), ch);
	for (ch = 36; ch <= 165; ch += 4)
		chk("5g roundtrip", ob_freq_to_channel(ob_channel_to_freq(ch, OB_BAND_5GHZ), OB_BAND_5GHZ), ch);

	/* MCS anchors (kbps) */
	chk("MCS0 20 GI800", ob_mcs_to_rate_kbps(0, 20, 1, false), 6500);
	chk("MCS7 20 GI800", ob_mcs_to_rate_kbps(7, 20, 1, false), 65000);
	chk("MCS0 40 GI800", ob_mcs_to_rate_kbps(0, 40, 1, false), 13500);
	chk("MCS0 80 GI800", ob_mcs_to_rate_kbps(0, 80, 1, false), 29250);
	chk("MCS9 80 GI800", ob_mcs_to_rate_kbps(9, 80, 1, false), 390000);
	chk("MCS0 20 GI400", ob_mcs_to_rate_kbps(0, 20, 1, true), 7222);
	chk("MCS9 80 GI400", ob_mcs_to_rate_kbps(9, 80, 1, true), 433333);
	chk("MCS9 80 2ss", ob_mcs_to_rate_kbps(9, 80, 2, false), 780000);
	chk("invalid mcs", ob_mcs_to_rate_kbps(20, 20, 1, false), 0);

	if (failures) {
		printf("openbrcm math tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm math tests: PASS\n");
	return 0;
}
