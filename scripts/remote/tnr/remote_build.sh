#!/usr/bin/env bash
# tnr host side of scripts/remote/tnr_build.sh. Runs as decomp-ops on tnr, from
# the synced copy of the worktree. Not meant to be called by hand; see
# docs/native/REMOTE_BUILD.md.
#
# Inputs (environment, set by tnr_build.sh):
#   TNR_ROOT      /scratch/dc3-native-build (everything we own on tnr)
#   TNR_KEY       per-worktree key (the desk worktree path, '/' -> '_')
#   TNR_WT        the worktree's DESK path; it is mounted at that same path
#   TNR_ENGINE    desk path of the milo-native-engine checkout
#   TNR_DAWN      desk path of the Dawn prebuilt root (.../dc3-decomp-deps/dawn)
#   TNR_IMGUI     desk path of the shared imgui source (may be empty)
#   TNR_CC/TNR_CXX the compiler paths the desk configure picks (same in CMakeCache)
#   TNR_ASSETS    desk path orig-assets resolves to
#   TNR_MILO_LIB  desk path of the default MILO_LIB (milo-rnd-library/dc3)
#   TNR_CCACHE_BIN / TNR_CCACHE_DIR   desk paths ccache is configured with ('' = off)
#   TNR_JOBS      ninja -j (and the CPU quota)
#   TNR_IMAGE     podman image
#   TNR_MODE      build | test
#   TNR_RECONFIGURE 1 = configure even if the build dir has a CMakeCache
#   TNR_TARGETS   space-separated ninja targets ('' = the gate's required set)
#   TNR_TEST_ARGS extra args for native_test.sh in test mode
#
# Etiquette (docs/native/REMOTE_BUILD.md has the reasoning). tnr serves models
# for other people. This script mirrors the freeze rules of the host's own
# agent-job guard (cmp-agent-guard, TNR-CPU-OFFLOAD-2026-10-20.md):
#   * refuses to START while /run/cmp-measure-window or /run/cmp-agent-pause
#     exists or the guard says "frozen" (exit 75);
#   * PAUSES the container (cgroup freezer) whenever any of those appear
#     mid-build, and resumes 30 s after all are clear, as the guard does;
#   * runs on the guard's measured CPU set (32-63,96-127,160-191,224-254,
#     never the serving reservation), nice 19 + SCHED_IDLE + idle IO,
#     a CPU quota of TNR_JOBS cores and a memory cap;
#   * at most TNR_SLOTS (2) builds at once host-wide; a second build of the
#     same worktree is refused rather than queued.
#
# Exit: container's exit code (0 ok, 1 build failed, ...), 75 = tnr busy or
# in a measurement window (try later), 9 = image or input missing.

set -u
B="${TNR_ROOT:?}"
KEY="${TNR_KEY:?}"
CPUSET="${TNR_CPUSET:-32-63,96-127,160-191,224-254}"
SLOTS="${TNR_SLOTS:-2}"
SLOT_WAIT="${TNR_SLOT_WAIT:-1800}"
MEM="${TNR_MEMORY:-96g}"
GUARD_JSON=/run/cmp-agent/guard.json

export XDG_CONFIG_HOME="$B/xdg/config" XDG_DATA_HOME="$B/xdg/data" XDG_CACHE_HOME="$B/xdg/cache"

log() { echo "tnr: $*" >&2; }

# ---- the host's freeze rule, read-only ---------------------------------------
barred() {
    # Prints the reason and returns 0 when agent work must not run now.
    if [ -e /run/cmp-measure-window ]; then echo "measurement window (/run/cmp-measure-window)"; return 0; fi
    if [ -e /run/cmp-agent-pause ]; then echo "operator pause (/run/cmp-agent-pause)"; return 0; fi
    if [ -r "$GUARD_JSON" ]; then
        local st
        st="$(python3 -c 'import json,sys,time
d=json.load(open(sys.argv[1]))
fresh = time.time() - float(d.get("updated", 0)) < 60
print("%s %s %s" % (d.get("state","?"), "fresh" if fresh else "stale", ",".join(d.get("reasons") or []) or "-"))' "$GUARD_JSON" 2>/dev/null)"
        case "$st" in
            "running "*|"") ;;
            *" fresh "*) echo "cmp-agent-guard: $st"; return 0 ;;
        esac
    fi
    return 1
}

