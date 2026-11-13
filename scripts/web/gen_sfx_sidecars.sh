#!/usr/bin/env bash
# gen_sfx_sidecars.sh -- give the web build an .ogg sidecar for EVERY kXMA
# sample the game can load, derived from the asset tree (PLAN W-7).
#
# The web build has no FFmpeg, so SampleData::Load swaps each kXMA sample for
# sfx/gen/xma_pcm/<key>.ogg (native/src/platform/XmaPcmSidecar.h). Those used to
# exist only for samples some desktop run had decoded, so a full song 404'd on
# ~20 (tutorial VO, xptoaster, campaign scene banks) and played them silent.
#
#   1. native/tools/xma_sidecars (dc3-xma-sidecars) walks every .milo_xbox,
#      inflates it, finds each kXMA SampleData, keys it exactly as the runtime
#      does, and decodes the ones not yet cached to <key>.pcm with the desktop
#      build's own DecodeXMAToPCM. Locale: eng only (the other 14 languages are
#      ~28k keys / ~0.9 GB of XMA the game never asks for).
#   2. gen_sfx_ogg.py encodes <key>.ogg for every key in the manifest into the
#      web deploy dir, native/web/build/sfx_sidecars/, which server.py serves
#      ahead of <assets>/sfx/gen/xma_pcm/ (it never writes into orig-assets,
#      which is shared by every checkout and the public deployment).
#
# The key list is committed: native/web/sfx_sidecars.manifest (key, channels,
# rate, samples, xma bytes, uses, first container). The .pcm/.ogg are NOT --
# they are ~740 MB / ~100 MB. PCMs are cached content-addressed (the key is a
# hash of the XMA payload) in $DC3_SFX_PCM_CACHE (default
# ~/.cache/dc3-xma-sidecars), so every checkout shares one decode.
#
# Usage:
#   scripts/web/gen_sfx_sidecars.sh            # fill the deploy dir (idempotent)
#   scripts/web/gen_sfx_sidecars.sh --check    # exit 1 if any manifest key has no .ogg
#   scripts/web/gen_sfx_sidecars.sh --update-manifest   # rescan, rewrite the manifest
# Env: DC3_ASSETS (default <repo>/orig-assets/extracted), DC3_SFX_PCM_CACHE,
#      DC3_SFX_LOCALE (default eng).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ASSETS="${DC3_ASSETS:-$REPO_ROOT/orig-assets/extracted}"
CACHE="${DC3_SFX_PCM_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/dc3-xma-sidecars}"
LOCALE="${DC3_SFX_LOCALE:-eng}"
MANIFEST="$REPO_ROOT/native/web/sfx_sidecars.manifest"
OUT="$REPO_ROOT/native/web/build/sfx_sidecars"
TOOL_SRC="$REPO_ROOT/native/tools/xma_sidecars"
TOOL_BUILD="$REPO_ROOT/native/build-xma-sidecars"
TOOL="$TOOL_BUILD/dc3-xma-sidecars"

MODE=generate
case "${1:-}" in
    "") ;;
    --check) MODE=check ;;
    --update-manifest) MODE=update ;;
    -h|--help) sed -n '/^# Usage:/,/^set -euo/p' "$0" | sed '$d'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
esac

# Keys (first column) of the committed manifest that have no .ogg in $OUT.
missing_keys() {
    local k
    while read -r k _; do
        case "$k" in ''|'#'*) continue ;; esac
        [ -s "$OUT/$k.ogg" ] || echo "$k"
    done < "$MANIFEST"
}

if [ "$MODE" = check ]; then
    total="$(grep -vc '^#' "$MANIFEST")"
    miss="$(missing_keys | wc -l)"
    echo "sfx sidecars: $((total - miss)) of $total manifest keys have an .ogg in $OUT"
    if [ "$miss" -gt 0 ]; then
        echo "MISSING $miss (first: $(missing_keys | head -1)); run scripts/web/gen_sfx_sidecars.sh" >&2
        exit 1
    fi
    exit 0
fi

if [ "$MODE" = generate ] && [ -f "$MANIFEST" ] && [ -z "$(missing_keys | head -1)" ]; then
    echo "==> sfx sidecars: all $(grep -vc '^#' "$MANIFEST") present in $OUT"
    exit 0
fi

[ -d "$ASSETS" ] || { echo "ERROR: no assets at $ASSETS (set DC3_ASSETS)" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ERROR: ffmpeg not on PATH (needed for the .ogg encode)" >&2; exit 1; }

echo "==> sfx sidecars: building dc3-xma-sidecars"
if [ ! -f "$TOOL_BUILD/build.ninja" ] && [ ! -f "$TOOL_BUILD/Makefile" ]; then
    gen=(); command -v ninja >/dev/null && gen=(-G Ninja)
    cxx=(); command -v clang++ >/dev/null && cxx=(-DCMAKE_CXX_COMPILER=clang++)
    cmake -S "$TOOL_SRC" -B "$TOOL_BUILD" "${gen[@]}" "${cxx[@]}" >/dev/null
fi
cmake --build "$TOOL_BUILD" >/dev/null

mkdir -p "$CACHE" "$OUT"
scan_manifest="$(mktemp "${TMPDIR:-/tmp}/dc3-sfx-manifest.XXXXXX")"
echo "==> sfx sidecars: scanning $ASSETS (locale $LOCALE), decoding into $CACHE"
if [ "$MODE" = update ]; then
    "$TOOL" --locale "$LOCALE" --manifest "$scan_manifest" "$ASSETS"
    mv "$scan_manifest" "$MANIFEST"
    echo "==> wrote $MANIFEST ($(grep -vc '^#' "$MANIFEST") keys)"
    exit 0
fi
"$TOOL" --locale "$LOCALE" --out "$CACHE" --manifest "$scan_manifest" "$ASSETS"
if ! cmp -s <(grep -v '^#' "$scan_manifest" | cut -f1) <(grep -v '^#' "$MANIFEST" | cut -f1); then
    echo "WARNING: the assets at $ASSETS give a different key set from the committed" >&2
    echo "         $MANIFEST (diff: $scan_manifest). Different asset dump, or the" >&2
    echo "         manifest is stale: --update-manifest rewrites it." >&2
else
    rm -f "$scan_manifest"
fi

echo "==> sfx sidecars: encoding .ogg into $OUT"
python3 "$REPO_ROOT/scripts/web/gen_sfx_ogg.py" --dir "$CACHE" --out-dir "$OUT" --keys "$MANIFEST"
exec "$0" --check
