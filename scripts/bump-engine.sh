#!/usr/bin/env bash
# bump-engine.sh — update MILO_ENGINE_PIN in native/CMakeLists.txt to the
# current milo-native-engine HEAD commit.
#
# Usage:
#   scripts/bump-engine.sh              # dry-run: prints old->new SHAs
#   scripts/bump-engine.sh --apply      # writes native/CMakeLists.txt
#   scripts/bump-engine.sh --dry-run    # same as default (explicit)
#
# The script must run from the dc3-decomp repo root (or any worktree that has
# native/CMakeLists.txt).  The milo-native-engine repo is expected at the
# default sibling path (../../milo-native-engine relative to the dc3-decomp
# root, i.e. /home/free/code/milohax/milo-native-engine).
#
# After bumping, rebuild dc3-native to pick up the new engine state:
#   ninja -C native/build dc3-native milo-tests
# or use the native CMake build directly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_FILE="${REPO_ROOT}/native/CMakeLists.txt"

# ---- parse args ----
DRY_RUN=1
for arg in "$@"; do
    case "$arg" in
        --apply)     DRY_RUN=0 ;;
        --dry-run)   DRY_RUN=1 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | grep '^#' | sed 's/^# *//'
            exit 0 ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: $0 [--apply|--dry-run]" >&2
            exit 1 ;;
    esac
done

# ---- locate engine repo ----
# CMakeLists.txt sets MILO_ENGINE_PATH relative to CMAKE_SOURCE_DIR (native/),
# so its ../../milo-native-engine = repo_root/../milo-native-engine (a sibling).
ENGINE_DEFAULT="${REPO_ROOT}/../milo-native-engine"
if [ ! -d "${ENGINE_DEFAULT}/.git" ]; then
    # A git worktree lives at <...>/wt/<name>, so the sibling guess above lands
    # in the worktree pool, not next to the main checkout. `--git-common-dir`
    # resolves to <main-checkout>/.git from anywhere in the repo.
    _common="$(git -C "${REPO_ROOT}" rev-parse --path-format=absolute \
                   --git-common-dir 2>/dev/null || true)"
    if [ -n "${_common}" ]; then
        _main_checkout="$(dirname "${_common}")"
        _alt="$(dirname "${_main_checkout}")/milo-native-engine"
        [ -d "${_alt}/.git" ] && ENGINE_DEFAULT="${_alt}"
    fi
fi
ENGINE_PATH="${MILO_ENGINE_PATH:-${ENGINE_DEFAULT}}"
ENGINE_PATH="$(cd "${ENGINE_PATH}" 2>/dev/null && pwd)" || {
    echo "ERROR: milo-native-engine not found at ${ENGINE_DEFAULT}" >&2
    echo "       Set MILO_ENGINE_PATH to override." >&2
    exit 1
}
if [ ! -d "${ENGINE_PATH}/.git" ]; then
    echo "ERROR: ${ENGINE_PATH} is not a git repo." >&2
    exit 1
fi

# ---- read current HEAD ----
NEW_SHA="$(git -C "${ENGINE_PATH}" rev-parse HEAD)"
if [ -z "${NEW_SHA}" ]; then
    echo "ERROR: could not read engine HEAD" >&2
    exit 1
fi

# ---- read old PIN from CMakeLists.txt ----
if [ ! -f "${CMAKE_FILE}" ]; then
    echo "ERROR: ${CMAKE_FILE} not found" >&2
    exit 1
fi

