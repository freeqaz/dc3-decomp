# Pose input: webcam → Kinect skeleton → scoring

**State (2026-11-13, measured on Linux, no webcam):** the pipeline scores end to
end. A recorded video fed through MediaPipe is scored; a replay of the song's own
choreography gets all-perfect ratings; `scripts/pose_scoring_gate.sh` passes.
**It has never been run against a live webcam**, it has a reproducible crash on a
motionless skeleton, menus cannot be driven by gesture, and the player is never
shown on screen. Web has no pose path at all. Work items: [PLAN.md](PLAN.md)
track P.

## Data path

| Stage | Code | Gate |
|---|---|---|
| Camera frame | `native/scripts/pose_server.py` (`cv2.VideoCapture`), a Python child spawned by `NativeSkeletonProvider::Start` in `native/src/platform/Skeleton_Native.cpp` | `DC3_POSE_CAMERA` (index) |
| 2D + 3D pose | `native/scripts/pose_mediapipe.py` `MediaPipeBackend`: BlazePose world landmarks (metres), absolute root by PnP, one-euro filter on root depth, centroid tracker. Emits DC3's 20 Kinect joints in camera-space metres. | `DC3_POSE_MODEL` (default `native/models/pose_landmarker_full.task`, gitignored), `DC3_POSE_HFOV` |
| Wire | Unix socket, protocol v2, layout 1 (20 DC3 joints). Layout 0 = COCO, remapped by `MapCOCOToDC3`. | `DC3_POSE_SOCKET`, `DC3_POSE_NO_SPAWN=1` (connect to a server you started) |
| Socket → slots | `GestureMgr_Native.cpp`: `BuildSensorFrame` → `FillPersonSlots` / `AssignSlots` / `NativeAutoAssignPlayers`. `FillSkeletonData` holds the last good position of joints under 0.3 confidence. No provider → a jittered standing dummy. | `DC3_POSE` (`off`, `external`, …), `DC3_NATIVE_SCORING` (default on), `DC3_POSE_SELFTEST` |
| Scoring | Decompiled code: SkeletonUpdate → `Skeleton::Poll` → FilterQueue / MoveDir / async MoveDetectors → `move_passed` | `DC3_REAL_MOVE_PASSED` (default on) |
| Feedback | Ratings and score through the normal HUD. **No** player silhouette or camera preview (see Gaps). | — |

The de facto seam for any new backend (web, in-process) is
`NativeSkeletonProvider::PersonData` — 20 joints in metres, per-joint confidence,
trackId — plus frame id and timestamp, consumed by `FillPersonSlots`. There is no
abstract interface yet; `BuildSensorFrame` has two hardcoded branches.

**Legacy in-process path** (`native/src/pose/*`, ncnn): compiled in because
`native_configure.sh` turns `ENABLE_NCNN` on, tried first on every windowed boot
unless `DC3_POSE=external`, and always fails because its YOLO model was retired
(`23f92cb78`). Its log line says "ncnn + RTMPose"; it is YOLO. The YOLO weights
`native/yolo11n-pose.pt` are still tracked. Scheduled for removal (PLAN P-6).

Env-var reference: [SCORING_ENV_VARS.md](SCORING_ENV_VARS.md) and the master
table in [../debugging/native.md](../debugging/native.md).

## Running it

```bash
# Live webcam (Linux; mediapipe + opencv in the repo .venv)
scripts/dc3-agent-test.sh                         # spawns pose_server.py on camera 0

# Recorded video instead of a camera
python3 native/scripts/pose_server.py --video clip.mp4 --loop &
DC3_POSE=external DC3_POSE_NO_SPAWN=1 scripts/dc3-agent-test.sh

# No camera: synthetic performer that dances whatever the game asks for
python3 scripts/synthetic_kinect.py --perform    # reads GET /api/pose/target

# Gate (GPU, ~11 min)
scripts/pose_scoring_gate.sh
```

