/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-only exercise of the source trace core and the existing radio order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ob_device_loss_trace.h"
#include "ob_radio.h"

#define MAX_RECORDS 96

struct capture {
	struct ob_dl_record records[MAX_RECORDS];
	unsigned int record_count;
	unsigned int metadata_count;
	unsigned int observer_count;
	unsigned int clock_tick;
};

struct fake_radio {
	u32 sentinel[2];
	u16 data[2];
	unsigned int reads32;
	unsigned int reads16;
	unsigned int writes;
};

static void check(int condition, const char *what)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", what);
		exit(1);
	}
}

static int capture_metadata(void *ctx, const struct ob_dl_metadata *metadata)
{
	struct capture *capture = ctx;

	capture->metadata_count++;
	check(strcmp(metadata->schema, OB_DL_TRACE_SCHEMA) == 0,
	      "metadata schema");
	check(strcmp(metadata->source_commit, "UNAVAILABLE") == 0,
	      "source commit is explicit unavailable");
	return 0;
}

static int capture_observer(void *ctx,
				   const struct ob_dl_observer_def *observer)
{
	struct capture *capture = ctx;

	capture->observer_count++;
	if (strcmp(observer->observer_id, "l2.bar.raw") == 0) {
		check(observer->admission == OB_DL_ADMISSION_UNAVAILABLE,
		      "BAR observer unavailable");
		check(!observer->implemented && !observer->enabled,
		      "BAR observer disabled without implementation");
		check(strcmp(observer->reason, OB_DL_NO_BAR_PROBE_REASON) == 0,
		      "BAR observer reason");
	}
	return 0;
}

static int capture_record(void *ctx, const struct ob_dl_record *record)
{
	struct capture *capture = ctx;

	check(capture->record_count < MAX_RECORDS, "record capacity");
	capture->records[capture->record_count++] = *record;
	return 0;
}

static unsigned long long capture_now(void *ctx)
{
	struct capture *capture = ctx;

	return 100000ULL + ++capture->clock_tick;
}

static const struct ob_dl_trace_sink capture_sink = {
	.metadata = capture_metadata,
	.observer = capture_observer,
	.record = capture_record,
};

static const struct ob_dl_trace_clock capture_clock = {
	.now_ns = capture_now,
};

static u32 fake_read32(void *ctx, u16 off)
{
	struct fake_radio *fake = ctx;

	check(off == OB_RADIO_REG_SENTINEL, "sentinel offset");
	return fake->sentinel[fake->reads32++ < 1 ? fake->reads32 - 1 : 1];
}

static u16 fake_read16(void *ctx, u16 off)
{
	struct fake_radio *fake = ctx;

	check(off == OB_RADIO_REG_DATA, "radio data offset");
	return fake->data[fake->reads16++ < 1 ? fake->reads16 - 1 : 1];
}

static void fake_write16(void *ctx, u16 off, u16 value)
{
	struct fake_radio *fake = ctx;

	check(off == OB_RADIO_REG_ADDR_LATCH, "radio selector offset");
	check(value == 0 || value == 1, "radio selector value");
	fake->writes++;
}

static const struct ob_radio_io_ops fake_io = {
	.read32_trusted = fake_read32,
	.read16 = fake_read16,
	.write16 = fake_write16,
};

static enum ob_dl_phase trace_phase_for_stage(enum ob_radio_stage stage)
{
	switch (stage) {
	case OB_RADIO_STAGE_PRE_ACCESS: return OB_DL_PHASE_PRE;
	case OB_RADIO_STAGE_SELECTOR0_WRITE: return OB_DL_PHASE_RADIO_SELECTOR_0;
	case OB_RADIO_STAGE_REG0_READ: return OB_DL_PHASE_RADIO_DATA_0;
	case OB_RADIO_STAGE_SELECTOR1_WRITE: return OB_DL_PHASE_RADIO_SELECTOR_1;
	case OB_RADIO_STAGE_REG1_READ: return OB_DL_PHASE_RADIO_DATA_1;
	case OB_RADIO_STAGE_MID: return OB_DL_PHASE_MID;
	case OB_RADIO_STAGE_POST_ACCESS: return OB_DL_PHASE_SENTINEL;
	default: return OB_DL_PHASE_TRACE_BEGIN;
	}
}

