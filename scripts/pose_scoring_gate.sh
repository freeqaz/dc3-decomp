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
# plays the same song, START TO FINISH, under several pose inputs and asserts
# they DISAGREE in the right direction:
#
#   selftest  the choreography's own reference pose fed back as the player.
#             Perfect mimicry, so this must score HIGH (~1.0).
#   dummy     a static standing skeleton (no provider). A person standing
#             still is not dancing, so this must score clearly BELOW selftest.
#   sensor    scripts/synthetic_kinect.py PERFORMING: the same choreography,
#             replayed as live camera-space skeleton frames through the
#             external pose socket (GET /api/pose/target), so the live-skeleton
#             half of the pipeline -- history, bone lengths, quality filter,
#             displacement lookback -- runs, which selftest bypasses.  Must also
#             score clearly ABOVE dummy.
#   replay    a recorded human-video keypoint stream (MediaPipe run offline over
#             a rendered dance clip, native/tests/data/pose/*.dc3pose, made by
#             scripts/pose_fixture_record.py) replayed through the socket KEYED
#             ON SONG TIME -- deterministic under DC3_FAST_TIME.  A real-looking
#             person dancing (a different dance from the song's).
#   still     the same stream's first frame held all song: the same body,
#             standing still.  replay must differ from it MOVE BY MOVE far more
#             than two standing bodies (still, dummy) differ from each other --
#             proof the recorded motion reaches the scorer.  Not asserted:
#             that replay out-SCORES still.  It does not (the fixture is a
#             different dance; measured 33,662 vs 49,119 -- PLAN P-7).
#   video     real pose estimation from a recorded dancer (--video CLIP), wall-
#             clock paced. Optional; reported but not asserted, since its value
#             depends on the clip and the run is not deterministic.
#
# Every run uses DC3_FAST_TIME (song time steps 1/120 s per frame) and is
# stopped when perform_endgame_screen enters -- the song has ended -- or at the --frames
# cap; a config whose song did not end FAILS (the old gate's 25k wall-clock
# frames stopped ~2 minutes into Better Off Alone). Scores and ratings come
# from GET /api/pose/target, recorded by synthetic_kinect.py (as the sensor, or
# with no --socket as a pure recorder). Judgement: scripts/pose_gate_report.py
# (means, final scores, ratings and per-move distances, never maximums
# alone -- see its docstring).
#
# Usage:
#   scripts/pose_scoring_gate.sh [--frames N] [--song FLOW] [--video CLIP]
#                                [--fixture F] [--only cfg,cfg] [--out DIR]
#
# Requires a built native binary (native/build/dc3-native) and a GPU-capable
# environment; run it OUTSIDE the agent sandbox. ~2.5-3 min per config.

set -uo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

FRAMES=60000
FLOW="scripts/dc3-input-flows/betteroffalone.txt"
VIDEO=""
FIXTURE="native/tests/data/pose/taye01_stand_dance.dc3pose"
POSE_MODEL="native/models/pose_landmarker_full.task"
ONLY="selftest,dummy,sensor,replay,still,video"
OUT="${TMPDIR:-/tmp}/pose_gate_$$"
PORT_BASE=0   # 0: each engine binds a free port and writes it to $OUT/NAME.port
CONFIG_WALL_LIMIT=1500   # seconds; a run that cannot finish the song in this is stuck

while [ $# -gt 0 ]; do
    case "$1" in
        --frames)  FRAMES="$2";  shift 2 ;;
        --song)    FLOW="$2";    shift 2 ;;
        --video)   VIDEO="$2";   shift 2 ;;
        --model)   POSE_MODEL="$2"; shift 2 ;;
        --fixture) FIXTURE="$2"; shift 2 ;;
        --only)    ONLY="$2";    shift 2 ;;
        --out)     OUT="$2";     shift 2 ;;
        --port)    PORT_BASE="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

BIN="$REPO/native/build/dc3-native"
[ -x "$BIN" ] || { echo "FAIL: $BIN not built"; exit 1; }
[ -f "$FIXTURE" ] || { echo "FAIL: fixture $FIXTURE missing"; exit 1; }
mkdir -p "$OUT"

COMMON_ENV=(
    "DC3_DATA=$REPO/orig-assets"
    "DC3_FAST_BOOT=1"
    "DC3_FAST_TIME=1"
    "MILO_HEADLESS=1"
    "MILO_RENDER=1"
    "MILO_MAX_FRAMES=$FRAMES"
    "MILO_INPUT_SCRIPT=$FLOW"
    # The flow drives the menus with button presses and has no `wake`
    # directives.  With the sensor configs' pose provider running, the default
    # controller-mode policy (auto) is faithful, where the 360 swallows a press
    # that only wakes controller mode; this gate measures scoring, not the
    # input mode, so pin the pad route (docs/debugging/native.md "Controller
    # Mode Policy").
    "DC3_CONTROLLER_MODE=forced"
    "DC3_DETECTFRAC_PROBE=1"
    "DC3_SCORING_DEBUG=1"
    "DC3_HTTP=1"
)

