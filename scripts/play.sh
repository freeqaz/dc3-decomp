#!/usr/bin/env bash
# play.sh -- start Dance Central 3 (native port) for a PLAYER: a window, real
# sound, the webcam pose tracker, menus by gesture, and a session folder with
# everything needed to look at a problem afterwards (docs/native/PLAYTEST.md).
#
#   scripts/play.sh                 check, then play (windowed, camera 0)
#   scripts/play.sh --check         only check the machine, camera and player
#   scripts/play.sh --fullscreen    fill the screen
#   scripts/play.sh --camera 1      another camera
#   scripts/play.sh --video F.mp4   a video file instead of the camera (testing)
#
# Quit with Ctrl+Q or by closing the window. When the game ends, the session
# folder is packed into <session>.tar.gz: that file is what to send back.
#
# Options:
#   --check            run the checks and stop
#   --no-person        (with --check) do not look for a person in the picture
#   --camera N         camera index (default 0, or $DC3_POSE_CAMERA)
#   --video FILE       footage instead of a camera (looped)
#   --fullscreen       fullscreen on the main monitor (MILO_FULLSCREEN=1)
#   --size WxH         window size (default 1280x720)
#   --no-pose          no tracker: play with keyboard / pad only
#   --fast-boot        skip the intro and title (straight to the main menu)
#   --home DIR         saves, settings cache and sessions (default
#                      ~/.local/share/dc3/playtest; $DC3_PLAYTEST_HOME)
#   --session-dir DIR  put this session's files in DIR (default
#                      <home>/sessions/<date-time>)
#   --replay FILE      replay a recorded pose stream (a session's
#                      pose.dc3session, or one song cut from it by
#                      scripts/playtest/pose_session.py) instead of the camera
#   --replay-sync song|wall   how the replay is timed (default song)
#   --headless         no window (automation; implies no sound device)
#   --no-pack          do not make the .tar.gz at the end
#   -- ARGS            passed to dc3-native
#
# Any DC3_*/MILO_* variable already set in the environment wins over the
# defaults below (see docs/debugging/native.md for the table).
#
# Exit: the game's exit code (0 = quit normally), 1 usage, 3 not ready
# (the checks failed; nothing was started).

set -o pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMON="$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
MAIN_CHECKOUT="${COMMON:+$(dirname "$COMMON")}"

CHECK=0; NO_PERSON=0; CAMERA="${DC3_POSE_CAMERA:-0}"; VIDEO=""; FULLSCREEN=0; SIZE=""
NO_POSE=0; FAST_BOOT=0; HOME_DIR="${DC3_PLAYTEST_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/dc3/playtest}"
SESSION=""; REPLAY=""; REPLAY_SYNC="song"; HEADLESS=0; PACK=1; ENGINE_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1 ;;
        --no-person) NO_PERSON=1 ;;
        --camera) CAMERA="$2"; shift ;;
        --video) VIDEO="$(realpath "$2")"; shift ;;
        --fullscreen) FULLSCREEN=1 ;;
        --size) SIZE="$2"; shift ;;
        --no-pose) NO_POSE=1 ;;
        --fast-boot) FAST_BOOT=1 ;;
        --home) HOME_DIR="$2"; shift ;;
        --session-dir) SESSION="$2"; shift ;;
        --replay) REPLAY="$(realpath "$2")"; shift ;;
        --replay-sync) REPLAY_SYNC="$2"; shift ;;
        --headless) HEADLESS=1 ;;
        --no-pack) PACK=0 ;;
        --) shift; ENGINE_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,42p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

BINARY="$REPO/native/build/dc3-native"
DATA="${DC3_DATA:-$REPO/orig-assets}"
MODEL="${DC3_POSE_MODEL:-$REPO/native/models/pose_landmarker_full.task}"

# The tracker's Python: $DC3_POSE_PYTHON, the bootstrap's venv, the main
# checkout's developer .venv, then whatever python3 is on PATH.
pick_python() {
    local c
    for c in "${DC3_POSE_PYTHON:-}" "${DC3_POSE_VENV:-$HOME/.local/share/dc3/pose-venv}/bin/python3" \
             "$REPO/.venv/bin/python3" "${MAIN_CHECKOUT:+$MAIN_CHECKOUT/.venv/bin/python3}"; do
        [ -n "$c" ] && [ -x "$c" ] && "$c" -I -c 'import mediapipe, cv2' >/dev/null 2>&1 && { echo "$c"; return; }
    done
    command -v python3
}
POSE_PY="$(pick_python)"

pre_args=(--python "$POSE_PY" --data "$DATA" --binary "$BINARY" --model "$MODEL")
[ -n "$VIDEO" ] && pre_args+=(--video "$VIDEO") || pre_args+=(--camera "$CAMERA")
[ "$NO_POSE" = 1 ] || [ -n "$REPLAY" ] && pre_args+=(--no-pose)
[ "$NO_PERSON" = 1 ] && pre_args+=(--no-person)
[ "$HEADLESS" = 1 ] && pre_args+=(--headless)

if [ "$CHECK" = 1 ]; then
    echo "DC3 playtest check ($(git -C "$REPO" rev-parse --short HEAD 2>/dev/null))"
    python3 -I "$REPO/scripts/playtest/preflight.py" "${pre_args[@]}"
    exit $?
fi

[ -n "$SESSION" ] || SESSION="$HOME_DIR/sessions/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$SESSION" "$HOME_DIR/saves" "$HOME_DIR/cache"
SESSION="$(cd "$SESSION" && pwd)"
echo "DC3 playtest session: $SESSION"
echo "Checking the machine (scripts/play.sh --check runs only this) ..."
if ! python3 -I "$REPO/scripts/playtest/preflight.py" "${pre_args[@]}" --no-person \
        --json "$SESSION/preflight.json" | tee "$SESSION/preflight.txt"; then
    echo "Not starting: fix the problems above first." >&2
    exit 3