static int test_trace_phase(void *ctx, enum ob_radio_stage stage)
{
	struct ob_dl_trace *trace = ctx;
	enum ob_dl_phase phase = trace_phase_for_stage(stage);

	return ob_dl_trace_transition(trace, phase);
}

static int test_trace_mid_event(struct ob_dl_trace *trace, const char *id,
					 enum ob_dl_layer layer,
					 const char *observer_id,
					 const char *address_space, u16 off,
					 unsigned int width, bool base_valid,
					 unsigned int base_offset,
					 unsigned long long raw,
					 const char *classification)
{
	struct ob_dl_observation observation = {
		.kind = OB_DL_RECORD_EVENT,
		.phase = OB_DL_PHASE_MID,
		.id = id,
		.layer = layer,
		.observer_id = observer_id,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.access = OB_DL_ACCESS_READ,
		.address_space = address_space,
		.offset = off,
		.width = width,
		.base_valid = base_valid,
		.base_offset = base_offset,
		.write_valid = false,
		.write_value = 0,
		.raw_valid = true,
		.raw_value = raw,
		.abnormal = false,
		.classification = classification,
		.trusted_source = false,
		.trusted_loss = false,
		.status = OB_DL_STATUS_OK,
		.access_rc_valid = true,
		.access_rc = 0,
		.provenance = "synthetic-pci-observer",
		.reason = "SYNTHETIC_TEST_BACKEND",
	};

	return ob_dl_trace_record(trace, &observation);
}

static int test_trace_mid(void *ctx)
{
	struct ob_dl_trace *trace = ctx;
	int ret;

	ret = test_trace_mid_event(trace, "mid.pci.vendor_device", OB_DL_LAYER_L0,
					   "l0.pci.config", "PCI_CFG", 0x00, 32,
					   false, 0, 0x43b114e4u, "PCI_CONFIG_OK");
	if (ret)
		return ret;
	return test_trace_mid_event(trace, "mid.pcie.link_status", OB_DL_LAYER_L1,
					    "l1.pcie.link", "PCI_EXP_CAP", 0x12, 16,
					    true, 0x40, 0x2000u, "LINK_UP");
}

static int record_event(struct ob_dl_trace *trace, enum ob_dl_phase phase,
				const char *id, enum ob_dl_layer layer,
				const char *observer_id, enum ob_dl_access access,
				const char *address_space, u16 off, unsigned int width,
				bool write_valid, unsigned long long write_value,
				bool raw_valid, unsigned long long raw_value,
				bool abnormal, bool trusted_source, bool trusted_loss,
				const char *classification, const char *provenance)
{
	struct ob_dl_observation observation = {
		.kind = OB_DL_RECORD_EVENT,
		.phase = phase,
		.id = id,
		.layer = layer,
		.observer_id = observer_id,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.side_effect = access == OB_DL_ACCESS_WRITE ?
			OB_DL_SIDE_EFFECT_STATE_CHANGING :
			OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.access = access,
		.address_space = address_space,
		.offset = off,
		.width = width,
		.write_valid = write_valid,
		.write_value = write_value,
		.raw_valid = raw_valid,
		.raw_value = raw_value,
		.abnormal = abnormal,
		.classification = classification,
		.trusted_source = trusted_source,
		.trusted_loss = trusted_loss,
		.status = OB_DL_STATUS_OK,
		.provenance = provenance,
		.reason = "SYNTHETIC_TEST_BACKEND",
	};

	return ob_dl_trace_record(trace, &observation);
}

static int test_trace_read32(void *ctx, enum ob_radio_stage stage,
				     u16 off, u32 value)
{
	struct ob_dl_trace *trace = ctx;
	enum ob_dl_phase phase = trace_phase_for_stage(stage);
	const char *id = stage == OB_RADIO_STAGE_PRE_ACCESS ?
		"pre.d11.maccontrol_0120" : "sentinel.d11.maccontrol_0120";
	bool loss = value == 0xffffffffu;

	return record_event(trace, phase, id, OB_DL_LAYER_L4,
				   "l4.d11.functional", OB_DL_ACCESS_READ,
				   "D11_CORE", off, 32, false, 0, true, value,
				   loss, true, loss, loss ? "DEVICE_LOST" : "NONE",
				   "SYNTHETIC_TEST_BACKEND");
}