OLD_SHA="$(grep -E 'set\(MILO_ENGINE_PIN ' "${CMAKE_FILE}" | \
           sed 's/.*set(MILO_ENGINE_PIN "\([^"]*\)".*/\1/')"
if [ -z "${OLD_SHA}" ]; then
    echo "ERROR: could not parse MILO_ENGINE_PIN from ${CMAKE_FILE}" >&2
    exit 1
fi

# ---- guard: the source pin must actually be able to take effect ----
# `set(MILO_ENGINE_PIN ... CACHE STRING ...)` WITHOUT `FORCE` is permanently
# shadowed by CMakeCache.txt, which made every previous run of this script a
# silent no-op against any existing build dir (toolchain audit, 2026-08-19).
# Refuse to write a value that cannot be read back, rather than reporting
# success for a change nobody will ever see.
PIN_SET_LINE="$(grep -n -A1 -E 'set\(MILO_ENGINE_PIN ' "${CMAKE_FILE}" | tr '\n' ' ')"
case "${PIN_SET_LINE}" in
    *CACHE*FORCE*) ;;
    *CACHE*)
        echo "ERROR: MILO_ENGINE_PIN is set with CACHE but without FORCE in" >&2
        echo "       ${CMAKE_FILE}." >&2
        echo "       CMakeCache.txt would permanently shadow the source value," >&2
        echo "       so bumping the pin here would have no effect. Restore FORCE" >&2
        echo "       (see the comment above the set() call) and re-run." >&2
        exit 1 ;;
    *) ;;   # not a cache variable at all -- source always wins, fine
esac

# ---- report ----
echo "milo-native-engine: ${ENGINE_PATH}"
echo ""
echo "  Old MILO_ENGINE_PIN: ${OLD_SHA}"
echo "  New MILO_ENGINE_PIN: ${NEW_SHA}"
echo ""

if [ "${OLD_SHA}" = "${NEW_SHA}" ]; then
    echo "Already up to date — no change needed."
    exit 0
fi

if [ "${DRY_RUN}" = "1" ]; then
    echo "(dry-run) Would update ${CMAKE_FILE}"
    echo "          Run with --apply to write the change."
    exit 0
fi

# ---- apply: update the PIN in CMakeLists.txt ----
# Use a temp file + mv to avoid partial writes.
TMP_FILE="$(mktemp)"
trap 'rm -f "${TMP_FILE}"' EXIT

sed "s|set(MILO_ENGINE_PIN \"${OLD_SHA}\"|set(MILO_ENGINE_PIN \"${NEW_SHA}\"|" \
    "${CMAKE_FILE}" > "${TMP_FILE}"

# Verify the replacement landed.
UPDATED_SHA="$(grep -E 'set\(MILO_ENGINE_PIN ' "${TMP_FILE}" | \
               sed 's/.*set(MILO_ENGINE_PIN "\([^"]*\)".*/\1/')"
if [ "${UPDATED_SHA}" != "${NEW_SHA}" ]; then
    echo "ERROR: sed replacement did not land correctly (got: ${UPDATED_SHA})" >&2
    exit 1
fi

mv "${TMP_FILE}" "${CMAKE_FILE}"
echo "Updated ${CMAKE_FILE}"
echo ""

# ---- report build dirs whose cached pin still disagrees ----
# With FORCE these are overwritten on the next configure, but listing them makes
# the "N different pin values are live at once" state visible instead of latent.
STALE=0
for cache in "${REPO_ROOT}"/native/build*/CMakeCache.txt; do
    [ -f "${cache}" ] || continue
    cached="$(sed -n 's/^MILO_ENGINE_PIN:STRING=//p' "${cache}")"
    [ -n "${cached}" ] || continue
    if [ "${cached}" != "${NEW_SHA}" ]; then
        if [ "${STALE}" = "0" ]; then
            echo "Build dirs whose cached pin still disagrees (FORCE rewrites these"
            echo "on the next configure -- listed so the drift is visible):"
        fi
        echo "  ${cached}  <-  $(dirname "${cache}")"
        STALE=1
    fi
done
[ "${STALE}" = "1" ] && echo ""

echo "Remember to:"
echo "  1. Run  ninja -C native/build  (or equivalent) to rebuild with the new engine."
echo "  2. Commit the CMakeLists.txt change with a message like:"
echo "       chore(engine): bump MILO_ENGINE_PIN to ${NEW_SHA:0:7}"
