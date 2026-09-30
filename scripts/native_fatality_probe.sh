#!/bin/bash
# native_fatality_probe.sh -- drive a dance battle and force ONE final-pose
# fatality, probing PoseFatalities state every few beats.
#
# WHY: natively nothing rates a move well enough to start a fatality.  Dance
# battle rates moves from MoveDir's async detector (`last_detector_result`),
# which returns 0 without a skeleton feed (MoveAsyncDetector::MoveRatingFrac's
# native SkeletonUpdate guard), so HamDirector::CheckBeginFatal (rating <= 1
# on a final-pose move) never fires on its own.  This probe stands in for the
# ONE thing native lacks -- the player nailing the final pose -- by calling
#     {meta_performer move_passed 0 <final-pose move> 0 1.0}
# exactly as dance_battle.dta's move_passed handler would for a perfect
# rating.  Everything after that is the game's own code: ActivateFatal,
# PoseFatalities::Poll/OnBeat (BeginFatal, AddFatal's pose clips, combo
# scoring), UpdateMatchingPose (matching comes from Autoplay, which the probe
# turns on for both players; with no skeleton the pose-compare half reads 0),
# EndFatal, fatals_over.
#
# usage: scripts/native_fatality_probe.sh <dc3-native> <outdir> <port> [INJECT_BEAT] [extra harvest args]
#   default INJECT_BEAT 200.  Needs the GPU (run outside the sandbox).
set -euo pipefail
bin=$1; out=$2; port=$3; inject=${4:-200}; shift 3
if [ $# -gt 0 ]; then shift; fi
FIND='{do ($fp "") {{{$hamdirector get_world} find moves} iterate HamMove $m {if {$m get final_pose} {set $fp $m}}} $fp}'
INJ='{do ($fp "") {{{$hamdirector get_world} find moves} iterate HamMove $m {if {$m get final_pose} {set $fp $m}}} {if $fp {meta_performer move_passed 0 $fp 0 1.0}} $fp}'
PROBE='{sprint "fatal_active=" {$pose_fatalities fatal_active} " in_fatalities=" {hamprovider get in_fatalities} " stage=" {hamprovider get game_stage} " p0score=" {{gamedata getp 0 provider} get score} " p1score=" {{gamedata getp 1 provider} get score}}'
args=(--gameplay-eval "1:{toggle_autoplay 0}{toggle_autoplay 1}$FIND" --gameplay-eval "$inject:$INJ")
for d in -2 6 12 18 24 30 36 42 50 58 64; do args+=(--gameplay-eval "$((inject + d)):$PROBE"); done
cd "$(dirname "$0")/.."
exec python3 scripts/native_assert_harvest.py --out "$out" --port "$port" --mode-downs 2 \
  --post-screens dancebattle_perform_endgame_screen,dancebattle_perform_complete_screen \
  --stall-timeout 120 --binary "$bin" "${args[@]}" "$@"
