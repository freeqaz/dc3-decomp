#!/usr/bin/env bash
# Build (or test) a worktree's native port on tnr instead of the desk.
#
#   scripts/remote/tnr_build.sh [options] <worktree> [ninja targets...]
#
# Syncs the worktree's sources (git tracked + untracked-not-ignored files) and
# the milo-native-engine checkout to tnr, builds there in a container whose
# toolchain is byte-identical to the desk's, with the worktree, engine, Dawn and
# build dir mounted at their DESK paths, then copies the build dir back into
# <worktree>/native/build. Because every path and every compiler/library byte
# is the same, the desk's ninja treats the fetched build as current: the lane
# gets dc3-native, milo-tests and milo-viewer without spending desk CPU, and a
# later desk `ninja` or `scripts/native_test.sh` rebuilds only what it changes.
#
# Default targets: the native gate's required set
# (native/build/milo_test_required_targets.txt).
#
# Options:
#   --test [ARGS]    run scripts/native_test.sh on tnr (lavapipe, no GPU)
#                    instead of a plain build; ARGS go to native_test.sh
#   --no-fetch       leave the build on tnr (only the log comes back)
#   --reconfigure    re-run scripts/native_configure.sh on tnr first
#   --jobs N         ninja -j and CPU quota on tnr (default 64, max 96)
#   --build-image    (re)build the container image on tnr and exit
#   --sync-assets    copy orig-assets / archive / milo-rnd-library to tnr
#                    (needed for --test only; under TNR_ROOT, nowhere else)
#
# Environment: TNR_HOST (tnr-decomp), TNR_ROOT (/scratch/dc3-native-build),
# TNR_IMAGE (localhost/dc3-native-build:20261020).
#
# Exit: 0 built (and fetched), 1 build failed, 2 usage, 3 sync failed,
# 4 built but the desk toolchain no longer matches the container's (not fetched
# into a usable state: rebuild the image), 9 missing input (Dawn, image),
# 75 tnr busy or in a measurement window: try again later.
#
# Read docs/native/REMOTE_BUILD.md before changing the etiquette parts.

set -u
HOST="${TNR_HOST:-tnr-decomp}"
B="${TNR_ROOT:-/scratch/dc3-native-build}"
IMAGE="${TNR_IMAGE:-localhost/dc3-native-build:20261020}"
JOBS=64; FETCH=1; RECONF=0; MODE=build; TEST_ARGS=""; ACTION=build

usage() { sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
WT=""; TARGETS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --test) MODE=test; shift
                while [ $# -gt 0 ] && [ "${1#-}" != "$1" ] && [ "$1" != "--" ]; do TEST_ARGS="$TEST_ARGS $1"; shift; done ;;
        --no-fetch) FETCH=0; shift ;;
        --reconfigure) RECONF=1; shift ;;
        --jobs) JOBS="${2:?}"; shift 2 ;;
        --jobs=*) JOBS="${1#--jobs=}"; shift ;;
        --build-image) ACTION=image; shift ;;
        --sync-assets) ACTION=assets; shift ;;
        -h|--help) usage ;;
        -*) echo "unknown option $1" >&2; usage ;;
        *) if [ -z "$WT" ]; then WT="$1"; else TARGETS+=("$1"); fi; shift ;;
    esac
done
case "$JOBS" in ''|*[!0-9]*) echo "--jobs must be a number" >&2; exit 2 ;; esac
[ "$JOBS" -gt 96 ] && { echo "--jobs capped at 96 on tnr (docs/native/REMOTE_BUILD.md)" >&2; JOBS=96; }

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
CM="${XDG_RUNTIME_DIR:-/tmp}/tnr-build-cm-%C"
SSH=(ssh -o BatchMode=yes -o ControlMaster=auto -o "ControlPath=$CM" -o ControlPersist=120)
RSH="ssh -o BatchMode=yes -o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=120"
rs() { rsync -e "$RSH" "$@"; }
now() { date +%s.%N; }
since() { awk -v a="$1" -v b="$(now)" 'BEGIN{printf "%.1f", b-a}'; }

if [ "$ACTION" = image ]; then
    "${SSH[@]}" "$HOST" "mkdir -p $B/image" || exit 3
    rs -a "$SELF_DIR/tnr/Containerfile" "$HOST:$B/image/Containerfile" || exit 3
    echo "building $IMAGE on tnr (pacman from the Arch archive; a few minutes)"
    "${SSH[@]}" "$HOST" "export XDG_CONFIG_HOME=$B/xdg/config XDG_DATA_HOME=$B/xdg/data XDG_CACHE_HOME=$B/xdg/cache;
        cd $B/image && taskset -c 32-63 nice -n 19 podman build -t $IMAGE -f Containerfile . > build.log 2>&1;
        rc=\$?; tail -5 build.log; exit \$rc"
    exit $?
fi