static int test_trace_read16(void *ctx, enum ob_radio_stage stage,
				     u16 off, u16 value)
{
	struct ob_dl_trace *trace = ctx;
	enum ob_dl_phase phase = trace_phase_for_stage(stage);
	const char *id = stage == OB_RADIO_STAGE_REG0_READ ?
		"radio.data.0" : "radio.data.1";
	bool abnormal = value == 0xffffu;

	return record_event(trace, phase, id, OB_DL_LAYER_L6,
				   "l6.radio.window", OB_DL_ACCESS_READ,
				   "RADIO_WINDOW", off, 16, false, 0, true, value,
				   abnormal, false, false,
				   abnormal ? "ABNORMAL_RAW" : "NONE",
				   "SYNTHETIC_TEST_BACKEND");
}

static int test_trace_write16(void *ctx, enum ob_radio_stage stage,
				      u16 off, u16 value)
{
	struct ob_dl_trace *trace = ctx;
	enum ob_dl_phase phase = trace_phase_for_stage(stage);
	const char *id = stage == OB_RADIO_STAGE_SELECTOR0_WRITE ?
		"radio.selector.0" : "radio.selector.1";

	return record_event(trace, phase, id, OB_DL_LAYER_L6,
				   "l6.radio.window", OB_DL_ACCESS_WRITE,
				   "RADIO_WINDOW", off, 16, true, value, false, 0,
				   false, false, false, "NONE",
				   "SYNTHETIC_TEST_BACKEND");
}

static int test_trace_stop(void *ctx, bool lost)
{
	struct ob_dl_trace *trace = ctx;

	if (!ob_dl_trace_is_active(trace))
		return 0;
	return ob_dl_trace_stop(trace, lost ? "TRUSTED_SENTINEL_DEVICE_LOST" :
					       "TRACE_STOP_FAIL_CLOSED");
}

static const struct ob_radio_trace_hooks trace_hooks = {
	.phase = test_trace_phase,
	.mid = test_trace_mid,
	.read32 = test_trace_read32,
	.read16 = test_trace_read16,
	.write16 = test_trace_write16,
	.stop = test_trace_stop,
};

static int emit_prep_records(struct ob_dl_trace *trace)
{
	int ret;

	ret = record_event(trace, OB_DL_PHASE_PRE, "prep.d11.01e0",
				  OB_DL_LAYER_L5, "l5.d11.prerequisite",
				  OB_DL_ACCESS_READ, "D11_CORE", 0x01e0, 32,
				  false, 0, true, 0x070b0042u, false, false, false,
				  "NONE", "SYNTHETIC_TEST_BACKEND");
	if (ret)
		return ret;
	ret = record_event(trace, OB_DL_PHASE_PRE, "prep.bcma.ioctl.read",
				  OB_DL_LAYER_L3, "l3.bcma.wrapper",
				  OB_DL_ACCESS_READ, "BCMA_WRAPPER", 0x0408, 32,
				  false, 0, true, 0x00000001u, false, false, false,
				  "NONE", "SYNTHETIC_TEST_BACKEND");
	if (ret)
		return ret;
	ret = record_event(trace, OB_DL_PHASE_PRE, "prep.bcma.ioctl.write",
				  OB_DL_LAYER_L3, "l3.bcma.wrapper",
				  OB_DL_ACCESS_WRITE, "BCMA_WRAPPER", 0x0408, 32,
				  true, 0x00000011u, false, 0, false, false, false,
				  "NONE", "SYNTHETIC_TEST_BACKEND");
	if (ret)
		return ret;
	return record_event(trace, OB_DL_PHASE_PRE, "prep.bcma.ioctl.readback",
				   OB_DL_LAYER_L3, "l3.bcma.wrapper",
				   OB_DL_ACCESS_READ, "BCMA_WRAPPER", 0x0408, 32,
				   false, 0, true, 0x00000011u, false, false, false,
				   "NONE", "SYNTHETIC_TEST_BACKEND");
}

static void capture_init(struct capture *capture)
{
	memset(capture, 0, sizeof(*capture));
}