Run from the repo root: the server script and the model are found relative to the
working directory when `/proc/self/exe` resolution fails.

## Measured scoring (Better Off Alone, 25k frames, GPU)

| Input | Ratings | p0 score at ≤100 s |
|---|---|---|
| perfect replay of the target | all perfect | 1,680,671 |
| replay + webcam-like noise (σxy 3 cm, σz 8.5 cm) | all perfect | 1,674,172 |
| same, 150 ms late | all perfect | 1,662,191 |
| person standing exactly still | awesome ×4, bad ×1 — then **SIGSEGV** at 130 s | 16,995 |
| MediaPipe on a video of a different dance | bad 3, ok 2, awesome 1, perfect 1 | 6,317 |

Readings: scoring is very lenient (noise + lateness is still all perfect; standing
still earns "awesome"). `config/scoring.dta` `latency_offset` is 100 ms, tuned for
Kinect, not a webcam pipeline. The gate compares only maximums and its 25k frames
end before the song does.

## Gaps

| # | Gap | Size | Platform |
|---|---|---|---|
| P-1 | **Crash on a motionless skeleton.** `RhythmDetector::AnalyzeData` divides by a variance that becomes zero → NaN → `SymbolKeys::SetFrame` segfault (`RhythmBattlePlayer::Poll`). A webcam trips it whenever joints are held (legs out of frame are held all song). | S–M | all |
| P-2 | Never run against a live webcam: camera mode, wall-clock timestamps, 640×480 request, HFOV. No "no camera" message to the player; a 5 s silent stall then the dummy. | S–M | all |
| P-3 | Gesture menus: `DC3_CONTROLLER_MODE` defaults to `forced`; `faithful` runs the original nav code but the hand meshes are hidden by `native/src/platform/MeshFilter.cpp` and nothing tests swipe nav. | M–L | all |
| P-4 | Player on screen: silhouette, camera preview, photo moments. `LiveCameraInput` is null natively; `StreamRenderer::DrawToTexture` and `DepthBuffer3D` are stubbed; `MeshFilter` hides `silhouette_guy*`, `preview.mesh`, `pose_flash*`. Needs webcam RGB + a segmentation mask. | L | all |
| P-5 | Tests: no ctest for the socket reader, COCO mapping or `AssignSlots`; gate is max-only and short; no deterministic human-video fixture. | S–M | all |
| P-6 | Remove the dead ncnn/YOLO path and its tracked weights; fix the log line. | S | all |
| P-7 | Scoring believability: calibrate against a recorded human, expose a latency knob. | M | all |
| P-8 | macOS: `/proc/self/exe` → `_NSGetExecutablePath`; camera permission goes to the Python child. See [MACOS.md](MACOS.md). | S–M | mac |
| P-9 | Web: provider seam + `dc3_push_pose(...)` export in `native/src/main_web.cpp`; move `FillSkeletonData` out of the `!__EMSCRIPTEN__` stub; getUserMedia + MediaPipe tasks-vision in `native/web/`, ideally in a worker; port the ~400 lines of geometry in `pose_mediapipe.py`. See [WEB.md](WEB.md). | M–L | web |

## Testing without a webcam

- `scripts/synthetic_kinect.py --perform` — same wire protocol, replays the
  target. Wrap it to inject noise, latency, dropout.
- `pose_server.py --video` — real MediaPipe on a file; runs end to end but is not
  deterministic (wall-clock paced).
- Planned deterministic fixture (P-5): run MediaPipe offline over a video once,
  store the DC3-20 keypoint stream, and replay it keyed on song time under
  `DC3_FAST_TIME`.
- On-disk video: rendered dance loops under `archive/screenshots-old/woot/`
  (MediaPipe detects the rendered dancer in 300/300 frames). No Kinect `.clp`
  recordings exist; for capturing them from a real console see
  [../tools/console/KINECT_CAPTURE.md](../tools/console/KINECT_CAPTURE.md).
