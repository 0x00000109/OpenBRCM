#!/usr/bin/env bash
#
# OpenBRCM deterministic isolated-mode runtime test harness.
#
#   sudo scripts/runtime-test.sh --mode <mode> [--candidate <commit>] [module.ko]
#        scripts/runtime-test.sh --mode <mode> --dry-run [module.ko]
#        scripts/runtime-test.sh --evaluate-log <mode> <file> [--marker <m>]
#        scripts/runtime-test.sh --help
#
# Exactly one isolated mode is required. The module is always loaded as
#
#     insmod "$KO" "${MODE}=1"
#
# A bare `insmod "$KO"` (full normal bring-up) is never issued.
#
# Safety contract:
#   * refuses to load if openbrcm is already loaded (no auto-rmmod of an
#     unknown previous instance);
#   * verifies --candidate against `git rev-parse HEAD` before any hardware;
#   * --dry-run performs zero modprobe/insmod/rmmod/MMIO/bind and only prints
#     the commands that would run;
#   * --evaluate-log performs zero hardware access and only classifies an
#     existing capture (used by host tests);
#   * after the run, on any fault signature, leaves the module loaded and
#     prints REBOOT REQUIRED (never rmmod after a suspected device loss);
#   * automatic rmmod happens only when a deterministic, mode-specific
#     successful teardown marker is found in the current-run log.
#
# This script is not an opencode hook and never reboots automatically.
#
set -u
set -o pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ALLOWED_MODES="fw_validate_only ucode_test_only initvals_test_only dma_test_only d11_tail_test_only sprom_evidence_only bsinitvals_test_only radio_id_probe_only"

# Fault signatures: if any appears in the CURRENT-RUN log, do not rmmod.
FAULT_RE='FATAL|DEVICE LOST|dev_lost|radio-probe: FAIL|reboot[ -]required|RESET TIMEOUT|quiesce NOT verified|NOT verified|BUG:|Oops|WARNING:|Call Trace|DMA-API|AER|lockup|hung task|sync flood|machine check'

say() { printf '\n=== %s ===\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
Usage: runtime-test.sh --mode <mode> [--candidate <commit>] [--dry-run] [module.ko]
       runtime-test.sh --evaluate-log <mode> <file> [--marker <marker>]

Isolated OpenBRCM runtime test. Loads the module with exactly one isolated
mode parameter: insmod "$KO" "<mode>=1".

Modes (exactly one required):
  fw_validate_only       validate rev42 firmware images; hardware untouched
  ucode_test_only        ucode upload + PSM start
  initvals_test_only     common initvals
  dma_test_only          isolated DMA lifecycle
  d11_tail_test_only     vendor post-common / pre-PHY D11 tail
  sprom_evidence_only    read-only external-SPROM capture
  bsinitvals_test_only   band init + d11ac1bsinitvals42
  radio_id_probe_only    isolated BCM2069 radio id/revision probe (radio reg 0/1)

Options:
  --mode <mode>          isolated mode (required for a real/dry run)
  --candidate <commit>   require git HEAD to equal this commit
  --dry-run              print the commands; perform no hardware action
  --evaluate-log <mode> <file>
                         classify an existing capture (no hardware); with
                         --marker, isolate the current run first
  --marker <marker>      current-run marker for --evaluate-log isolation
  --help, -h             show this help

Arguments:
  [module.ko]            module path (default: <repo>/openbrcm.ko)

Exit status:
  0  verified success (and, when applicable, verified teardown + rmmod)
  2  usage / candidate / precondition error (no hardware action)
  3  fault detected in the current-run log -> REBOOT REQUIRED, module left loaded
  4  run finished without a deterministic successful teardown marker

Examples:
  sudo scripts/runtime-test.sh --mode radio_id_probe_only --candidate 185408d ./openbrcm.ko
  scripts/runtime-test.sh --mode radio_id_probe_only --dry-run
  scripts/runtime-test.sh --evaluate-log radio_id_probe_only /tmp/dmesg.txt
EOF
}

match_mode() {
	local m="$1" x
	for x in $ALLOWED_MODES; do [ "$x" = "$m" ] && return 0; done
	return 1
}

success_marker() {
	case "$1" in
	fw_validate_only) echo "fw all rev42 firmware validated; hardware untouched" ;;
	ucode_test_only) echo "ucode-test: PASS - stopped before initvals/PHY/radio/DMA" ;;
	initvals_test_only) echo "initvals-test: PASS - stopped before bsinitvals/PHY/radio/channel/DMA" ;;
	dma_test_only) echo "dma-test: PASS - bring-up + teardown proven" ;;
	d11_tail_test_only) echo "d3a1-test: PASS - vendor-ordered D3A1 tail + DMA lifecycle" ;;
	sprom_evidence_only) echo "sprom-evidence: read-only external-SPROM capture (no bring-up)" ;;
	bsinitvals_test_only) echo "d3b-test: PASS - D3B band-init + bsinitvals42 (DMA live through D3B)" ;;
	# radio_id_probe_only requires ALL markers (see radio_markers_complete);
	# the final marker is reported here for symmetry.
	radio_id_probe_only) echo "radio-probe: STOPPED BEFORE PLL/RADIO INIT" ;;
	*) echo "" ;;
	esac
}