static int run_trace(struct capture *capture, u32 post, u16 data0, u16 data1,
			     bool include_prep, struct fake_radio *fake_out)
{
	struct ob_dl_metadata metadata = {
		.schema = OB_DL_TRACE_SCHEMA,
		.candidate_id = "synthetic-source-candidate",
		.source_commit = "UNAVAILABLE",
		.build_id = "host-test",
		.module_version = "UNAVAILABLE",
		.architecture = "host-test",
		.diagnostic_flags = "synthetic_backend",
		.observer_set = OB_DL_OBSERVER_SET_FINGERPRINT,
	};
	struct fake_radio fake = {
		.sentinel = { 0, post },
		.data = { data0, data1 },
	};
	struct ob_radio_io_ops io = fake_io;
	struct ob_dl_trace trace = { 0 };
	bool lost = false;
	struct ob_radio_seq_result result;
	int ret;

	ret = ob_dl_trace_begin(&trace, &capture_sink, capture,
				&capture_clock, capture, &metadata);
	check(ret == 0, "trace begin");
	ret = ob_dl_trace_transition(&trace, OB_DL_PHASE_PRE);
	check(ret == 0, "PRE transition");
	if (include_prep) {
		ret = emit_prep_records(&trace);
		check(ret == 0, "preparation records");
	}
	io.trace = &trace_hooks;
	io.trace_ctx = &trace;
	ret = ob_radio_sequence(&io, &fake, &lost, &result);
	if (!lost && ret == 0) {
		ret = ob_dl_trace_transition(&trace, OB_DL_PHASE_POST);
		check(ret == 0, "POST transition");
		ret = ob_dl_trace_note(&trace, OB_DL_PHASE_POST, "post.observers",
					"observer-admission",
					"NO_POST_LOSS_HOST_OBSERVER_ADMITTED");
		check(ret == 0, "POST note");
		ret = ob_dl_trace_stop(&trace, "TRACE_COMPLETE");
		check(ret == 0, "normal stop");
	}
	if (lost)
		ret = 0;
	if (fake_out)
		*fake_out = fake;
	return ret;
}

static void test_normal_order(void)
{
	struct capture capture;
	struct fake_radio fake;
	int ret;

	capture_init(&capture);
	ret = run_trace(&capture, 0, 0x2069, 0x2069, true, &fake);
	check(ret == 0, "normal trace return");
	check(capture.metadata_count == 1, "one metadata record");
	check(capture.observer_count == 7, "all observer definitions");
	check(fake.reads32 == 2 && fake.reads16 == 2 && fake.writes == 2,
	      "synthetic backend operation counts");
	check(capture.records[4].id &&
	      strcmp(capture.records[4].id, "pre.d11.maccontrol_0120") == 0,
	      "pre sentinel follows preparation");
	check(strcmp(capture.records[5].id, "radio.selector.0") == 0,
	      "selector zero order");
	check(strcmp(capture.records[6].id, "radio.data.0") == 0,
	      "data zero order");
	check(strcmp(capture.records[7].id, "radio.selector.1") == 0,
	      "selector one order");
	check(strcmp(capture.records[8].id, "radio.data.1") == 0,
	      "data one order");
	check(strcmp(capture.records[11].id, "sentinel.d11.maccontrol_0120") == 0,
	      "final sentinel order");
	check(strcmp(capture.records[9].id, "mid.pci.vendor_device") == 0,
	      "mid PCI vendor/device order");
	check(strcmp(capture.records[10].id, "mid.pcie.link_status") == 0,
	      "mid PCIe link order");
	check(capture.records[10].base_valid && capture.records[10].base_offset == 0x40,
	      "synthetic PCIe capability base preserved");
	check(capture.records[9].access_rc_valid && capture.records[9].access_rc == 0,
	      "synthetic PCI config status preserved");
	check(capture.records[11].raw_valid && capture.records[11].raw_value == 0,
	      "normal sentinel raw value");
}

