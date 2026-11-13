# Pose input: webcam → Kinect skeleton → scoring

**State (2026-11-13, measured on Linux, no webcam):** the pipeline scores end to
end. A recorded video fed through MediaPipe is scored; a replay of the song's own
choreography gets all-perfect ratings; `scripts/pose_scoring_gate.sh` passes.
**It has never been run against a live webcam**, it has a reproducible crash on a
motionless skeleton, menus cannot be driven by gesture, and the player is never
shown on screen. **Web** has a pose path since `np-web-pose` (see [Web](#web)).
Work items: [PLAN.md](PLAN.md) track P.

## Data path

| Stage | Code | Gate |
|---|---|---|
| Camera frame | `native/scripts/pose_server.py` (`cv2.VideoCapture`), a Python child spawned by `NativeSkeletonProvider::Start` in `native/src/platform/Skeleton_Native.cpp` | `DC3_POSE_CAMERA` (index) |
| 2D + 3D pose | `native/scripts/pose_mediapipe.py` `MediaPipeBackend`: BlazePose world landmarks (metres), absolute root by PnP, one-euro filter on root depth, centroid tracker. Emits DC3's 20 Kinect joints in camera-space metres. | `DC3_POSE_MODEL` (default `native/models/pose_landmarker_full.task`, gitignored), `DC3_POSE_HFOV` |
| Wire | Unix socket, protocol v2, layout 1 (20 DC3 joints). Layout 0 = COCO, remapped by `MapCOCOToDC3`. | `DC3_POSE_SOCKET`, `DC3_POSE_NO_SPAWN=1` (connect to a server you started) |
| Socket → slots | `GestureMgr_Native.cpp`: `BuildSensorFrame` → `FillPersonSlots` / `AssignSlots` / `NativeAutoAssignPlayers`. `FillSkeletonData` holds the last good position of joints under 0.3 confidence. No provider → a jittered standing dummy. | `DC3_POSE` (`off`, `external`, …), `DC3_NATIVE_SCORING` (default on), `DC3_POSE_SELFTEST` |
| Scoring | Decompiled code: SkeletonUpdate → `Skeleton::Poll` → FilterQueue / MoveDir / async MoveDetectors → `move_passed` | `DC3_REAL_MOVE_PASSED` (default on) |
| Feedback | Ratings and score through the normal HUD. **No** player silhouette or camera preview (see Gaps). | — |

The seam for any new backend is `NativeSkeletonProvider::PublishFrame(persons,
count, frameId, timestamp)`: `PersonData` = 20 joints in metres, per-joint
confidence, trackId. The desktop socket reader and the web push both call it,
so everything after it — `Poll`, the frame-id gate, `FillSkeletonData` (joint
hold, noise floor), `FillPersonSlots` — is one code path, and `BuildSensorFrame`
has one external-provider branch (plus the legacy ncnn one). Pinned by
`NativeSkeletonProviderPublishTest` in `native/tests/test_native_posesynth.cpp`.

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

## Web

| Stage | Code |
|---|---|
| Camera | `native/web/pose/pose_input.js`: `getUserMedia` (640×480 ideal, 30 fps) on page load; states shown in a panel at the canvas's bottom-left: asking → loading model → looking for you / tracking, or blocked / no camera / error with a retry button. Mirrored preview with the BlazePose skeleton drawn (green tracked, amber inferred; joints under 0.3 are not drawn because the engine holds them). |
| Frames → worker | `MediaStreamTrackProcessor` (max buffer 1, stale frames dropped) transfers the camera stream to the worker; no per-frame main-thread work. Fallback `?pose_transport=bitmap`: `requestVideoFrameCallback` + `createImageBitmap`. |
| Inference | `native/web/pose/pose_worker.js`, a module worker: `@mediapipe/tasks-vision` 1.0.0 (the Python mediapipe version) `PoseLandmarker`, VIDEO mode, the same `pose_landmarker_full.task` and options as `pose_server.py`. CPU (XNNPACK) delegate by default. |
| Geometry | `native/web/pose/pose_geometry.js`: line-for-line port of `pose_mediapipe.py` (root DLT/torso fallback, axis flips, centre joints, hands, feet, one-euro on root depth, centroid tracker). Timestamps are the `VideoFrame` capture time. |
| Push | `Module.dc3PosePush` (`native/web/dc3_pre.js`, heap copy) → `dc3_pose_push` (`native/src/platform/PoseInput_Web.cpp`) → `PublishFrame`. First push starts the provider; `dc3_pose_stop` (track ended) returns to the dummy. A 0-person frame is real: nobody tracked. |
| Status | `window.dc3PoseSummary()` (page: state, frames, fps, inference ms) and `window.dc3PoseGame` (engine: player 0's bound skeleton joints, score, rating; every 15 engine frames). |

URL knobs: `?pose=off|manual`, `pose_model=lite|full|heavy`,
`pose_delegate=CPU|GPU`, `pose_transport=stream|bitmap`, `pose_cdn=1`,
`pose_hfov=<deg>`.

Assets: `scripts/web/build.sh` vendors tasks-vision (bundle + module wasm, from
`scripts/web/node_modules`, `npm ci --prefix scripts/web`) and the `lite`/`full`
`.task` models (from `native/models`) into `native/web/build/pose/`, so the page
loads nothing cross-origin and needs no CDN inside its COOP/COEP isolation. If
either is missing the page falls back to jsdelivr / storage.googleapis.com
(the survey found jsdelivr's headers compatible; storage.googleapis.com is
unverified).

Tests (no webcam needed):

```bash
npm ci --prefix scripts/web
scripts/web/make_pose_fixtures.sh /tmp/fx         # dance/still/empty .y4m + parity PNGs
.venv/bin/python scripts/web/pose_parity.py --frames /tmp/fx/frames --out /tmp/fx/ref.json
node scripts/web/pose_parity.mjs geometry /tmp/fx/ref.json                 # JS geometry on Python's landmarks
node scripts/web/pose_parity.mjs browser /tmp/fx/ref.json --port 8432 --no-gpu   # whole JS pipeline
node scripts/web/pose-gameplay.mjs --port 8432 --camera /tmp/fx/dance.y4m --out /tmp/run   # GPU
#   --difficulty easy|medium|expert|beginner (default easy); --song-index 2 = Better Off Alone
```

`pose-gameplay.mjs` drives Chromium's fake camera
(`--use-fake-device-for-media-stream --use-file-for-fake-video-capture=…`),
plays a song and writes per-second page + engine samples to `summary.json`.

### Measured (2026-11-13, Chromium 1223 headless shell, RTX 3090, fake camera)

Same song and path as the WEB.md survey (song-select index 3, `-O0` release
build), one run per input, player 0 score from `window.dc3PoseGame`:

| Fake camera | Engine sees | Final score | Ratings | Outcome |
|---|---|---|---|---|
| dancing (`taye01_stand_dance`, 640×360) | tracked 166/166 s | **29,859** (rises all song) | 23 changes, awesome/ok/bad | whole song, results screen |
| person standing still (frame 0 held) | tracked 161/161 s | 7,240 (0 until the end) | 4 changes | whole song, **no crash** (P-1 fix holds on web) |
| empty grey frame + noise | nobody tracked | 0 | — | game's own *PAUSED — stand in front of the Kinect sensor* 5 s into the song |
| no camera (stand-in dummy) | dummy tracked | 7,581 | 4 changes | same score as the survey's dummy run |

- Joints reach the game: provider running, player 0's bound skeleton tracked,
  camera-space hands moving (σ ≈ 0.15–0.22 m per axis while dancing, 0 at
  display precision while still), root depth ≈ 3.1–3.7 m.
- ~~`DetectFrac` is `branch=no-move-key keys=0` on every beat~~ — **the
  difficulty, not the web** (`np-web-movekey`, below). Those runs played
  Beginner and The Hustle; the table above is kept as measured.

- JS vs Python: on identical landmarks the geometry agrees to 1.8e-15 m with
  identical track ids; the whole web pipeline (tasks-vision 1.0.0 in the worker)
  vs desktop `MediaPipeBackend` (mediapipe 1.0.0) on the same 150 frames at the
  same timestamps: 150/150 detections agree, joint error mean 0.05 mm,
  p95 0.14 mm, max 0.71 mm. A one-frame-shift sabotage reads 28 mm mean.
- Cost: inference + geometry in the worker p50 35 ms, worst in-song p95 63 ms
  (full model, CPU delegate); pose rate median 28 fps (min 21) on a 30 fps
  camera; main-thread work per pose frame p50 0.05 ms, p95 0.17 ms. Game wasm
  heap in gameplay 458.9–461.6 MB, unchanged from the survey (MediaPipe has its
  own heap in the worker; that heap was not measured).
- Caveat: the box was heavily loaded by other lanes (load average 60–70 on 32
  cores). The still-person run got engine 30 fps / pose 10 fps / inference
  117 ms against the dancing run's 60 / 28 / 35; the empty and no-camera runs
  shared the GPU with other lanes. Treat timings as upper bounds.

### DetectFrac on web vs desktop (`np-web-movekey`, 2026-11-13)

**Cause of `no-move-key`:** `multiuser_screen`'s difficulty list opens on
`beginner`, and the Enter-only driver confirmed it. `MoveDir::ResetDetectFrames`
builds **no move keys for Beginner** — the image skips it (`cmpwi r15, 3; beq`
at `0x82505714`), so Beginner is rated by the async detectors alone and every
per-beat `DetectFrac` reads `branch=no-move-key keys=0`. That is the game's
behaviour on every platform: the desktop flow with its one `down` removed reads
`no-move-key` on 40/40 probe lines with `diff=beginner`. The desktop flow
(`betteroffalone.txt`) always pressed Down for Easy, which is why desktop
"worked". Nothing on the web side was missing — no asset, no wasm32 lookup.

The probe line now carries `diff=<difficulty>`, and `pose-gameplay.mjs` takes
`--difficulty` (default `easy`) and exits non-zero unless the probe agrees with
it (see its header: 0 pass, 1 check failed, 2 song not played through).
`--recheck DIR` re-applies that check to a finished run without a browser. On the
np-web-pose runs it reports what they were: `--recheck …/np-web-pose-runs/dance
--difficulty easy` → exit 1, 40/40 `no-move-key`, no `diff=` (old engine). Also
fixed there: the probe pattern was `branch=(\w+) ` and never matched the
hyphenated branches, so `summary.json` never held a `no-move-key` sample — the
claim came from the console log alone.
Sabotage, live: `--difficulty easy --no-select-difficulty` (the old Enter-only
driver) played Beginner (`diff=beginner`, 40/40 `no-move-key`) and **exited 1**;
the same command without the flag exited 0 with 35 `ham2` samples.

Better Off Alone, Easy, same fake-camera fixtures, player 0, `ham2` samples
from the probe (its first 40 lines = measures 0–39; the first 5 are
`async-empty-range` on both, before the first detect frame):

| Input | Web (Chromium, `--song-index 2`) | Desktop (`pose_server.py --video`, same .y4m) |
|---|---|---|
| dance clip | n 35, mean **0.239**, 0.050–0.760, score 25,492 | n 35, mean **0.267**, 0.092–0.774 |
| person standing still | n 35, mean **0.267**, 0.016–0.918, score 18,037 | n 35, mean **0.315**, 0.175–0.913 |
| empty frame | game pauses (*stand in front of the Kinect*), no probe line, exit 2 | pauses before the song starts (`songMs` −3995), no probe line |
| dance clip, **Beginner** | `no-move-key` 40/40, `diff=beginner` (score 34,995 from the async detectors) | `no-move-key` 40/40, `diff=beginner` |

- Web and desktop agree. The still input is the same frame every frame, so it
  is the one comparison that does not depend on where the video loop sits
  against song time: for measures 12–39, 26 of 28 agree within 0.02
  (0.23 0.17 0.19 0.19 0.19 0.28 … on both; the outliers are measures 38–39).
  The dance clip loops unsynchronised with the song on both sides, so its
  per-measure values differ (6 of 28 within 0.02) while the means agree.
- **DetectFrac does not separate this dance clip from standing still — on
  either platform.** The clip (`taye01_stand_dance`) is not Better Off Alone's
  choreography, so neither input is doing the moves; the still person even
  scores higher on the first measures (0.91, intro moves that are close to
  standing). Only empty (no person → pause) is distinguished. That is scoring
  calibration (P-7), not transport; the input that should score high is the
  routine itself, which only desktop can feed today
  (`synthetic_kinect.py --perform`, `pose_scoring_gate.sh`).
- One run per cell; load average 30–40 on 32 cores during them (other lanes),
  web pose inference p50 up to ~140 ms in the dance run. The still input's
  measures 7–8 read 0.24/0.20 on web and 0.91/0.84 on desktop — likely when the
  first tracked frames arrived, not scoring.

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
| P-9 | ~~Web pose~~ **Done in `np-web-pose`** (see [Web](#web)). Left: never run on a real webcam/permission prompt; GPU delegate and the lite model unmeasured on an idle box; no camera preview inside the game (P-4). | — | web |

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