owns_dma() {
	case "$1" in
	dma_test_only | d11_tail_test_only | bsinitvals_test_only) return 0 ;;
	*) return 1 ;;
	esac
}

# All deterministic success markers required for radio_id_probe_only. The
# strings are exactly those emitted by src/ob_radio.c. "decoded result valid"
# means the decoded fields are present; the deep validity check is performed
# by scripts/decode_radio_probe.py (which never influences hardware).
radio_markers_complete() {
	local f="$1" m
	for m in \
		"radio-probe: BEGIN" \
		"radio-probe: pre_access=" \
		"radio-probe: reg0_raw=" \
		"radio-probe: reg1_raw=" \
		"radio-probe: radiorev=" \
		"radio-probe: revision_class=" \
		"radio-probe: pll_branch=" \
		"radio-probe: post_access=" \
		"radio-probe: PASS" \
		"radio-probe: STOPPED BEFORE PLL/RADIO INIT"; do
		grep -qF -- "$m" "$f" || return 1
	done
	# a valid accessibility sentinel is never all-ones on a live run
	grep -qE 'pre_access=0xffffffff|post_access=0xffffffff' "$f" && return 1
	return 0
}

# Isolate the current run from a full capture using the marker. Prints the
# slice to stdout. If the marker is absent, the caller decides the fallback.
isolate_run() {
	local full="$1" marker="$2"
	if grep -qF -- "$marker" "$full" 2>/dev/null; then
		awk -v m="$marker" 'index($0,m){f=1} f' "$full"
	else
		return 1
	fi
}

# Classify a current-run log. Returns:
#   0  deterministic clean success for the mode
#   3  fault signature present -> REBOOT REQUIRED, no rmmod
#   4  no deterministic success marker
classify() {
	local mode="$1" runlog="$2" sm

	if grep -qiE "$FAULT_RE" "$runlog"; then
		return 3
	fi

	if [ "$mode" = "radio_id_probe_only" ]; then
		radio_markers_complete "$runlog" || return 4
		return 0
	fi

	sm="$(success_marker "$mode")"
	{ [ -n "$sm" ] && grep -qF -- "$sm" "$runlog"; } || return 4
	if owns_dma "$mode"; then
		grep -qF -- "all DMA engines stopped" "$runlog" || return 4
		grep -qF -- "rings released" "$runlog" || return 4
	fi
	return 0
}

# ---------------------------------------------------------------- parse args
MODE=""
CANDIDATE=""
DRY_RUN=0
KO=""
EVAL_LOG=""
MARKER=""

while [ $# -gt 0 ]; do
	case "$1" in
	--mode)
		[ $# -ge 2 ] || die "--mode requires an argument"
		MODE="$2"
		shift 2
		;;
	--mode=*)
		MODE="${1#*=}"
		shift
		;;
	--candidate)
		[ $# -ge 2 ] || die "--candidate requires an argument"
		CANDIDATE="$2"
		shift 2
		;;
	--candidate=*)
		CANDIDATE="${1#*=}"
		shift
		;;
	--evaluate-log)
		[ $# -ge 3 ] || die "--evaluate-log requires <mode> <file>"
		MODE="$2"
		EVAL_LOG="$3"
		shift 3
		;;
	--evaluate-log=*)
		[ $# -ge 2 ] || die "--evaluate-log requires <mode> <file>"
		MODE="${1#*=}"
		EVAL_LOG="$2"
		shift 2
		;;
	--marker)
		[ $# -ge 2 ] || die "--marker requires an argument"
		MARKER="$2"
		shift 2
		;;
	--marker=*)
		MARKER="${1#*=}"
		shift
		;;
	--dry-run)
		DRY_RUN=1
		shift
		;;
	--help | -h)
		usage
		exit 0
		;;
	--)
		shift
		break
		;;
	-*)
		die "unknown option: $1 (see --help)"
		;;
	*)
		[ -z "$KO" ] || die "unexpected extra argument: $1"
		KO="$1"
		shift
		;;
	esac
done

[ -n "$MODE" ] || die "exactly one --mode is required (see --help)"
match_mode "$MODE" || die "invalid --mode: $MODE (see --help)"

