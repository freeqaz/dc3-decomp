#!/bin/bash
# record_xenia.sh -- one Xenia boot -> one loader capture.
#
# usage: record_xenia.sh <xenia-headless> <run-dir> <capture.json> <milo-spec>...
#
# Boots the original debug.xex with boot_xenia.sh (title_screen, DTA channel),
# waits for `wait_screen 'title_screen' SATISFIED` and the channel's first
# poll, runs `python3 -m state_diff.golden capture --target xenia:<sock>`,
# then stops ONLY the Xenia process this script started. Every wait is bounded.
# Run it under fr-slot.sh (FR_SLOT_OWNER=xenia-golden), unsandboxed.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$(cd "$HERE/../.." && pwd)"
XENIA="${1:?xenia}"; RUN="${2:?run dir}"; OUT="${3:?capture json}"; shift 3
milos=()
for m in "$@"; do milos+=(--milo "$m"); done
mkdir -p "$RUN"
"$HERE/boot_xenia.sh" "$XENIA" "$RUN" "${XG_BOOT_TIMEOUT_S:-900}" &
boot=$!
deadline=$(( $(date +%s) + ${XG_TITLE_DEADLINE_S:-240} ))
until grep -q "wait_screen 'title_screen' SATISFIED" "$RUN/run.log" 2>/dev/null \
      && grep -q "DTA channel: first poll" "$RUN/run.log" 2>/dev/null; do
  if ! kill -0 "$boot" 2>/dev/null; then echo "record_xenia: boot exited before title" >&2; exit 3; fi
  if [ "$(date +%s)" -ge "$deadline" ]; then
    echo "record_xenia: TIMEOUT waiting for title_screen" >&2
    pkill -TERM -P "$boot" 2>/dev/null; wait "$boot"; exit 4
  fi
  sleep 2
done
sleep "${XG_SETTLE_S:-5}"
( cd "$TOOLS" && timeout "${XG_CAPTURE_TIMEOUT_S:-600}" python3 -m state_diff.golden capture \
    --target "xenia:$RUN/dta.sock" "${milos[@]}" -o "$OUT" )
rc=$?
grep -n "screen ->" "$RUN/run.log" | tail -3 > "$RUN/screens_at_end.txt"
# stop our own xenia: the timeout(1) child of boot_xenia.sh
for p in $(pgrep -P "$boot"); do pkill -TERM -P "$p" 2>/dev/null; kill -TERM "$p" 2>/dev/null; done
wait "$boot" 2>/dev/null
echo "record_xenia: capture rc=$rc -> $OUT"
exit $rc