if why="$(barred)"; then
    log "not starting: $why. tnr is measuring or serving is under stress; try later."
    exit 75
fi

# ---- inputs ------------------------------------------------------------------
TREE="$B/trees/$KEY"
BUILD="$B/builds/$KEY"
mkdir -p "$BUILD" "$B/ccache" "$B/locks" "$B/stubs"
if ! podman image exists "$TNR_IMAGE" 2>/dev/null; then
    log "image $TNR_IMAGE is missing: run scripts/remote/tnr_build.sh --build-image"
    exit 9
fi
# The image's identity scopes both caches. A rebuilt image (an added package)
# can change what a compile sees through __has_include without changing clang,
# and neither ccache nor ninja would notice: measured 2026-11-14, onetbb added to
# the image changed the PCH's dependency set while ninja called the old one
# current. So: one ccache dir per image, and a build dir built by another image
# is cleaned first (container_build.sh).
IMAGE_ID="$(podman image inspect --format '{{.Id}}' "$TNR_IMAGE" | cut -c1-12)"
CCACHE_HOST="$B/ccache/$IMAGE_ID"
mkdir -p "$CCACHE_HOST"

# Prune files the desk no longer has (the sync only adds and updates). The list
# is exactly what was sent: git's tracked + untracked-not-ignored files plus the
# worktree's top-level symlinks. Plain files only, by explicit name.
prune() {  # <dir> <list>
    local dir="$1" list="$2"
    [ -f "$list" ] || return 0
    ( cd "$dir" && comm -z -23 \
        <(find . \( -type f -o -type l \) -printf '%P\0' | sort -z) \
        <(sort -z "$list") ) |
    while IFS= read -r -d '' f; do rm -f -- "$dir/$f"; done
}
prune "$TREE/src" "$TREE/src.list"
prune "$TREE/engine" "$TREE/engine.list"

# Configure-time probes must see what the desk sees, or the tnr build dir would
# configure differently from the desk's (other gate set, other -D on
# test_gates.cpp). Real data when it has been copied here, else a stub file
# that satisfies the existence probe (builds never read assets).
pick() {  # <real dir> <stub dir> <probe relpath>
    if [ -e "$1/$3" ]; then echo "$1"; return; fi
    mkdir -p "$2/$(dirname "$3")" && : > "$2/$3"
    echo "$2"
}
MOUNTS=(
    -v "$TREE/src:$TNR_WT"
    -v "$BUILD:$TNR_WT/native/build"
    -v "$TREE/engine:$TNR_ENGINE"
    -v "$B/deps/dawn:$TNR_DAWN:ro"
)
# An empty TNR_ASSETS / TNR_MILO_LIB means the desk has no such data either, so
# nothing is mounted and the gate configures OFF on both sides.
if [ -n "${TNR_ASSETS:-}" ]; then
    MOUNTS+=(-v "$(pick "$B/assets/orig-assets" "$B/stubs/orig-assets" gen/main_xbox.hdr):$TNR_ASSETS:ro")
fi
if [ -n "${TNR_MILO_LIB:-}" ]; then
    MOUNTS+=(-v "$(pick "$B/assets/milo-lib-dc3" "$B/stubs/milo-lib-dc3" char/crowd/anim/gen/female_base.milo_xbox):$TNR_MILO_LIB:ro")
fi
[ -n "${TNR_IMGUI:-}" ] && MOUNTS+=(-v "$B/deps/imgui-src:$TNR_IMGUI:ro")
if [ -n "${TNR_CCACHE_BIN:-}" ]; then
    MOUNTS+=(-v "$B/bin/ccache:$TNR_CCACHE_BIN:ro" -v "$CCACHE_HOST:$TNR_CCACHE_DIR")
