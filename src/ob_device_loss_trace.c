// SPDX-License-Identifier: GPL-2.0-only
/* openbrcm-device-loss-trace/v1 — pure state machine and event producer. */
#include "ob_device_loss_trace.h"

static const struct ob_dl_observer_def ob_dl_observers[] = {
	{
		.observer_id = "l0.pci.config",
		.layer = "L0", .layer_id = OB_DL_LAYER_L0,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "host_pci_pci_read_config_dword",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "MID_HOST_CONFIG_READS_ONLY_NO_BAR_MMIO",
	},
	{
		.observer_id = "l1.pcie.link",
		.layer = "L1", .layer_id = OB_DL_LAYER_L1,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "host_pci_pcie_cap_cached_offset_plus_LNKSTA",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "MID_CACHED_PCIE_CAP_CONFIG_READ_SINGLE_SAMPLE",
	},
	{
		.observer_id = "l2.bar.raw",
		.layer = "L2", .layer_id = OB_DL_LAYER_L2,
		.side_effect = OB_DL_SIDE_EFFECT_UNKNOWN,
		.admission = OB_DL_ADMISSION_UNAVAILABLE,
		.dependency = "independent_raw_BAR_probe_address",
		.remains_admissible_after_loss = false,
		.implemented = false, .enabled = false,
		.reason = OB_DL_NO_BAR_PROBE_REASON,
	},
	{
		.observer_id = "l3.bcma.wrapper",
		.layer = "L3", .layer_id = OB_DL_LAYER_L3,
		.side_effect = OB_DL_SIDE_EFFECT_STATE_CHANGING,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "existing_AXI_BCMA_0x408_access_in_preparation",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "EXISTING_ACCESS_WRAPPED_ONLY",
	},
	{
		.observer_id = "l4.d11.functional",
		.layer = "L4", .layer_id = OB_DL_LAYER_L4,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "existing_trusted_D11_0x120_access",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "EXISTING_ACCESS_WRAPPED_ONLY",
	},
	{
		.observer_id = "l5.d11.prerequisite",
		.layer = "L5", .layer_id = OB_DL_LAYER_L5,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "existing_D11_0x1e0_access_in_preparation",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "EXISTING_ACCESS_WRAPPED_ONLY",
	},
	{
		.observer_id = "l6.radio.window",
		.layer = "L6", .layer_id = OB_DL_LAYER_L6,
		.side_effect = OB_DL_SIDE_EFFECT_STATE_CHANGING,
		.admission = OB_DL_ADMISSION_ADMITTED,
		.dependency = "existing_selector_data_transaction",
		.remains_admissible_after_loss = false,
		.implemented = true, .enabled = true,
		.reason = "EXISTING_ACCESS_WRAPPED_ONLY",
	},
};

const char *ob_dl_record_kind_name(enum ob_dl_record_kind kind)
{
	switch (kind) {
	case OB_DL_RECORD_EVENT: return "EVENT";
	case OB_DL_RECORD_NOTE: return "NOTE";
	case OB_DL_RECORD_TERMINAL: return "TERMINAL";
	default: return "UNKNOWN";
	}
}

const char *ob_dl_phase_name(enum ob_dl_phase phase)
{
	switch (phase) {
	case OB_DL_PHASE_TRACE_BEGIN: return "TRACE_BEGIN";
	case OB_DL_PHASE_PRE: return "PRE";
	case OB_DL_PHASE_RADIO_SELECTOR_0: return "RADIO_SELECTOR_0";
	case OB_DL_PHASE_RADIO_DATA_0: return "RADIO_DATA_0";
	case OB_DL_PHASE_RADIO_SELECTOR_1: return "RADIO_SELECTOR_1";
	case OB_DL_PHASE_RADIO_DATA_1: return "RADIO_DATA_1";
	case OB_DL_PHASE_MID: return "MID";
	case OB_DL_PHASE_SENTINEL: return "SENTINEL";
	case OB_DL_PHASE_POST: return "POST";
	case OB_DL_PHASE_STOP: return "STOP";
	default: return "UNKNOWN";
	}
}

const char *ob_dl_layer_name(enum ob_dl_layer layer)
{
	switch (layer) {
	case OB_DL_LAYER_L0: return "L0";
	case OB_DL_LAYER_L1: return "L1";
	case OB_DL_LAYER_L2: return "L2";
	case OB_DL_LAYER_L3: return "L3";
	case OB_DL_LAYER_L4: return "L4";
	case OB_DL_LAYER_L5: return "L5";
	case OB_DL_LAYER_L6: return "L6";
	default: return "NONE";
	}
}

