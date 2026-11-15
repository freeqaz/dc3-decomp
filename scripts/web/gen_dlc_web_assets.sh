#!/usr/bin/env bash
# gen_dlc_web_assets.sh -- what the web build needs beside a DLC package that
# desktop does not (PLAN W-25, docs/native/DLC.md "Web"). Run by build.sh; a
# no-op without DLC.
#
# The packages themselves are served IN PLACE by server.py from the DLC root
# (orig-assets/dlc, made by scripts/dlc/import.sh); nothing here copies them.
# Two things the browser cannot take as the 360 shipped them:
#
#   1. Song previews: each song's <song>_prev.bik (Bink, 256x256 + audio) gets
#      a <song>_prev.webm NEXT TO IT, as the disc's previews have in
#      orig-assets/extracted (WebMovieImpl rewrites .bik -> .webm). VP9 + Opus,
#      scripts/web/transcode_bink.py's defaults. ~0.5-1 MB a song, written once
#      per box (orig-assets is shared by every checkout); the run writes
#      <dlc root>/web-video-manifest.json (a file, so no package).
#   2. kXMA sidecars for the samples in the packages' milos (song barks, one
#      locale): the same native/tools/xma_sidecars scan the disc set comes from
#      (gen_sfx_sidecars.sh), decoded into the shared PCM cache and encoded to
#      .ogg in native/web/build/sfx_sidecars/ beside the disc's. The key list
#      depends on which packages are imported, so it is NOT committed: it is
#      written to native/web/build/dlc_sfx_sidecars.manifest. Measured with
#      both DC2 packs: 1156 keys, all but 5 already in the disc set.
#
# Usage:
#   scripts/web/gen_dlc_web_assets.sh           # generate what is missing (idempotent)
#   scripts/web/gen_dlc_web_assets.sh --check   # exit 1 if a preview or a sidecar is missing
# Env: DC3_DLC_ASSETS (DLC root; default <repo>/orig-assets/dlc), DC3_SFX_PCM_CACHE,
#      DC3_SFX_LOCALE (default eng), DC3_DLC_PREVIEW_JOBS (default 8).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DLC="${DC3_DLC_ASSETS:-$REPO_ROOT/orig-assets/dlc}"
CACHE="${DC3_SFX_PCM_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/dc3-xma-sidecars}"
LOCALE="${DC3_SFX_LOCALE:-eng}"
JOBS="${DC3_DLC_PREVIEW_JOBS:-8}"
OUT="$REPO_ROOT/native/web/build/sfx_sidecars"
MANIFEST="$REPO_ROOT/native/web/build/dlc_sfx_sidecars.manifest"
TOOL_SRC="$REPO_ROOT/native/tools/xma_sidecars"
TOOL_BUILD="$REPO_ROOT/native/build-xma-sidecars"
TOOL="$TOOL_BUILD/dc3-xma-sidecars"

MODE=generate
case "${1:-}" in
    "") ;;
    --check) MODE=check ;;
    -h|--help) sed -n '/^# Usage:/,/^set -euo/p' "$0" | sed '$d'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
esac

if [ ! -d "$DLC" ] || [ -z "$(find -L "$DLC" -mindepth 2 -maxdepth 2 -name songs -type d -print -quit)" ]; then
    echo "==> dlc web assets: no DLC packages under $DLC (nothing to do)"
    exit 0
fi

# Previews with no .webm twin.
missing_previews() {
    find -L "$DLC" -mindepth 4 -maxdepth 4 -path '*/songs/*/*_prev.bik' -print | sort | while read -r bik; do
        [ -s "${bik%.bik}.webm" ] || echo "$bik"
    done
}
missing_keys() {
    [ -f "$MANIFEST" ] || { echo "(no $MANIFEST)"; return; }
    local k
    while read -r k _; do
        case "$k" in ''|'#'*) continue ;; esac
        [ -s "$OUT/$k.ogg" ] || echo "$k"
    done < "$MANIFEST"
}

if [ "$MODE" = check ]; then
    np="$(find -L "$DLC" -mindepth 4 -maxdepth 4 -path '*/songs/*/*_prev.bik' | wc -l)"
    mp="$(missing_previews | wc -l)"
    mk="$(missing_keys | wc -l)"
    nk="$( [ -f "$MANIFEST" ] && grep -vc '^#' "$MANIFEST" || echo 0)"
    echo "dlc web assets: previews $((np - mp)) of $np have a .webm; sfx sidecars $((nk - mk)) of $nk keys have an .ogg"
    [ "$mp" -eq 0 ] && [ "$mk" -eq 0 ] && exit 0
    [ "$mp" -gt 0 ] && echo "MISSING preview (first: $(missing_previews | head -1))" >&2
    [ "$mk" -gt 0 ] && echo "MISSING sidecar (first: $(missing_keys | head -1))" >&2
    echo "run scripts/web/gen_dlc_web_assets.sh" >&2
    exit 1
fi

command -v ffmpeg >/dev/null || { echo "ERROR: ffmpeg not on PATH (previews and .ogg encode)" >&2; exit 1; }

# 1. previews
if [ -n "$(missing_previews | head -1)" ]; then
    echo "==> dlc web assets: transcoding $(missing_previews | wc -l) song previews (.bik -> .webm) in $DLC"
    python3 "$REPO_ROOT/scripts/web/transcode_bink.py" --assets-root "$DLC" \
        --match '*/songs/*/*_prev.bik' --jobs "$JOBS"
else
    echo "==> dlc web assets: every song preview has a .webm"
fi

# 2. kXMA sidecars
if [ ! -x "$TOOL" ]; then
    echo "==> dlc web assets: building dc3-xma-sidecars"
    if [ ! -f "$TOOL_BUILD/build.ninja" ] && [ ! -f "$TOOL_BUILD/Makefile" ]; then
        gen=(); command -v ninja >/dev/null && gen=(-G Ninja)
        cxx=(); command -v clang++ >/dev/null && cxx=(-DCMAKE_CXX_COMPILER=clang++)
        cmake -S "$TOOL_SRC" -B "$TOOL_BUILD" "${gen[@]}" "${cxx[@]}" >/dev/null
    fi
    cmake --build "$TOOL_BUILD" >/dev/null
fi
mkdir -p "$CACHE" "$OUT"
echo "==> dlc web assets: scanning $DLC for kXMA samples (locale $LOCALE)"
"$TOOL" --locale "$LOCALE" --out "$CACHE" --manifest "$MANIFEST" "$DLC"
python3 "$REPO_ROOT/scripts/web/gen_sfx_ogg.py" --dir "$CACHE" --out-dir "$OUT" --keys "$MANIFEST"
exec "$0" --check
