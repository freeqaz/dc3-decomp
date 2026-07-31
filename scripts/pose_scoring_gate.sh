#!/usr/bin/env bash
# Differential gate for the native move-scoring pipeline.
#
# WHY THIS EXISTS: scoring has twice been silently degenerate — DetectFrac
# identically 0.000 (mCamBoneLengths never populated => every error node pinned
# at max) and then identically 1.000 (mNodesInverseScale never populated => the
# error kernel multiplied every difference by zero). Both states look "stable"
# to a smoke test: the game boots, runs 18000 frames, and exits 0.
#
# A single absolute number cannot catch that. This gate is DIFFERENTIAL — it
# runs the same song under three pose inputs and asserts they DISAGREE in the
# right direction:
#
#   selftest  the choreography's own reference pose fed back as the player.
#             Perfect mimicry, so this must score HIGH (~1.0).
#   dummy     a static standing skeleton (no provider). A person standing
#             still is not dancing, so this must score clearly BELOW selftest.
#   video     real pose estimation from a recorded dancer, if footage + server
#             are available. Optional; reported but not asserted, since its
#             value depends on the clip.
#
# FAIL conditions: any config scoring identically 0.000 or identically 1.000
# across every move, or dummy scoring >= selftest.
#
# Usage:
#   scripts/pose_scoring_gate.sh [--frames N] [--song FLOW] [--video CLIP]
#
# Requires a built native binary (native/build/dc3-native) and a GPU-capable
# environment; run it OUTSIDE the agent sandbox.

set -uo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

FRAMES=25000
FLOW="scripts/dc3-input-flows/betteroffalone.txt"
VIDEO=""
SOCKET="/tmp/dc3_pose_gate.sock"
POSE_MODEL="native/models/pose_landmarker_full.task"

while [ $# -gt 0 ]; do
    case "$1" in
        --frames)  FRAMES="$2";  shift 2 ;;
        --song)    FLOW="$2";    shift 2 ;;
        --video)   VIDEO="$2";   shift 2 ;;
        --model)   POSE_MODEL="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

BIN="$REPO/native/build/dc3-native"
[ -x "$BIN" ] || { echo "FAIL: $BIN not built"; exit 1; }

COMMON_ENV=(
    "DC3_DATA=$REPO/orig-assets"
    "DC3_FAST_BOOT=1"
    "MILO_HEADLESS=1"
    "MILO_RENDER=1"
    "MILO_MAX_FRAMES=$FRAMES"
    "MILO_INPUT_SCRIPT=$FLOW"
    "DC3_DETECTFRAC_PROBE=1"
    "DC3_SCORING_DEBUG=1"
)

# Echoes "<count> <min> <max> <distinct>" over the ham2-scored DetectFrac
# samples for player 0 in a run log.
summarize() {
    grep 'DC3 DETECTFRAC' "$1" 2>/dev/null \
        | grep 'branch=ham2' | grep 'p=0' \
        | sed 's/.*frac=//' \
        | awk 'NR==1{lo=hi=$1} {n++; s[$1]=1; if($1<lo)lo=$1; if($1>hi)hi=$1}
               END{d=0; for(k in s)d++; printf "%d %s %s %d\n", n, (n?lo:"-"), (n?hi:"-"), d}'
}

run_cfg() {
    local name="$1"; shift
    local log="/tmp/pose_gate_${name}.log"
    ( env "${COMMON_ENV[@]}" "$@" "$BIN" >"$log" 2>&1 )
    local exit_code=$?
    local segv
    segv=$(grep -c 'Caught SIGSEGV' "$log")
    echo "$exit_code $segv $(summarize "$log")"
}

echo "=== native move-scoring differential gate (${FRAMES} frames, $(basename "$FLOW"), model: $(basename "$POSE_MODEL")) ==="

read -r ST_EXIT ST_SEGV ST_N ST_LO ST_HI ST_D <<<"$(run_cfg selftest DC3_POSE_SELFTEST=1)"
echo "selftest : exit=$ST_EXIT segv=$ST_SEGV samples=$ST_N range=[$ST_LO..$ST_HI] distinct=$ST_D"

read -r DU_EXIT DU_SEGV DU_N DU_LO DU_HI DU_D <<<"$(run_cfg dummy)"
echo "dummy    : exit=$DU_EXIT segv=$DU_SEGV samples=$DU_N range=[$DU_LO..$DU_HI] distinct=$DU_D"

VI_N=0
if [ -n "$VIDEO" ] && [ -f "$VIDEO" ]; then
    rm -f "$SOCKET"
    .venv/bin/python native/scripts/pose_server.py --video "$VIDEO" --loop --fps 15 \
        --socket "$SOCKET" --model "$POSE_MODEL" \
        >/tmp/pose_gate_server.log 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 60); do [ -S "$SOCKET" ] && break; sleep 1; done
    read -r VI_EXIT VI_SEGV VI_N VI_LO VI_HI VI_D <<<"$(run_cfg video \
        DC3_POSE=external DC3_POSE_NO_SPAWN=1 "DC3_POSE_SOCKET=$SOCKET")"
    echo "video    : exit=$VI_EXIT segv=$VI_SEGV samples=$VI_N range=[$VI_LO..$VI_HI] distinct=$VI_D"
    kill "$SERVER_PID" 2>/dev/null
fi

FAIL=0
note() { echo "FAIL: $1"; FAIL=1; }

[ "$ST_EXIT" = "0" ] || note "selftest exited $ST_EXIT"
[ "$DU_EXIT" = "0" ] || note "dummy exited $DU_EXIT"
[ "$ST_SEGV" = "0" ] || note "selftest crashed"
[ "$DU_SEGV" = "0" ] || note "dummy crashed"
[ "${ST_N:-0}" -gt 0 ] || note "selftest produced no ham2 DetectFrac samples (pipeline not reached)"
[ "${DU_N:-0}" -gt 0 ] || note "dummy produced no ham2 DetectFrac samples (pipeline not reached)"

# The degeneracy checks: a config whose score never varies AND sits at an
# extreme is the signature of a broken kernel, not of a consistent player.
if [ "${DU_D:-0}" = "1" ] && { [ "$DU_LO" = "0.0000" ] || [ "$DU_LO" = "1.0000" ]; }; then
    note "dummy DetectFrac is degenerate (always $DU_LO) — scoring kernel is broken, not 'stable'"
fi
if [ "${ST_D:-0}" = "1" ] && [ "$ST_LO" = "0.0000" ]; then
    note "selftest scores 0 — perfect mimicry must not score zero"
fi

# Direction: standing still must not beat dancing the routine perfectly.
awk -v st="${ST_HI:-0}" -v du="${DU_HI:-0}" 'BEGIN{exit !(du+0 >= st+0)}' \
    && note "dummy (${DU_HI}) scores >= selftest (${ST_HI}) — scoring cannot tell dancing from standing"

if [ "$FAIL" = "0" ]; then
    echo "PASS: scoring is differential (selftest ${ST_LO}..${ST_HI} > dummy ${DU_LO}..${DU_HI})"
else
    echo "GATE FAILED"
fi
exit "$FAIL"
