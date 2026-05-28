#!/bin/bash
#
# setup_worktree.sh — create a buildable + diffable git worktree, cheaply (CoW).
#
# A naive `git worktree add` produces an UNBUILDABLE tree here: the big build
# inputs/outputs are gitignored (build/, orig/*, build.ninja, objdiff.json),
# so a fresh worktree has no target binary, no toolchain, no generated
# build.ninja, and a cold object cache. This script fixes that in seconds using
# btrfs/xfs copy-on-write reflinks (with graceful fall-back to full copies on
# non-CoW filesystems like tmpfs/ext4).
#
# Usage:
#   scripts/setup_worktree.sh [path] [branch-name] [base-ref] [--cold-cache]
#
# Arguments:
#   path       - Where to create the worktree (default: .claude/worktrees/wt-<timestamp>)
#   branch     - Branch name for the worktree (default: wt-<basename of path>)
#   base-ref   - Git ref to branch from (default: current HEAD)
#   --cold-cache - Do NOT warm-start the object cache. Use for a guaranteed-clean A/B
#                  test or if a warm cache triggers a full rebuild on your setup.
#
# Examples:
#   scripts/setup_worktree.sh /tmp/claude/my-feature my-feature
#   scripts/setup_worktree.sh /tmp/claude/test test-branch dev
#   scripts/setup_worktree.sh                                # auto-generates path/branch
#   scripts/setup_worktree.sh .claude/worktrees/perf perf --cold-cache
#
# What gets shared, and WHY symlink vs reflink-copy per directory
# ----------------------------------------------------------------
# The rule: anything the BUILD WRITES TO must be a real (reflinked) copy, never
# a symlink into the main tree — a symlink would let this worktree's build
# corrupt the shared main build dir (catastrophic with the permuter fleet
# running). Anything only READ can be a symlink (cheapest).
#
#   orig/                    reflink copy   read-only, but reflink is free on
#                                           CoW and avoids any symlink edge case
#   build/compilers/         symlink        read-only toolchain
#   build/tools/             symlink        read-only toolchain (dtk, wibo,
#                                           objdiff-cli)
#   build/373307D9/          reflink copy   THE build dir. The `split` rule
#                                           regenerates config.json + obj/ INTO
#                                           this dir, and every compiled .obj
#                                           lands in src/ here. Must be a private
#                                           real copy. Reflinking it also
#                                           warm-starts the object cache for
#                                           fast incremental builds.
#
# After setup:
#   cd <worktree>
#   ninja build/373307D9/src/system/flow/FlowCommand.obj
#   bin/objdiff-cli diff -u <unit> <symbol> --format json-pretty -o /dev/stdout
#
# Or via the MCP orchestrator:
#   run_objdiff(symbol, project_dir="<worktree>")
#
# Prerequisite: the main repo must have been built once (build/tools/dtk,
# build/tools/wibo, build/compilers/ populated by configure.py's download step).

set -euo pipefail

MAIN_REPO="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="373307D9"

# ---- args (DC3 keeps positional path / branch / base-ref + --cold-cache flag)
POSITIONAL=()
WARM_CACHE=1
for arg in "$@"; do
    case "$arg" in
        --cold-cache) WARM_CACHE=0 ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done

WORKTREE_PATH="${POSITIONAL[0]:-$MAIN_REPO/.claude/worktrees/wt-$(date +%s)}"
BRANCH="${POSITIONAL[1]:-wt-$(basename "$WORKTREE_PATH")}"
BASE_REF="${POSITIONAL[2]:-HEAD}"

# Resolve the base ref to a concrete commit for clarity
BASE_COMMIT="$(git -C "$MAIN_REPO" rev-parse --short "$BASE_REF" 2>/dev/null)" || {
    echo "ERROR: Cannot resolve ref '$BASE_REF'" >&2
    exit 1
}
BASE_BRANCH="$(git -C "$MAIN_REPO" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "detached")"

# ---- tool sanity (everything lives under MAIN_REPO) -------------------------
DTK="$MAIN_REPO/build/tools/dtk"
# Prefer the wibo recorded in the main repo's build.ninja (typically a newer
# build at a sibling path) over the in-repo build/tools/wibo, which can be
# stale and lack inline-env-var support. Fall back to the in-repo binary.
WIBO="$(grep -oE '/(home|usr|opt|root|var|mnt)/[^ "]*/wibo/build/release/wibo' "$MAIN_REPO/build.ninja" 2>/dev/null | head -n1)"
if [ -z "$WIBO" ] || [ ! -x "$WIBO" ]; then
    WIBO="$MAIN_REPO/build/tools/wibo"
