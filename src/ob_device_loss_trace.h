/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * openbrcm-device-loss-trace/v1 — source-side, fail-closed trace core.
 *
 * This file contains no hardware accessor.  It only records observations
 * supplied by an admitted caller.  The production sink is responsible for
 * transporting records to the kernel log; host tests use a synthetic sink.
 */
#ifndef _OB_DEVICE_LOSS_TRACE_H_
#define _OB_DEVICE_LOSS_TRACE_H_

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#endif

#define OB_DL_TRACE_SCHEMA "openbrcm-device-loss-trace/v1"
#define OB_DL_OBSERVER_SET_FINGERPRINT \
		"mid-v2-pci-vendor-link-no-bar-probe"
#define OB_DL_NO_BAR_PROBE_REASON "NO_ADMITTED_RAW_BAR_PROBE"

enum ob_dl_record_kind {
	OB_DL_RECORD_EVENT = 0,
	OB_DL_RECORD_NOTE,
	OB_DL_RECORD_TERMINAL,
};

enum ob_dl_phase {
	OB_DL_PHASE_TRACE_BEGIN = 0,
	OB_DL_PHASE_PRE,
	OB_DL_PHASE_RADIO_SELECTOR_0,
	OB_DL_PHASE_RADIO_DATA_0,
	OB_DL_PHASE_RADIO_SELECTOR_1,
	OB_DL_PHASE_RADIO_DATA_1,
	OB_DL_PHASE_MID,
	OB_DL_PHASE_SENTINEL,
	OB_DL_PHASE_POST,
	OB_DL_PHASE_STOP,
};

enum ob_dl_layer {
	OB_DL_LAYER_NONE = -1,
	OB_DL_LAYER_L0 = 0,
	OB_DL_LAYER_L1,
	OB_DL_LAYER_L2,
	OB_DL_LAYER_L3,
	OB_DL_LAYER_L4,
	OB_DL_LAYER_L5,
	OB_DL_LAYER_L6,
};

enum ob_dl_admission {
	OB_DL_ADMISSION_ADMITTED = 0,
	OB_DL_ADMISSION_NOT_ADMITTED,
	OB_DL_ADMISSION_UNAVAILABLE,
	OB_DL_ADMISSION_DISABLED,
};

enum ob_dl_side_effect {
	OB_DL_SIDE_EFFECT_PASSIVE = 0,
	OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
	OB_DL_SIDE_EFFECT_STATE_CHANGING,
	OB_DL_SIDE_EFFECT_UNKNOWN,
};

enum ob_dl_access {
	OB_DL_ACCESS_NONE = 0,
	OB_DL_ACCESS_READ,
	OB_DL_ACCESS_WRITE,
};

enum ob_dl_status {
	OB_DL_STATUS_OK = 0,
	OB_DL_STATUS_ERROR,
	OB_DL_STATUS_UNAVAILABLE,
	OB_DL_STATUS_SKIPPED,
};

struct ob_dl_metadata {
	const char *schema;
	const char *candidate_id;
	const char *source_commit;
	const char *build_id;
	const char *module_version;
	const char *architecture;
	const char *diagnostic_flags;
	const char *observer_set;
};

struct ob_dl_observer_def {
	const char *observer_id;
	const char *layer;
	enum ob_dl_layer layer_id;
	enum ob_dl_side_effect side_effect;
	enum ob_dl_admission admission;
	const char *dependency;
	bool remains_admissible_after_loss;
	bool implemented;
	bool enabled;
	const char *reason;
};

/* One observation or non-hardware note.  raw_valid and write_valid are
 * independent: a valid all-ones raw result is not the same as a missing raw
 * result, and a write has no read result. */