[ -n "$WT" ] || usage
WT="$(cd "$WT" 2>/dev/null && git rev-parse --show-toplevel 2>/dev/null)" || { echo "not a git worktree: $WT" >&2; exit 2; }
WT="$(cd "$WT" && pwd -P)"
KEY="$(printf '%s' "${WT#/}" | tr '/' '_')"

# ---- what the desk's configure would use --------------------------------------
dry="$(cd "$WT" && MILO_CONFIGURE_DRY_RUN=1 bash scripts/native_configure.sh 2>&1)" || {
    echo "$dry" | tail -20 >&2; echo "desk configure dry run failed (see above)" >&2; exit 9; }
field() { printf '%s\n' "$dry" | sed -n "s/^    $1 *: //p" | head -1; }
DAWN_DIR="$(field Dawn_DIR)"
DAWN_ROOT="${DAWN_DIR%/lib/cmake/Dawn}"; DAWN_ROOT="${DAWN_ROOT%/lib64/cmake/Dawn}"
IMGUI="$(field 'imgui src' | sed 's/ (reused.*//')"
cc_line="$(field ccache)"
CCACHE_BIN=""; CCACHE_DIR=""
case "$cc_line" in
    off*) ;;
    *" -> "*) CCACHE_BIN="${cc_line%% -> *}"; CCACHE_DIR="${cc_line#* -> }"; CCACHE_DIR="${CCACHE_DIR%% (*}" ;;
esac
# The compiler PATHS too: PATH order differs in the container (/usr/sbin/clang),
# and CMakeCache + every ninja command line would then differ from the desk's.
CC_DESK="$(printf '%s\n' "$dry" | grep -o -- '-DCMAKE_C_COMPILER=[^ ]*' | head -1 | cut -d= -f2-)"
CXX_DESK="$(printf '%s\n' "$dry" | grep -o -- '-DCMAKE_CXX_COMPILER=[^ ]*' | head -1 | cut -d= -f2-)"
[ -n "$DAWN_ROOT" ] && [ -d "$DAWN_ROOT" ] || { echo "cannot locate Dawn from the dry run" >&2; exit 9; }

# MILO_ENGINE_PATH exactly as native/CMakeLists.txt would compute it on the desk.
ENGINE="$(sed -n 's/^MILO_ENGINE_PATH:PATH=//p' "$WT/native/build/CMakeCache.txt" 2>/dev/null)"
if [ -z "$ENGINE" ]; then
    if [ -e "$WT/native/../../milo-native-engine/.git" ]; then
        ENGINE="$WT/native/../../milo-native-engine"
    else
        common="$(git -C "$WT" rev-parse --path-format=absolute --git-common-dir)"
        ENGINE="$(dirname "$(dirname "$common")")/milo-native-engine"
    fi
fi
[ -d "$ENGINE" ] || { echo "engine checkout not found: $ENGINE" >&2; exit 9; }
ENGINE_REAL="$(cd "$ENGINE" && pwd -P)"

# Configure-time probes (native/CMakeLists.txt): orig-assets and MILO_LIB.
ASSETS=""
for c in "$WT/orig-assets" /home/free/code/milohax/dc3-decomp/orig-assets; do
    [ -e "$c/gen/main_xbox.hdr" ] && { ASSETS="$(readlink -f "$c")"; break; }
done
MILO_LIB="$HOME/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3"
[ -e "$MILO_LIB/char/crowd/anim/gen/female_base.milo_xbox" ] || MILO_LIB=""
ARCHIVE="$( [ -e "$WT/archive" ] && readlink -f "$WT/archive" )"

"${SSH[@]}" "$HOST" "mkdir -p $B/trees/$KEY/src $B/trees/$KEY/engine $B/deps/dawn $B/deps/imgui-src $B/bin $B/locks $B/assets" \
    || { echo "cannot reach $HOST" >&2; exit 75; }

if [ "$ACTION" = assets ]; then
    t=$(now)
    [ -n "$ASSETS" ] && { echo "orig-assets <- $ASSETS"; rs -a --delete "$ASSETS/" "$HOST:$B/assets/orig-assets/" || exit 3; }
    [ -n "$ARCHIVE" ] && { echo "archive <- $ARCHIVE"; rs -a --delete "$ARCHIVE/" "$HOST:$B/assets/archive/" || exit 3; }
    [ -n "$MILO_LIB" ] && { echo "milo-rnd-library/dc3 <- $MILO_LIB"; rs -a --delete "$MILO_LIB/" "$HOST:$B/assets/milo-lib-dc3/" || exit 3; }
    echo "assets synced in $(since "$t") s"
    exit 0
fi

# ---- sync ---------------------------------------------------------------------
t_sync=$(now)
tmp="$(mktemp -d "${TMPDIR:-/tmp}/tnr-build.XXXXXX")"
( cd "$WT" && { git ls-files -co --exclude-standard -z; find . -maxdepth 1 -type l -printf '%P\0'; } ) > "$tmp/src.list"
if [ -e "$ENGINE_REAL/.git" ]; then
    git -C "$ENGINE_REAL" ls-files -co --exclude-standard -z > "$tmp/engine.list"
else
    ( cd "$ENGINE_REAL" && find . -path ./build -prune -o \( -type f -o -type l \) -printf '%P\0' ) > "$tmp/engine.list"