fi

# ---- the environment the game runs in ---------------------------------------
setdef() { [ -n "${!1+x}" ] || export "$1=$2"; }   # caller's value wins
export DC3_SAVE_DIR="${DC3_SAVE_DIR:-$HOME_DIR/saves}"
export DC3_CACHE_DIR="${DC3_CACHE_DIR:-$HOME_DIR/cache}"
export DC3_DATA="$DATA"
setdef DC3_CONTROLLER_MODE auto      # menus by gesture while the tracker runs
setdef DC3_TAG_MODALS 1              # every notify / fail / assert in the log
setdef DC3_MOVE_RATING_LOG 1         # every move's rating in the log
setdef DC3_PERF_LOG 1                # game fps + pose fps every 5 s (PLAN L-3)
setdef DC3_POSE_STATS 1              # the tracker's own rate and inference time
setdef DC3_HTTP 1                    # read-only-ish debug API, this machine only:
setdef DC3_HTTP_HOST 127.0.0.1
setdef DC3_HTTP_PORT 0
export DC3_HTTP_PORT_FILE="$SESSION/http_port"
setdef MILO_MAX_FRAMES 0
setdef DC3_SFX_PCM_DIR "$HOME_DIR/cache/sfx_pcm"   # decoded SFX, not into the game data
export DC3_POSE_CAMERA="$CAMERA"
export DC3_POSE_PYTHON="$POSE_PY"
export DC3_POSE_MODEL="$MODEL"
[ "$FAST_BOOT" = 1 ] && export DC3_FAST_BOOT=1
[ "$FULLSCREEN" = 1 ] && export MILO_FULLSCREEN=1
if [ -n "$SIZE" ]; then export MILO_WIDTH="${SIZE%x*}" MILO_HEIGHT="${SIZE#*x}"; fi
[ "$HEADLESS" = 1 ] && { export MILO_HEADLESS=1; setdef DC3_POSE external; }
if [ "$NO_POSE" = 1 ]; then
    export DC3_POSE=off
elif [ -n "$REPLAY" ]; then
    export DC3_POSE=external DC3_POSE_PERFORM="session:$REPLAY" DC3_POSE_SESSION_SYNC="$REPLAY_SYNC"
else
    [ -n "$VIDEO" ] && export DC3_POSE_VIDEO="$VIDEO"
    setdef DC3_POSE_RECORD "$SESSION/pose.dc3session"
fi
mkdir -p "$DC3_SFX_PCM_DIR"

{
    echo "started $(date -Iseconds)"
    echo "host $(uname -n) $(uname -srm)"
    echo "dc3 $(git -C "$REPO" rev-parse HEAD 2>/dev/null) $(git -C "$REPO" status --porcelain 2>/dev/null | wc -l) uncommitted"
    eng="${MILO_ENGINE_PATH:-$(dirname "${MAIN_CHECKOUT:-$REPO}")/milo-native-engine}"
    echo "engine $(git -C "$eng" rev-parse HEAD 2>/dev/null)"
    echo "binary $(stat -c '%y' "$BINARY" 2>/dev/null) $(sha256sum "$BINARY" 2>/dev/null | cut -c1-16)"
    command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|driverInfo' | sort -u
    for d in /sys/class/video4linux/video*; do [ -e "$d/name" ] && echo "camera $(basename "$d"): $(cat "$d/name")"; done
    command -v v4l2-ctl >/dev/null && [ -z "$VIDEO" ] && v4l2-ctl -d "/dev/video$CAMERA" --get-fmt-video --get-parm 2>/dev/null
    command -v pactl >/dev/null && pactl info 2>/dev/null | grep -E 'Server Name|Default Sink'
} >"$SESSION/system.txt" 2>&1
env | grep -E '^(DC3|MILO)_' | sort >"$SESSION/env.txt"
echo "${ENGINE_ARGS[*]}" >"$SESSION/engine_args.txt"

echo
echo "Starting the game. Quit: Ctrl+Q or close the window."
echo "Log: $SESSION/engine.log"
# The game takes Ctrl+C as "quit at the next frame" (H-9); this script keeps
# running so it can write the summary afterwards.
trap '' INT
cd "$REPO/native/build"
stdbuf -oL -eL "$BINARY" "${ENGINE_ARGS[@]+"${ENGINE_ARGS[@]}"}" >"$SESSION/engine.log" 2>&1 &
GAME_PID=$!
echo "$GAME_PID" >"$SESSION/game.pid"
wait "$GAME_PID"
RC=$?
trap - INT
echo "$RC" >"$SESSION/exit_code"

# Saves after the session (the game rewrites them in place).
mkdir -p "$SESSION/saves-after"
cp -a "$DC3_SAVE_DIR/." "$SESSION/saves-after/" 2>/dev/null
cp -a "$DC3_CACHE_DIR/global" "$SESSION/saves-after/cache-global" 2>/dev/null

python3 -I "$REPO/scripts/playtest/session_summary.py" "$SESSION" | tee "$SESSION/summary.txt"
if [ "$PACK" = 1 ]; then
    tar -czf "$SESSION.tar.gz" -C "$(dirname "$SESSION")" "$(basename "$SESSION")" \
        && echo && echo "Send back: $SESSION.tar.gz ($(du -h "$SESSION.tar.gz" | cut -f1))"
fi
exit "$RC"