fi
COMPILERS="$MAIN_REPO/build/compilers"
# Resolve objdiff-cli to a real absolute path (it's typically a symlink chain).
OBJDIFF="$(readlink -f "$MAIN_REPO/bin/objdiff-cli" 2>/dev/null || echo "$MAIN_REPO/bin/objdiff-cli")"
for t in "$DTK" "$WIBO" "$OBJDIFF"; do
    [ -e "$t" ] || {
        echo "ERROR: required tool missing: $t" >&2
        echo "  (run a full 'ninja' in the main repo at least once first)" >&2
        exit 1
    }
done
[ -d "$COMPILERS" ] || { echo "ERROR: compilers dir missing: $COMPILERS" >&2; exit 1; }

# ---- reflink helper ---------------------------------------------------------
# Reflink-copy a directory tree (CoW). Falls back to a normal copy if the
# filesystem doesn't support reflinks (cp --reflink=auto handles that
# transparently), but we warn so the operator knows the "instant + free"
# property was lost.
reflink_dir() {
    local src="$1" dst="$2"
    rm -rf "$dst"
    mkdir -p "$(dirname "$dst")"
    cp -a --reflink=auto "$src" "$dst"
}

# Warn if the destination isn't on a reflink-capable fs (script still works,
# just slow because cp falls back to full copies).
DEST_FSTYPE="$(findmnt -no FSTYPE --target "$(dirname "$WORKTREE_PATH")" 2>/dev/null || echo unknown)"
case "$DEST_FSTYPE" in
    btrfs|xfs|zfs) : ;;
    *) echo "WARN: $(dirname "$WORKTREE_PATH") is on '$DEST_FSTYPE'; reflinks may be unavailable — copies will be full (slow, space-hungry)." >&2 ;;
esac

# ---- worktree (idempotent) --------------------------------------------------
if [ -e "$WORKTREE_PATH/.git" ]; then
    echo "==> Worktree already exists at $WORKTREE_PATH (reconfiguring in place)"
else
    echo "==> Creating worktree at $WORKTREE_PATH"
    echo "    branch=$BRANCH  base=$BASE_REF ($BASE_COMMIT, on $BASE_BRANCH)"
    if git -C "$MAIN_REPO" show-ref --verify --quiet "refs/heads/$BRANCH"; then
        git -C "$MAIN_REPO" worktree add "$WORKTREE_PATH" "$BRANCH"
    else
        git -C "$MAIN_REPO" worktree add "$WORKTREE_PATH" -b "$BRANCH" "$BASE_REF"
    fi
fi

# ---- orig/ : reflink copy (read-only, but free on CoW) ----------------------
echo "==> orig/  (reflink copy — target binaries)"
reflink_dir "$MAIN_REPO/orig" "$WORKTREE_PATH/orig"

# ---- build/compilers, build/tools : symlinks (read-only toolchain) ----------
mkdir -p "$WORKTREE_PATH/build"
for d in compilers tools binutils; do
    if [ -e "$MAIN_REPO/build/$d" ]; then
        echo "==> build/$d  (symlink — read-only toolchain)"
        rm -rf "$WORKTREE_PATH/build/$d"
        ln -s "$MAIN_REPO/build/$d" "$WORKTREE_PATH/build/$d"
    fi
done

# ---- build/<VERSION>/ : reflink copy (build WRITES here; warm cache) --------
WT_BUILD="$WORKTREE_PATH/build/$VERSION"
if [ "$WARM_CACHE" -eq 1 ]; then
    echo "==> build/$VERSION/  (reflink copy — private build dir + WARM object cache)"
    reflink_dir "$MAIN_REPO/build/$VERSION" "$WT_BUILD"
else
    echo "==> build/$VERSION/  (cold: copying only obj/ + config.json, no object cache)"
    rm -rf "$WT_BUILD"
    mkdir -p "$WT_BUILD"
    # obj/ (target objects from split) and config.json are inputs the build
    # needs even with a cold src/ cache. Reflink them so split/diff work.
    [ -d "$MAIN_REPO/build/$VERSION/obj" ] && reflink_dir "$MAIN_REPO/build/$VERSION/obj" "$WT_BUILD/obj"
    [ -f "$MAIN_REPO/build/$VERSION/config.json" ] && cp --reflink=auto "$MAIN_REPO/build/$VERSION/config.json" "$WT_BUILD/config.json"