# ------------------------------------------------- evaluate an existing log
# Deterministic, no root, no hardware. Used by host tests and offline review.
if [ -n "$EVAL_LOG" ]; then
	[ -f "$EVAL_LOG" ] || die "log not found: $EVAL_LOG"
	EVALRUN="$(mktemp)"
	if [ -n "$MARKER" ]; then
		isolate_run "$EVAL_LOG" "$MARKER" >"$EVALRUN" ||
			{ rm -f "$EVALRUN"; die "marker not found in $EVAL_LOG: $MARKER"; }
	else
		cp "$EVAL_LOG" "$EVALRUN"
	fi
	say "evaluate-log ($MODE)"
	classify "$MODE" "$EVALRUN"
	RC=$?
	rm -f "$EVALRUN"
	case "$RC" in
	0) echo "verdict: CLEAN PASS (rmmod permitted)" ;;
	3) echo "verdict: FAULT -> REBOOT REQUIRED (no rmmod)" ;;
	*) echo "verdict: INCOMPLETE (no rmmod)" ;;
	esac
	exit "$RC"
fi

KO="${KO:-$REPO/openbrcm.ko}"

# ------------------------------------------------- candidate / HEAD check
if [ -n "$CANDIDATE" ]; then
	HEAD_REV="$(git -C "$REPO" rev-parse HEAD 2>/dev/null)" ||
		die "git rev-parse HEAD failed in $REPO"
	WANT_REV="$(git -C "$REPO" rev-parse --verify "${CANDIDATE}^{commit}" 2>/dev/null)" ||
		die "cannot resolve --candidate: $CANDIDATE"
	[ "$WANT_REV" = "$HEAD_REV" ] ||
		die "candidate mismatch: --candidate=$CANDIDATE ($WANT_REV) != HEAD ($HEAD_REV)"
fi

# ---------------------------------------------------------------- dry run
if [ "$DRY_RUN" -eq 1 ]; then
	say "DRY-RUN (no modprobe/insmod/rmmod/MMIO/bind will run)"
	echo "mode:      $MODE"
	echo "candidate: ${CANDIDATE:-<none>}"
	echo "module:    $KO"
	echo "root:      $([ "$(id -u)" -eq 0 ] && echo yes || echo no) (root not required for --dry-run)"
	[ -d /sys/module/openbrcm ] && echo "note:      openbrcm is currently loaded (a real run would refuse)"
	echo
	echo "# commands that would execute:"
	echo "modprobe cfg80211"
	echo "modprobe mac80211"
	echo "modprobe bcma"
	echo "insmod \"$KO\" \"${MODE}=1\""
	if owns_dma "$MODE"; then
		echo "# rmmod openbrcm   (only after verified '$MODE' teardown)"
	else
		echo "# rmmod openbrcm   (only after verified '$MODE' success)"
	fi
	exit 0
fi

# ---------------------------------------------------------------- real run
[ "$(id -u)" -eq 0 ] || die "must run as root (sudo $0 --mode $MODE)"
[ -f "$KO" ] || die "module not found: $KO"

if [ -d /sys/module/openbrcm ]; then
	die "openbrcm is already loaded; refusing to touch an unknown previous instance (reboot or rmmod manually)"
fi

STAMP="openbrcm-test-$(date +%Y%m%d-%H%M%S)-$$"
MARKER="[openbrcm-test] marker $STAMP"
LOGFILE="/tmp/openbrcm-runtime-$STAMP.log"
RUNLOG="/tmp/openbrcm-runtime-$STAMP.run.log"
JOURNAL="/tmp/openbrcm-journal-$STAMP.log"
RADIOLOG="/tmp/openbrcm-radio-$STAMP.log"
DECODED_JSON="/tmp/openbrcm-radio-$STAMP.json"

print_captures() {
	say "captures"
	echo "  full dmesg:          $LOGFILE"
	echo "  kernel journal:      $JOURNAL"
	echo "  current-run log:     $RUNLOG"
	echo "  focused radio log:   $RADIOLOG"
	echo "  decoded JSON:        ${DECODED_JSON} $([ -f "$DECODED_JSON" ] && echo '(written)' || echo '(none)')"
}

say "target module"
echo "mode: $MODE"
echo "ko:   $KO"

# --- dependencies (load/verify before insmod) -----------------------------
say "dependencies"
for m in cfg80211 mac80211 bcma; do
	if [ -d "/sys/module/$m" ]; then
		echo "  $m: already loaded"
		continue
	fi
	echo "  modprobe $m"
	if ! modprobe "$m"; then
		die "modprobe $m failed"
	fi
done

# --- marker --------------------------------------------------------------
# The kernel log is NOT cleared; the marker delimits this run so historical
# messages cannot be misclassified.
say "dmesg marker"
if [ -w /dev/kmsg ]; then
	printf '%s\n' "$MARKER" >/dev/kmsg 2>/dev/null || logger -t openbrcm-test "$MARKER"