const char *ob_dl_admission_name(enum ob_dl_admission admission)
{
	switch (admission) {
	case OB_DL_ADMISSION_ADMITTED: return "ADMITTED";
	case OB_DL_ADMISSION_NOT_ADMITTED: return "NOT_ADMITTED";
	case OB_DL_ADMISSION_UNAVAILABLE: return "UNAVAILABLE";
	case OB_DL_ADMISSION_DISABLED: return "DISABLED";
	default: return "UNKNOWN";
	}
}

const char *ob_dl_side_effect_name(enum ob_dl_side_effect side_effect)
{
	switch (side_effect) {
	case OB_DL_SIDE_EFFECT_PASSIVE: return "PASSIVE";
	case OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE:
		return "READ_ONLY_BUT_POTENTIALLY_STATE_SENSITIVE";
	case OB_DL_SIDE_EFFECT_STATE_CHANGING: return "STATE_CHANGING";
	case OB_DL_SIDE_EFFECT_UNKNOWN: return "UNKNOWN_SIDE_EFFECT";
	default: return "UNKNOWN_SIDE_EFFECT";
	}
}

const char *ob_dl_access_name(enum ob_dl_access access)
{
	switch (access) {
	case OB_DL_ACCESS_READ: return "READ";
	case OB_DL_ACCESS_WRITE: return "WRITE";
	default: return "NONE";
	}
}

const char *ob_dl_status_name(enum ob_dl_status status)
{
	switch (status) {
	case OB_DL_STATUS_OK: return "OK";
	case OB_DL_STATUS_ERROR: return "ERROR";
	case OB_DL_STATUS_UNAVAILABLE: return "UNAVAILABLE";
	case OB_DL_STATUS_SKIPPED: return "SKIPPED";
	default: return "UNKNOWN";
	}
}

const struct ob_dl_observer_def *ob_dl_default_observers(unsigned int *count)
{
	if (count)
		*count = (unsigned int)(sizeof(ob_dl_observers) /
					 sizeof(ob_dl_observers[0]));
	return ob_dl_observers;
}

static unsigned long long ob_dl_now(const struct ob_dl_trace *trace,
					unsigned int seq)
{
	if (trace->clock && trace->clock->now_ns)
		return trace->clock->now_ns(trace->clock_ctx);
	return (unsigned long long)seq;
}

static bool ob_dl_transition_allowed(enum ob_dl_phase current,
					     enum ob_dl_phase next)
{
	if (current == next)
		return true;
	switch (current) {
	case OB_DL_PHASE_TRACE_BEGIN:
		return next == OB_DL_PHASE_PRE;
	case OB_DL_PHASE_PRE:
		return next == OB_DL_PHASE_RADIO_SELECTOR_0;
	case OB_DL_PHASE_RADIO_SELECTOR_0:
		return next == OB_DL_PHASE_RADIO_DATA_0;
	case OB_DL_PHASE_RADIO_DATA_0:
		return next == OB_DL_PHASE_RADIO_SELECTOR_1;
	case OB_DL_PHASE_RADIO_SELECTOR_1:
		return next == OB_DL_PHASE_RADIO_DATA_1;
	case OB_DL_PHASE_RADIO_DATA_1:
		return next == OB_DL_PHASE_MID;
	case OB_DL_PHASE_MID:
		return next == OB_DL_PHASE_SENTINEL;
	case OB_DL_PHASE_SENTINEL:
		return next == OB_DL_PHASE_POST;
	case OB_DL_PHASE_POST:
		return next == OB_DL_PHASE_STOP;
	default:
		return false;
	}
}

int ob_dl_trace_begin(struct ob_dl_trace *trace,
			      const struct ob_dl_trace_sink *sink, void *sink_ctx,
			      const struct ob_dl_trace_clock *clock, void *clock_ctx,
			      const struct ob_dl_metadata *metadata)
{
	const struct ob_dl_observer_def *observers;
	unsigned int count, i;
	int ret;

	if (!trace || !sink || !metadata || !sink->metadata || !sink->observer)
		return -1;
	if (trace->active)
		return -2;
	trace->sink = sink;
	trace->sink_ctx = sink_ctx;
	trace->clock = clock;
	trace->clock_ctx = clock_ctx;
	trace->phase = OB_DL_PHASE_TRACE_BEGIN;
	trace->seq = 0;
	trace->first_abnormal_seq = 0;
	trace->first_abnormal_id = 0;
	trace->stop_reason = 0;
	trace->active = true;
	trace->stopped = false;
	trace->device_lost = false;
	trace->first_abnormal_seen = false;

	ret = sink->metadata(sink_ctx, metadata);
	if (ret)
		goto fail;
	observers = ob_dl_default_observers(&count);
	for (i = 0; i < count; i++) {
		ret = sink->observer(sink_ctx, &observers[i]);
		if (ret)
			goto fail;
	}
	return 0;

fail:
	trace->active = false;
	trace->stopped = true;
	return ret;
}