want() { case ",$ONLY," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

# run_cfg NAME PORT [socket-args for synthetic_kinect | -] [extra env...]
#   PORT 0 (the default): the engine binds a free port (DC3_HTTP_PORT=0) and
#   writes it to $OUT/NAME.port, which the sensor waits for
#   (--perform-port-file).  A port picked here and passed in could be taken
#   by any process before the engine binds it (scripts/engine_http_port.py).
#   Starts synthetic_kinect.py (as a sensor on a socket, or with "-" as a pure
#   score recorder), runs the engine until the song ends (endgame screen) or the frame cap,
#   then stops both. Writes $OUT/NAME.log and $OUT/NAME.json.
run_cfg() {
    local name="$1" port="$2" kin="$3"; shift 3
    local log="$OUT/$name.log" rec="$OUT/$name.json"
    local pfile="$OUT/$name.port"
    rm -f "$pfile"
    local kargs=(--perform-port-file "$pfile" --record "$rec")
    [ "$port" != 0 ] && kargs=(--perform "http://127.0.0.1:$port/api" --record "$rec")
    local sock=""
    if [ "$kin" != "-" ]; then
        sock="$OUT/$name.sock"
        # shellcheck disable=SC2206
        kargs+=(--socket "$sock" --people 1 $kin)
    fi
    python3 scripts/synthetic_kinect.py "${kargs[@]}" >"$OUT/$name.kinect.log" 2>&1 &
    local kpid=$!
    if [ -n "$sock" ]; then
        for _ in $(seq 1 50); do [ -S "$sock" ] && break; sleep 0.1; done
        set -- DC3_POSE=external DC3_POSE_NO_SPAWN=1 "DC3_POSE_SOCKET=$sock" "$@"
    fi
    env "${COMMON_ENV[@]}" "DC3_HTTP_PORT=$port" "DC3_HTTP_PORT_FILE=$pfile" "$@" "$BIN" >"$log" 2>&1 &
    local epid=$!
    local start=$SECONDS stopped=""
    while kill -0 "$epid" 2>/dev/null; do
        if grep -q "Screen 'perform_endgame_screen' Enter" "$log" 2>/dev/null; then
            stopped="song-end"; sleep 2; kill "$epid" 2>/dev/null; break
        fi
        if [ $((SECONDS - start)) -ge "$CONFIG_WALL_LIMIT" ]; then
            stopped="wall-limit"; kill "$epid" 2>/dev/null; break
        fi
        sleep 2
    done
    wait "$epid" 2>/dev/null
    local code=$?
    kill "$kpid" 2>/dev/null; wait "$kpid" 2>/dev/null
    echo "$name: engine exit=$code stop=${stopped:-engine-exited} wall=$((SECONDS - start))s"
}

echo "=== native move-scoring differential gate (whole song, DC3_FAST_TIME, cap ${FRAMES} frames, $(basename "$FLOW")) ==="
echo "    out: $OUT"

# port: 0 = engine-picked; --port N pins N, N+1, ... (one per config)
cfg_port() { [ "$PORT_BASE" = 0 ] && echo 0 || echo $((PORT_BASE + $1)); }
want selftest && run_cfg selftest "$(cfg_port 0)" - DC3_POSE_SELFTEST=1
want dummy    && run_cfg dummy    "$(cfg_port 1)" -
want sensor   && run_cfg sensor   "$(cfg_port 2)" ""
want replay   && run_cfg replay   "$(cfg_port 3)" "--replay $FIXTURE"
want still    && run_cfg still    "$(cfg_port 4)" "--replay $FIXTURE --replay-still"

if want video && [ -n "$VIDEO" ] && [ -f "$VIDEO" ]; then
    VSOCK="$OUT/video_server.sock"
    .venv/bin/python native/scripts/pose_server.py --video "$VIDEO" --loop --fps 15 \
        --socket "$VSOCK" --model "$POSE_MODEL" >"$OUT/video_server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 60); do [ -S "$VSOCK" ] && break; sleep 1; done
    run_cfg video "$(cfg_port 5)" - DC3_POSE=external DC3_POSE_NO_SPAWN=1 "DC3_POSE_SOCKET=$VSOCK"
    kill "$SERVER_PID" 2>/dev/null
fi

python3 scripts/pose_gate_report.py "$OUT" --configs "$ONLY" --json "$OUT/report.json"