else
	logger -t openbrcm-test "$MARKER" 2>/dev/null || echo "  (no /dev/kmsg or logger; marker not written)"
fi
echo "  $MARKER"

# --- load (never bare insmod) --------------------------------------------
say "insmod"
echo "  insmod \"$KO\" \"${MODE}=1\""
insmod "$KO" "${MODE}=1"
RC=$?
echo "  insmod rc=$RC"
sleep 1

# --- capture and isolate the current run ---------------------------------
dmesg >"$LOGFILE" 2>/dev/null || true
: >"$JOURNAL"
if command -v journalctl >/dev/null 2>&1; then
	journalctl -k -n 4000 --no-pager >"$JOURNAL" 2>/dev/null || true
fi
if isolate_run "$LOGFILE" "$MARKER" >"$RUNLOG"; then
	:
else
	echo "WARN: marker not found in dmesg; using last 1000 lines" >&2
	tail -n 1000 "$LOGFILE" >"$RUNLOG"
fi
grep -aF -- "radio-probe:" "$RUNLOG" >"$RADIOLOG" 2>/dev/null || true

say "current-run log (from marker; full lines)"
grep -iE 'openbrcm|ob_|dma-test|d3a1-test|d3b-test|ucode-test|initvals-test|radio-probe|fw|sprom|PSM|call trace|BUG|Oops|WARNING|RIP:' "$RUNLOG" | tail -n 250

# --- read-only diagnostics (never affects the verdict) -------------------
say "BCMA binding"
for d in /sys/bus/bcma/devices/*; do
	[ -e "$d" ] || continue
	drv="$(basename "$(readlink -f "$d/driver" 2>/dev/null)" 2>/dev/null)"
	printf '  %s -> driver=%s\n' "$(basename "$d")" "${drv:-<none>}"
done

# --- deterministic verdict -----------------------------------------------
classify "$MODE" "$RUNLOG"
VERDICT=$?

if [ "$VERDICT" -eq 3 ]; then
	say "FAILURE SIGNATURE DETECTED in current run"
	grep -iE "$FAULT_RE" "$RUNLOG" | tail -n 40
	echo
	echo "REBOOT REQUIRED"
	echo "openbrcm left loaded; rmmod NOT attempted (device may be lost)."
	echo "do not unbind, do not retry; capture logs and reboot."
	print_captures
	exit 3
fi

if [ "$VERDICT" -ne 0 ] || [ "$RC" -ne 0 ]; then
	say "no deterministic successful teardown for '$MODE'"
	SM="$(success_marker "$MODE")"
	echo "  insmod rc=$RC; success marker: ${SM:-<none>}"
	echo "openbrcm left loaded; rmmod NOT attempted."
	print_captures
	exit 4
fi

# --- radio_id_probe_only: offline decode (never influences hardware) -----
DECODE_OK=""
if [ "$MODE" = "radio_id_probe_only" ]; then
	DECODER="$REPO/scripts/decode_radio_probe.py"
	say "offline decode"
	if [ -f "$DECODER" ] && command -v python3 >/dev/null 2>&1; then
		if python3 "$DECODER" --log "$RADIOLOG" --json "$DECODED_JSON"; then
			DECODE_OK=1
			python3 - "$DECODED_JSON" <<'PY' || true
import json, sys
try:
    d = json.load(open(sys.argv[1]))
    i = d.get("identity", {})
    print("  radioid        : %s" % i.get("radio_id"))
    print("  radiorev       : %s" % i.get("radio_rev"))
    print("  revision_class : %s" % i.get("revision_class"))
    print("  pll_branch     : %s" % i.get("pll_branch"))
except Exception as exc:
    print("  (could not pretty-print: %s)" % exc)
PY
		else
			DECODE_OK=0
			echo "CAPTURE CLEAN / DECODE FAILED"
			echo "hardware sequence completed cleanly, but the PLL blocker is NOT proven."
		fi
	else
		DECODE_OK=0
		echo "CAPTURE CLEAN / DECODE FAILED (decoder or python3 unavailable)"
	fi
fi

say "verified success; unloading openbrcm"
if rmmod openbrcm; then
	echo "  rmmod openbrcm: ok"
else
	echo "  rmmod openbrcm: FAILED; leaving loaded"
	print_captures
	exit 4
fi

print_captures
if [ "$MODE" = "radio_id_probe_only" ] && [ "$DECODE_OK" != "1" ]; then
	say "done ($STAMP) CAPTURE CLEAN / DECODE FAILED"
	exit 4
fi
say "done ($STAMP) PASS"
exit 0