static void test_abnormal_and_loss(void)
{
	struct capture capture;
	struct fake_radio fake;
	int ret;
	unsigned int i, abnormal_count = 0;

	capture_init(&capture);
	ret = run_trace(&capture, 0, 0xffffu, 0x2069u, false, &fake);
	check(ret == 0, "radio abnormal does not latch device loss");
	check(fake.reads32 == 2 && fake.reads16 == 2 && fake.writes == 2,
	      "radio abnormal still completes one transaction");
	check(capture.records[5].id &&
	      strcmp(capture.records[5].id, "mid.pci.vendor_device") == 0,
	      "synthetic MID observer runs without production PCI accessor");
	for (i = 0; i < capture.record_count; i++) {
		if (capture.records[i].kind == OB_DL_RECORD_EVENT &&
		    capture.records[i].abnormal) {
			abnormal_count++;
			check(capture.records[i].first_abnormal,
			      "first abnormal marked");
			check(capture.records[i].raw_valid &&
			      capture.records[i].raw_value == 0xffffu,
			      "radio all-ones raw preserved");
		}
	}
	check(abnormal_count == 1, "first abnormal not overwritten");
	check(!capture.records[capture.record_count - 1].device_lost_after,
	      "radio abnormal is not device loss");

	capture_init(&capture);
	ret = run_trace(&capture, 0xffffffffu, 0x2069u, 0x2069u, false, &fake);
	check(ret == 0, "loss trace terminal callback return");
	check(fake.reads32 == 2 && fake.reads16 == 2 && fake.writes == 2,
	      "loss trace stops after final sentinel");
	for (i = 0; i < capture.record_count; i++) {
		if (capture.records[i].id &&
		    strcmp(capture.records[i].id,
			   "sentinel.d11.maccontrol_0120") == 0) {
			check(capture.records[i].raw_valid &&
			      capture.records[i].raw_value == 0xffffffffu,
			      "sentinel all-ones raw preserved");
			check(capture.records[i].first_abnormal &&
			      capture.records[i].device_lost_after,
			      "trusted loss is separate latch and first abnormal");
		}
	}
	check(capture.records[capture.record_count - 1].kind ==
	      OB_DL_RECORD_TERMINAL,
	      "loss has terminal STOP record");
	check(capture.records[capture.record_count - 1].classification &&
	      strcmp(capture.records[capture.record_count - 1].classification,
		     "DEVICE_LOST") == 0,
	      "terminal loss classification");

	capture_init(&capture);
	ret = run_trace(&capture, 0xffffffffu, 0xffffu, 0x2069u, false,
				&fake);
	check(ret == 0, "combined abnormal/loss trace return");
	for (i = 0; i < capture.record_count; i++) {
		if (capture.records[i].id &&
		    strcmp(capture.records[i].id, "radio.data.0") == 0)
			check(capture.records[i].first_abnormal,
			      "earlier abnormal remains first");
		if (capture.records[i].id &&
		    strcmp(capture.records[i].id,
			       "sentinel.d11.maccontrol_0120") == 0)
			check(!capture.records[i].first_abnormal &&
			      capture.records[i].device_lost_after,
			      "trusted loss does not overwrite first abnormal");
	}
}

static void test_invalid_transitions_and_unavailable(void)
{
	struct capture capture;
	struct ob_dl_metadata metadata = {
		.schema = OB_DL_TRACE_SCHEMA,
		.candidate_id = "synthetic",
		.source_commit = "UNAVAILABLE",
		.build_id = "synthetic",
		.module_version = "UNAVAILABLE",
		.architecture = "host-test",
		.diagnostic_flags = "synthetic_backend",
		.observer_set = OB_DL_OBSERVER_SET_FINGERPRINT,
	};
	struct ob_dl_trace trace = { 0 };
	int ret;

	capture_init(&capture);
	ret = ob_dl_trace_begin(&trace, &capture_sink, &capture,
				&capture_clock, &capture, &metadata);
	check(ret == 0, "invalid-transition trace begin");
	check(ob_dl_trace_transition(&trace, OB_DL_PHASE_PRE) == 0,
	      "invalid-transition PRE");
	check(ob_dl_trace_transition(&trace, OB_DL_PHASE_MID) != 0,
	      "invalid transition rejected");
	check(ob_dl_trace_unavailable(&trace, OB_DL_PHASE_PRE,
					     "pre.bar.mmio_probe", OB_DL_LAYER_L2,
					     "l2.bar.raw", OB_DL_NO_BAR_PROBE_REASON) == 0,
	      "unavailable observer record");
	check(capture.records[capture.record_count - 1].status ==
		      OB_DL_STATUS_UNAVAILABLE &&
	      !capture.records[capture.record_count - 1].raw_valid,
	      "unavailable has no implicit raw value");
	check(ob_dl_trace_stop(&trace, "test-stop") == 0,
	      "explicit stop");
	check(ob_dl_trace_transition(&trace, OB_DL_PHASE_PRE) != 0,
	      "transition after STOP rejected");
}