struct ob_dl_record {
	enum ob_dl_record_kind kind;
	unsigned int seq;
	unsigned long long timestamp_ns;
	enum ob_dl_phase phase;
	const char *id;
	enum ob_dl_layer layer;
	const char *observer_id;
	enum ob_dl_admission admission;
	enum ob_dl_side_effect side_effect;
	enum ob_dl_access access;
	const char *address_space;
	unsigned int offset;
	unsigned int width;
	bool base_valid;
	unsigned int base_offset;
	bool write_valid;
	unsigned long long write_value;
	bool raw_valid;
	unsigned long long raw_value;
	bool abnormal;
	const char *classification;
	bool trusted_source;
	bool device_lost_before;
	bool device_lost_after;
	bool first_abnormal;
	enum ob_dl_status status;
	bool access_rc_valid;
	int access_rc;
	const char *provenance;
	const char *reason;
};

struct ob_dl_trace_sink {
	int (*metadata)(void *ctx, const struct ob_dl_metadata *metadata);
	int (*observer)(void *ctx, const struct ob_dl_observer_def *observer);
	int (*record)(void *ctx, const struct ob_dl_record *record);
};

struct ob_dl_trace_clock {
	unsigned long long (*now_ns)(void *ctx);
};

struct ob_dl_trace {
	const struct ob_dl_trace_sink *sink;
	void *sink_ctx;
	const struct ob_dl_trace_clock *clock;
	void *clock_ctx;
	enum ob_dl_phase phase;
	unsigned int seq;
	unsigned int first_abnormal_seq;
	const char *first_abnormal_id;
	const char *stop_reason;
	bool active;
	bool stopped;
	bool device_lost;
	bool first_abnormal_seen;
};

/* Input contract for one admitted observation. */
struct ob_dl_observation {
	enum ob_dl_record_kind kind;
	enum ob_dl_phase phase;
	const char *id;
	enum ob_dl_layer layer;
	const char *observer_id;
	enum ob_dl_admission admission;
	enum ob_dl_side_effect side_effect;
	enum ob_dl_access access;
	const char *address_space;
	unsigned int offset;
	unsigned int width;
	bool base_valid;
	unsigned int base_offset;
	bool write_valid;
	unsigned long long write_value;
	bool raw_valid;
	unsigned long long raw_value;
	bool abnormal;
	const char *classification;
	bool trusted_source;
	bool trusted_loss;
	enum ob_dl_status status;
	bool access_rc_valid;
	int access_rc;
	const char *provenance;
	const char *reason;
};

const char *ob_dl_record_kind_name(enum ob_dl_record_kind kind);
const char *ob_dl_phase_name(enum ob_dl_phase phase);
const char *ob_dl_layer_name(enum ob_dl_layer layer);
const char *ob_dl_admission_name(enum ob_dl_admission admission);
const char *ob_dl_side_effect_name(enum ob_dl_side_effect side_effect);
const char *ob_dl_access_name(enum ob_dl_access access);
const char *ob_dl_status_name(enum ob_dl_status status);

const struct ob_dl_observer_def *ob_dl_default_observers(unsigned int *count);

int ob_dl_trace_begin(struct ob_dl_trace *trace,
			      const struct ob_dl_trace_sink *sink, void *sink_ctx,
			      const struct ob_dl_trace_clock *clock, void *clock_ctx,
			      const struct ob_dl_metadata *metadata);
int ob_dl_trace_transition(struct ob_dl_trace *trace, enum ob_dl_phase next);
int ob_dl_trace_record(struct ob_dl_trace *trace,
			       const struct ob_dl_observation *observation);
int ob_dl_trace_note(struct ob_dl_trace *trace, enum ob_dl_phase phase,
			     const char *id, const char *provenance,
			     const char *reason);
int ob_dl_trace_unavailable(struct ob_dl_trace *trace, enum ob_dl_phase phase,
				    const char *id, enum ob_dl_layer layer,
				    const char *observer_id, const char *reason);
int ob_dl_trace_stop(struct ob_dl_trace *trace, const char *reason);
bool ob_dl_trace_is_active(const struct ob_dl_trace *trace);
bool ob_dl_trace_is_lost(const struct ob_dl_trace *trace);

#endif /* _OB_DEVICE_LOSS_TRACE_H_ */
