#!/usr/bin/env python3
"""Run the desktop MediaPipe backend over one song segment of a long video and
write a dc3-pose-fixture v1 file keyed on SONG time.

The frames come from ffmpeg (accurate seek, crop, optional scale) so a 2.5 h
stream is never read whole; each frame k of the segment is video time
`start + k / fps` and is written at song time `video_time - t0`, where t0 is
the video time of song time 0 (scripts/video_calib/song_id_audio.py).
MediaPipe sees the frames at k * 1000 / fps ms, exactly as
scripts/pose_replay_record.py does, so the output is deterministic for a given
video, crop and model.

  python3 -I scripts/video_calib/record_segment.py --video V --t0 434.5 \
      --start -2 --dur 172 --crop 1056:1080:280:0 --out dabutt.dc3pose

Output lines: <song_t> <tracked 0|1> [20 x (x y z conf)], the format the
in-process performer reads (DC3_POSE_PERFORM=replay:FILE with
DC3_POSE_PERFORM_REPLAY_SYNC=song).  The person is the lowest track id, as
the socket path picks.  No rigid re-centring: MediaPipeBackend's own placement
is kept (the scorer is translation invariant).
"""
import argparse
import os
import subprocess
import sys

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "native", "scripts"))
from pose_mediapipe import MediaPipeBackend  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--video", required=True)
    ap.add_argument("--t0", type=float, required=True, help="video time of song time 0 (s)")
    ap.add_argument("--start", type=float, default=-2.0, help="segment start, song time (s)")
    ap.add_argument("--dur", type=float, required=True, help="segment length (s)")
    ap.add_argument("--crop", default="1056:1080:280:0", help="ffmpeg crop w:h:x:y")
    ap.add_argument("--width", type=int, default=0, help="scale to this width (0 = keep)")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--hfov", type=float, default=MediaPipeBackend.DEFAULT_HFOV_DEG)
    ap.add_argument("--model", default=os.path.join(REPO, "native/models/pose_landmarker_full.task"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--image-mode", action="store_true",
                    help="MediaPipe IMAGE running mode: every frame on its own, no temporal "
                         "landmark filter (to measure the lag VIDEO mode's filter adds)")
    a = ap.parse_args()

    cw, ch = (int(v) for v in a.crop.split(":")[:2])
    w, h = cw, ch
    vf = f"crop={a.crop}"
    if a.width:
        w = a.width
        h = int(round(ch * a.width / cw / 2)) * 2
        vf += f",scale={w}:{h}"
    vf += f",fps={a.fps}"
    ss = a.t0 + a.start
    cmd = ["ffmpeg", "-nostdin", "-loglevel", "error", "-ss", f"{ss:.3f}", "-i", a.video,
           "-t", f"{a.dur:.3f}", "-vf", vf, "-an", "-f", "rawvideo", "-pix_fmt", "bgr24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    backend = MediaPipeBackend(a.model, num_poses=1, hfov_deg=a.hfov, min_conf=0.5)
    if a.image_mode:
        from mediapipe.tasks import python as mpp
        from mediapipe.tasks.python import vision
        backend._landmarker.close()
        lm = vision.PoseLandmarker.create_from_options(vision.PoseLandmarkerOptions(
            base_options=mpp.BaseOptions(model_asset_path=a.model),
            running_mode=vision.RunningMode.IMAGE, num_poses=1,
            min_pose_detection_confidence=0.5, min_pose_presence_confidence=0.5,
            min_tracking_confidence=0.5))

        class _ImageMode:  # detect_for_video -> detect, same result type
            def detect_for_video(self, img, _ts):
                return lm.detect(img)

            def close(self):
                lm.close()
        backend._landmarker = _ImageMode()
    n = seen = 0
    size = w * h * 3
    with open(a.out, "w") as out:
        out.write("# dc3-pose-fixture v1\n")
        out.write(f"# record_segment.py t0={a.t0} start={a.start} dur={a.dur} crop={a.crop} "
                  f"width={a.width} fps={a.fps} hfov={a.hfov} model={os.path.basename(a.model)} "
                  f"mode={'image' if a.image_mode else 'video'}\n")
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            frame = np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 3)
            t_ms = n * 1000.0 / a.fps
            people = backend.process(frame, t_ms)
            t = a.start + n / a.fps
            if people:
                seen += 1
                joints = min(people, key=lambda p: p[0])[1]
                vals = " ".join(f"{x:.5f} {y:.5f} {z:.5f} {c:.3f}" for x, y, z, c in joints)
                out.write(f"{t:.4f} 1 {vals}\n")
            else:
                out.write(f"{t:.4f} 0\n")
            n += 1
    proc.wait()
    backend.close()
    print(f"{a.out}: {n} frames, person in {seen}")
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
