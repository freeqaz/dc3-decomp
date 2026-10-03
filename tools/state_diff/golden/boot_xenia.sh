#!/bin/bash
# boot_xenia.sh -- boot the ORIGINAL DC3 debug.xex under the dc3 Xenia fork to
# title_screen with the DTA channel on, for recording a Xenia golden.
#
# usage: boot_xenia.sh <xenia-headless> <run-dir> [timeout_s] [extra cvars...]
#
# Mirrors xenia tools/fork-regress lib/common.sh dc3_original_args (the S2
# command line) with three differences, all deliberate:
#   * the flow is golden/title_only.flow.txt: stop at title_screen;
#   * --dc3_dta_channel=<run-dir>/dta.sock;
#   * the timeout is the recording window, not the S1 song length.
# Every cvar is explicit; the config is a private COPY of the harness's pinned
# all-defaults toml; storage is private. Never touches ~/.local/share/Xenia.
# Writes <run-dir>/cmd.txt (argv, binary xxh3, xex sha256) and run.log.
# Wrap in fr-slot.sh (FR_SLOT_OWNER=xenia-golden) and run unsandboxed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
XENIA="${1:?xenia-headless binary}"; RUN="${2:?run dir}"; TIMEOUT_S="${3:-600}"
shift 3 || shift $#
CONTENT="${FORK_REGRESS_CONTENT:-/home/free/tmp/fork-regress-content}"
HARNESS="${XENIA_HARNESS:-$(cd "$(dirname "$XENIA")/../../../.." && pwd)/tools/fork-regress}"
CFG_SRC="$HARNESS/config/dc3-oracle.defaults.toml"
I="$CONTENT/dc3-inputs"
FLOW="$HERE/title_only.flow.txt"
for f in "$CONTENT/dc3-original/debug.xex" "$CFG_SRC" "$FLOW" \
         "$I/symbols.dc3-decomp-c362ede1c.txt" \
         "$I/dc3_nui_fingerprints.xenia-a5fc2f1b6.txt" \
         "$CONTENT/dc3-decomp-2026-08-24/xenia_dc3_patch_manifest.json"; do
  [ -e "$f" ] || { echo "boot_xenia: missing input $f" >&2; exit 2; }
done
mkdir -p "$RUN/storage"
cp "$CFG_SRC" "$RUN/config.toml"
cp "$FLOW" "$RUN/flow.txt"
argv=(
  "--storage_root=$RUN/storage"
  "--config=$RUN/config.toml"
  "--target=$CONTENT/dc3-original/debug.xex"
  --gpu=null
  --dc3_nui_patch_layout=original
  --dc3_crt_skip_nui=true
  --stub_nui_functions=true
  --fake_kinect_data=true
  "--scripted_input_file=$RUN/flow.txt"
  "--headless_timeout_ms=$(( (TIMEOUT_S - 10) * 1000 ))"
  --dc3_headless_autonav=true
  "--dc3_nui_symbol_map_path=$I/symbols.dc3-decomp-c362ede1c.txt"
  "--dc3_nui_layout_fingerprint_cache_path=$I/dc3_nui_fingerprints.xenia-a5fc2f1b6.txt"
  "--dc3_nui_patch_manifest_path=$CONTENT/dc3-decomp-2026-08-24/xenia_dc3_patch_manifest.json"
  "--dc3_dta_channel=$RUN/dta.sock"
)
argv+=( "$@" )
{
  echo "xenia: $XENIA"
  echo "xenia_xxh3: $(xxhsum -H3 "$XENIA" | awk '{print $1}')"
  echo "xex_sha256: $(sha256sum "$CONTENT/dc3-original/debug.xex" | awk '{print $1}')"
  echo "config_sha256: $(sha256sum "$RUN/config.toml" | awk '{print $1}')"
  echo "flow_sha256: $(sha256sum "$RUN/flow.txt" | awk '{print $1}')"
  echo "xenia_git: $(git -C "$(dirname "$XENIA")/../../../.." rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "load_start: $(cut -d' ' -f1-3 /proc/loadavg)"
  printf 'argv: %q' "$XENIA"; printf ' %q' "${argv[@]}"; echo
} > "$RUN/cmd.txt"
cd "$RUN"
ulimit -c 0
set +e
timeout -k 10 "$TIMEOUT_S" "$XENIA" "${argv[@]}" > "$RUN/run.log" 2>&1
rc=$?
set -e
echo "rc: $rc" >> "$RUN/cmd.txt"
echo "load_end: $(cut -d' ' -f1-3 /proc/loadavg)" >> "$RUN/cmd.txt"
rm -rf "$RUN/storage"
exit $rc