fi

# Drop stale ninja state copied from main (own lock + logs per build dir).
rm -f "$WORKTREE_PATH/.ninja_log" "$WORKTREE_PATH/.ninja_deps" \
      "$WORKTREE_PATH/.ninja_lock" "$WORKTREE_PATH/.ninja-build.lock" 2>/dev/null || true

# ---- clangd config + bin/objdiff-cli + venv : read-only symlinks ------------
echo "==> Symlinking clangd config"
[ -e "$MAIN_REPO/compile_commands.json" ] && ln -sfn "$MAIN_REPO/compile_commands.json" "$WORKTREE_PATH/compile_commands.json"
[ -e "$MAIN_REPO/.clangd" ] && ln -sfn "$MAIN_REPO/.clangd" "$WORKTREE_PATH/.clangd"

echo "==> Symlinking bin/objdiff-cli"
mkdir -p "$WORKTREE_PATH/bin"
ln -sfn "$MAIN_REPO/bin/objdiff-cli" "$WORKTREE_PATH/bin/objdiff-cli"

if [ -d "$MAIN_REPO/venv" ]; then
    echo "==> Symlinking Python venv"
    ln -sfn "$MAIN_REPO/venv" "$WORKTREE_PATH/venv"
fi

# ---- configure.py : bake absolute tool paths into this worktree's build.ninja
echo "==> Running configure.py with absolute tool paths"
(
    cd "$WORKTREE_PATH"
    python3 configure.py \
        --dtk "$DTK" \
        --objdiff "$OBJDIFF" \
        --wibo "$WIBO"
)

# ---- safety assertion : worktree build/ must be its own real dir ------------
if [ -L "$WT_BUILD" ]; then
    echo "FATAL: $WT_BUILD is a symlink — the build would corrupt the main tree. Aborting." >&2
    exit 1
fi

# ---- prime ninja state : trigger SPLIT + configure.py regeneration ----------
# Without this, the worktree's first `ninja -t commands <obj>` query (used by
# the permuter, MCP orchestrator, and objdiff scripts) can return commands
# derived from a not-yet-fully-consistent build.ninja, leading to baseline
# match% returning 0.00% on the first invocation of every function in the
# unit. Running ninja once here re-runs SPLIT (regenerates config.json from
# config.yml) and the configure.py edge inside build.ninja, leaving the build
# graph fully consistent. With the warm reflinked object cache, this is a
# no-op rebuild (touches no .obj files) but updates `.ninja_log` and
# `.ninja_deps` so subsequent queries are deterministic.
echo "==> Priming ninja state (regenerates config.json + warms .ninja_log)"
(
    cd "$WORKTREE_PATH"
    # The historical `code=287` failures under heavy concurrent ninja load
    # were a wibo bug: `resolveCaseInsensitive` discarded parent-directory
    # case fixes when the leaf file did not yet exist, so cl.exe wrote the
    # PCH/.obj at one casing and then failed to reopen it. Fixed upstream
    # in wibo commit 6a7c37e ("files: fix case-insensitive path resolution
    # for not-yet-created leaves"). One unconditional prime is now enough;
    # if it fails the error is real and worth surfacing.
    prime_log="$(mktemp)"
    if ninja >"$prime_log" 2>&1; then
        tail -5 "$prime_log"
        rm -f "$prime_log"
        exit 0
    fi
    echo "FATAL: ninja prime failed — full output:" >&2
    cat "$prime_log" >&2
    rm -f "$prime_log"
    exit 1
)

echo ""
echo "Worktree ready:  $WORKTREE_PATH"
echo "  branch:        $BRANCH  (from $BASE_COMMIT on $BASE_BRANCH)"
echo ""
echo "Next:"
echo "  cd $WORKTREE_PATH"
echo "  ninja build/$VERSION/src/<File>.obj   # warm cache = fast"
echo ""
echo "Usage with MCP orchestrator:"
echo "  run_objdiff(symbol, project_dir=\"$WORKTREE_PATH\")"
echo ""
echo "Remove when done:  git -C $MAIN_REPO worktree remove --force $WORKTREE_PATH"