fi
if [ "${TNR_MODE:-build}" = test ] && [ -n "${TNR_ARCHIVE:-}" ] && [ -d "$B/assets/archive" ]; then
    MOUNTS+=(-v "$B/assets/archive:${TNR_ARCHIVE:?}:ro")
fi

# ---- one build per worktree, TNR_SLOTS host-wide ------------------------------
exec 8>"$B/locks/tree-$KEY.lock"
if ! flock -n 8; then
    log "another tnr build of this worktree is running; not queueing a second one"
    exit 75
fi
slot=""
deadline=$(( $(date +%s) + SLOT_WAIT ))
while :; do
    for i in $(seq 0 $((SLOTS - 1))); do
        exec 9>"$B/locks/slot-$i.lock"
        if flock -n 9; then slot=$i; break 2; fi
        exec 9>&-
    done
    if [ "$(date +%s)" -ge "$deadline" ]; then
        log "all $SLOTS build slots busy for ${SLOT_WAIT}s; giving up"
        exit 75
    fi
    [ -z "${_said_wait:-}" ] && { log "all $SLOTS build slots busy; waiting (max ${SLOT_WAIT}s)"; _said_wait=1; }
    sleep 10
done

NAME="dc3nb-$(printf '%s' "$KEY" | sha1sum | cut -c1-12)"
podman rm -f "$NAME" >/dev/null 2>&1   # a leftover from a dropped session (ours by name)

ENVS=(
    -e "TNR_JOBS=$TNR_JOBS" -e "TNR_CPUSET=$CPUSET" -e "TNR_MODE=${TNR_MODE:-build}"
    -e "TNR_RECONFIGURE=${TNR_RECONFIGURE:-0}" -e "TNR_TARGETS=${TNR_TARGETS:-}"
    -e "TNR_ENGINE=$TNR_ENGINE" -e "TNR_IMGUI=${TNR_IMGUI:-}"
    -e "TNR_CC=${TNR_CC:-}" -e "TNR_CXX=${TNR_CXX:-}"
    -e "DAWN_DIR=$TNR_DAWN/lib/cmake/Dawn"
    -e "MILO_CCACHE=${TNR_MILO_CCACHE:-}"
    -e "MILO_CCACHE_BIN=${TNR_CCACHE_BIN:-}" -e "MILO_CCACHE_DIR=${TNR_CCACHE_DIR:-}"
    -e "TNR_TEST_ARGS=${TNR_TEST_ARGS:-}" -e "TNR_IMAGE_ID=$IMAGE_ID"
)

log "slot $slot, container $NAME, -j$TNR_JOBS on CPUs $CPUSET, mem $MEM"
podman run --rm --name "$NAME" --network=none \
    --cpus "$TNR_JOBS" --memory "$MEM" --pids-limit 16384 \
    --security-opt label=disable \
    "${MOUNTS[@]}" "${ENVS[@]}" -w "$TNR_WT" \
    "$TNR_IMAGE" bash "$TNR_WT/scripts/remote/tnr/container_build.sh" &
pid=$!

# ---- follow the host's freeze rule while the build runs -----------------------
paused=0; clear_since=0; paused_s=0; t_pause=0
while kill -0 "$pid" 2>/dev/null; do
    if why="$(barred)"; then
        clear_since=0
        if [ "$paused" = 0 ] && podman pause "$NAME" >/dev/null 2>&1; then
            paused=1; t_pause=$(date +%s)
            log "PAUSED: $why (resumes 30 s after it clears)"
        fi
    elif [ "$paused" = 1 ]; then
        [ "$clear_since" = 0 ] && clear_since=$(date +%s)
        if [ $(( $(date +%s) - clear_since )) -ge 30 ]; then
            podman unpause "$NAME" >/dev/null 2>&1
            paused=0; paused_s=$(( paused_s + $(date +%s) - t_pause ))
            log "resumed after $(( $(date +%s) - t_pause )) s paused"
        fi
    fi
    sleep 5
done
wait "$pid"; rc=$?
[ "$paused_s" -gt 0 ] && log "total time paused for tnr's measurement windows: ${paused_s}s"
exit "$rc"
