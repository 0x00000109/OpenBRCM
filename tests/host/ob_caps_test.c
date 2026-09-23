// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host tests for the advertised 2.4 GHz capabilities.
 *
 * These assert exactly what ob_mac80211.c programs into mac80211, so a change
 * in the exposed channel/rate/HT set is caught here.
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
	int i;

	/* 2.4 GHz channel table */
	chk("n_channels", OB_N_2GHZ_CHANNELS, 14);
	for (i = 0; i < OB_N_2GHZ_CHANNELS; i++) {
		chk("channel number", ob_2ghz_channels[i], i + 1);
		chk("channel freq",
		    ob_channel_to_freq(ob_2ghz_channels[i], OB_BAND_2GHZ),
		    i == 13 ? 2484 : 2407 + 5 * (i + 1));
	}

	/* 2.4 GHz rate set: bitrate(100kbps) = (rate500 & 0x7f) * 5 */
	chk("n_rates", OB_N_2GHZ_RATES, 12);
	{
		static const int exp_mbps_x10[OB_N_2GHZ_RATES] = {
			10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540
		};
		for (i = 0; i < OB_N_2GHZ_RATES; i++) {
			long bitrate = (ob_2ghz_rates[i].rate500 & 0x7f) * 5;
			chk("rate bitrate", bitrate, exp_mbps_x10[i]);
		}
	}

	/* HT: the advertised highest rate (144) is derived from our MCS formula */
	chk("HT rx_highest is MCS7 20MHz 2ss SGI/1000",
	    ob_mcs_to_rate_kbps(7, 20, 2, true) / 1000, 144);
	/* 2 streams are backed by the acphy 2x2 target */
	chk("2ss supported", ob_mcs_to_rate_kbps(0, 20, 2, false), 13000);

	if (failures) {
		printf("openbrcm caps tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm caps tests: PASS\n");
	return 0;
}
