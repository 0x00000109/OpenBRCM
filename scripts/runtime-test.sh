#!/usr/bin/env bash
#
# OpenBRCM deterministic isolated-mode runtime test harness.
#
#   sudo scripts/runtime-test.sh --mode <mode> [--candidate <commit>] [module.ko]
#        scripts/runtime-test.sh --mode <mode> --dry-run [module.ko]
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

ALLOWED_MODES="fw_validate_only ucode_test_only initvals_test_only dma_test_only d11_tail_test_only sprom_evidence_only bsinitvals_test_only"

# Fault signatures: if any appears in the CURRENT-RUN log, do not rmmod.
FAULT_RE='FATAL|DEVICE LOST|dev_lost|reboot[ -]required|RESET TIMEOUT|quiesce NOT verified|NOT verified|BUG:|Oops|WARNING:|Call Trace|DMA-API|AER|lockup|hung task|sync flood|machine check'

say() { printf '\n=== %s ===\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
Usage: runtime-test.sh --mode <mode> [--candidate <commit>] [--dry-run] [module.ko]

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

Options:
  --mode <mode>          isolated mode (required)
  --candidate <commit>   require git HEAD to equal this commit
  --dry-run              print the commands; perform no hardware action
  --help, -h             show this help

Arguments:
  [module.ko]            module path (default: <repo>/openbrcm.ko)

Exit status:
  0  verified success (and, when applicable, verified teardown + rmmod)
  2  usage / candidate / precondition error (no hardware action)
  3  fault detected in the current-run log -> REBOOT REQUIRED, module left loaded
  4  run finished without a deterministic successful teardown marker

Examples:
  sudo scripts/runtime-test.sh --mode bsinitvals_test_only --candidate ffa2a548
  scripts/runtime-test.sh --mode dma_test_only --dry-run
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
	*) echo "" ;;
	esac
}

owns_dma() {
	case "$1" in
	dma_test_only | d11_tail_test_only | bsinitvals_test_only) return 0 ;;
	*) return 1 ;;
	esac
}

# ---------------------------------------------------------------- parse args
MODE=""
CANDIDATE=""
DRY_RUN=0
KO=""

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
if grep -qF -- "$MARKER" "$LOGFILE"; then
	awk -v m="$MARKER" 'index($0,m){f=1} f' "$LOGFILE" >"$RUNLOG"
else
	echo "WARN: marker not found in dmesg; using last 1000 lines" >&2
	tail -n 1000 "$LOGFILE" >"$RUNLOG"
fi

say "current-run log (from marker; full lines)"
grep -iE 'openbrcm|ob_|dma-test|d3a1-test|d3b-test|ucode-test|initvals-test|fw|sprom|PSM|call trace|BUG|Oops|WARNING|RIP:' "$RUNLOG" | tail -n 250

# --- read-only diagnostics (never affects the verdict) -------------------
say "BCMA binding"
for d in /sys/bus/bcma/devices/*; do
	[ -e "$d" ] || continue
	drv="$(basename "$(readlink -f "$d/driver" 2>/dev/null)" 2>/dev/null)"
	printf '  %s -> driver=%s\n' "$(basename "$d")" "${drv:-<none>}"
done

# --- fault detection (do not rmmod on fault) -----------------------------
if grep -qiE "$FAULT_RE" "$RUNLOG"; then
	say "FAILURE SIGNATURE DETECTED in current run"
	grep -iE "$FAULT_RE" "$RUNLOG" | tail -n 40
	echo
	echo "REBOOT REQUIRED"
	echo "openbrcm left loaded; rmmod NOT attempted (device may be lost)."
	exit 3
fi

# --- deterministic teardown / success ------------------------------------
SM="$(success_marker "$MODE")"
ok=1
[ "$RC" -eq 0 ] || ok=0
if [ -n "$SM" ] && grep -qF -- "$SM" "$RUNLOG"; then
	:
else
	ok=0
fi
if owns_dma "$MODE"; then
	grep -qF -- "all DMA engines stopped" "$RUNLOG" || ok=0
	grep -qF -- "rings released" "$RUNLOG" || ok=0
fi

if [ "$ok" -eq 1 ]; then
	say "verified success; unloading openbrcm"
	if rmmod openbrcm; then
		echo "  rmmod openbrcm: ok"
	else
		echo "  rmmod openbrcm: FAILED; leaving loaded"
		exit 4
	fi
	say "done ($STAMP) PASS"
	exit 0
fi

say "no deterministic successful teardown for '$MODE'"
echo "  insmod rc=$RC; success marker: ${SM:-<none>}"
echo "openbrcm left loaded; rmmod NOT attempted."
exit 4
