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
# Prefer the dtk recorded in the main repo's build.ninja over the in-repo
# build/tools/dtk. dtk decides function boundaries from config/*/symbols.txt,
# so a worktree that splits with a different dtk than main produces a report
# that differs from main's for reasons that have nothing to do with the code
# under test -- i.e. phantom regressions in any worktree-vs-main comparison.
# (Observed 2026-08-04: build/tools/dtk was a Feb build while main used
# ../jeff/target/release/dtk, which split Curl_raw_toupper differently.)
# Same reasoning, and same fallback, as WIBO below.
DTK="$(sed -n '/^rule split$/,/^ *description/p' "$MAIN_REPO/build.ninja" 2>/dev/null \
    | tr '\n' ' ' | grep -oE '[^ $]+dtk xex split' | head -n1 | sed 's/ xex split$//')"
case "$DTK" in
    "") ;;
    /*) ;;
    *) DTK="$(cd "$MAIN_REPO" && realpath -e "$DTK" 2>/dev/null || echo "")" ;;
esac
if [ -z "$DTK" ] || [ ! -x "$DTK" ]; then
    DTK="$MAIN_REPO/build/tools/dtk"
fi
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

# ---- reflink helpers ---------------------------------------------------------
# Reflink-copy a directory tree (CoW). Falls back to a normal copy if the
# filesystem doesn't support reflinks (cp --reflink=auto handles that
# transparently). Retries transient failures: when $src is a shared build dir
# being written by concurrent builds, `cp -a` can abort with "file changed as
# we read it" or a vanished temp .obj. A retry after a short pause usually
# lands in a quiet window.
reflink_dir() {
    local src="$1" dst="$2" tries="${3:-4}" i
    mkdir -p "$(dirname "$dst")"
    for ((i=1; i<=tries; i++)); do
        rm -rf "$dst"
        if cp -a --reflink=auto "$src" "$dst" 2>/dev/null; then
            return 0
        fi
        sleep $((i))
    done
    return 1
}

# Best-effort reflink for a REGENERABLE cache (the build dir): tolerate a
# partial copy — a few temp objs that vanished mid-copy under concurrent
# writes just get recompiled by ninja. Fails only if the copy produced nothing.
reflink_dir_besteffort() {
    local src="$1" dst="$2" i
    mkdir -p "$(dirname "$dst")"
    rm -rf "$dst"
    for i in 1 2 3 4; do
        if cp -a --reflink=auto "$src" "$dst" 2>/dev/null; then return 0; fi
        # partial copy left in place is fine; retry to fill in more, then accept
        sleep "$i"
    done
    [ -d "$dst" ] && return 0
    return 1
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

# ---- orig-assets/ : symlink (huge, read-only, and the native gate needs it) --
# Without this the native test suite cannot open gen/main_xbox.hdr and
# DirLoaderTest aborts the whole binary before any test body runs. That has been
# written off in more than one lane as "a worktree asset-availability artifact,
# not a regression" -- true, but it silently removed the tests that actually
# exercise loading from every worktree gate. With the symlink the suite runs
# whole: 441 tests, 357 passed, 84 skipped, 0 failed.
if [ -e "$MAIN_REPO/orig-assets" ] && [ ! -e "$WORKTREE_PATH/orig-assets" ]; then
    echo "==> orig-assets/  (symlink — extracted game assets, read-only)"
    ln -s "$MAIN_REPO/orig-assets" "$WORKTREE_PATH/orig-assets"
fi

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
    # Bring main's object cache up to date BEFORE snapshotting it. Landings
    # advance main's HEAD but do NOT rebuild, so the shared cache goes stale
    # and every worktree would otherwise recompile the newly-landed TUs
    # (cold-cache contention — builds crawl). `ninja all_source` builds the
    # objects only (skips the slow report/progress tail) and is a NO-OP once
    # main is current — so only the FIRST creation after a landing pays the
    # small incremental rebuild and every other worktree reflinks an
    # already-warm cache for free. flock serializes concurrent worktree
    # creations against each other. Non-fatal: main may be mid-repair (a
    # common reason to spin up a worktree).
    echo "==> Refreshing main's object cache (amortized; no-op if already current)"
    ( cd "$MAIN_REPO" && mkdir -p build && flock build/.worktree-warm.lock ninja all_source ) >/dev/null 2>&1 \
        || echo "  WARN: main cache refresh failed (non-fatal; worktree will rebuild what's stale)" >&2

    echo "==> build/$VERSION/  (reflink copy — private build dir + WARM object cache)"
    reflink_dir_besteffort "$MAIN_REPO/build/$VERSION" "$WT_BUILD"
    # Critical build inputs must survive the (possibly partial) copy. If a
    # temp obj vanished mid-copy that's fine (ninja recompiles), but obj/
    # (target split objects) and config.json are required — reflink them
    # individually if the best-effort pass dropped them.
    [ -d "$WT_BUILD/obj" ] || reflink_dir "$MAIN_REPO/build/$VERSION/obj" "$WT_BUILD/obj"
    [ -f "$WT_BUILD/config.json" ] || cp --reflink=auto "$MAIN_REPO/build/$VERSION/config.json" "$WT_BUILD/config.json" 2>/dev/null || true
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

# ---- warm-cache validation : make the reflinked object cache VALID under ninja
# `git worktree add` stamps every checked-out source with a fresh (now) mtime,
# and the toolchain symlinks (build/compilers, build/tools) resolve to main's
# dirs — every compile edge here also takes tools/transform_dep.py and the PCH
# as implicit inputs. Ninja is mtime-only (no content hashing), so it treats
# every reflinked object as stale vs those now-stamped inputs and recompiles —
# N worktrees per wave => N redundant full rebuilds + machine saturation.
#
# A fresh worktree is byte-identical to its base ($BASE_REF) — that's exactly
# what produced the reflinked objects — so the whole reflinked cache IS
# current. Old-stamp every tracked source and bump every build output so ninja
# sees the cache as up-to-date by MTIME; a later source EDIT gets a newer
# mtime and rebuilds normally. If the worktree DIFFERS from its base (branch
# reuse, or main has local mods to build inputs) we skip this and let ninja
# rebuild — correctness over speed.
#
# NB: this makes INCREMENTAL (single-obj) builds warm — the common agent case.
# A full `ninja` still recompiles once per worktree: the compile rules use
# `deps = msvc` (ninja's binary deps DB, .ninja_deps at the repo root) which a
# fresh worktree never inherits, so a full build sees "deps missing" on every
# obj. See rb3-xenon docs/decomp/handoff/worktree-build-tooling-findings-2026-07-01.md
# for the measured mechanism.
if [ "$WARM_CACHE" -eq 1 ]; then
    # Only BUILD INPUTS matter for cache validity — a compiled source/header
    # (src/) or a split/objects config (config/). Dirty scripts, docs, or
    # tooling in main don't make any reflinked object stale, so exclude them.
    # NB: `grep -c` exits 1 when the count is 0 — the `|| true` keeps `set -e`
    # from aborting the whole script when main is clean.
    # The reflinked objects were produced by MAIN at MAIN's HEAD. So the cache
    # is only current for a worktree whose base is that same commit -- comparing
    # the worktree against its OWN base ref is trivially zero and proves nothing
    # (it is the same tree by construction). Basing a worktree on an older ref
    # (a pinned eval substrate, a bisect point) and marking main's newer objects
    # "current" makes ninja skip every stale unit SILENTLY: a scoring run then
    # grades against wrong-warm baselines and reports green. Measured 2026-08-05
    # on a worktree pinned 286 commits behind main -- 7 of the 123 source files
    # a 225-function eval roster spans had changed, and `ninja` said "no work to
    # do". Include main's HEAD-vs-base delta in the same count.
    _changed="$( { git -C "$MAIN_REPO" diff --name-only 2>/dev/null;
                   git -C "$MAIN_REPO" diff --name-only --cached 2>/dev/null;
                   git -C "$WORKTREE_PATH" diff --name-only "$BASE_REF" 2>/dev/null;
                   git -C "$MAIN_REPO" diff --name-only "$BASE_REF" HEAD 2>/dev/null; } \
                 | grep -cE '^(src/|config/)' || true )"
    if [ "$_changed" -eq 0 ]; then
        echo "==> Validating warm object cache (worktree == $BASE_REF; marking outputs current)"
        # Old-stamp every tracked source (a fixed old timestamp, not main's,
        # which may itself be recent) so no tracked input — including
        # tools/download_tool.py + tools/transform_dep.py, implicit inputs to
        # the toolchain/compile edges — is ever newer than the reflinked
        # outputs. A later source EDIT still gets a fresh mtime and rebuilds
        # normally.
        # NB: run FROM the worktree — `git ls-files` prints worktree-relative
        # paths, so touch must resolve them against the worktree, not the
        # setup script's CWD, or it silently touches main's copies instead.
        ( cd "$WORKTREE_PATH" && git ls-files -z 2>/dev/null \
            | xargs -0 -r touch -h -d '2020-01-01' 2>/dev/null ) || true
        find "$WT_BUILD" -type f -exec touch {} + 2>/dev/null || true
        echo "  reflinked cache marked current — incremental (single-obj) builds are warm"
    else
        echo "==> Warm cache NOT validated: worktree differs from $BASE_REF ($_changed path(s)); first build will rebuild"
    fi
fi

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

# ---- unicorn runner's C trampoline hook
# The .so is gitignored, so a fresh worktree has none and the runner silently
# falls back to its slower Python hook. That fallback is not merely slower --
# it used to compute call-site offsets wrongly, which made the wrong-callee
# check's dict lookup miss every time and dropped its warnings entirely, so
# the same sabotage produced different output in a worktree than in the main
# repo. Link the main repo's build if there is one, else build it; either way,
# never leave the worktree on a silently different code path.
UNICORN_SO=$(ls "$MAIN_REPO"/scripts/unicorn_runner/_trampoline_hook*.so 2>/dev/null | head -1)
if [ -n "$UNICORN_SO" ]; then
    echo "==> Symlinking unicorn trampoline hook ($(basename "$UNICORN_SO"))"
    ln -sfn "$UNICORN_SO" "$WORKTREE_PATH/scripts/unicorn_runner/$(basename "$UNICORN_SO")"
elif command -v gcc >/dev/null 2>&1; then
    echo "==> Building unicorn trampoline hook"
    make -s -C "$WORKTREE_PATH/scripts/unicorn_runner" 2>/dev/null \
        || echo "    (build failed — unicorn runner will use the Python fallback)"
else
    echo "==> No unicorn trampoline hook and no gcc; runner will use the Python fallback"
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

# ---- prune zero-byte orphan .obj files --------------------------------------
# The main build dir can accumulate zero-byte orphan objects with no ninja rule
# (e.g. build/$VERSION/src/system/utl/StreamRecorder.obj — the real source is
# system/gesture/StreamRecorder.cpp). Reflinking them into the worktree breaks
# the post-build .obj patchers (obj_dynamic_init_patcher.py et al. tried to
# struct.unpack_from a COFF header out of an empty buffer). The patchers now
# skip short/empty files defensively, but deleting the orphans here keeps the
# worktree clean and avoids the warning spam.
if [ -d "$WT_BUILD/src" ]; then
    pruned=$(find "$WT_BUILD/src" -name '*.obj' -type f -size 0 -print -delete 2>/dev/null | wc -l)
    [ "$pruned" -gt 0 ] && echo "==> Pruned $pruned zero-byte orphan .obj file(s)"
fi

# ---- safety assertion : configure.py must have produced build.ninja ---------
# Any silent configure failure that leaves no build.ninja produces an
# UNBUILDABLE worktree: every build dies with `ninja: error: loading
# 'build.ninja'` and stale reports read as false results. Fail LOUD here
# instead of downstream.
if [ ! -f "$WORKTREE_PATH/build.ninja" ]; then
    echo "FATAL: configure.py did not produce $WORKTREE_PATH/build.ninja." >&2
    echo "       The worktree is unbuildable; refusing to hand back a broken tree." >&2
    exit 1
fi

# ---- prime ninja state : settle SPLIT + the configure generator edge --------
# Without this, the worktree's first `ninja -t commands <obj>` query (used by
# the permuter, MCP orchestrator, and objdiff scripts) can return commands
# derived from a not-yet-fully-consistent build.ninja, leading to baseline
# match% returning 0.00% on the first invocation of every function in the
# unit.
#
# CRITICAL — prime the `config.json` target, NOT the bare-ninja default.
# Bare `ninja` builds the full default target (all objs + report). A fresh
# worktree has no `.ninja_deps` (it lives at the repo root, never inherited),
# and the compile rules use `deps = msvc`, so a full build sees "deps missing"
# on EVERY obj and recompiles all of them — a full-rebuild tax on every
# worktree creation. Building `config.json` alone performs the SPLIT +
# graph-settle (the actual determinism goal) with ZERO obj compiles; the warm
# validated cache then serves single-obj agent builds incrementally. Same fix
# as rb3-xenon (docs/decomp/handoff/worktree-build-tooling-findings-2026-07-01.md
# there). A later full `ninja` (if a full report is needed) still pays the
# one-time full rebuild — do that serially in ONE worktree, not per-lane.
#
# (The historical `code=287` failures under heavy concurrent ninja load were a
# wibo bug, fixed upstream in wibo commit 6a7c37e.)
#
# A failed prime is a loud WARNING, not fatal: the main tree is frequently
# mid-repair (often the very reason to spin up a worktree), so a prime failure
# must not block worktree creation. `WT_SKIP_PRIME=1` skips it entirely.
if [ "${WT_SKIP_PRIME:-0}" -eq 1 ]; then
    echo "==> WT_SKIP_PRIME=1 : skipping ninja prime (worktree configured but not primed)"
else
echo "==> Priming ninja state (scoped to config.json — no object builds)"
(
    cd "$WORKTREE_PATH"
    prime_log="$(mktemp)"
    if ninja "build/$VERSION/config.json" >"$prime_log" 2>&1; then
        tail -5 "$prime_log"
    else
        echo "WARN: ninja prime failed (the main tree may not build yet)." >&2
        echo "      The worktree is still configured and usable; fix the build inside it." >&2
        echo "      ---- prime output ----" >&2
        cat "$prime_log" >&2
        echo "      ----------------------" >&2
    fi
    rm -f "$prime_log"
)
fi

# ---------------------------------------------------------------------------
# decomp.db tripwire
# ---------------------------------------------------------------------------
# A worktree must never grow its own decomp.db. Running `ninja` here used to
# create one -- every row, ZERO verdicts and ZERO percentages -- and every
# analysis script that defaults to `--db decomp.db` then answered out of it.
# Measured 2026-08-19, identical queries: AT_LIMIT certs 0 vs 3,796 in main;
# near-misses 0 vs 89; the 80-95 band 0 vs 325. An empty result set reads as
# "this class is exhausted", which is the one failure mode this project has a
# standing rule against.
#
# scripts/orchestrator/database.py now refuses to open a worktree-local
# decomp.db, but ~50 scripts call sqlite3.connect() directly and would sail
# straight past a Python-side guard. So plant a file that is deliberately NOT a
# valid SQLite database: any reader, in any language, gets
# "file is not a database" on its first query instead of a plausible answer,
# and `cat decomp.db` explains why.
#
# NB: $MAIN_REPO is wherever this script was invoked from, which may itself be a
# worktree. The canonical decomp.db lives in the MAIN checkout, so resolve that
# separately -- `--git-common-dir` is <main>/.git from anywhere in the repo.
CANON_REPO="$MAIN_REPO"
_common="$(git -C "$MAIN_REPO" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
if [ -n "$_common" ]; then
    CANON_REPO="$(dirname "$_common")"
fi
if [ ! -e "$WORKTREE_PATH/decomp.db" ]; then
    cat > "$WORKTREE_PATH/decomp.db" <<TRIPWIRE
This is NOT a database. It is a tripwire, planted by scripts/setup_worktree.sh.

A worktree-local decomp.db carries every row and no judgement: 0 verdicts, 0
percentages. Work queries against it return nothing, which reads exactly like
"this class is exhausted" -- so it answers plausibly and wrongly. Failing loudly
is the point of this file.

The real database is:   $CANON_REPO/decomp.db

Pass it explicitly:     --db $CANON_REPO/decomp.db
                        db_path="$CANON_REPO/decomp.db"

The MCP orchestrator tools are unaffected -- they resolve decomp.db against the
server's own project root, not your cwd. Keep using project_dir="$WORKTREE_PATH".

If you genuinely want a throwaway per-worktree database, delete this file and
set DC3_ALLOW_SHADOW_DB=1.
TRIPWIRE
    echo "==> decomp.db tripwire planted (reads fail loudly; real DB stays in $CANON_REPO)"
fi

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