int ob_dl_trace_transition(struct ob_dl_trace *trace, enum ob_dl_phase next)
{
	if (!trace || !trace->active || trace->stopped)
		return -1;
	if (!ob_dl_transition_allowed(trace->phase, next))
		return -2;
	trace->phase = next;
	return 0;
}

static int ob_dl_validate_input(const struct ob_dl_trace *trace,
				const struct ob_dl_observation *in)
{
	if (!trace || !trace->active || trace->stopped || !in || !in->id)
		return -1;
	if (in->phase != trace->phase)
		return -2;
	if (in->kind != OB_DL_RECORD_EVENT && in->kind != OB_DL_RECORD_NOTE)
		return -3;
	if (trace->device_lost && in->kind == OB_DL_RECORD_EVENT &&
	    in->status != OB_DL_STATUS_UNAVAILABLE &&
	    in->status != OB_DL_STATUS_SKIPPED)
		return -4;
	if (in->status == OB_DL_STATUS_UNAVAILABLE ||
	    in->status == OB_DL_STATUS_SKIPPED) {
		if (in->access != OB_DL_ACCESS_NONE || in->raw_valid ||
		    in->write_valid)
			return -5;
	}
	if (in->trusted_loss &&
	    (!in->raw_valid || in->width != 32 || !in->abnormal ||
	     in->raw_value != 0xffffffffULL ||
	     in->status != OB_DL_STATUS_OK))
		return -6;
	return 0;
}

int ob_dl_trace_record(struct ob_dl_trace *trace,
			       const struct ob_dl_observation *observation)
{
	struct ob_dl_record record;
	int ret;

	ret = ob_dl_validate_input(trace, observation);
	if (ret)
		return ret;
	record.kind = observation->kind;
	record.seq = observation->kind == OB_DL_RECORD_EVENT ? ++trace->seq : 0;
	record.timestamp_ns = ob_dl_now(trace, record.seq);
	record.phase = observation->phase;
	record.id = observation->id;
	record.layer = observation->layer;
	record.observer_id = observation->observer_id;
	record.admission = observation->admission;
	record.side_effect = observation->side_effect;
	record.access = observation->access;
	record.address_space = observation->address_space;
	record.offset = observation->offset;
	record.width = observation->width;
	record.base_valid = observation->base_valid;
	record.base_offset = observation->base_offset;
	record.write_valid = observation->write_valid;
	record.write_value = observation->write_value;
	record.raw_valid = observation->raw_valid;
	record.raw_value = observation->raw_value;
	record.abnormal = observation->abnormal;
	record.classification = observation->classification;
	record.trusted_source = observation->trusted_source;
	record.device_lost_before = trace->device_lost;
	record.device_lost_after = trace->device_lost || observation->trusted_loss;
	record.first_abnormal = observation->abnormal &&
		!trace->first_abnormal_seen;
	record.status = observation->status;
	record.access_rc_valid = observation->access_rc_valid;
	record.access_rc = observation->access_rc;
	record.provenance = observation->provenance;
	record.reason = observation->reason;

	/* Emit the raw record before changing the latch state.  The record itself
	 * carries both latch values, so the sink cannot replace raw data with a
	 * DEVICE_LOST label. */
	ret = trace->sink && trace->sink->record ?
		trace->sink->record(trace->sink_ctx, &record) : -7;
	if (observation->abnormal && !trace->first_abnormal_seen) {
		trace->first_abnormal_seen = true;
		trace->first_abnormal_seq = record.seq;
		trace->first_abnormal_id = observation->id;
	}
	if (observation->trusted_loss)
		trace->device_lost = true;
	return ret;
}

