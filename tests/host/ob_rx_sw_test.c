/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_sw.h"

struct fake_sink {
	unsigned int calls;
	const unsigned char *frame;
	ob_rx_sw_size len;
	struct ob_rx_sw_status status;
	int result;
};

static unsigned int failures;

static void check(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

static void check_u(const char *name, unsigned int got, unsigned int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %u expected %u\n", name, got, expected);
		failures++;
	}
}

static int sink(void *ctx, const ob_rx_sw_u8 *frame,
		ob_rx_sw_size len, const struct ob_rx_sw_status *status)
{
	struct fake_sink *fake = ctx;

	fake->calls++;
	fake->frame = frame;
	fake->len = len;
	fake->status = *status;
	return fake->result;
}

static void reset_sink(struct fake_sink *fake, int result)
{
	memset(fake, 0, sizeof(*fake));
	fake->result = result;
}

int main(void)
{
	/* Beacon management frame: valid FC, complete 24-byte minimum header. */
	unsigned char mgmt[24] = { 0x80, 0x00 };
	/* Non-QoS data frame with ToDS+FromDS: base 24 plus address 4. */
	unsigned char addr4[30] = { 0x08, 0x03 };
	/* QoS data with HT control indication: 24 + QoS + HTC. */
	unsigned char qos_htc[30] = { 0x88, 0x80 };
	/* CTS control frame, one of the fixed-size control forms admitted. */
	unsigned char cts[10] = { 0xc4, 0x00 };
	unsigned char mgmt_with_fcs[28] = { 0x80, 0x00 };
	unsigned char short_mgmt[23] = { 0x80, 0x00 };
	unsigned char bad_version[24] = { 0x81, 0x00 };
	unsigned char unsupported_ctrl[16] = { 0xb4, 0x00 };
	unsigned char extension[24] = { 0x0c, 0x00 };
	struct ob_rx_sw_metadata meta = {
		.provenance = OB_RX_SW_PROVENANCE_VALIDATED,
		.valid_fields = OB_RX_SW_META_CHANNEL | OB_RX_SW_META_RATE |
				OB_RX_SW_META_SIGNAL | OB_RX_SW_META_FCS_LEN,
		.declared_len = sizeof(mgmt),
		.complete = 1,
		.channel = 6,
		.rate_index = 3,
		.signal_dbm = -47,
		.fcs_len = 0,
	};
	struct ob_rx_sw_status status;
	struct fake_sink fake;
	int ret;

	reset_sink(&fake, 0);
	ret = ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake);
	check("valid management delivered", ret, 0);
	check_u("single sink handoff", fake.calls, 1);
	check("sink borrows original frame", fake.frame == mgmt, 1);
	check_u("frame length preserved", fake.len, sizeof(mgmt));
	check_u("trusted channel copied", fake.status.channel, 6);
	check_u("trusted rate copied", fake.status.rate_index, 3);
	check("trusted signal copied", fake.status.signal_dbm, -47);
	check_u("trusted FCS contract copied", fake.status.fcs_len, 0);
	check_u("all trusted bits copied", fake.status.valid_fields,
		OB_RX_SW_META_KNOWN_MASK & ~OB_RX_SW_META_RATE_HT);
	check("input remains caller-owned and unchanged", mgmt[0] == 0x80, 1);

	/* HT carries an MCS index and its proven width/GI bits. */
	meta.valid_fields |= OB_RX_SW_META_RATE_HT;
	meta.rate_index = 7;
	meta.rate_bw40 = 1;
	meta.rate_sgi = 1;
	ret = ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status);
	check("HT metadata accepted", ret, 0);
	check_u("HT rate bit retained", status.valid_fields & OB_RX_SW_META_RATE_HT,
		OB_RX_SW_META_RATE_HT);
	check_u("HT MCS copied", status.rate_index, 7);
	check("HT width/GI copied", status.rate_is_ht && status.rate_bw40 &&
	      status.rate_sgi, 1);
	meta.rate_index = 32;
	check("HT MCS 32 rejected",
	      ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status), -ERANGE);
	meta.rate_index = 7;
	meta.rate_bw40 = 2;
	check("non-boolean HT width rejected",
	      ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status), -EINVAL);
	meta.rate_bw40 = 1;
	meta.valid_fields &= ~OB_RX_SW_META_RATE;
	check("HT bit without rate bit rejected",
	      ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status), -EINVAL);
	meta.valid_fields |= OB_RX_SW_META_RATE;
	meta.rate_index = 3;
	meta.rate_bw40 = 0;
	meta.rate_sgi = 0;
	meta.valid_fields &= ~OB_RX_SW_META_RATE_HT;
	check("legacy metadata restored",
	      ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status), 0);

	meta.declared_len = sizeof(addr4);
	ret = ob_rx_sw_validate(addr4, sizeof(addr4), &meta, &status);
	check("complete four-address data frame", ret, 0);
	meta.declared_len = sizeof(qos_htc);
	ret = ob_rx_sw_validate(qos_htc, sizeof(qos_htc), &meta, &status);
	check("complete QoS plus HTC frame", ret, 0);
	meta.declared_len = sizeof(cts);
	ret = ob_rx_sw_validate(cts, sizeof(cts), &meta, &status);
	check("complete CTS frame", ret, 0);
	meta.fcs_len = 4;
	meta.declared_len = sizeof(mgmt_with_fcs);
	ret = ob_rx_sw_validate(mgmt_with_fcs, sizeof(mgmt_with_fcs), &meta,
				&status);
	check("explicit FCS-present frame", ret, 0);
	check_u("FCS length retained", status.fcs_len, 4);
	meta.fcs_len = 0;

	/* Trusted-field mask controls exactly what reaches the sink. */
	meta.declared_len = sizeof(mgmt);
	meta.valid_fields = OB_RX_SW_META_CHANNEL | OB_RX_SW_META_FCS_LEN;
	meta.rate_index = 0xffff;	/* untrusted value must not be copied or range-checked */
	meta.signal_dbm = 0x7fffffff;
	ret = ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status);
	check("untrusted candidate values ignored", ret, 0);
	check_u("only trusted field retained", status.valid_fields,
		OB_RX_SW_META_CHANNEL | OB_RX_SW_META_FCS_LEN);
	check_u("untrusted rate zeroed", status.rate_index, 0);
	check("untrusted signal zeroed", status.signal_dbm, 0);

	/* Invalid metadata and malformed/truncated frames never reach sink. */
	reset_sink(&fake, 0);
	meta.provenance = OB_RX_SW_PROVENANCE_UNKNOWN;
	check("unknown provenance rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake),
	      -ENODATA);
	check_u("unknown provenance has no handoff", fake.calls, 0);
	meta.provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	meta.valid_fields = 1u << 9;
	check("unknown metadata mask rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -EINVAL);
	check_u("unknown mask has no handoff", fake.calls, 0);
	meta.valid_fields = OB_RX_SW_META_CHANNEL | OB_RX_SW_META_FCS_LEN;
	meta.channel = 15;
	check("out-of-profile channel rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -ERANGE);
	check_u("bad channel has no handoff", fake.calls, 0);
	meta.channel = 6;
	meta.fcs_len = 0;
	meta.rate_index = 12;
	meta.valid_fields = OB_RX_SW_META_RATE | OB_RX_SW_META_FCS_LEN;
	check("out-of-profile rate rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -ERANGE);
	meta.valid_fields = OB_RX_SW_META_SIGNAL | OB_RX_SW_META_FCS_LEN;
	meta.signal_dbm = 128;
	check("signal outside s8 API range rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -ERANGE);
	check_u("metadata failures have no handoff", fake.calls, 0);
	meta.valid_fields = OB_RX_SW_META_CHANNEL;
	meta.channel = 6;
	meta.declared_len = sizeof(mgmt);
	check("unknown FCS status rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -ENODATA);
	meta.valid_fields = OB_RX_SW_META_FCS_LEN;
	meta.fcs_len = 2;
	check("invalid FCS length rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -EINVAL);

	meta.valid_fields = OB_RX_SW_META_FCS_LEN;
	meta.fcs_len = 0;
	meta.signal_dbm = 0;
	meta.declared_len = sizeof(mgmt) - 1;
	check("declared length mismatch rejected",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake),
	      -EMSGSIZE);
	meta.declared_len = sizeof(short_mgmt);
	check("truncated management frame rejected",
	      ob_rx_sw_deliver(short_mgmt, sizeof(short_mgmt), &meta, sink, &fake),
	      -EMSGSIZE);
	meta.declared_len = sizeof(bad_version);
	check("unsupported frame-control version rejected",
	      ob_rx_sw_deliver(bad_version, sizeof(bad_version), &meta, sink, &fake),
	      -EPROTONOSUPPORT);
	meta.declared_len = sizeof(unsupported_ctrl);
	check("unvalidated control subtype rejected",
	      ob_rx_sw_deliver(unsupported_ctrl, sizeof(unsupported_ctrl), &meta,
			       sink, &fake), -EOPNOTSUPP);
	meta.declared_len = sizeof(extension);
	check("extension format rejected",
	      ob_rx_sw_deliver(extension, sizeof(extension), &meta, sink, &fake),
	      -EOPNOTSUPP);
	check_u("malformed frames have no handoff", fake.calls, 0);
	check("null sink rejected", ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta,
						      NULL, &fake), -EINVAL);

	/* Sink failures propagate; the sink is called exactly once. */
	meta.declared_len = sizeof(mgmt);
	reset_sink(&fake, -EIO);
	check("sink error propagated",
	      ob_rx_sw_deliver(mgmt, sizeof(mgmt), &meta, sink, &fake), -EIO);
	check_u("failed sink invoked once", fake.calls, 1);

	/* Validation clears output on failure rather than leaking stale status. */
	memset(&status, 0xa5, sizeof(status));
	meta.provenance = OB_RX_SW_PROVENANCE_UNKNOWN;
	check("invalid status decode rejected",
	      ob_rx_sw_validate(mgmt, sizeof(mgmt), &meta, &status), -ENODATA);
	check_u("failure clears normalized validity mask", status.valid_fields, 0);

	if (failures) {
		printf("ob_rx_sw tests: %u failures\n", failures);
		return 1;
	}
	puts("ob_rx_sw tests: PASS");
	return 0;
}