fi
LOCKED="flock $B/locks/deps.lock rsync"
rs -a -0 --ignore-missing-args --files-from="$tmp/src.list" "$WT/" "$HOST:$B/trees/$KEY/src/" \
 && rs -a "$tmp/src.list" "$tmp/engine.list" "$HOST:$B/trees/$KEY/" \
 && rs -a -0 --ignore-missing-args --files-from="$tmp/engine.list" "$ENGINE_REAL/" "$HOST:$B/trees/$KEY/engine/" \
 && rs -a --delete --rsync-path="$LOCKED" "$DAWN_ROOT/" "$HOST:$B/deps/dawn/" \
 && { [ -z "$IMGUI" ] || rs -a --delete --exclude=.git --rsync-path="$LOCKED" "$IMGUI/" "$HOST:$B/deps/imgui-src/"; } \
 && { [ -z "$CCACHE_BIN" ] || rs -a --rsync-path="$LOCKED" "$(readlink -f "$CCACHE_BIN")" "$HOST:$B/bin/ccache"; } \
 || { echo "sync to $HOST failed" >&2; exit 3; }
echo "tnr: synced $(tr -cd '\0' < "$tmp/src.list" | wc -c) files + engine in $(since "$t_sync") s"

# ---- build ----------------------------------------------------------------------
q() { printf '%q' "$1"; }
ENV_ARGS="TNR_ROOT=$(q "$B") TNR_KEY=$(q "$KEY") TNR_WT=$(q "$WT") TNR_ENGINE=$(q "$ENGINE")"
ENV_ARGS="$ENV_ARGS TNR_DAWN=$(q "$DAWN_ROOT") TNR_IMGUI=$(q "$IMGUI") TNR_ASSETS=$(q "$ASSETS")"
ENV_ARGS="$ENV_ARGS TNR_MILO_LIB=$(q "$MILO_LIB") TNR_ARCHIVE=$(q "$ARCHIVE")"
ENV_ARGS="$ENV_ARGS TNR_CCACHE_BIN=$(q "$CCACHE_BIN") TNR_CCACHE_DIR=$(q "$CCACHE_DIR") TNR_MILO_CCACHE=$(q "${MILO_CCACHE:-}")"
ENV_ARGS="$ENV_ARGS TNR_CC=$(q "$CC_DESK") TNR_CXX=$(q "$CXX_DESK")"
ENV_ARGS="$ENV_ARGS TNR_JOBS=$JOBS TNR_IMAGE=$(q "$IMAGE") TNR_MODE=$MODE TNR_RECONFIGURE=$RECONF"
ENV_ARGS="$ENV_ARGS TNR_TARGETS=$(q "${TARGETS[*]+${TARGETS[*]}}") TNR_TEST_ARGS=$(q "$TEST_ARGS")"
echo "tnr: $MODE of $WT (branch $(git -C "$WT" rev-parse --abbrev-ref HEAD)) -j$JOBS"
t_build=$(now)
"${SSH[@]}" "$HOST" "env $ENV_ARGS bash $(q "$B/trees/$KEY/src/scripts/remote/tnr/remote_build.sh")"
rc=$?
echo "tnr: $MODE exit $rc after $(since "$t_build") s"
[ "$rc" = 75 ] && exit 75

# ---- fetch ----------------------------------------------------------------------
LOG="$WT/native/build/tnr-build-last.log"
if [ "$FETCH" = 1 ] && [ "$rc" = 0 ]; then
    t_fetch=$(now)
    mkdir -p "$WT/native/build"
    rs -a --delete --exclude=/Testing/ "$HOST:$B/builds/$KEY/" "$WT/native/build/" \
        || { echo "fetch failed" >&2; exit 3; }
    echo "tnr: fetched build dir in $(since "$t_fetch") s"
else
    mkdir -p "$tmp/log" && rs -a "$HOST:$B/builds/$KEY/tnr-build-last.log" "$tmp/log/" 2>/dev/null
    LOG="$tmp/log/tnr-build-last.log"
fi
echo "---- log tail ($LOG) ----"
tail -n 25 "$LOG" 2>/dev/null
echo "----"

if [ "$FETCH" = 1 ] && [ "$rc" = 0 ]; then
    if ! python3 "$WT/scripts/native_toolchain_check.py" --check "$WT/native/build"; then
        echo "tnr: WARNING: the desk toolchain differs from the container's. The fetched" >&2
        echo "     build dir will be reconfigured by the next native_test.sh. Rebuild the" >&2
        echo "     image for the desk's current packages (docs/native/REMOTE_BUILD.md)." >&2
        exit 4
    fi
    if [ "$MODE" = build ]; then
        read -r -a want <<< "${TARGETS[*]+${TARGETS[*]}}"
        [ ${#want[@]} -gt 0 ] || read -r -a want < "$WT/native/build/milo_test_required_targets.txt"
        left="$(ninja -C "$WT/native/build" -n "${want[@]}" 2>&1 | tail -1)"
        echo "tnr: desk ninja dry run on the fetched dir: $left"
    fi
fi
exit "$rc"
