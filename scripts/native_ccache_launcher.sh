#!/usr/bin/env bash
# CMAKE_<LANG>_COMPILER_LAUNCHER for the native build when ccache is on.
# Set up by scripts/native_configure.sh; see docs/native/BUILD_CACHE.md.
#
#   native_ccache_launcher.sh <ccache> [KEY=VALUE ...] <compiler> <args...>
#
# Every ordinary compile is handed to ccache unchanged (exec, no overhead
# beyond this shell). The one exception is a PRECOMPILED HEADER build
# (`-Xclang -emit-pch`, which is how CMake builds cmake_pch.hxx.pch with clang):
#
#   * It runs the compiler DIRECTLY, never through ccache. A .pch records the
#     absolute paths of every header in it. Fetched from the cache, it would be
#     another worktree's PCH, naming that worktree's headers: clang would then
#     validate (and, for #pragma once headers, identify) files in a tree that
#     may since have changed or been deleted. A PCH is ~4 compiles per tree;
#     not caching them costs seconds.
#   * After a successful build it writes <pch>.sum, a digest of what the PCH
#     is MADE OF (compiler args and the content of every header it read, with
#     this tree's root replaced by a placeholder), and ccache runs with
#     pch_external_checksum=true, so a TU that uses the PCH is keyed on that
#     digest instead of on the PCH's bytes. Two worktrees with identical
#     headers get identical digests, so their PCH users share cache entries.
#     The .sum is deleted BEFORE the compile and written only after it
#     succeeds, inside the same ninja edge, so a .sum can never describe an
#     older PCH than the one next to it. A missing .sum is safe: ccache then
#     hashes the .pch itself (a guaranteed cross-tree miss, never a wrong hit).
#
# CCACHE_DISABLE=1 bypasses ccache for one build without reconfiguring.

set -u
if [ $# -lt 2 ]; then
    echo "native_ccache_launcher.sh: usage: <ccache> [KEY=VALUE...] <compiler> <args...>" >&2
    exit 2
fi

is_pch=0
prev=""
for a in "$@"; do
    if [ "$prev" = "-Xclang" ] && [ "$a" = "-emit-pch" ]; then is_pch=1; break; fi
    prev="$a"
done

if [ "$is_pch" = 0 ]; then
    exec "$@"
fi

# Drop "<ccache> KEY=VALUE..." to reach the compiler.
shift
while [ $# -gt 0 ]; do
    case "$1" in
        *=*) case "$1" in -*) break ;; esac; shift ;;
        *) break ;;
    esac
done

out=""
dep=""
prev=""
for a in "$@"; do
    [ "$prev" = "-o" ] && out="$a"
    [ "$prev" = "-MF" ] && dep="$a"
    prev="$a"
done

[ -n "$out" ] && rm -f "$out.sum"
"$@"
rc=$?
if [ "$rc" = 0 ] && [ -n "$out" ] && [ -n "$dep" ]; then
    tree="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
    # Failure to write the digest must not fail the build: without it ccache
    # simply hashes the .pch (correct, just no cross-tree sharing).
    python3 "$tree/scripts/native_pch_sum.py" --tree "$tree" --depfile "$dep" \
        --out "$out.sum" -- "$@" \
        || echo "native_ccache_launcher.sh: warning: no $out.sum written" >&2
fi
exit "$rc"
