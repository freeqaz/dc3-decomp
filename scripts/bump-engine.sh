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
echo "Remember to:"
echo "  1. Run  ninja -C native/build  (or equivalent) to rebuild with the new engine."
echo "  2. Commit the CMakeLists.txt change with a message like:"
echo "       chore(engine): bump MILO_ENGINE_PIN to ${NEW_SHA:0:7}"