static void print_source_capture(const struct capture *capture)
{
	unsigned int i, count;
	const struct ob_dl_observer_def *observers =
		ob_dl_default_observers(&count);

	printf("openbrcm-dl:v1 kind=meta schema=%s candidate_id=synthetic-source-candidate source_commit=UNAVAILABLE build_id=host-test module_version=UNAVAILABLE architecture=host-test diagnostic_flags=synthetic_backend observer_set=%s\n",
	       OB_DL_TRACE_SCHEMA, OB_DL_OBSERVER_SET_FINGERPRINT);
	for (i = 0; i < count; i++)
		printf("openbrcm-dl:v1 kind=observer observer_id=%s layer=%s admission=%s side_effect=%s dependency=%s remains_after_loss=%d implemented=%d enabled=%d reason=%s\n",
		       observers[i].observer_id, observers[i].layer,
		       ob_dl_admission_name(observers[i].admission),
		       ob_dl_side_effect_name(observers[i].side_effect),
		       observers[i].dependency,
		       observers[i].remains_admissible_after_loss ? 1 : 0,
		       observers[i].implemented ? 1 : 0,
		       observers[i].enabled ? 1 : 0, observers[i].reason);
	for (i = 0; i < capture->record_count; i++) {
		const struct ob_dl_record *r = &capture->records[i];

		printf("openbrcm-dl:v1 kind=record record_kind=%s seq=%u timestamp_ns=%llu phase=%s id=%s layer=%s observer_id=%s admission=%s side_effect=%s access=%s address_space=%s offset=0x%04x width=%u base_valid=%d base_offset=0x%04x write_valid=%d write_value=0x%llx raw_valid=%d raw_value=0x%llx abnormal=%d classification=%s trusted_source=%d device_lost_before=%d device_lost_after=%d first_abnormal=%d status=%s access_rc_valid=%d access_rc=%d provenance=%s reason=%s\n",
		       ob_dl_record_kind_name(r->kind), r->seq, r->timestamp_ns,
		       ob_dl_phase_name(r->phase), r->id ? r->id : "UNAVAILABLE",
		       ob_dl_layer_name(r->layer),
		       r->observer_id ? r->observer_id : "NONE",
		       ob_dl_admission_name(r->admission),
		       ob_dl_side_effect_name(r->side_effect),
		       ob_dl_access_name(r->access),
		       r->address_space ? r->address_space : "UNAVAILABLE",
		       r->offset, r->width, r->base_valid ? 1 : 0,
		       r->base_offset, r->write_valid ? 1 : 0,
		       r->write_value, r->raw_valid ? 1 : 0, r->raw_value,
		       r->abnormal ? 1 : 0,
		       r->classification ? r->classification : "NONE",
		       r->trusted_source ? 1 : 0,
		       r->device_lost_before ? 1 : 0,
		       r->device_lost_after ? 1 : 0,
		       r->first_abnormal ? 1 : 0, ob_dl_status_name(r->status),
		       r->access_rc_valid ? 1 : 0, r->access_rc,
		       r->provenance ? r->provenance : "UNAVAILABLE",
		       r->reason ? r->reason : "UNAVAILABLE");
	}
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--emit-source") == 0) {
		struct capture capture;

		capture_init(&capture);
		check(run_trace(&capture, 0, 0x2069u, 0x2069u, true, NULL) == 0,
		      "source fixture run");
		print_source_capture(&capture);
		return 0;
	}
	test_normal_order();
	test_abnormal_and_loss();
	test_invalid_transitions_and_unavailable();
	puts("ob_device_loss_trace_test: PASS");
	return 0;
}
