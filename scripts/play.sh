#!/usr/bin/env bash
# play.sh -- start Dance Central 3 (native port) for a PLAYER: a window, real
# sound, the webcam pose tracker, menus by gesture, and a session folder with
# everything needed to look at a problem afterwards (docs/native/PLAYTEST.md).
#
#   scripts/play.sh                 check, then play (windowed, the first camera)
#   scripts/play.sh --check         only check the machine, camera and player
#   scripts/play.sh --fullscreen    fill the screen
#   scripts/play.sh --camera /dev/v4l/by-id/usb-...   a particular camera
#   scripts/play.sh --video F.mp4   a video file instead of the camera (testing)
#
# Quit with Ctrl+Q or by closing the window. When the game ends, the session
# folder is packed into <session>.tar.gz: that file is what to send back.
#
# Options:
#   --check            run the checks and stop
#   --no-person        (with --check) do not look for a person in the picture
#   --camera auto|N|PATH (default auto: the first video-capture device;
#                      or $DC3_POSE_CAMERA). A camera can come back under
#                      another number after it re-connects, so prefer auto
#                      or the /dev/v4l/by-id name to an index
#   --video FILE       footage instead of a camera: frame 0 (the dancer
#                      standing) in the menus, then the clip in step with the
#                      song clock (scripts/playtest/footage_camera.py)
#   --video-pre S      seconds of clip before song time 0 (default 8, the
#                      clips scripts/playtest/make_footage.py cuts)
#   --video-loop       just loop the file instead (pose_server.py --video --loop)
#   --fullscreen       NOT AVAILABLE YET: the game draws 1280x720 into the
#                      top-left of any bigger window (PLAN H-18); says so and
#                      plays windowed
#   --size WxH         window size (default 1280x720; anything else shows
#                      H-18's corner picture)
#   --wayland          on a Wayland desktop, open a Wayland window (default:
#                      an X11 window through Xwayland, which the compositor
#                      scales as a whole; see H-18)
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

CHECK=0; NO_PERSON=0; CAMERA="${DC3_POSE_CAMERA:-auto}"; VIDEO=""; FULLSCREEN=0; SIZE=""
NO_POSE=0; FAST_BOOT=0; HOME_DIR="${DC3_PLAYTEST_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/dc3/playtest}"
SESSION=""; REPLAY=""; REPLAY_SYNC="song"; HEADLESS=0; PACK=1; ENGINE_ARGS=()
VIDEO_PRE=8; VIDEO_LOOP=0; WAYLAND=0
while [ $# -gt 0 ]; do
    case "$1" in
        --check) CHECK=1 ;;
        --no-person) NO_PERSON=1 ;;
        --camera) CAMERA="$2"; shift ;;
        --video) VIDEO="$(realpath "$2")"; shift ;;
        --video-pre) VIDEO_PRE="$2"; shift ;;
        --video-loop) VIDEO_LOOP=1 ;;
        --fullscreen) FULLSCREEN=1 ;;
        --wayland) WAYLAND=1 ;;
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
        -h|--help) sed -n '2,/^$/p' "$0"; exit 0 ;;   # the header comment
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

BINARY="$REPO/native/build/dc3-native"
DATA="${DC3_DATA:-$REPO/orig-assets}"
MODEL="${DC3_POSE_MODEL:-$REPO/native/models/pose_landmarker_full.task}"

# The tracker's Python is the game's own choice (DC3_POSE_PYTHON if set, the
# bootstrap's venv, the checkout's .venv, python3: pose_python.py); preflight
# checks that same one.  A DC3_POSE_PYTHON set by the caller passes through.
pre_args=(--data "$DATA" --binary "$BINARY" --model "$MODEL")
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
# The dev profile (DC3_PROFILE, the default) already turns on the debug API
# (loopback only), gesture menus while the tracker runs and the per-move
# rating log; the session summary reads that log.
setdef DC3_TAG_MODALS 1              # every notify / fail / assert, with a backtrace
setdef DC3_PERF_LOG 1                # game fps + pose fps every 5 s (PLAN L-3)
setdef DC3_POSE_STATS 1              # the tracker's own rate and inference time
setdef DC3_HTTP_PORT 0               # any free port, written to the session folder
export DC3_HTTP_PORT_FILE="$SESSION/http_port"
setdef MILO_MAX_FRAMES 0
setdef DC3_SFX_PCM_DIR "$HOME_DIR/cache/sfx_pcm"   # decoded SFX, not into the game data
# The engine passes DC3_POSE_CAMERA to pose_server.py --camera as given;
# unset is auto (the server re-resolves it on every open).
if [ "$CAMERA" = auto ]; then unset DC3_POSE_CAMERA; else export DC3_POSE_CAMERA="$CAMERA"; fi
export DC3_POSE_MODEL="$MODEL"
[ "$FAST_BOOT" = 1 ] && export DC3_FAST_BOOT=1
if [ "$FULLSCREEN" = 1 ]; then
    echo "note: --fullscreen is not available yet (the game would sit in the top-left"
    echo "      corner of the screen, PLAN H-18); playing in a 1280x720 window."
fi
# Wayland: GLFW would open a native Wayland window, which a scaled (HiDPI)
# desktop hands a framebuffer twice the size, and the game draws into its
# top-left corner (H-18).  Through Xwayland the window is 1280x720 pixels and
# the compositor scales the whole picture.
if [ "$HEADLESS" = 0 ] && [ "$WAYLAND" = 0 ] && [ -n "${WAYLAND_DISPLAY:-}" ] && [ -n "${DISPLAY:-}" ]; then
    echo "Wayland desktop: opening the window through Xwayland (DISPLAY=$DISPLAY); --wayland for native"
    unset WAYLAND_DISPLAY
fi
if [ -n "$SIZE" ]; then export MILO_WIDTH="${SIZE%x*}" MILO_HEIGHT="${SIZE#*x}"; fi
[ "$HEADLESS" = 1 ] && { export MILO_HEADLESS=1; setdef DC3_POSE external; }
if [ "$NO_POSE" = 1 ]; then
    export DC3_POSE=off
elif [ -n "$REPLAY" ]; then
    export DC3_POSE=external DC3_POSE_PERFORM="session:$REPLAY" DC3_POSE_SESSION_SYNC="$REPLAY_SYNC"
else
    if [ -n "$VIDEO" ]; then
        export DC3_POSE_VIDEO="$VIDEO"
        if [ "$VIDEO_LOOP" = 0 ]; then
            setdef DC3_POSE_SERVER "$REPO/scripts/playtest/footage_camera.py"
            setdef DC3_FOOTAGE_PRE "$VIDEO_PRE"
        fi
    fi
fi
# Every session records what the sensor accepted -- a replay too, so a replay
# can be checked against the file it replayed (pose_session.py compare).
[ "$NO_POSE" = 1 ] || setdef DC3_POSE_RECORD "$SESSION/pose.dc3session"
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
    if [ -z "$VIDEO" ]; then
        CAM_NODE="$(python3 -I "$REPO/scripts/playtest/camera_resolve.py" "$CAMERA" 2>&1)" \
            && echo "camera --camera $CAMERA -> $CAM_NODE" || { echo "camera --camera $CAMERA: $CAM_NODE"; CAM_NODE=""; }
        ls -l /dev/v4l/by-id 2>/dev/null
        command -v v4l2-ctl >/dev/null && [ -n "$CAM_NODE" ] && v4l2-ctl -d "$CAM_NODE" --get-fmt-video --get-parm --list-ctrls 2>/dev/null
    fi
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