int ob_dl_trace_note(struct ob_dl_trace *trace, enum ob_dl_phase phase,
			     const char *id, const char *provenance,
			     const char *reason)
{
	struct ob_dl_observation note;

	note.kind = OB_DL_RECORD_NOTE;
	note.phase = phase;
	note.id = id;
	note.layer = OB_DL_LAYER_NONE;
	note.observer_id = 0;
	note.admission = OB_DL_ADMISSION_NOT_ADMITTED;
	note.side_effect = OB_DL_SIDE_EFFECT_PASSIVE;
	note.access = OB_DL_ACCESS_NONE;
	note.address_space = "NONE";
	note.offset = 0;
	note.width = 0;
	note.base_valid = false;
	note.base_offset = 0;
	note.write_valid = false;
	note.write_value = 0;
	note.raw_valid = false;
	note.raw_value = 0;
	note.abnormal = false;
	note.classification = "NONE";
	note.trusted_source = false;
	note.trusted_loss = false;
	note.status = OB_DL_STATUS_OK;
	note.access_rc_valid = false;
	note.access_rc = 0;
	note.provenance = provenance;
	note.reason = reason;
	return ob_dl_trace_record(trace, &note);
}

int ob_dl_trace_unavailable(struct ob_dl_trace *trace, enum ob_dl_phase phase,
				    const char *id, enum ob_dl_layer layer,
				    const char *observer_id, const char *reason)
{
	struct ob_dl_observation unavailable;

	unavailable.kind = OB_DL_RECORD_EVENT;
	unavailable.phase = phase;
	unavailable.id = id;
	unavailable.layer = layer;
	unavailable.observer_id = observer_id;
	unavailable.admission = OB_DL_ADMISSION_UNAVAILABLE;
	unavailable.side_effect = OB_DL_SIDE_EFFECT_UNKNOWN;
	unavailable.access = OB_DL_ACCESS_NONE;
	unavailable.address_space = "UNAVAILABLE";
	unavailable.offset = 0;
	unavailable.width = 0;
	unavailable.base_valid = false;
	unavailable.base_offset = 0;
	unavailable.write_valid = false;
	unavailable.write_value = 0;
	unavailable.raw_valid = false;
	unavailable.raw_value = 0;
	unavailable.abnormal = false;
	unavailable.classification = "UNKNOWN";
	unavailable.trusted_source = false;
	unavailable.trusted_loss = false;
	unavailable.status = OB_DL_STATUS_UNAVAILABLE;
	unavailable.access_rc_valid = false;
	unavailable.access_rc = 0;
	unavailable.provenance = "observer-definition";
	unavailable.reason = reason;
	return ob_dl_trace_record(trace, &unavailable);
}

int ob_dl_trace_stop(struct ob_dl_trace *trace, const char *reason)
{
	struct ob_dl_record terminal;
	int ret;

	if (!trace || !trace->active || trace->stopped)
		return -1;
	trace->phase = OB_DL_PHASE_STOP;
	trace->stopped = true;
	trace->stop_reason = reason;
	terminal.kind = OB_DL_RECORD_TERMINAL;
	terminal.seq = trace->seq;
	terminal.timestamp_ns = ob_dl_now(trace, terminal.seq);
	terminal.phase = OB_DL_PHASE_STOP;
	terminal.id = "trace.stop";
	terminal.layer = OB_DL_LAYER_NONE;
	terminal.observer_id = 0;
	terminal.admission = OB_DL_ADMISSION_NOT_ADMITTED;
	terminal.side_effect = OB_DL_SIDE_EFFECT_PASSIVE;
	terminal.access = OB_DL_ACCESS_NONE;
	terminal.address_space = "NONE";
	terminal.offset = 0;
	terminal.width = 0;
	terminal.base_valid = false;
	terminal.base_offset = 0;
	terminal.write_valid = false;
	terminal.write_value = 0;
	terminal.raw_valid = false;
	terminal.raw_value = 0;
	terminal.abnormal = false;
	terminal.classification = trace->device_lost ?
		"DEVICE_LOST" : "TRACE_COMPLETE";
	terminal.trusted_source = trace->device_lost;
	terminal.device_lost_before = trace->device_lost;
	terminal.device_lost_after = trace->device_lost;
	terminal.first_abnormal = false;
	terminal.status = OB_DL_STATUS_OK;
	terminal.access_rc_valid = false;
	terminal.access_rc = 0;
	terminal.provenance = "trace-state-machine";
	terminal.reason = reason;
	ret = trace->sink && trace->sink->record ?
		trace->sink->record(trace->sink_ctx, &terminal) : -7;
	return ret;
}

bool ob_dl_trace_is_active(const struct ob_dl_trace *trace)
{
	return trace && trace->active && !trace->stopped;
}

bool ob_dl_trace_is_lost(const struct ob_dl_trace *trace)
{
	return trace && trace->device_lost;
}
