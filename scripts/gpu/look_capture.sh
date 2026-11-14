#!/bin/bash
# look_capture.sh -- the standard "how does the game look" capture set.
#
# One gameplay frame per venue in both venue states, from one binary, laid out
# as a labelled contact sheet.  Run it on two builds (before / after a renderer
# change) and compare the sheets, or put a sheet next to the retail reference
# set (archive/np-gfx-2026-11-14/retail/).
#
#   peak    DC3_POSE_PERFORM=choreo : a perfect performer, so the venue is in its
#           stylised "FLAWLESS" look (dark glossy floor, neon stripes)
#   normal  DC3_POSE_PERFORM=still  : a still body, so the venue keeps its
#           ordinary lit look -- the state most retail footage shows
#
# usage: scripts/gpu/look_capture.sh <out-dir> [--binary B] [--song S]
#            [--venues a,b,c] [--seconds N] [--port-base P] [--states peak,normal]
#
# <out-dir> must not exist (a stale picture from an earlier run would read as
# this run's).  GPU run: needs the sandbox off; takes the shared GPU lock.
#
# Exit 0 only if every asked-for cell produced a gameplay frame.  A port clash
# makes native_song_sweep.py report "gameplay NO" while still exiting 0 and
# writing a contact sheet, so this script counts the frames itself.
set -u

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[ $# -ge 1 ] || usage
OUT=$1; shift
REPO=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$REPO/native/build/dc3-native
SONG=omg
VENUES=dci,dclive,glitterati,houseparty,rollerrink,streetside,throneroom
SECONDS_IN=45
PORT=19300
STATES=peak,normal
while [ $# -gt 0 ]; do
    case $1 in
        --binary) BIN=$2; shift 2 ;;
        --song) SONG=$2; shift 2 ;;
        --venues) VENUES=$2; shift 2 ;;
        --seconds) SECONDS_IN=$2; shift 2 ;;
        --port-base) PORT=$2; shift 2 ;;
        --states) STATES=$2; shift 2 ;;
        *) usage ;;
    esac
done
[ -e "$OUT" ] && { echo "look_capture: $OUT exists; give a fresh directory" >&2; exit 1; }
[ -x "$BIN" ] || { echo "look_capture: no binary at $BIN" >&2; exit 1; }
mkdir -p "$OUT" || exit 1
LOCK=${DC3_GPU_LOCK:-/home/free/tmp/claude-1000/gpu1.lock}
[ -e "$LOCK" ] || LOCK=$OUT/.gpu.lock

missing=0
cells=()
IFS=, read -r -a state_list <<< "$STATES"
IFS=, read -r -a venue_list <<< "$VENUES"
for state in "${state_list[@]}"; do
    case $state in
        peak) performer=choreo ;;
        normal) performer=still ;;
        *) echo "look_capture: unknown state $state" >&2; exit 1 ;;
    esac
    flock -s -w 900 "$LOCK" python3 "$REPO/scripts/native_song_sweep.py" \
        --out "$OUT/$state" --binary "$BIN" --songs "$SONG" --venue "$VENUES" \
        --performer "$performer" --jobs 3 --port-base "$PORT" \
        --song-seconds "$SECONDS_IN" --timeout 400 > "$OUT/$state.log" 2>&1
    PORT=$((PORT + 20))
    for v in "${venue_list[@]}"; do
        # gameplay_end.png is the frame N song-seconds in; gameplay.png is the
        # (usually black) frame at game_screen Enter.
        f=$OUT/$state/${SONG}_$v/gameplay_end.png
        if [ -s "$f" ]; then
            cp "$f" "$OUT/${state}_$v.png"
            cells+=("$OUT/${state}_$v.png")
        else
            echo "look_capture: MISSING $state/$v (see $OUT/$state/summary.md)" >&2
            missing=$((missing + 1))
        fi
    done
done

if [ ${#cells[@]} -gt 0 ] && command -v montage > /dev/null; then
    montage -label '%t' -pointsize 22 "${cells[@]}" -tile "${#venue_list[@]}x" \
        -geometry 640x360+4+4 "$OUT/contact.png"
    echo "look_capture: $OUT/contact.png (${#cells[@]} cells)"
fi
if [ $missing -gt 0 ]; then
    echo "look_capture: $missing cell(s) missing -- NOT a complete capture" >&2
    exit 3
fi
exit 0
