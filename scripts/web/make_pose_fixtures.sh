#!/usr/bin/env bash
# Generate the fake-camera fixtures for the web pose-input tests. Nothing here
# is committed (the .y4m files are ~100 MB); regenerate on demand.
#
#   scripts/web/make_pose_fixtures.sh OUTDIR [CLIP.mp4]
#
# OUTDIR/dance.y4m   a rendered DC3 dancer (default archive/screenshots-old/woot/
#                    taye01_stand_dance.mp4), 640x360 @ 30 fps, 10 s -- the
#                    "player dancing" input
# OUTDIR/still.y4m   frame 0 of the same clip held for 2 s (Chromium loops it) --
#                    a person standing motionless
# OUTDIR/empty.y4m   a grey frame with per-frame sensor noise, no person
# OUTDIR/frames/     the first 150 dance frames as PNG, for pose_parity.{py,mjs}
#
# Feed one to Chromium with
#   --use-fake-ui-for-media-stream --use-fake-device-for-media-stream
#   --use-file-for-fake-video-capture=OUTDIR/dance.y4m
set -euo pipefail
OUT="${1:?usage: make_pose_fixtures.sh OUTDIR [CLIP.mp4]}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
MAIN="$(cd "$(git -C "$REPO" rev-parse --git-common-dir)/.." && pwd)"
CLIP="${2:-}"
if [ -z "$CLIP" ]; then
    for c in "$REPO/archive/screenshots-old/woot/taye01_stand_dance.mp4" \
             "$MAIN/archive/screenshots-old/woot/taye01_stand_dance.mp4"; do
        [ -f "$c" ] && { CLIP="$c"; break; }
    done
fi
[ -f "$CLIP" ] || { echo "no clip (pass one, or restore archive/screenshots-old/woot/)" >&2; exit 1; }
mkdir -p "$OUT/frames"
VF="scale=640:360:flags=bicubic,fps=30,format=yuv420p"
ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }
ff -i "$CLIP" -vf "$VF" -frames:v 300 -f yuv4mpegpipe "$OUT/dance.y4m"
ff -i "$CLIP" -vf "$VF,trim=start_frame=0:end_frame=1,loop=59:1:0,setpts=N/30/TB" -frames:v 60 \
   -f yuv4mpegpipe "$OUT/still.y4m"
ff -f lavfi -i "color=c=0x5a5a5a:s=640x360:r=30,noise=alls=10:allf=t,format=yuv420p" -frames:v 60 \
   -f yuv4mpegpipe "$OUT/empty.y4m"
ff -i "$CLIP" -vf "scale=640:360:flags=bicubic,fps=30" -frames:v 150 "$OUT/frames/frame_%04d.png"
ls -la "$OUT"/*.y4m
echo "$(ls "$OUT/frames" | wc -l) parity frames in $OUT/frames (clip: $CLIP)"
