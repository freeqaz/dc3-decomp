#!/usr/bin/env bash
# Run native_song_sweep.py once per config (replay fixtures, Medium, no GPU),
# so a latency/variant grid needs no hand-typed loop.
#
#   scripts/video_calib/run_grid.sh OUT_ROOT PORT_BASE JOBS SONGS FIXTURE_DIR NAME:ENV[,ENV...] ...
#
# Each NAME:ENV list becomes OUT_ROOT/NAME, run with
#   DC3_POSE_PERFORM=replay:FIXTURE_DIR/{song}.dc3pose DC3_POSE_PERFORM_REPLAY_SYNC=song
# plus the listed K=V pairs (comma-separated; '-' for none), and --eval
# "$GRID_EVAL" when that is set (e.g. '{profile_mgr toggle_disable_freestyle}',
# which the story-mode footage needs: its routines have no freestyle).  Configs run one
# after another; run two of these on different PORT_BASEs to use more cores.
set -u
out=$1 port=$2 jobs=$3 songs=$4 fx=$5
shift 5
here=$(cd "$(dirname "$0")/../.." && pwd)
for cfg in "$@"; do
    name=${cfg%%:*}
    envs=${cfg#*:}
    args=()
    if [ "$envs" != "-" ]; then
        IFS=, read -ra kv <<< "$envs"
        for p in "${kv[@]}"; do args+=(--env "$p"); done
    fi
    [ -n "${GRID_EVAL:-}" ] && args+=(--eval "$GRID_EVAL")
    [ -f "$out/$name/summary.json" ] && { echo "[grid] $name: done already"; continue; }
    timeout 1500 python3 "$here/scripts/native_song_sweep.py" --out "$out/$name" --songs "$songs" \
        --no-gpu --jobs "$jobs" --port-base "$port" --difficulty medium \
        --performer "replay:$fx/{song}.dc3pose" --env DC3_POSE_PERFORM_REPLAY_SYNC=song \
        --stall 60 "${args[@]}" > "$out/$name.log" 2>&1
    echo "[grid] $name: exit $?"
done
