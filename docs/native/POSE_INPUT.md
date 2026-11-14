# Pose input: webcam → Kinect skeleton → scoring

**State (2026-11-13, measured on Linux, no webcam):** the pipeline scores end to
end. A recorded video fed through MediaPipe is scored; a replay of the song's own
choreography gets all-perfect ratings; `scripts/pose_scoring_gate.sh` passes.
**It has never been run against a live webcam**, it has a reproducible crash on a
motionless skeleton. The player's
silhouette is drawn from the camera picture since `np-player-view`
([Player on screen](#player-on-screen-p-4)); photos are not. **Web** has a pose path since `np-web-pose` (see [Web](#web)).
Work items: [PLAN.md](PLAN.md) track P.

## Data path

| Stage | Code | Gate |
|---|---|---|
| Camera frame | `native/scripts/pose_server.py` (`cv2.VideoCapture`: 640×480 @ 30 fps, 1-frame driver buffer), a Python child spawned by `NativeSkeletonProvider::Start` in `native/src/platform/Skeleton_Native.cpp`. Packet timestamp = capture time (the device's `CAP_PROP_POS_MSEC` when it is on our monotonic clock, as V4L2's is; else when `read()` returned). | `DC3_POSE_CAMERA` (default `auto`; also an index, a `/dev/videoN` path or a `/dev/v4l/by-id` name — [camera selection](#which-camera-and-a-camera-that-goes-away)) |
| 2D + 3D pose | `native/scripts/pose_mediapipe.py` `MediaPipeBackend`: BlazePose world landmarks (metres), absolute root by PnP, one-euro filter on root depth, centroid tracker. Emits DC3's 20 Kinect joints in camera-space metres. | `DC3_POSE_MODEL` (default `native/models/pose_landmarker_full.task` of the executable's tree, gitignored), `DC3_POSE_HFOV` |
| Wire | Unix socket, protocol v2, layout 1 (20 DC3 joints). The header's u16 after the layout byte is `flags`; bit 0 `FLAG_UNLEVELLED` = "this camera does not know its gravity, estimate it" (`pose_server.py` always sets it); bit 1 `FLAG_NO_CAMERA` = "the camera is gone and the server is reopening it" (zero-person heartbeats every 0.1 s). Bit 2 `FLAG_LATENCY_DECLARED` = "score me at bits 8..15 × 5 ms" (`--video` declares 0; see [Who sets the latency](#who-sets-the-latency-np-latency-harness)). Layout 0 = COCO, remapped by `MapCOCOToDC3`. | `DC3_POSE_SOCKET`, `DC3_POSE_NO_SPAWN=1` (connect to a server you started) |
| Socket → slots | `GestureMgr_Native.cpp`: `BuildSensorFrame` → `FillPersonSlots` / `AssignSlots` / `NativeAutoAssignPlayers`. `FillSkeletonData` holds the last good position of joints under 0.3 confidence. No provider → a jittered standing dummy (two in two-player modes, `DC3_DUMMY_PLAYERS`). Every valid person fills its own slot: two people on the socket or in one `dc3_pose_push` frame are two players. | `DC3_POSE` (`off`, `external`, …), `DC3_NATIVE_SCORING` (default on), `DC3_POSE_SELFTEST` |
| Gravity (np-pose-tilt) | `PoseLevel_Native.cpp` `NativeSensorLevel`, called by `BuildSensorFrame`: for a camera that does not know its gravity (socket packets with header flag bit 0, web pushes, performer `replay:`) it estimates `vNormalToGravity` from the player's standing posture, then runs `SkeletonFrame::Create`'s own gravity step (`sUpVectorSmoother` → `mFloorNormal` → `NuiTransformMatrixLevel` on every joint; `mRawPositions` stays raw). A Kinect-like source (synthetic modes) is levelled by (0,1,0). [Camera tilt](#camera-tilt-np-pose-tilt-2026-11-13) | `DC3_POSE_LEVEL` (default `auto`; `0` off; degrees = fixed), `DC3_POSE_LEVEL_LOG` |
| Scoring | Decompiled code: SkeletonUpdate → `Skeleton::Poll` → FilterQueue / MoveDir / async MoveDetectors → `move_passed` | `DC3_REAL_MOVE_PASSED` (default on) |
| Feedback | Ratings and score through the normal HUD. **Player view (P-4):** the camera picture and a per-person mask become the Kinect's colour and depth\|player streams, so the decompiled `StreamRenderer` draws the player's silhouette in the menu and gameplay boxes ([Player on screen](#player-on-screen-p-4)). | `DC3_CAMERA_FRAMES` (`WxH`, default `320x240`; `off`) · web `?pose_frames=` |

The seam for any new backend is `NativeSkeletonProvider::PublishFrame(persons,
count, frameId, timestamp)`: `PersonData` = 20 joints in metres, per-joint
confidence, trackId. The desktop socket reader and the web push both call it,
so everything after it — `Poll`, the frame-id gate, `FillSkeletonData` (joint
hold, noise floor), `FillPersonSlots` — is one code path, and `BuildSensorFrame`
has one provider branch. Pinned by
`NativeSkeletonProviderPublishTest` in `native/tests/test_native_posesynth.cpp`.

The old in-process path (`native/src/pose/*`: ncnn running YOLO-Pose, behind
`ENABLE_NCNN`, `DC3_POSE=internal`, `DC3_POSE_MODELS`, `DC3_POSE_GPU`) was
removed on 2026-11-13 (P-6) together with the tracked AGPL weights
`native/yolo11n-pose.pt`. It was tried first on every windowed boot and always
failed, because its model had been retired in `23f92cb78`; a windowed boot now
goes straight to the MediaPipe server.

### When there is no camera (desktop)

`pose_server.py` checks the cheap things first and exits with a code that says
what failed: model file missing **5**, `cv2`/`mediapipe` not importable **6**,
camera will not open or opens and sends no frame within 3 s **3**, `--video`
unreadable **4**. Only then does it bind the socket (before loading the model,
so the game is not blocked on the ~1 s load). Mid-run, a camera that delivers
nothing for 2 s exits **7**. `Start()` polls the child every 50 ms while it
waits for the socket, so a failure is noticed when it happens, not after the
5 s timeout: on this box (no `/dev/video*`) a windowed boot now gives up after
**200–350 ms** instead of 5 s.

The game then tells the player, through `NativeNotice`
(`native/src/platform/NativeNotice.{h,cpp}`, drawn by `DebugPanel::DrawNotices`
from App.cpp's ImGui overlay pass whether or not the debug panel is open): a
banner across the top for 20 s from the first frame that can draw it — *"No
camera found (camera 0) - pose tracking is off. The game is using a stand-in
skeleton, so your dancing is not scored. Connect a webcam and restart, or pick
another camera with DC3_POSE_CAMERA."* — then a one-line reminder in the
bottom-right corner (*"No camera: pose tracking off"*) for the rest of the
session. Each exit code, a crash, a lost stream and the 5 s timeout have their
own text. Every notice is also printed as `DC3 NOTICE [pose]: …` on stderr.

Why an ImGui overlay: the decompiled game has no "no sensor" screen (on the 360
the dashboard owned that), and the game's own *stand in front of the Kinect*
pause never fires because the stand-in skeleton is tracked; the overlay is the
one layer that draws on every screen, in every state, without touching game UI
data. Caveat: ImGui has no display size in headless runs, so the overlay — like
the debug panel — is not in `/api/screenshot` captures; it was verified on a
windowed boot under Xvfb (screen grab).

### How the server is launched: the spawner, and restart-on-death

`dc3-native` never forks `pose_server.py` from the game process. The first
line of `main()` forks a small **spawner** (`native/src/platform/
ProcessSpawner_Native.{h,cpp}`, `dc3-spawner` in `ps`) before audio, graphics
or any thread exists; the provider sends it argv plus the game's *current*
environment and working directory, it fork+execs the server, reports the pid
and relays the exit status back (so "crashed (signal N)" still works).

Why (measured on a GPD Pocket, Arch + PipeWire, no rtkit installed): the
engine's ALSA audio goes through PipeWire's ALSA plugin, whose
`libpipewire-module-rt` — with no rtkit to ask — lowers `RLIMIT_RTTIME` to
soft 0 / hard 0 for the whole game process (`/proc/<pid>/limits`: `Max
realtime timeout 0 0 us`). A child forked after that inherits the hard 0 and,
unprivileged, can never raise it; MediaPipe's realtime thread then trips the
check and the kernel SIGKILLs the server at model load. The spawner keeps the
limits the game was *started* with. (`6fc29da83` stops mediapipe's
`sounddevice` import setting the same limit inside the server itself; both are
needed.)

Lifetime: the spawner exits on EOF of its socket (the kernel closes the game's
end however the game dies, SIGKILL included) or on `PR_SET_PDEATHSIG`; it then
SIGTERMs the server, waits up to 2 s, SIGKILLs it and reaps it. The server has
`PR_SET_PDEATHSIG(SIGTERM)` against the spawner, so even a SIGKILLed spawner
does not leak it. A process that never calls `NativeSpawner::Init()` (unit
tests that do not opt in) falls back to a direct fork and says so on stderr.

Restart-on-death: a server the game launched that dies **after it had
connected** is relaunched by the reader thread, at most **3** times per
session, after 1 s, 2 s, 4 s, with one line on stderr and one on-screen
notice per attempt (*"The pose server crashed (signal 9) - restarting pose
tracking (1 of 3)."*); once it reconnects the notice clears and
`Pose server restarted (pid N): pose tracking resumed` is printed. Exits a
relaunch cannot fix — model missing (5), packages missing (6), video
unreadable (4), python3 not runnable (127), a clean exit (0) — are not
retried; after the last attempt the usual "pose tracking is off" notice
stands. A server that fails *before* connecting at boot is not retried (that
is a missing camera/model/package, reported as before).

Tests: `PoseSpawnerTest.*` (milo-tests, no assets: the RTTIME regression with
its own negative control, exit-status relay, the signal-9 notice, restart and
its bound, nothing outliving the game however it ends) and
`NativePoseSpawnTest.*` (the same against a real `dc3-native`: the test lowers
the running game's `RLIMIT_RTTIME` with `prlimit(2)` after the spawner fork and
before the pose launch, which is where audio init sits, and a stub `python3`
on `PATH` records the limits it got).

### Which camera, and a camera that goes away

The GPD's integrated camera re-enumerates over USB on its own — eight times in
two hours on 2026-11-13, `/dev/video0` becoming `/dev/video1` with a metadata
node beside it. A fixed index then named a node that no longer existed
(`EXIT_NO_CAMERA` at boot) and a mid-song loss (`VIDIOC_REQBUFS` errno 19) was
exit 7 with no way back.

`pose_server.py --camera` (from `DC3_POSE_CAMERA`, passed as given) is:

| Value | Opens |
|---|---|
| `auto` (default; also unset/empty) | the first `/dev/videoN` whose `VIDIOC_QUERYCAP` says video capture; metadata nodes are skipped and named in the log line (`Camera: using /dev/video1 (Integrated Camera) [auto; skipped …]`) |
| an index `N` | `/dev/videoN` if it exists and is a capture device, else the `auto` scan, with a log line saying why |
| `/dev/videoN`, `/dev/v4l/by-id/<name>` or a bare by-id name | that device, following the symlink — `usb-SunplusIT_Inc_Integrated_Camera_JX20250923V2-video-index0` names the GPD camera whatever its number |

Off Linux (no V4L2) an index goes to OpenCV as before and `auto` is 0. The
spec is resolved again on every open.

When frames stop for 2 s mid-run the server keeps the socket, prints
`CAMERA_LOST`, and sends zero-person packets with `FLAG_NO_CAMERA` every
0.1 s while it reopens the camera (re-resolving the spec) at 0.5, 1, 2 s, then
every 4 s. On success it prints `CAMERA_BACK after N s` and tracking resumes on
the same connection; after 60 s without a camera it exits 7 as before (*"The
camera stopped sending pictures and did not come back"*). The game posts one
notice on the flag's rising edge — *"The camera disconnected - looking for it
again. Pose tracking is paused until it is back."* (banner, then the corner
line *"No camera: looking for it"* while it lasts) — and one on the falling
edge, *"The camera is back - pose tracking resumed."* (banner only).

Tests: `tests/test_pose_server.py` `ResolveCameraTest` (both GPD layouts,
by-id, the fallbacks, the real `VIDIOC_QUERYCAP` on whatever box runs it) and
`CameraLossTest` (a camera that vanishes and comes back renumbered is found
and tracking resumes; one that never comes back is exit 7 after the budget,
with backed-off rescans); milo-tests
`PoseSocketReaderTest.ACameraThatGoesAndComesBackPostsOneNoticeEach` and
`PoseServerStartTest.TheCameraSpecReachesTheServerAsGivenAndDefaultsToAuto`.

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

# Gate (GPU, ~13 min): five configs, each the whole song under DC3_FAST_TIME
scripts/pose_scoring_gate.sh                      # --only replay,still  --out DIR

# A player's session (docs/native/PLAYTEST.md): camera, gesture menus, session folder
scripts/play.sh                                   # --video clip.mp4 for footage in step with the song
```

### Recording a live session and replaying it (PLAN L-1)

`DC3_POSE_RECORD=FILE` (set by `scripts/play.sh` to `<session>/pose.dc3session`)
writes every sensor frame the game **accepts**, at the point the scorer gets it
(`BuildSensorFrame`, after the frame-id gate, before levelling): each person's
20 joints, confidences and tracking id, the frame id, the sensor timestamp, the
song time, and the floor normal the gravity estimator had reached. Format
`dc3-pose-session v1`, one `F` line per frame and one `P` line per person,
flushed per frame (`native/src/platform/PoseSession_Native.cpp`).

`DC3_POSE=external DC3_POSE_PERFORM=session:FILE` replays it through the same
`PublishFrame` seam the socket reader uses. With `DC3_POSE_SESSION_SYNC=song`
(the default) a frame is published when the song clock reaches its recorded
song time, with its recorded timestamp, and the sensor levels it with its
recorded floor normal instead of re-estimating one (the estimator is session
state, so a replay that starts fresh would level differently). A whole session
holds many songs, each with its own clock; `scripts/playtest/pose_session.py`
lists the song segments and cuts one out. Under song sync the replay holds the
first frame until the song clock comes down to the cut's first frame (the
count-in; the menus' clock runs up from zero through the song's own times, and
a dance played into the menus took the pad's presses, H-20), then publishes
every frame on a render frame of its own -- catching up one per frame after a
hitch, as the live game accepted them -- and restarts the sensor noise floor's
counters so a replay does not depend on how long the menus took. Tests:
`PoseSessionFileTest`, `PoseSessionTest.AReplayedSessionReachesTheScorerAsItDidLive`,
`PoseSessionTest.ASongReplayWaitsForTheCountInNotTheMenuClock`,
`SensorNoiseFloorTest.AResetRestartsTheOffsets`
(`native/tests/test_pose_session.cpp`, `test_native_pose_socket.cpp`).

A replay records itself as well, and `pose_session.py compare CUT REPLAYED`
checks that every frame reached the scorer once, in order and unchanged.
End to end with the real tracker and footage, `scripts/playtest/replay_proof.py`
(numbers in STATUS): under `DC3_FAST_TIME` a replay is exact -- the same frames,
the same rating and detector fraction on every move, the same score, three runs
out of three. In real time two runs of one input differ on a few borderline
moves (the scorer samples the skeleton on render frames), and a box that
renders below 30 fps drops frames, as a live game on that box would.

Run from the repo root: the server script and the model are found relative to the
working directory when `/proc/self/exe` resolution fails.

## Measured scoring (Better Off Alone, 25k frames, GPU, pre-2026-11-13)

> **Caveat (P-7, `np-scoring`).** These rows came from a socket performer that sent
> one skeleton per *game* frame. Scoring keeps each node's best error over every
> player frame inside a ±0.23 s window, so more frames per song second means more
> chances to match. Under `DC3_FAST_TIME` that is 120 frames per song second; a
> Kinect delivers 30. With the same noise, Better Off Alone Easy rates 77/77
> perfect at 120 Hz, 75/77 at 30 Hz and 52/77 at 15 Hz. "Noise + lateness is still
> all perfect" is partly a frame-rate artefact. For the 30 Hz numbers, see
> [Scoring believability](#scoring-believability-p-7-np-scoring).

| Input | Ratings | p0 score at ≤100 s |
|---|---|---|
| perfect replay of the target | all perfect | 1,680,671 |
| replay + webcam-like noise (σxy 3 cm, σz 8.5 cm) | all perfect | 1,674,172 |
| same, 150 ms late | all perfect | 1,662,191 |
| person standing exactly still | awesome ×4, bad ×1 — then **SIGSEGV** at 130 s | 16,995 |
| MediaPipe on a video of a different dance | bad 3, ok 2, awesome 1, perfect 1 | 6,317 |

Readings: scoring is very lenient (noise + lateness is still all perfect; standing
still earns "awesome"). `config/scoring.dta` `latency_offset` is 100 ms, tuned for
Kinect, not a webcam pipeline. (The SIGSEGV was P-1, since fixed.)

### The gate, whole song (2026-11-13, `np-pose-desk`)

`scripts/pose_scoring_gate.sh` plays Better Off Alone **start to finish** under
`DC3_FAST_TIME` for each input and stops when `perform_endgame_screen` enters
(~2.5–3 min wall each; `--frames` cap 60k is only a backstop). Scores and ratings
are recorded from `GET /api/pose/target`; `scripts/pose_gate_report.py` judges.
DetectFrac is the 35 `ham2` samples the probe logs for player 0.

| config | input | DetectFrac mean (min–max) | final score | ratings |
|---|---|---|---|---|
| selftest | reference fed back inside the scorer | 1.0000 (1.0–1.0) | 2,617,260 | perfect 77 |
| sensor | choreography streamed through the socket | 1.0000 (1.0–1.0) | 2,674,598 | perfect 77 |
| dummy | static skeleton, no provider | 0.2579 (0.14–0.73) | 16,992 | awesome 4, bad 2 |
| replay | MediaPipe fixture (a different dance), keyed on song time | 0.3010 (0.05–0.66) | 33,662 | awesome 4, ok 5, bad 10 |
| still | the fixture's frame 0 held all song | 0.3066 (0.17–0.90) | 49,119 | perfect 2, awesome 2, ok 4, bad 6 |

Ratings count rated events, one per change of (move, rating, score).
Two `replay` runs gave identical scores and ratings, and a mean of 0.3010 both times.

What it asserts: every config finishes the song with no crash; selftest and
sensor score mean ≥ 0.95 and beat dummy on mean and on final score; no config
other than those two is pinned at one DetectFrac value of 0 or 1; dummy, replay
and still rate perfect on fewer than half their rated moves. The last check is
move by move. The mean per-move |replay − still| DetectFrac distance is
**0.146**. The same distance between two still bodies (still vs dummy) is
**0.049**. The gate requires the first to be more than 2× the second (measured
2.96×), which shows the fixture's motion reaches the scorer.

What it does **not** assert: that the dancing stream out-scores the still one.
It does not. Standing still beats a different dance, 49,119 to 33,662. The
opening moves rate a standing body at 0.80–0.90, and the wrong dance draws
"bad". This agrees with the 25k-frame table above (video 6,317 vs still
16,995). It is a P-7 scoring-believability finding, not something to freeze into a gate. A
positive control for that direction needs a recording of the song's own
choreography; none exists in the repo.

## Scoring believability (P-7, `np-scoring`)

**Question.** Is the leniency the original game's, or does our input fail to look
like a Kinect skeleton?

**Answer.** The leniency is the original's. Two things in our input did diverge
from what the original expects, and both are now handled or measurable:

1. **Frame rate.** Old harnesses sent a skeleton every game frame, which made
   scoring look more lenient than it is. This was an instrument problem, not a
   game problem.
2. **Latency.** `latency_offset` describes the Kinect's latency. A webcam has
   its own, and `DC3_POSE_LATENCY_MS` now declares it.

The other differences are real but small, or move scores both ways:

- MediaPipe jitters 5–10× more than a Kinect. On real footage in VIDEO mode
  the level is ~1.7 mm, below anything the scorer reacts to
  ([Video calibration](#video-calibration-np-video-calib-2026-11-13)).
- MediaPipe arms come out ~15% shorter.
- A webcam's tilt reaches the score: the scorer uses camera-space joints.
  Measured on real footage, correcting 10° of tilt raised the score 6%.

**Why the scoring itself is not the cause:**

- **The scoring code is the original's.** These are 100% matched under
  `name_check`, with no `HX_NATIVE` shadow bodies:
  - `DetectFrame::AddError`, `LimbPSNR`
  - `HamMove::PSNRToDetectFrac`, `DetectRangePSNR`
  - `MoveDetector::LastDetectFrac`, `MoveAsyncDetector::MoveRatingFrac`
  - `Game::SetHamMove`, `MoveDir::LoadScoring`

  Other facts about the original's scoring:
  - Body size is normalised: `ErrorNode::NormBoneLengths` scales by summed bone
    lengths.
  - The per-move PSNR thresholds were set from recorded bad / awesome / perfect
    distributions. `scoring.dta`'s ok and awesome thresholds sit at
    bad_mean + bad_std. If the bad distribution is roughly normal, about one bad
    performance in six reaches ok or better by design.
- **A real Kinect stream gets the same treatment as ours.** UTD-MHAD (Kinect v1,
  30 Hz, 862 trials) was replayed through the same path. It scores like our
  synthetic and MediaPipe inputs. Standing still earns credit only on near-static
  moves:
  - On Better Off Alone that is `Roxbury_betteroffalone.move`, whose reference
    moves 1.7 cm. A still body gets perfect or awesome there, depending on its
    stance.
  - The real Kinect still stream rates it awesome ×4.
  - The Kinect frame baked into the image (`StubSkeletonData`), and a MediaPipe
    still photo, rate it perfect ×4.
  - Everywhere else a still body draws ok or bad.
- **Still beating a different dance on Better Off Alone is the original's
  behaviour.** The P-5 gate measured still 49,119 vs a different dance 33,662.
  The same ordering happens with genuine Kinect data:
  - The image's own Kinect frame, held still: 104k.
  - Real Kinect motion that is not the choreography: 75k.

  Four Roxbury perfects outweigh the scattered awesomes a wrong dance collects.
  On The Hustle, which has no such move, every moving input beats every still
  one. Dancing the *song's own* moves at the wrong time (26 beats off) beats
  still everywhere: 187k vs 104k.
- **Half tempo and 300 ms late still earning many perfect and awesome ratings is
  the algorithm.** The ±0.23 s best-of window and the bone-normalised PSNR
  forgive timing.

### The matrix

The matrix runs in-process and deterministically: `DC3_POSE_PERFORM`,
`DC3_FAST_TIME`, 30 Hz, headless, no GPU, about 25 s per song. It runs each whole
song to the end.

- Cells read `perfect/awesome/ok/bad` · mean `LastDetectFrac` over rated moves ·
  final score.
- Re-running a cell gives identical ratings and DetectFrac. The final score varies
  by ±5 points.

| input | BOA Easy | BOA Expert | Hustle Easy | Hustle Expert |
|---|---|---|---|---|
| choreography, exact (synthetic) | 77/0/0/0 · 1.00 · 2,746k | 77/0/0/0 · 1.00 · 4,021k | 59/0/0/0 · 1.00 · 2,225k | 59/0/0/0 · 1.00 · 3,239k |
| choreography + webcam-like noise¹ | 75/2/0/0 · 0.96 · 2,506k | 70/7/0/0 · 0.96 · 3,520k | 59/0/0/0 · 0.98 · 2,092k | 58/1/0/0 · 0.97 · 2,897k |
| … + 150 ms late | 64/13/0/0 · 0.94 · 2,317k | 64/13/0/0 · 0.93 · 3,250k | 57/2/0/0 · 0.96 · 1,965k | 54/5/0/0 · 0.95 · 2,710k |
| … + 150 ms late, `DC3_POSE_LATENCY_MS=250` | 76/1/0/0 · 0.97 · 2,502k | 71/6/0/0 · 0.96 · 3,545k | 58/1/0/0 · 0.98 · 2,063k | 58/1/0/0 · 0.97 · 2,932k |
| choreography 300 ms late | 29/28/0/20 · 0.74 · 1,265k | 24/30/8/15 · 0.75 · 1,246k | 22/32/1/4 · 0.83 · 1,076k | 20/32/7/0 · 0.83 · 1,449k |
| … 300 ms late, `DC3_POSE_LATENCY_MS=400` | 77/0/0/0 · 1.00 · 2,746k | 77/0/0/0 · 1.00 · 4,020k | 59/0/0/0 · 1.00 · 2,225k | 59/0/0/0 · 1.00 · 3,239k |
| choreography at half tempo | 19/29/3/26 · 0.67 · 829k | 26/20/6/25 · 0.68 · 1,085k | 14/33/6/6 · 0.79 · 654k | 11/26/11/11 · 0.71 · 732k |
| wrong choreography (same song, 26 beats off) | 10/3/12/52 · 0.45 · 187k | 8/6/8/55 · 0.45 · 258k | 1/11/15/32 · 0.48 · 130k | 1/6/10/42 · 0.44 · 149k |
| **real Kinect** motion, not the choreography² | 0/13/2/62 · 0.38 · 75k | 0/10/2/65 · 0.38 · 85k | 2/6/13/38 · 0.46 · 90k | 2/3/12/42 · 0.43 · 99k |
| MediaPipe, a different dance (`taye01` fixture) | 0/1/3/73 · 0.29 · 23k | 0/0/3/74 · 0.29 · 29k | 0/6/19/34 · 0.44 · 94k | 0/5/9/45 · 0.40 · 89k |
| MediaPipe, a different dance (second woot clip) | 0/1/2/74 · 0.29 · 14k | 0/0/2/75 · 0.29 · 19k | 0/6/20/33 · 0.45 · 95k | 0/5/11/43 · 0.41 · 89k |
| **real Kinect** standing still² | 0/4/4/69 · 0.31 · 33k | 0/3/1/73 · 0.31 · 20k | 0/0/34/25 · 0.46 · 87k | 0/0/21/38 · 0.38 · 72k |
| the image's Kinect frame (`StubSkeletonData`), held | 4/0/4/69 · 0.30 · 104k | 3/0/1/73 · 0.30 · 45k | 0/0/30/29 · 0.45 · 77k | 0/0/18/41 · 0.36 · 63k |
| MediaPipe on a still photo, held | 4/0/4/69 · 0.29 · 104k | 3/0/1/73 · 0.28 · 45k | 0/0/35/24 · 0.45 · 77k | 0/0/21/38 · 0.37 · 63k |
| stand-in stick figure, held | 0/4/0/73 · 0.25 · 18k | 0/3/0/74 · 0.24 · 11k | 0/0/25/34 · 0.41 · 53k | 0/0/15/44 · 0.34 · 41k |

¹ AR(1) Gaussian per joint per axis: σxy 3 cm, σz 8.5 cm, ρ 0.9 per 30 Hz frame
(`DC3_POSE_PERFORM_NOISE=0.03,0.085,0.9,1`).

² [UTD-MHAD](https://personal.utdallas.edu/~kehtar/UTD-MHAD.html) Kinect v1
skeletons, downloaded and kept outside the repo:
- "Still": the subject's quietest standing, facing, arms-down 20-frame window,
  played forwards and backwards.
- "Motion": subject 1's 25 standing actions, middle 60% of each, mean joint speed
  0.65 m/s. For comparison, the MediaPipe dances run at 0.46–0.61 m/s.

**MediaPipe on video of the correct choreography** (Better Off Alone Easy only).
Source: the engine's own render, mirrored, measures 5–40.
- Rendered: `DC3_POSE_PERFORM=choreo DC3_POSE_PERFORM_CLOCKLOG=1`, 640×360,
  every 4th frame = 30 Hz of song.
- Keyed to song time with `pose_replay_record.py --times`, lead dancer
  (`--pick maxx`), untracked frames held.

| input, same 36 measures | P/A/O/B | mean DetectFrac |
|---|---|---|
| MediaPipe on the render of the correct choreography | 2/8/5/21 | 0.52 |
| synthetic choreography + noise¹ | 35/1/0/0 | 0.96 |
| real Kinect motion, not the choreography | 0/8/1/27 | 0.41 |
| held still (image frame / real Kinect) | 4/0/2/30 · 0/4/2/30 | 0.33 · 0.33 |
| MediaPipe, a different dance | 0/0/3/33 | 0.29 |

This is a lower bound, not a webcam measurement:
- The gameplay camera cuts, frames the dancers at the knees and shows two of them.
- MediaPipe found a person in only 1,008 of 2,125 frames.
- Wrist speed reads 10 m/s, because tracking jumps between the two dancers.

It does rank correctly. The next step is a real player on a real webcam (P-2).

**Frame rate.** Same noisy choreography, Better Off Alone Easy:

| performer rate | P/A/O/B | mean |
|---|---|---|
| 120 Hz (`DC3_FAST_TIME` game rate, the old harnesses) | 77/0/0/0 | 1.00 |
| 30 Hz (Kinect) | 75/2/0/0 | 0.96 |
| 15 Hz | 52/25/0/0 | 0.90 |

The Hustle Easy shows the same pattern: 59/0, 59/0, 51/8. **The P-5 gate's
`sensor` config also streams at game rate**, so its "perfect sensor" row is a
120 Hz figure.

### Our skeletons vs a Kinect's

| quantity | Kinect v1 (UTD-MHAD, the image's frame) | MediaPipe (desktop backend) | matters? |
|---|---|---|---|
| rate | 30 Hz | camera rate. The performer and replays are fixed at 30 Hz of song time. | **yes**: see Frame rate above |
| still-body jitter, median per axis | x 1.9 · y 1.8 · z 2.1 mm, hands z 7.8 mm | still photo + video noise: x 2.4–5.1 · y 2.7–5.8 · **z 9–19 mm**, hands z 10–24 mm. **Real footage, VIDEO mode** ([Video calibration](#video-calibration-np-video-calib-2026-11-13), its own estimator, which reads the Kinect at 0.3/0.3/0.5): 0.4/0.3/0.6 mm; high-frequency σ 1.7 mm, hands z 3.9 | mild noise (z 9 mm) scores like a Kinect. Heavy noise (z 19 mm) raises a still body on The Hustle Easy from 0/0/34/25 (Kinect) to 0/19/21/19. |
| bone lengths, median | upper arm 26.9, forearm 22.9, shoulders 33.2, torso 41.8 cm | upper arm 22–23, forearm 19, shoulders 32–33, torso 39–40 cm | scale is normalised (`NormBoneLengths`). Arm-to-torso proportion is ~10% short. |
| depth | per-joint ToF/structured light | per-joint z from BlazePose world landmarks plus a pinhole root (`DC3_POSE_HFOV`) | the spread of hand z relative to the hip while moving is similar: Kinect UTD actions 21.7 cm, MediaPipe dances 19.8–21.1 cm |
| inferred joints | Kinect tracking state | confidence ≥ 0.6 tracked, 0.3–0.6 inferred, < 0.3 held: ~1% inferred on clean video | small. UTD-MHAD carries no tracking state to compare against. |
| coordinate frame | camera space, +y up, +z away, player-left = −x | same (`pose_mediapipe.py`) | — |

The original consumes `NuiSkeletonGetNextFrame` directly
(`SkeletonUpdate::Update`, tracking flags `2`). Nothing in the game calls
`NuiTransformSmooth`. So there is no game-side smoothing we skip.

### Latency: `DC3_POSE_LATENCY_MS`

`MoveDir::PostUpdateFilters` scores the skeleton that arrives at song time S
against the choreography at S − `latency_offset`. `latency_offset` is 100 ms in
`config/scoring.dta`, tuned for the Kinect. `DC3_POSE_LATENCY_MS` replaces it with
the player's own sensor latency, the same quantity in ms:

- unset: the pose source's own declaration if it made one (a recorded file
  declares 0, see [Who sets the latency](#who-sets-the-latency-np-latency-harness)),
  else the image's value
- outside 0–1000: ignored, with a warning
- hook: one `HX_NATIVE` call in `MoveDir::LoadScoring`, still 100.0% `name_check`

Set it to `latency_offset` + (webcam pipeline latency − Kinect pipeline latency).
For example, if capture + MediaPipe + socket is 150 ms slower than the Kinect, use
`DC3_POSE_LATENCY_MS=250`. The matrix shows it closes the gap exactly:
- 300 ms late + knob 400 = an on-time sensor, all four cells.
- An on-time sensor declared 400 ms rates 33/16/2/26, like 300 ms late.

**The default stays unset.** Nobody has measured a real webcam's latency yet
(P-2). It can be read from the capture timestamps `pose_server.py` already puts on
each packet. Recorded footage cannot stand in for that, and
[Video calibration](#video-calibration-np-video-calib-2026-11-13) explains why.
That run did measure how much accuracy the knob needs: the score curve is
flat to ~4% over ±50 ms around its peak. `DC3_POSE_LATENCY_LOG` now prints the
capture → game part of the delay, and the live fake-camera grid (np-tracking)
is in [Latency on the live path](#latency-on-the-live-path-l-4). That grid
prefers 0, for reasons a real webcam does not share. So a webcam's default
stays unset, and recorded footage now declares its own 0
([Who sets the latency](#who-sets-the-latency-np-latency-harness)).

### Running the matrix

```bash
# One cell (headless, no GPU, ~25 s per whole song under DC3_FAST_TIME)
DC3_POSE=external DC3_POSE_PERFORM=choreo DC3_MOVE_RATING_LOG=1 DC3_FAST_TIME=1 \
  MILO_HEADLESS=1 MILO_NORENDER=1 DC3_FAST_BOOT=1 MILO_INPUT_SCRIPT=<flow> native/build/dc3-native
#   modes: choreo | wrong | halftempo | still | stand | replay:FILE
#   DC3_POSE_PERFORM_NOISE=sxy,sz,rho,seed  _LEAD_MS=-150  _HZ=30  _SHIFT_BEATS=26
#   _REPLAY_SYNC=song  _CLOCKLOG=1 (render frame -> song time)
# Per move: DC3 MOVE_RATING p=0 measure=12 move=Roxbury_betteroffalone.move rating=move_perfect lastfrac=0.9066 score=... diff=easy

# A replay file (dc3-pose-fixture v1, same format as scripts/pose_fixture_record.py)
.venv/bin/python scripts/pose_replay_record.py --video clip.mp4 --out clip.dc3pose
.venv/bin/python scripts/pose_replay_record.py --frames SHOTS --times T.txt --mirror --pick maxx --out r.dc3pose
```

The performer (`native/src/platform/PosePerformer_Native.{h,cpp}`):
- Publishes through `NativeSkeletonProvider::PublishFrame`, the seam the socket
  and the web push use, so everything after it is the shipped path.
- Sends 30 frames per second of song time, however fast the game loop runs.
- Models a sensor with `scoring.dta`'s latency, so `DC3_POSE_LATENCY_MS` stays
  measurable against it.
- Reseeds its noise when the song clock restarts.
- Publishes an untracked replay frame as 0 persons, as the socket does.

Tests: `PosePerformerClockTest`, `PosePerformerReplayTest`, `SensorLatencyTest`
(`native/tests/test_pose_scoring.cpp`). Each was sabotage-checked.

### Video calibration (`np-video-calib`, 2026-11-13)

**Question.** Real people dancing in front of a real camera, with the original
game's own verdict on screen: does MediaPipe through our path score them the
way the Xbox scored them? And what does the footage say about latency,
jitter and per-joint error?

**Source.** One public stream: *finishing the Dance Central 3 story mode!*
(YouTube `mqcXJrH5vUo`, 8990 s, 1920×1080 30 fps). A full-frame player camera
(front-facing, **mirrored**, whole body visible) with the Xbox capture as an
inset, bottom right. Every readable results card says **Medium**. The video
and its frames are someone else's content, so they are **not in the repo**:
`scripts/video_calib/mqcXJrH5vUo.json` records the layout, crop, per-song
offsets and the 17 original results cards, and every number below is
reproducible from a fresh `yt-dlp` download.

**Method** (all scripts in `scripts/video_calib/`, all read the download as
untrusted data and run with `python3 -I`):

1. **Find the songs.** `song_id_audio.py` cross-correlates each song's
   unencrypted `<id>_prev.webm` (the `.mogg` is encrypted) against the stream
   audio: 18 songs, NCC 0.35–0.70, each confirmed by the title card in the
   inset. The finale's six short battle cuts are identified but not used (no
   preview match, remixed audio). One song, Teach Me How To Dougie, is
   excluded: it was paused mid-song and has no results card.
2. **Record.** `record_segment.py` runs the desktop `MediaPipeBackend` (full
   model, VIDEO mode, CPU) over the crop `1056:1080:280:0` at 30 fps and
   writes a dc3-pose-fixture keyed on song time. Tracked 94–100% of frames,
   30.00 fps, bones: upper arm 23, forearm 23–24, shoulders 32, torso 38 cm.
3. **Prepare.** `fixture_transform.py --hold --mirror`. Both are required:
   - untracked frames published as 0 persons paused 3 songs, so they are held;
   - unmirrored, every lead scored mean lastfrac 0.49–0.59, mirrored 0.84.
4. **Count the same moves.** The story mode has no freestyle, so its sections
   are scored moves. `--eval '{profile_mgr toggle_disable_freestyle}'` on
   multiuser_screen reproduces that. The original's move count is solved from
   its card (`percent = (P+A)·100 // N`, `MetaPerformer::GetMovesPassed`), and
   ours matches it on every song: **1162 = 1162**.
5. **Score.** `run_grid.sh` drives `native_song_sweep.py --difficulty medium
   --performer replay:DIR/{song}.dc3pose --no-gpu` with
   `DC3_POSE_PERFORM_REPLAY_SYNC=song`, one config per lead.
   `compare_cards.py` compares our per-move ratings (`DC3_MOVE_RATING_LOG`)
   with the original's card.
6. **Geometry, independent of scoring.** `dump_target.py` records the
   reference skeleton the scorer uses (`GET /api/pose/target`, whole song).
   `pose_vs_reference.py` compares it with the fixture, hip-centred and
   torso-normalised: limb-direction error per lead, plus per-joint signed
   bias. `pvr_summary.py` aggregates songs.

**Per-move ground truth was not extracted.** The in-game rating text is
occluded and small, and no OCR tool is installed. The comparison is per
song, against the card's perfect and awesome counts.

#### Results (MEASURED)

One song was misaligned, and the geometry found it. getlow's audio peak was
two measures early. Over a ±15 s search the limb error was 38.8° at lead 0
and 25.6° at +4661 ms. Every other song has its minimum within ±200 ms and a
6–12° margin over any far offset. The manifest carries the corrected
`t0`. Until the fix, getlow rated 11/12/8/42: the "worst song" was an
instrument fault.

Lead sweep: 15 songs (getlow excluded, so every row covers the same songs),
1089 moves, one shared `DC3_POSE_PERFORM_LEAD_MS`. The original's rates are
P 0.790 · A 0.165 · rest 0.045.

| lead ms | ours P / A / ok+bad | score ours/orig | mean lastfrac |
|---|---|---|---|
| −150 | 0.382 / 0.416 / 0.202 | 0.539 | 0.805 |
| −100 | 0.437 / 0.421 / 0.141 | 0.625 | 0.831 |
| **−50** | **0.463 / 0.411 / 0.126** | **0.650** | **0.836** |
| 0 | 0.449 / 0.407 / 0.144 | 0.627 | 0.828 |
| +50 | 0.421 / 0.418 / 0.161 | 0.590 | 0.816 |
| +100 | 0.343 / 0.437 / 0.220 | 0.504 | 0.782 |
| +150 | 0.302 / 0.439 / 0.259 | 0.448 | 0.761 |
| per-song lead (geometry) | 0.528 / 0.376 / 0.096 | 0.727 | 0.854 |

All 16 songs, per-song lead, before and after a 10° pitch correction
(`--pitch 10`, below). Each song is one run; macarena's `p10` run stalled in
the menus once (the replay-in-menus artifact) and is a retry.

| song | original P/A of N | ours P/A/ok/bad | ours p10 | score ours/orig (p10) |
|---|---|---|---|---|
| dabutt | 49/15 of 65 | 34/27/4/0 | 36/21/8/0 | 0.74 (0.66) |
| supersonic | 62/16 of 83 | 53/23/6/1 | 58/17/6/2 | 0.84 (0.84) |
| yougotittherightstuff | 55/15 of 77 | 33/38/2/4 | 54/18/1/4 | 0.83 (1.01) |
| letthemusicplay | 56/20 of 81 | 37/35/3/6 | 44/28/7/2 | 0.71 (0.74) |
| electricboogie | 50/14 of 69 | 32/26/8/3 | 25/31/9/4 | 0.54 (0.43) |
| iceicebaby | 59/7 of 69 | 33/30/2/4 | 43/21/1/4 | 0.62 (0.70) |
| aint2proud2beg | 63/9 of 74 | 30/40/4/0 | 34/36/4/0 | 0.72 (0.75) |
| macarena | 54/6 of 61 | 42/18/1/0 | 49/11/1/0 | 0.85 (0.92) |
| getlow | 61/10 of 73 | 52/19/1/1 | 57/14/1/1 | 0.91 (0.94) |
| 12step | 54/7 of 62 | 38/16/5/3 | 41/17/3/1 | 0.65 (0.74) |
| indaclub | 50/6 of 57 | 37/18/2/0 | 41/14/2/0 | 0.81 (0.82) |
| cupidshuffle | 72/13 of 85 | 47/28/5/5 | 50/23/6/6 | 0.61 (0.60) |
| boyfriend | 49/11 of 65 | 25/36/2/2 | 33/30/1/1 | 0.78 (0.94) |
| mrsaxobeat | 70/11 of 86 | 44/27/13/2 | 52/23/7/4 | 0.62 (0.76) |
| starships | 54/22 of 81 | 40/31/7/3 | 42/33/5/1 | 0.89 (0.87) |
| scream | 63/8 of 74 | 50/17/6/1 | 51/18/5/0 | 0.77 (0.83) |
| **total, 1162 moves** | **P 0.793 · A 0.164 · rest 0.044** | **0.540 · 0.369 · 0.091** | **0.611 · 0.306 · 0.083** | **0.739 (0.781)** |

- **Pass/fail agrees, and the perfect/awesome split does not.** Perfect+awesome is 0.909
  ours vs 0.957 original, a mean per-song gap of 4.9 points. But ours
  rates awesome where the Xbox rated perfect: P 0.54 vs 0.79. So scores
  come out at 74% of the original's.
- **Pitch correction helps, and it is systematic.** Rotating the fixture 10°
  about the camera's x axis lowers the limb error on **17 of 17** songs
  (median 25.6° → 24.3°) and raises the total score 0.739 → 0.781 (12 of 16
  songs up). The player's whole body (ankle → head) leans **+6.9° ± 1.4°**
  more toward the camera than the reference does (legs +7.7° ± 1.7°), the
  same on every song. The per-joint bias says the same: knees, ankles and feet
  read +0.25…+0.53 torso units farther and −0.2…−0.3 lower, and the head
  0.19 nearer. INFERRED: the stream's camera looks down at the player by
  ~7–10°. The scorer uses camera-space joints, so a webcam's tilt reaches the
  score directly.
- **The remaining limb error is arms, not legs.** At the best lead the median
  error is upper arms 32–35° and forearms 34°, but thighs 18–21° and shins
  13–15°. Arms are also the bones MediaPipe gets ~15% short.

**Latency.** By geometry, the per-song best lead runs from −170 to +127 ms
(median −5). It has a weak trend against video time (−75 ms/h, r = −0.48,
residual sd 81 ms). By score, a single shared lead peaks at −50 ms. The
curve is broad: −100..0 is within 4% of the peak, and ±150 ms costs 17–31%.
Aligning each song to its own geometric lead is worth +12% score (0.650 →
0.727). INFERRED: the lead in a recording is the stream's camera-vs-capture
sync plus the dancer's own timing plus MediaPipe's lag. It says nothing
about a live webcam's pipeline latency, which is what
`DC3_POSE_LATENCY_MS` declares. MediaPipe's VIDEO-mode lag, measured against
IMAGE mode on macarena, is 0 frames on wrists, elbows and head and 1 frame
on knees.

**Jitter**, `fixture_stats.py`, on the same estimators for both sources:

| estimator | MediaPipe VIDEO mode, this footage | MediaPipe IMAGE mode (macarena) | Kinect v1, UTD-MHAD |
|---|---|---|---|
| still windows, median per-frame step x/y/z: body · hands | 0.4/0.3/0.6 · 0.5/0.4/0.8 mm (5 songs, 180 steps) | — | 0.3/0.3/0.5 · 0.6/0.7/0.6 mm (25 trials, 409 steps) |
| high-frequency σ (2nd-difference residual, joints < 0.3 m/s): body · hands | 1.65/1.59/1.69 · 1.7/1.9/**3.9** mm (18-song median) | 4/4/5.4 · 4.3/5.7/**14.3** mm | 0.08/0.10/0.10 · 0.18/0.24/0.31 mm |

- Real footage through VIDEO mode is far quieter than this page's earlier
  MediaPipe figure (z 9–19 mm, a still photo plus synthetic video noise).
  That figure looks like IMAGE mode, which has no temporal filter. The
  desktop backend runs VIDEO mode.
- The Kinect's frame-to-frame residual is ~20× smaller still. INFERRED:
  UTD-MHAD's recorder smoothed it (no repeated frames, near-zero residual),
  so it is not the raw stream the game consumed.
- Both estimators are biased low for noise above ~5 mm, because they keep
  only slow joints. The IMAGE-mode column shows they can still read 14 mm.
- The matrix above found mild noise (z 9 mm) scores like a Kinect. Measured
  VIDEO-mode noise sits well below that.

**Smoothing.** A causal One Euro filter (`fixture_transform.py --one-euro
1,0.5`) cuts the high-frequency σ 3–4× (body 0.3–0.5 mm). It adds 60–130 ms
of lag and changes the limb error by under 1° (macarena 25.3 → 24.8°,
supersonic 21.4 → 20.9°, each at its own best lead). **Scored, it hurts.**
Over the 16 per-song-aligned fixtures, with the lead moved by its lag
(+66 ms), the rates went P 0.540 → 0.356, A 0.369 → 0.478, ok+bad 0.091 →
0.165, and the score 0.739 → 0.543 of the original's. A lighter filter
(`--one-euro 3,1`: 33 ms of lag, σ halved, lead +33 ms) still loses: P
0.467, score 0.642. Both settings hurt, and the stronger one hurts more.
INFERRED: the filter rounds off the moves' own fast content, which the
PSNR scorer rewards. Noise is
not what separates our ratings from the original's, so the default path gets
no filter.

#### Recommendations

- **`DC3_POSE_LATENCY_MS`: keep the default unset.** Recorded footage cannot
  measure it (above). It needs one live session that reads `pose_server.py`'s
  capture stamps. The broad score curve says ±50 ms accuracy is enough.
- **No smoothing by default.** VIDEO-mode MediaPipe is already filtered and
  adds no measurable lag on the arms. An extra One Euro filter lost 13% (light)
  to 26% (1 Hz) of the score. Do not switch the backend to IMAGE mode: that is 3–4× the noise.
- **Camera pitch is the systematic per-joint error worth fixing.** It is not
  depth noise. A follow-up: estimate the camera's pitch from the player's
  ankle → head lean (here +6.9° ± 1.4° over the reference, consistent per
  song) and rotate in `pose_mediapipe.py`, outside `src/`. The Kinect has a
  floor plane and a tilt motor for this. A webcam on a shelf has neither.
  Needs a live check before it becomes the default.
- **The perfect/awesome gap is open.** After alignment and pitch, 0.61 vs
  0.79 perfect remains. Candidates, untested: arm-length proportion, arm
  depth (the arms carry most of the limb error), and the stream crop's
  perspective. (Followed up by `np-pose-tilt`, below: the pitch is now
  estimated and levelled automatically, and the arm candidates were tested.)

#### Reproducing

```bash
D=$(mktemp -d ~/tmp/vc.XXXX)   # a fresh dir for the untrusted download
yt-dlp -f 137+251 --merge-output-format mkv -o "$D/src/video.%(ext)s" \
  https://www.youtube.com/watch?v=mqcXJrH5vUo
ffmpeg -i $D/src/video.mkv -ac 1 -ar 8000 $D/stream_8k.wav
python3 -I scripts/video_calib/song_id_audio.py --audio $D/stream_8k.wav \
  --songs-dir orig-assets/extracted/songs --out $D/song_hits.json
# per song (t0 from the manifest):
.venv/bin/python -I scripts/video_calib/record_segment.py --video $D/src/video.mkv \
  --t0 4004.0 --start -5 --dur 161.3 --out $D/pose/macarena.dc3pose
python3 -I scripts/video_calib/fixture_transform.py $D/pose/macarena.dc3pose \
  $D/fx/macarena.dc3pose --hold --mirror [--pitch 10] [--shift S]
GRID_EVAL='{profile_mgr toggle_disable_freestyle}' scripts/video_calib/run_grid.sh \
  $D/grid 9320 4 macarena,indaclub $D/fx l_m050:DC3_POSE_PERFORM_LEAD_MS=-50 l_0:-
python3 -I scripts/video_calib/compare_cards.py --manifest scripts/video_calib/mqcXJrH5vUo.json $D/grid/*/
python3 scripts/video_calib/dump_target.py --song macarena --song-order $D/grid/l_0/song_order.json \
  --port 9326 --out $D/macarena.target.jsonl
python3 -I scripts/video_calib/pose_vs_reference.py --fixture $D/fx/macarena.dc3pose \
  --target $D/macarena.target.jsonl --leads=-500:500:33 --json $D/pvr/macarena.json
```

Cost: MediaPipe on CPU is ~1–2× real time per song. A 16-song config runs
in ~5–8 min at 4 jobs, unpaced and without a GPU. The `everybody` replay
stalls on main_screen (the replayed dancer moves the menus), so it is not in
the grid.

### Camera tilt (`np-pose-tilt`, 2026-11-13)

**What the original does (MEASURED from the code).** The game levels every
skeleton with the Kinect's gravity reading. `SkeletonFrame::Create`
(`src/system/gesture/Skeleton.cpp`) smooths `NUI_SKELETON_FRAME::vNormalToGravity`
with `sUpVectorSmoother` (`default.dta` `kinect up_vector_smoothing`: alpha
10/s, trend 0), stores it as `mFloorNormal` and multiplies every joint and
the hip centre by `NuiTransformMatrixLevel(mFloorNormal)`. `mRawPositions`
keeps the raw camera-space joints. `vFloorClipPlane` is read only for its `w`
(`unkac4`). `mFloorNormal` is also "up" for `BaseSkeleton::MakeCameraToPlayerXfm`.
The native sensor built `SkeletonFrame`s directly and skipped this step, and
`NuiTransformMatrixLevel` was an identity stub. It is now ported from the
target listing (`nuiskeleton.s`, `0x829C3568`: normalise, axis =
`n × (0,1,0)`, identity when the axis is zero, else `XMMatrixRotationAxis(axis,
acos(n.y))`), in `native/src/xbox_link_stubs.cpp`. So the correction lives in
the sensor stand-in, the NUI analog, and **not** in `pose_mediapipe.py` /
`pose_geometry.js`. The game does its own levelling, exactly as on a 360. One
C++ path serves desktop and web: `PoseLevel_Native.cpp` is in both source
lists, and the JS geometry is unchanged (`pose_parity.mjs geometry`: max
joint error 2.2e-15 m, 14/14 detections).

**The estimate** (`NativeGravityEstimator`, `PoseLevel_Native.h`). A webcam
has no accelerometer, so `vNormalToGravity` comes from the player's posture.
Each tracked person in each new frame offers one sample: the lean of the
ankles → HipCenter axis, as pitch (toward the camera) and roll. The sample
is kept only when:
- HipCenter, ShoulderCenter, both shoulders, hips, knees and ankles are
  `kConfidenceTracked`;
- the axis is ≥ 0.3 m long and within 25° of camera +y;
- the trunk (HipCenter → ShoulderCenter) is within 20° of the axis;
- each knee bends ≤ 25° in the image plane;
- the player faces the camera (|shoulder line x| ≥ 0.8 × its length).

The last 600 kept samples (~20 s) form the window. Once a second, after 150
samples (~5 s), the estimate is the window's median pitch minus
`kPriorPitchDeg` and its median roll. The first estimate is taken directly,
later ones are blended in at 0.25 per update (~4 s time constant), and the
estimate is held between updates and through frames that fail the gates.
`kPriorPitchDeg` = **2.9°** is the Kinect reference choreography's own
ankles → HipCenter lean: the median over the story-mode songs' reference
skeletons (per-song +0.5..+5.2°, sd 1.3). A levelled Kinect dancer shows it,
so the estimate keeps it rather than forcing the player to 0. The estimate is
clamped to ±30°. Before the first estimate the frame is levelled by (0,1,0),
the identity.

Why the axis and not the whole body: MediaPipe's z is noisy and its knees read
~35° bent while standing, so a "near-still" gate found almost no frames. A
gated median over upright frames is stable. On the pre-song footage, the legs
out of frame produced ~15° outliers until the confidence, spine and facing
gates were added (MEASURED, `pre/*.dc3pose`).

**Results** (MEASURED, the np-video-calib grid: 16 songs, 1162 moves, Medium,
per-song lead, `run_grid.sh` configs `auto`, `none` = `DC3_POSE_LEVEL=0`,
`p10` = `DC3_POSE_LEVEL=10`). `none` reproduces np-video-calib's unrotated run
exactly: every song's counts and the 0.739 ratio. `p10`, levelled in the
engine, reproduces np-video-calib's fixture rotated by `--pitch 10`: 0.781,
per-song counts identical. The two independent paths agree.

| song | original P/A of N | none P/A/ok/bad | p10 | auto | score/orig none · p10 · auto | auto pitch, in-song median [range] | limb-optimal pitch |
|---|---|---|---|---|---|---|---|
| dabutt | 49/15 of 65 | 34/27/4/0 | 36/21/8/0 | 36/21/8/0 | 0.74 · 0.66 · **0.67** | 8.5 [7.4, 8.8] | 7 |
| supersonic | 62/16 of 83 | 53/23/6/1 | 58/17/6/2 | 59/17/5/2 | 0.84 · 0.84 · 0.85 | 9.2 [7.1, 9.9] | 10 |
| yougotittherightstuff | 55/15 of 77 | 33/38/2/4 | 54/18/1/4 | 54/18/1/4 | 0.83 · 1.01 · 0.99 | 8.8 [7.7, 10.1] | 8 |
| letthemusicplay | 56/20 of 81 | 37/35/3/6 | 44/28/7/2 | 41/30/8/2 | 0.71 · 0.74 · 0.74 | 9.0 [6.7, 9.4] | 9 |
| electricboogie | 50/14 of 69 | 32/26/8/3 | 25/31/9/4 | 29/27/10/3 | 0.54 · 0.43 · **0.49** | 7.6 [6.9, 8.5] | 7 |
| iceicebaby | 59/7 of 69 | 33/30/2/4 | 43/21/1/4 | 43/21/1/4 | 0.62 · 0.70 · 0.70 | 8.8 [8.1, 9.8] | 13 |
| aint2proud2beg | 63/9 of 74 | 30/40/4/0 | 34/36/4/0 | 36/34/4/0 | 0.72 · 0.75 · 0.75 | 8.3 [6.8, 10.2] | 10 |
| macarena | 54/6 of 61 | 42/18/1/0 | 49/11/1/0 | 49/11/1/0 | 0.85 · 0.92 · 0.92 | 7.3 [5.7, 9.1] | 8 |
| getlow | 61/10 of 73 | 52/19/1/1 | 57/14/1/1 | 57/14/1/1 | 0.91 · 0.94 · 0.94 | 8.5 [7.2, 10.2] | 8 |
| 12step | 54/7 of 62 | 38/16/5/3 | 41/17/3/1 | 40/18/3/1 | 0.65 · 0.74 · 0.73 | 9.4 [8.3, 10.0] | 10 |
| indaclub | 50/6 of 57 | 37/18/2/0 | 41/14/2/0 | 41/14/2/0 | 0.81 · 0.82 · 0.84 | 7.2 [6.3, 8.1] | 7 |
| cupidshuffle | 72/13 of 85 | 47/28/5/5 | 50/23/6/6 | 52/22/5/6 | 0.61 · 0.60 · 0.63 | 8.1 [6.3, 9.0] | 6 |
| boyfriend | 49/11 of 65 | 25/36/2/2 | 33/30/1/1 | 36/27/1/1 | 0.78 · 0.94 · 0.95 | 9.0 [7.9, 10.1] | 6 |
| mrsaxobeat | 70/11 of 86 | 44/27/13/2 | 52/23/7/4 | 51/24/7/4 | 0.62 · 0.76 · 0.76 | 9.4 [7.9, 10.3] | 9 |
| starships | 54/22 of 81 | 40/31/7/3 | 42/33/5/1 | 44/31/5/1 | 0.89 · 0.87 · 0.92 | 8.7 [7.3, 10.6] | 6 |
| scream | 63/8 of 74 | 50/17/6/1 | 51/18/5/0 | 52/16/6/0 | 0.77 · 0.83 · 0.81 | 8.4 [6.5, 9.4] | 6 |
| **total** | **P 0.793 · A 0.164** | **0.540 · 0.369** | **0.611 · 0.306** | **0.620 · 0.297** | **0.739 · 0.781 · 0.789** | 7.2–9.4 (song medians) | median 8 |

- **Auto beats no correction**: score up on 14 of 16 songs, perfect count
  up on 15 of 16, total 0.739 → **0.789**, perfect rate 0.540 → 0.620. It
  matches or beats the fixed 10° on 10 of 16 (total 0.781).
- **The two songs that lose, dabutt and electricboogie, lose under the
  fixed 10° too** (0.74 → 0.66 / 0.67, 0.54 → 0.43 / 0.49). On dabutt the
  perfect count rises (34 → 36) and four awesomes drop to ok. Auto loses less
  than p10 on both. The runs are deterministic (`none` reproduces
  np-video-calib exactly), so these are effects of the rotation, not noise.
  INFERRED: on those two, the dancer's own stance differs from the
  choreography's. The scorer rewards matching the choreography, not standing
  straight.
- **The limb error, independent of the scorer** (MEASURED, the estimator
  prototype offline on the same fixtures against `dump_target.py`'s
  reference): auto lowers it on **16 of 16** songs vs none, median 25.67° →
  24.41°, and matches or beats p10 on 10 of 16 (median 24.41°). The engine's
  in-song estimates (table) agree with the prototype's (7.1–9.5°).
- **Stable.** Per song the in-song estimate's sd is 0.4–1.0°. On every song
  the first estimate exists before the first rated move.
- **A replay artifact after the song, not in it.** When a fixture ends, the
  replay holds its last frame. That single repeated pose fills the window
  and the estimate drifts to its lean: −1.1° (electricboogie), 19° (scream),
  21° (supersonic). Every excursion begins after the song's last rated move,
  so no score is affected. A live camera sends new frames.
- `tilt_survey.py` (MEASURED): the prior is +2.85° (sd 1.34 over songs). The
  player's lean is +11.2° (sd 0.8). The limb-error-optimal pitch has median
  8.0° (sd 1.9). Estimate − optimum = +0.11° ± 1.76° per song. The per-song
  optimum is noisy (iceicebaby 13, starships 6), and no per-song estimate
  from posture can recover it. The prior is the one free parameter, and it
  comes from the reference, not the scores.

**Default: on** (`DC3_POSE_LEVEL` unset = `auto`). The original levels every
frame by gravity. The estimate brings the geometry closer on all 16 songs
and the score on 14. The two losses are shared with any fixed correction.
`DC3_POSE_LEVEL=0` restores the pre-np-pose-tilt frames bit-exactly.
INFERRED: a live webcam at a different height reads a different tilt, and
that is the case this exists for. Needs P-2's live session before anyone tunes
`kPriorPitchDeg`.

#### The arm error (MEASURED geometry, INFERRED conclusion)

The arms carry most of the residual limb error. Each candidate cause was
tested against the reference skeletons (`ana/arm_probe.py` in the lane
scratch):

- **Direction, not length or depth.** The upper-arm error is 25–27° in the
  image plane (xy) and ~20° in elevation; the forearm is 39° in xy. Scaling
  depth gains nothing. Arm-length normalisation cannot change a direction.
- **Timing is not it.** The arms' best lead is 0, the same as the legs'.
- **Proportions.** Ours / reference / a real Kinect v1 (UTD-MHAD):

  | ratio to torso | ours | reference | Kinect v1 |
  |---|---|---|---|
  | shoulder width | 0.846 | 0.685 | 0.78 |
  | head − ShoulderCenter | 0.435 | 0.22 | 0.466 |
  | upper arm | 0.62 | 0.57 | 0.64 |

  Ours is closer to a real Kinect than the reference is. Moving our shoulders
  medially by 0.2–0.4 of their width gains only 1.3–2.3° on the upper arms,
  and it would make the skeleton less Kinect-like. Not changed.
- **The COCO → Kinect mapping is unused** on this path: MediaPipe sends
  layout 1, its own 20 joints.
- **Conclusion (INFERRED):** no mapping fix in our code is justified. The arm
  error is mostly the dancer versus the choreography, which the Xbox saw too:
  the original also rated 0.164 awesome. What separates the perfect rate
  (0.62 vs 0.79) stays open.

#### Tests

- `native/tests/test_pose_level.cpp`:
  - `NuiLevelMatrixTest` (3 tests): the port against the decoded matrix.
  - `GravityEstimatorTest` (5 tests): a synthetic dancer seen by a camera
    at pitch 8°, roll −3° comes out upright within 0.3° and 1.5 cm. Also:
    nothing before 5 s, held through gate failures, updated once a second and
    slowly under a ramping camera, and a jump in the window median blended
    in.
  - `PoseLevelPolicyTest`, `PoseLevelFrameTest`: Create's gravity step,
    raw positions kept.
- `native/tests/test_native_pose_socket.cpp` `PoseSensorLevelTest`
  (4 tests): end to end over the fake socket. A flagged 8° camera is
  levelled to the prior, an unflagged packet is untouched, `DC3_POSE_LEVEL=0`
  is off, and a fixed pitch levels by exactly that.
- `tests/test_pose_server.py`: every packet carries `FLAG_UNLEVELLED`.
- **Sabotage, 15 of 15 caught**: identity level matrix, per-frame updates,
  no prior, no confidence gate, the reader ignoring the flag, raw positions
  levelled, every source estimated, `=0` ignored, no spine gate,
  `mFloorNormal` not stored, roll ignored, fixed pitch ignored, first
  estimate blended, a 1 s window, blend = 1. Plus the Python flag sabotage.

## Detection dropouts and re-acquired dancers (P-12, W-10, `np-tracking`)

**State (2026-11-13).** A dancer MediaPipe loses for up to ~2 s no longer
pauses the song, and a dancer it finds again as someone new is bound and
scored. A player who really leaves still pauses.

### What a Kinect and the image do

A Kinect keeps one tracking id for a person in view; joints it cannot see are
reported INFERRED, not dropped. A new id means the person was really lost.
The image is built for that:

- `SkeletonRecoverer` (`GestureMgr::sMaxRecoveryTime` 1.0 s,
  `sMinRecoveryTime` 0.7 s, `sMaxRecoveryDistance` 0.3 m) keeps a lost id bound
  for up to 1 s and moves the player to a new skeleton that appears near
  where she was.
- After that, `SkeletonChooser::SetPlayerSkeletonNavData` unbinds her
  (`HamPlayerData::IsPlaying` is tracking id > 0), and
  `Game::CheckForSkeletonLoss` pauses when fewer players are playing than the
  mode needs. perform.dta's `pause_on_skeleton_loss` arms from
  first_move_beat − 8, outside freestyle.

### P-12: the mid-song pause with the dancer in view

**Cause (MEASURED).** np-dogfood3's Cupid Shuffle clip
(`cupidshuffle_p2.mp4`): MediaPipe lost the dancer at song 42.00 s and found
her again at 43.43 s, 1.43 s later, under a new id. The 1 s recovery ran out
and the game paused at 43.25 s. Two things were short of a Kinect:

- `CentroidTracker` forgot an id after 15 frames (0.5 s), so she came back
  as a new person.
- The C++ slot layer dropped her the frame she was lost.

The dogfood's "0.7 s" gap was measured on a different recording, which is
also why the replay control did not pause. Over the 18 songs of
np-video-calib's stream fixtures: 50 in-song dropouts, the longest 1.2 s,
7 longer than 1.0 s. That is the "0-person frames paused 3 songs" noted there.

**Fix.**

- **Tracker memory 2 s** (`max_missing` 15 → 60 frames,
  `native/scripts/pose_mediapipe.py`, same in `native/web/pose/pose_geometry.js`).
  That is the 1 s hold plus the image's 1 s recovery. The filter age-out moved
  from 60 to 90 frames to match.
- **Dropout hold** (`GestureMgr_Native.cpp` `AssignSlots` / `FillPersonSlots`,
  `DC3_POSE_HOLD_MS`, default 1000). A person the detector loses keeps her
  slot and tracking id for that much sensor time. Every joint is reported at
  its last pose with confidence capped at INFERRED, the Kinect's own word for
  "not seen". Then the slot is released and the image's recovery gives its own
  1 s. Exceptions:
  - Not for someone seen for under 500 ms (a false detection).
  - Ended early when a new id appears within 0.3 m of the held hip (she is
    back under another id).
  - `0` turns it off. Capped at 5 s.

**Before / after (MEASURED, live `pose_server`, same clip and driver).**
Before: paused at 43.25 s with 21 moves rated. After: no pause, the whole
song, 85 moves rated, 507,276.

### W-10: the unscored song (the auto-assign fought the chooser)

**Reproduced on desktop (MEASURED).** Boyfriend through the live
`pose_server`, np-video-calib footage. Two runs out of two scored 0 with
no move cards, and "New player detected! Raise your hand to join." was on
screen. The fake camera's cut from its held first frame to the song frame
moved her hip by 0.25 of the frame. She came back under a new id, and for two
frames MediaPipe reported her twice. With the old id held, the game saw two
skeletons for about a second.

**Mechanism.** The image's chooser round-robins an unbound skeleton for a
raised hand (in a song; standing still in the menus). While it waits,
`UpdatePlayerSkeletonNavData` drops that skeleton's id from the player ids, so
`SetPlayerSkeletonNavData` unbinds whoever holds it. `NativeAutoAssignPlayers`,
the native policy that binds a player with no skeleton, bound her back on
every sensor frame: **634 re-binds in one song**. Each unbind is an Overshell
`player_quit` (perform.dta `on_player_quit`: `start_score_move_index 1000`,
flash cards cleared). Each rebind is a `player_join` that restarts the drop-in
grace. So nothing was ever scored. The auto-assign also bound player 2 to the
extra skeleton in Perform, and the chooser switched its active player back and
forth (the slot binding swapped every ~10 frames).

**Fix (`GestureMgr_Native.cpp`, `SkeletonChooser.h`).**

1. A skeleton the auto-assign binds stops being the chooser's candidate
   (`SkeletonChooser::NativeSkeletonBound`, an `HX_NATIVE` inline in the
   header; `UpdatePlayerSkeletonNavData` is still 100.0% under `name_check`).
   The image clears the candidate the same way when its own filter fires.
2. Player 2 is auto-assigned only in a mode that needs two players
   (`requires_2_players` / `is_in_party_mode`, the predicate the stand-in count
   already used). Anywhere else, a second person joins the image's way, by
   raising a hand.

**Before / after (MEASURED, Boyfriend, same clip and driver):**

| Build | Score | Moves rated | Re-binds |
|---|---|---|---|
| before | 0 | 0 | — |
| rule 2 only | 0 | 0 | 634 |
| both rules | 1,129,261 | 65 (90% passed) | 2 |

The original's card is 1,803,226. A later grid run of the same song read
545,236 while other lanes loaded the box (pose age up to 340 ms, below).

**Web (INFERRED).** The dogfood's web second song ran the same code, the
same auto-assign and chooser, over a looping mjpeg whose loop point is the
same kind of cut. The tracker change is ported (parity test below). A local
web rerun of two songs is in PLAN W-10.

### Tests

- `tests/test_pose_tracker.py` (ctest `PoseTracker.Python`):
  - a 1.43 s dropout keeps the id; past 2 s is a new person; the bound is
    exactly 60 frames;
  - two people keep their ids through one dropping out, in either detection
    order;
  - `TrackerWebParity`: `pose_geometry.js` under node gives the same ids on
    every sequence.
- `native/tests/test_native_pose_socket.cpp` `PoseSensorHoldTest` (6): the
  hold keeps slot and id with joints inferred at the last pose; it is bounded;
  `0` is off; a 3-frame detection is not held; a new id where she stood ends it;
  a newcomer elsewhere does not.
- `native/tests/test_native_pose_dropout.cpp` (`DC3_DTA_FLOW_TESTS`, real time,
  since the recoverer counts UI seconds):
  - `PoseDropoutTest`: two 1.43 s dropouts in YMCA, no pause;
  - `PoseDropoutHoldOffTest`: the same with the hold off pauses at 21.2 s,
    which shows the dropouts land where a pause is possible;
  - `PoseExitTest`: she leaves at 20 s, paused at 22.1 s.
- `native/tests/test_native_second_person.cpp` (`DC3_DTA_FLOW_TESTS`,
  `scripts/synthetic_kinect.py --jump-at` / `--second-person-from`):
  - `ReacquiredDancerTest`: at 20 s she reappears 0.6 m away under a new
    id. She is on the new id in every later sample, re-bound at most 6 times,
    and scored to the end;
  - `SecondPersonPerformTest`: a second person walks in at 20 s and never
    raises a hand. Player 2 is never bound, player 1 keeps her id, and the song
    is scored.
- **Sabotage, all caught:**
  - C++ hold: off, presence gate removed, bound removed, re-acquire check
    removed, inferred flag removed (5 of 5).
  - Python and JS `max_missing` back to 15.
  - Flow, hold disabled in code: paused at 21.16 s.
  - Candidate rule removed: 6,059 re-binds.
  - Player-2 rule removed: player 2 bound in every sample, and the re-acquired
    dancer's scoring stops at the jump (112,944 → 147,696).
  - `TwoPlayer` / `OnePerson` / `NoCamera` battle tests still pass with the
    fix.

### Latency on the live path (L-4)

**What "default" is.** `DC3_POSE_LATENCY_MS` unset means `scoring.dta`'s
`latency_offset`, 100 ms (`MoveDir::LoadScoring` → `Dc3SensorLatencySeconds`).
At 0 the scorer compares the skeleton at song time S with the choreography at
S itself.

**What the fake camera's delay is (MEASURED).** `DC3_POSE_LATENCY_LOG=1` prints
the age of each pose frame when the game takes it, from `pose_server.py`'s
capture stamp. On an unloaded box it was 61–67 ms median (p10 ~30, p90 ~70).
Other lanes' builds pushed it to 100–340 ms for minutes at a time. Every row
below gives its median and the share of 5 s windows over 100 ms.

**Grid (MEASURED, 2026-11-13).** Six songs at Medium through the live
`pose_server` on np-video-calib's footage, a fake camera that picks each frame
by the game's song clock (`fake_cam_pose_server.py`), and the original's own
results card as the yardstick. Cupid is a different recording from its card,
so read its column across, not down.

| song | card | 100 | 100 again | 50 | 0 | 0 again |
|---|---|---|---|---|---|---|
| macarena | 1,985,201 | 0.65 | 0.65 | 0.62 (18%) | 0.79 (11%) | 0.60 (44%) |
| cupidshuffle | 3,045,166 | 0.18 | 0.15 | 0.20 (19%) | 0.35 (14%) | 0.10 (68%) |
| starships | 2,153,727 | 0.95 | 0.90 | 0.67 (20%) | 0.73 | 0.32 (63%) |
| indaclub | 2,013,394 | 0.60 | 0.57 | 0.70 | 0.85 | 0.62 (38%) |
| 12step | 2,128,773 | 0.29 (20%) | 0.36 | 0.48 | 0.63 | 0.40 (24%) |
| boyfriend | 1,803,226 | 0.30 (34%) | 0.49 | 0.77 | 0.88 | 0.61 (22%) |
| **mean** | | **0.49** | **0.52** | **0.57** | **0.70** | **0.44** |
| *median pose age, ms* | | *61–66* | *66–73* | *66–69* | *65–67* | ***78–120*** |

Each cell is the score as a share of the card; a percentage in brackets is the share of 5 s windows whose pose age was over 100 ms (shown when it is at least 10%).

With the box quiet (pose age 61–67 ms), 0 beat the default on 5 of 6 songs,
with a mean of 0.70 of the card against 0.49 and 0.52 at the default. That
extends the dogfood's Macarena result (0.56 → 0.83) to five more songs. Two
caveats come out of the same grid:

- **The best knob moves with the real delay.** The "0 again" round ran while
  other lanes loaded the box, and the median pose age rose to 78–120 ms. At 0
  its mean fell to **0.44, below the default**. The two songs with the highest
  age read lowest: Cupid at 120 ms scored 0.10, Starships at 117 ms scored
  0.32. INFERRED from that: setting the knob below the true delay is the
  costly mistake, so a default should err high.
- **Repeats are noisy.** At the default, the two runs agree within 0.05 on
  Macarena, Cupid, Starships and Indaclub. 12 Step and Boyfriend differ by
  0.07 and 0.19, and in both cases the low run had the higher pose age (20%
  and 34% of windows over 100 ms). Boyfriend at the default read 545,236 in
  one run and 892,382 in the other. A single run per cell cannot separate
  knobs closer than about 0.1.

**Why that does not set a webcam's default (INFERRED).** The knob is the whole
delay from the player's movement to the scorer, on the song clock. The fake
camera has only part of it:

- the capture → game age above (~63 ms);
- no camera exposure or capture buffering: the frame is a decoded file, and
  `pose_server.py` stamps it when it reads it;
- no audio output latency: a real player moves to what she hears, which lags
  the song clock by the output buffer;
- the footage's dancer has her own timing: np-video-calib found the score
  peaks with a shared lead of −50 ms.

A real webcam adds the two missing terms, each typically tens of ms. That puts
a live setup near the image's 100, and the original's 100 was tuned for a
Kinect, whose own pipeline is of the same order. **Decision: the default stays
unset (100) for a live webcam; recorded footage scores at 0.** Since
`np-latency-harness` nobody has to pass `DC3_POSE_LATENCY_MS=0` for that: the
source declares it (next section). Scores taken at 100 on recorded footage (the
dogfood's live-path numbers) read about 30% low.

### Who sets the latency (`np-latency-harness`)

The source says what it is, and the engine believes it unless told otherwise.
`pose_server.py` puts a declared latency in every packet header (flags bit 2,
bits 8..15 = ms / 5). The engine applies it on the main thread when the first
frame arrives, and again whenever it changes
(`NativeSkeletonProvider::Poll` → `Dc3DeclareSourceLatencyMs`,
`Skeleton_Native.cpp`). Precedence, highest first:

| What | Latency | Engine log reason |
|---|---|---|
| `DC3_POSE_LATENCY_MS` set (0–1000) | that value, whatever the source says | `explicit` |
| `pose_server.py --video` (and anything built on it: the fake camera, `footage_camera.py`, `play.sh --video`, `pose_scoring_gate.sh`'s video mode) | **0** | `declared` |
| `pose_server.py --source-latency-ms N` (camera or file; e.g. a loopback camera fed from a file) | N | `declared` |
| `pose_server.py --camera` (a webcam, what the game spawns) | 100, `scoring.dta` `latency_offset` | `default` |
| `synthetic_kinect.py`, the in-process performer, an older server, the web page | 100 | `default` |

The engine prints one line each time the value or the reason changes:

```
DC3 POSE LATENCY: scoring at 100 ms -- default, scoring.dta latency_offset (the source declared nothing, as a live camera does; DC3_POSE_LATENCY_MS overrides)
DC3 POSE LATENCY: scoring at 0 ms -- declared by the pose source (pose_server.py: 0 for a recorded file, or --source-latency-ms; DC3_POSE_LATENCY_MS overrides)
```

A boot always prints the `default` line (LoadScoring runs before any pose
frame); the `declared` line follows on the source's first frame. `pose_server.py`
says on stderr what it declares (`Scoring latency: declaring 0 ms to the game`).

Not covered:

- **`synthetic_kinect.py --replay`.** The fixture is a different dance from
  the song's, so it has no right latency to measure, and changing it would
  move the gate's recorded replay/still numbers for no gain.
- **`--perform`.** It reads the choreography at the scorer's own latency
  (`/api/pose/target` `refSeconds`), so it is on time whatever the knob says.
- **The web page.** It declares nothing. Its harness camera is a looping
  mjpeg that is not synced to the song.

Tests:

- `SensorLatencyTest` pins the precedence, and a declaration that lands after
  LoadScoring.
- `PoseSocketReaderTest.ASourcesDeclaredLatencyReachesTheScorerAndAWebcamsDoesNot`
  pins the wire path.
- `tests/test_pose_server.py` `SourceLatencyTest` checks that every packet of a
  `--video` run declares 0 and no camera packet declares anything.

Each was sabotage-checked: four sabotages, each failing 1–3 tests.

**Grid re-check with no override (2026-11-14). The box was not quiet, so this
does not reproduce the 0.70.** I waited 29 min for the load to drop below 16, and
it never did: load average 95–200, from other lanes' builds and engines plus
`while True: pass` burners. All six songs logged both lines, `default` at boot and
then `declared … 0 ms` during the intro, before song select, with nothing after it.
`pose_server` printed `declaring 0 ms` six times. So every song was scored at 0
with no env var. That part is MEASURED.

The scores are not usable as a latency result. They are a measure of load:

| run | pose frames / log window | median capture → game age | score / card, mean |
|---|---|---|---|
| explicit 0, earlier grid (quiet) | ~120 | 65–67 ms | **0.70** |
| unset → declared 0, this run (load ~165) | ~30 | 182–258 ms | **0.04** |

A same-load control, run side by side (macarena twice at once, plus one more
engine; age about 148 ms in all three):

| run | score / card |
|---|---|
| macarena, declared 0 | 0.33 |
| macarena, explicit `DC3_POSE_LATENCY_MS=0` | 0.22 |
| indaclub, explicit `DC3_POSE_LATENCY_MS=0` | 0.17 |

Declared 0 scored no worse than explicit 0 under the same load, so the change
does what the env var did. INFERRED: the gap between 0.33 and 0.22 is
run-to-run spread, not an effect (one pair). On this harness, scoring falls steeply with
the pose rate. Re-run the six-song grid (scratch `lat_grid5.sh`, no env) on a
quiet box to get the number comparable with 0.70.

**For the owner's session (L-5):** run with `DC3_POSE_LATENCY_LOG=1` and note
the age. Play one song three times, at `DC3_POSE_LATENCY_MS` 50, 100 and 150,
and keep the best. Do not assume the curve is flat. The replay curve is
within 4% over −100..0, but on this live grid, with the box quiet, 50 ms off
the best cost 19% (0.70 → 0.57 mean), so the sweep is worth the three songs.
Close other GPU work first. A pose age that climbs during the sweep moves the
optimum (the "0 again" column), so a run whose logged age differs from the
others is not comparable to them.

## Player on screen (P-4)

**State (2026-11-13, `np-player-view`):** the player's silhouette is drawn by
the decompiled `StreamRenderer` from the camera picture, in the main-menu
player boxes (`cursor.milo`) and the gameplay box. Verified on screenshots from
synthetic frames and from real MediaPipe masks over a video; never on a live
webcam. Photos are not done (below).

### Who reads the Kinect image streams (ground truth: the decompiled code)

| Reader | Stream, format | When |
|---|---|---|
| `LiveCameraInput` (the device wrapper) | opens colour `NUI_IMAGE_TYPE_COLOR_YUV` 640×480 and depth `DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE` 320×240; `PollTracking` resets the polled/received flags every skeleton update | every frame |
| `StreamRenderer::DrawToTexture` (`.sr` objects) | depth\|player and/or colour per display mode (`DisplayStreams` = {2,1,1,1,1,2,3,3,3}: bit 1 depth, bit 2 colour); player colours by `GetPlayerIndexes` (game player *i* → NUI index skeleton+1); `mNumBlurs` blur passes; output = its render-target texture | each draw of a showing `.sr` |
| — `cursor.milo` `depth_buffer_left/right.sr` | mode 4 (player colours), blur 1 → `depth_buffer_*.tex`, drawn on the menu and gameplay boxes (`buffer_*` meshes, `srcAlphaAdd`) | menus, gameplay HUD |
| — `StreamRendererColor.sr` (augmented photo) | mode 8 crew photo: colour, background × `mCrewPhotoBackgroundBrightness` | photo moments |
| `DepthBuffer3D` | depth\|player at the 384-texel pitch (`row*0x180`) → a voxel grid (`grid_80by60`) | gameplay, where shown |
| `FreestyleMoveRecorder` | depth\|player (`src += 0x600` shorts = 4 rows) → a palette texture via `TexelsLock` | freestyle recording |
| `LiveCameraInput::TextureStore` (`StoreColorBuffer` / `StoreDepthBuffer` / `…Clip`) | colour → 640×480 16-bpp scratch texture (`YUVtoRGB`), depth → same | `ui store_color_buff_at` etc.; **no shipped script sends these** (party photos use `store_texture_at` / `store_texture_clip_at`, which copy a `TexRenderer` output) |
| `HamUI::Draw` snapshot | `take_snapshot` → `IncrementSnapshotCount`, `TexRenderer.rndtex` in `augmented_photo` renders into a 640×480 snapshot target | `midi_parsers.dta` `take` events, unless photos are disabled; `kinect camera max_snapshots` is 1 under `_DEBUG` |
| `CameraDump` / `camera_debug_depth` | debug | never in play |

Colour word layout (numeric, not bytes): bits 31..24 Cb, 23..16 Y0, 15..8 Cr,
7..0 Y1. Depth texel = `depth_mm << 3 | player`, player 0 = nobody, *k* =
skeleton slot *k*−1.

### Design

```
camera ─► MediaPipe (pose + per-pose segmentation masks)
            │  desktop: pose_server.py --frames WxH → protocol v3 packet
            │  web:     pose_worker.js → dc3_camera_push
            ▼
NativeCamera::Publish(Image{RGB888, ordinal mask, trackIds, hip depth})   any thread
            ▼  GestureMgr_NativePoll: Latch(slot for each trackId)
ConvertColor → 640×480 UYVY        ConvertDepth → 320×240 depth|player, pitch 384
            ▼  NuiImageStreamOpen / GetNextFrame / ReleaseFrame (typed natively)
LiveCameraInput stand-in (HX_NATIVE ctor: streams + display mats, no NUI/speech)
            ▼
StreamRenderer::DrawToTexture, HX_NATIVE body: compose on the CPU
(StreamCompose_Native: every display mode + [1 2 1] blur) → UploadRGBAToRndTex
```

- **Protocol v3** (`pose_server.py` docstring): v2 with magic `0x44503303` and,
  after the persons, `[u16 w][u16 h][u8 rgb=1][u8 mask=1][u16 0]`, `w*h*3` RGB888
  bytes, `w*h` mask bytes (0 nobody, *k* = the packet's *k*-th person; the most
  confident mask over 0.5 wins). Sent only when the game passes `--frames`;
  without it the server sends v2 byte for byte as before, and the reader still
  takes v1/v2. A v3 packet does not fit the socket buffer in one write, so the
  server keeps an outbox instead of treating a short write as a disconnect.
- **The picture is mirrored into the Kinect's view.** A Kinect frame is a
  mirror (the image's own projection, from `JointUtl`, puts the player's right
  at +u); a webcam frame as captured is not (`NormalizedToMeters` uses
  `x = 0.5 − nx`). `ConvertColor` / `ConvertDepth` flip columns; the wire
  carries the picture as captured.
- **No camera → no change.** The stand-in is created only after the first
  published frame (`NativeCamera::Active()`); until then
  `GestureMgr::GetLiveCameraInput()` stays null and every reader keeps its null
  path. `LiveCameraInput::sInstance` stays null natively in all cases (the
  skeleton fallback is the native sensor). The depth-image quads stay hidden
  as before.
- **MeshFilter** (`native/src/platform/MeshFilter.cpp`): P-3 (`np-gesture-menus`)
  unhid the box frame, glass, crowns and `silhouette_guy` (the empty-box
  placeholder and position guide, with arrows). P-4 gates the depth image
  itself: `buffer_left*` / `buffer_right*` (and their crowns) draw while
  `NativeCamera::Active()` and stay hidden without a stream (rule
  `depth-image`). Root cause of the hide: they show `depth_buffer_*.tex`, render
  targets nothing filled, so they drew as flat magenta/blue squares.
  Still hidden, with corrected comments:
  `pose_flash*` (an avatar `TexRenderer` → `projection.rndtex`, not the camera),
  `grid_80by60`
  (`DepthBuffer3D`'s voxel template; its draw is a native stub). `preview.mesh`
  (the song-preview movie `preview.tmov`) is no longer hidden (`np-media`): the
  movie decodes, and the card's opaque album meshes cover it as in Xenia (PLAN
  G-15). The ring that
  fades at a box's top-left is `cursor.milo`'s own animation (same fade whichever
  meshes are hidden); the left box draws its player in the right box's colour
  at alpha 0 by design (`srcAlphaAdd`).

### Measured

| | 160×120 | 320×240 (default) | 640×480 |
|---|---|---|---|
| v3 block per frame | 76,808 B | 307,208 B | 1,228,808 B |
| at 30 fps | 2.3 MB/s | 9.2 MB/s | 36.9 MB/s |
| `camera_block` (scale + mask) in `pose_server.py` | 0.44 ms | 0.25 ms | 0.65 ms |

- MediaPipe with masks on: median 21.7 ms per frame vs 21.6 ms off (same model).
- Unix socket: 0.046 ms per 307 KB packet.
- Web worker camera block: 1–3.5 ms per frame; worker masks vs desktop masks on
  the same three video frames: IoU 0.980 / 0.983 / 0.988.
- Web in game (debug build, Chromium 1223 headless shell, fake camera
  `dance.y4m` from `make_pose_fixtures.sh`, `pose-gameplay.mjs`): the gameplay
  box shows the dancer's silhouette; 4,691 pictures pushed, 0 rejected; picture
  block p50 2.25 ms / p95 7.6 ms (GPU shared with a desktop run).
- Engine (`GET /api/camera`, GPU run, `pose_server.py --video … --frames
  320x240`): latch (both conversions) 1.2 ms, compose + blur + upload 0.5 ms per
  box, frame age from arrival to texture upload 12–15 ms mean. Camera to box ≈
  inference 22 ms + transport < 0.1 ms + 12–15 ms + the frame's draw.

### Tests

- `native/tests/test_camera_stream.cpp` (milo-tests, no GPU): BT.601 colour
  against a float matrix and mirrored; depth\|player at the 384 pitch; an
  ordinal with no slot is nobody; the v3 block; the NUI stream contract (no
  frame until one is published, a latched frame delivered once, with its player
  index); player-mode and greenscreen compose; the box drawn only with a stream.
- `native/tests/test_native_camera_view.cpp` (`DC3_DTA_FLOW_TESTS`, one headless
  engine run each): `CameraViewTest` stands a v3 synthetic person 0.6 m right
  and asserts that at `main_screen` both `depth_buffer_*.tex` hold the sent
  mask, mirrored (752/768 cells agree; unmirrored would be 600);
  `NoCameraViewTest` (a v2 sensor) asserts no stream, no stand-in, nothing drawn.
- Sabotage, each built and run: player index dropped, colour not mirrored,
  depth not mirrored, compose ignoring the player, v3 block not parsed, box
  never gated, a frame delivered twice, stand-in without a stream — 8 of 8
  caught.
- `GET /api/camera?grid=WxH` (default 32×24): `active`, `standIn`, `latched`,
  `uploads`, `latchMsMean`, and per `StreamRenderer` output its size, uploads,
  `composeMsMean`, `frameAgeMsMean` and the last picture sampled on the grid
  (`rrggbbaa`). Works headless (`MILO_NORENDER=1`): `DrawToTexture` runs and the
  CPU copy is what it reports.

### Left

- **Photos: root-caused (`np-media`); the fix is an engine change, not landed.**
  Where the game shows them: `perform_endgame_panel reveal_photos` puts up to three
  snapshots on `result_photoshare_photo1..3.mesh` after the results timer, in every
  mode **except** campaign, rhythm battle, holla back and fitness playlists
  (`enable_photos` is an `unless`); perform takes 16 in the freestyle section
  (`photo_taker`, `config/midi_parsers.dta`), Dance Battle one at the final pose
  (`on_final_pose_photo`); `endgame_photos.milo` (share) shows one through `photo.mat`.
  Cause: `ui/augmented_photo.milo`'s `TexRenderer.rndtex` renders the crew photo into
  the snapshot with `clear_buffer 1`, `clear_alpha 1`, and all six of its materials
  have `alpha_write 0`, so the photo's alpha is that clear. On the 360 it is an
  immediate `D3DDevice_Clear` of the bound target (`DxRnd::Clear`, `DxCam::Select`
  100% `name_check`; a rendered `DxTex` is A8R8G8B8 at any bpp). The engine's
  `WgpuRnd::Clear` only stored the colour for the next frame pass, so the snapshot kept
  the texture pass's transparent black, and `result_photoshare_photo1..3.mat` are
  `kBlendSrcAlpha`: the photo was invisible. The render-target alpha rule
  (engine `17d00b6`) leaves these materials alone, as the image does: all six
  set `force_alpha_write 1` with `alpha_write 0` (asserted in the test). On `fe22a2e`
  with the clear disabled, 0 of 307200 texels are opaque. Measured, Dance Battle with two synthetic
  performers and `--frames 320x240` (GPU, `DC3_HTTP_PACE_FPS=30`, 1 snapshot):
  pinned engine — the results card shows only its "DC3" backing
  (`/home/free/tmp/claude-1000/np-media-photoA3/res/r_027_22s.png`); with
  engine branch `np-media` (`c810e49`; series also in `~/tmp/np-media-engine.patch`) — the two performers in colour over the dimmed
  background (`…/np-media-photoB3/res/r_029_21s.png`). Guard:
  `TexRendererClearTest` (`native/tests/test_texrenderer_clear.cpp`, GPU; compiled
  only against an engine with the patch): 307200 of 307200 texels opaque with it,
  0 with `WgpuRnd::Clear` reverted. `endgame_photos`' `photo.mat` is `kBlendSrc`, so
  that screen never depended on alpha. Party team photos (`store_texture_at` /
  `store_texture_clip_at`) were not reached (`--route party` stalls on
  `tutorial_crew_showdown_screen_1`; seeding the all-tutorials-seen cache as
  `test_native_twoplayer.cpp` does gets battle past its tutorials).
  Harness note: under `MILO_HEADLESS` the UI clock advances 1/30 s per frame
  (`UIManager::Poll`), so an unpaced GPU run passes the results timers in a second or
  two; pace it (`DC3_HTTP_PACE_FPS=30`) to see them.
- `DepthBuffer3D`'s draw (native stub), `FreestyleMoveRecorder`'s palette
  (`TexelsLock` on a texture with no CPU texels natively), the
  `TextureStore::StoreColorBuffer` family (same: a 16-bpp `kScratch` `RndTex`
  has no bitmap natively, so `TexelsLock` returns null — unreachable from
  shipped data, but a debug `ui store_color_buff_at` with a stream live would
  write through it).
- `pose_flash*` is not a camera mesh; it belongs to the avatar projection.
- A live webcam session (P-2), desktop and web.

## Web

| Stage | Code |
|---|---|
| Camera | `native/web/pose/pose_input.js`: `getUserMedia` (640×480 ideal, 30 fps) on page load; states shown in a panel in the canvas's bottom band, centred between the two help bars (W-13): asking → loading model → looking for you / tracking, or blocked / no camera / error with a retry button. Mirrored preview with the BlazePose skeleton drawn (green tracked, amber inferred; joints under 0.3 are not drawn because the engine holds them). |
| Frames → worker | `MediaStreamTrackProcessor` (max buffer 1, stale frames dropped) transfers the camera stream to the worker; no per-frame main-thread work. Fallback `?pose_transport=bitmap`: `requestVideoFrameCallback` + `createImageBitmap`. |
| Inference | `native/web/pose/pose_worker.js`, a module worker: `@mediapipe/tasks-vision` 1.0.0 (the Python mediapipe version) `PoseLandmarker`, VIDEO mode, the same `pose_landmarker_full.task` and options as `pose_server.py`. CPU (XNNPACK) delegate by default. |
| Geometry | `native/web/pose/pose_geometry.js`: line-for-line port of `pose_mediapipe.py` (root DLT/torso fallback, axis flips, centre joints, hands, feet, one-euro on root depth, centroid tracker). Timestamps are the `VideoFrame` capture time. |
| Push | `Module.dc3PosePush` (`native/web/dc3_pre.js`, heap copy) → `dc3_pose_push` (`native/src/platform/PoseInput_Web.cpp`) → `PublishFrame`. First push starts the provider; `dc3_pose_stop` (track ended) returns to the dummy. A 0-person frame is real: nobody tracked. |
| Picture (P-4) | With `?pose_frames=WxH` (default `320x240`, `off` = skeletons only) the worker turns on segmentation masks, scales the frame to RGB888 and builds the same ordinal mask as `pose_server.py`; `pose_input.js` sets `globalThis.dc3CameraPending` and calls `Module._dc3_camera_push(w, h, n, t)` right after the frame's `dc3_pose_push` (`native/src/platform/CameraInput_Web.cpp`, copies with `EM_ASM`, so `dc3_pre.js` is untouched) → `NativeCamera::Publish`. `_dc3_camera_stop` on track end. Stats: `picturesPushed` / `picturesRejected` / `pictureMs` in `dc3PoseSummary()`. |
| Status | `window.dc3PoseSummary()` (page: state, frames, fps, inference ms) and `window.dc3PoseGame` (engine: player 0's bound skeleton joints, score, rating; every 15 engine frames). |

URL knobs: `?pose=off|manual`, `pose_model=lite|full|heavy`,
`pose_delegate=CPU|GPU`, `pose_transport=stream|bitmap`, `pose_cdn=1`,
`pose_hfov=<deg>`.

Assets: `scripts/web/build.sh` vendors tasks-vision (bundle + module wasm, from
`scripts/web/node_modules`, `npm ci --prefix scripts/web`) and the `lite`/`full`
`.task` models (from `native/models`) into `native/web/build/pose/`, so the page
loads nothing cross-origin and needs no CDN inside its COOP/COEP isolation.
Since W-13 a missing piece is a **build failure** (`build.sh` runs `npm ci` when
tasks-vision is absent and fails `pose-vendor` if it is still missing, or if a
model is), and the page never falls back on its own: it logs
`[dc3-pose] pose model not deployed on this server: …` as an error, shows "Pose
model is not installed on this server — playing without camera", and the game
plays on the stand-in. `?pose_cdn=1` is the explicit opt-in to jsdelivr /
storage.googleapis.com (the survey found jsdelivr's headers compatible;
storage.googleapis.com is unverified).

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

## Performance (L-3, `np-pose-perf`, 2026-11-13)

Where a pose frame's time goes, on both paths, and what moves it. The target
is 30 fps at the lowest latency on the GPD Pocket (Ryzen AI 9 HX 370,
Radeon 890M, the playtest box). Every number is MEASURED unless marked
INFERRED. GPD runs were taken idle (load average 1–3) under
`flock -x /tmp/gpd-bench.lock`, between 15:36 and 16:05 PST. The rig
(32 cores) was running other lanes at load average 50–80, so its numbers
are only used side by side.

### The answer in one paragraph

Model inference is the frame on both paths; everything else together is
under 2 ms (desktop) or 5 ms (web). On the desktop, `num_poses 2` with one
dancer runs MediaPipe's person detector on every frame. That costs the GPD
17 ms of its 45 ms frame (21.7 fps). `--probe-second-person 0.5` tracks at
`num_poses 1` and looks for a second person off-thread twice a second:
**35.2 fps** with one dancer, with output identical to `num_poses 1`. Two
dancers cost 48 ms whatever is set (19.8 fps; 26.7 with `lite`). The web's
CPU (WASM) inference takes 73 ms on the GPD (12.7 fps). The GPU delegate
gets **29.1 fps** (**30.1** with `pose_persons=1`). That is the pose pipeline
alone: the game did not boot in headless Chromium there.
Its landmarks differ from the desktop's by 22 mm mean, though, so it stays
opt-in. Defaults are unchanged; both switches are owner decisions (below).

### Desktop stage breakdown (`scripts/pose_bench/stage_bench.py`)

The real `MediaPipeBackend.process` + `camera_block` + `pack_frame` over a
640×480 30 fps clip, unpaced, `--frames 320x240`, full model, CPU, p50 ms
(one dancer: 60 s of the np-video-calib stream; two: the playtest kit's
macarena duo):

| stage | GPD, poses 2 | GPD, poses 1 / probe 0.5 | GPD, two dancers | rig, poses 2 |
|---|---|---|---|---|
| decode (`cv2.read`) | 0.40 | 0.40 | 0.41 | 0.35 |
| BGR→RGB + `mp.Image` | 0.24 | 0.24 | 0.23 | 0.19 |
| **inference** | **43.2** | **25.6** | **48.1** | **21.8** |
| geometry (root, remap, mask copy) | 0.65 | 0.66 | 0.90 | 0.37 |
| tracker + root filter | 0.06 | 0.06 | 0.10 | 0.04 |
| v3 picture + mask | 0.52 | 0.53 | 0.68 | 0.45 |
| pack | 0.07 | 0.07 | 0.08 | 0.06 |
| **total → fps unpaced** | 45.5 → **21.7** | 27.9 → **35.2** | 50.7 → **19.8** | 23.4 → 42 |

The GPD numbers are from two interleaved runs each, which agree to 0.1 ms.
The socket write is not in the table: `Outbox` is a non-blocking send of
about 307 KB, and the engine's reader thread takes it in 12–15 ms from
arrival to texture (P-4).

Model, `num_poses` and the picture, GPD, inference p50 ms (fps unpaced):

| model | poses 2 | poses 1 | poses 2, `--frames` off | poses 1, `--frames` off |
|---|---|---|---|---|
| lite | 36.1 (25.6) | 18.5 (47.2) | 34.3 (27.9) | 16.9 (53.6) |
| full | 43.1 (21.8) | 25.7 (35.2) | 41.7 (22.9) | 23.9 (38.7) |
| heavy | 100.6 (9.7) | — | — | — |

- The person detector is the lever, not the model. With one dancer, poses
  2 → 1 saves 17.5 ms (40%), and full → lite saves 7 ms.
- The segmentation masks (the player view) cost 1.5–2 ms of inference plus
  0.5 ms for the picture.
- MediaPipe keeps about 1.4–1.8 cores busy (`infer_cpu` / wall). mediapipe
  1.0.0's Python `BaseOptions` has no thread count to raise.

### Web stage breakdown (`scripts/pose_bench/web_pose_fps.mjs`)

The page's own per-stage times (`window.dc3PoseSummary().stages`, added in
this lane). Headless Chromium 156 on the GPD, the macarena clip as a fake
640×480 30 fps camera, and a 15 s window after 25 s, p50 ms. **The game did
not boot in headless Chromium on the GPD.** It stays in `waiting` and
`dc3FrameCount` never advances; getting the game up there is np-gpd-test's
browser setup. So this table is the pose pipeline alone, with no game
sharing the GPU or the CPU:

| URL | pose fps | wait | infer | picture (draw / rgb / mask get / mask) | worker busy |
|---|---|---|---|---|---|
| (default: full, CPU, persons 2) | 12.7 | 0.1 | 73.5 | 3.8 / 0.32 / 0 / 0.47 | 78.2 |
| `pose_persons=1` | 17.7 | 0.1 | 51.6 | 3.6 / 0.32 / 0 / 0.48 | 56.1 |
| `pose_model=lite` | 15.7 | 0.1 | 58.2 | 3.9 / 0.32 / 0 / 0.47 | 63.1 |
| `pose_model=lite&pose_persons=1` | 24.5 | 0.1 | 35.8 | 3.7 / 0.32 / 0 / 0.48 | 40.4 |
| `pose_delegate=GPU` | 29.1 | 2.6 | 22.6 | 3.6 / 0.42 / 2.1 / 0.60 | 29.6 |
| `pose_delegate=GPU&pose_persons=1` | **30.1** | 10.9 | 15.5 | 3.8 / 0.33 / 1.9 / 0.47 | 22.4 |

Notes on the table:
- Worker → page transfer is 0.2–0.3 ms, and the page's handler (engine push
  + preview) is 0.1–0.16 ms.
- At 30.1 fps the worker waits 10.9 ms a frame for the camera: it is
  camera-bound.
- Instrument check: `pose_slow=camera:10` moved `draw` from 3.84 to 13.89 ms
  and `busy` from 78.2 to 88.3 ms. `infer` was unchanged (73.3), and fps fell
  12.7 → 11.3.

With the game running: not measured yet. The rig A/B
(`scripts/pose_bench/web_pose_fps.mjs`, which also reports the game's own
frame rate over the window) needs the exclusive GPU lock, and it was not
granted within 30 minutes while other lanes held it shared. Run it again on a
quiet rig before quoting a game-plus-pose number.

### Web second-person probe (`np-web4`, 2026-11-14)

`?pose_probe=<s>` (with the default `pose_persons=2`): the worker keeps a
numPoses 1 and a numPoses 2 landmarker, runs numPoses 1 on every frame and
numPoses 2 on one frame every `s` seconds, switches to numPoses 2 on every
frame once it sees two people and back after 1 s with fewer
(`pose_worker.js` `detect`; desktop's `SecondPersonProbe`). Off by default.

Parity (`scripts/web/pose_parity.mjs --probe 0.5`, CPU, MEASURED): one dancer,
150 frames: joint mean 3.26 mm against the desktop reference, the same as
`pose_persons=1` (10 probes, 0 switches, person count agrees on 150/150). Two
dancers, 90 frames: it switched on the first probe, joint mean 2.1 mm, person
count 90/90; `pose_persons=1` on the same frames agrees on 0/90.

Throughput with the game running (`web_pose_fps.mjs`, rig, attract screen,
`dance.y4m` fake camera, release, two interleaved rounds, load 30–55 with other
lanes; fps / inference p50 ms):

| URL | round 1 | round 2 |
|---|---|---|
| default (persons 2, CPU) | 18.1 / 41.0 | 11.7 / 63.7 |
| `pose_probe=0.5` | 16.9 / 28.8 (probe pass 51.1) | 10.9 / 51.4 (probe 70.5) |
| `pose_persons=1` | 25.3 / 30.3 | 12.3 / 50.9 |
| `pose_delegate=GPU&pose_probe=0.5` | 13.8 / 30.5 | 11.5 / 31.3 |
| `pose_frames=off&pose_probe=0.5` | 22.5 / 37.8 | 12.2 / 63.7 |

The probe saves ~12 ms on the frames it does not probe (28.8 vs 41.0 ms) and
still loses overall: the probe frame pays both passes in the one worker, and
the single pass's p95 rose to 107 ms (69 ms default). INFERRED: on web the
probe needs its own worker, as desktop runs it on a thread. Round 2's
whole-row slowdown is the box, not the settings. Nothing here reached 30 fps,
so the default is unchanged.

### Ranked levers

1. **Desktop: `--probe-second-person 0.5`.** GPD, one dancer:
   21.7 → 35.2 fps, inference 43.2 → 25.6 ms. Opt-in.
   `pose_mediapipe.SecondPersonProbe` keeps a 1-pose and a 2-pose
   landmarker:
   - With one dancer, the 1-pose landmarker runs every frame. The 2-pose
     landmarker gets a copy of the newest frame every 0.5 s on a background
     thread (MediaPipe releases the GIL).
   - Once two are seen, the 2-pose landmarker runs every frame. When it has
     seen fewer than two for 1 s, it goes back.
   - Cost: the probe's CPU, visible only as `infer_cpu` p95 rising from
     36 to 56 ms in the frames it overlaps.
   - Join latency: the second player is found up to one interval later. On
     a synthetic join (half the duo clip blacked out for 3 s) it took 1 and
     17 frames, against 0 and 1 for `num_poses 2`.
2. **Web: `pose_delegate=GPU`.** GPD, pose pipeline alone: 12.7 → 29.1
   fps (30.0 on a repeat; 30.1 with `pose_persons=1`). Accuracy is not free
   (below), so it stays opt-in. With the game on the same GPU, it was
   measured on the rig only (table above).
3. **`pose_persons=1` / `--max-persons 1`.** The same 40% inference cut
   as lever 1, but a second player is never found. Web CPU: 12.7 → 17.7 fps.
4. **`lite`.** GPD desktop: 7 ms saved with one dancer; with two dancers,
   48 → 34 ms (19.8 → 26.7 fps). Accuracy is unmeasured against the scoring
   grid, so it stays opt-in.

### Accuracy (`scripts/pose_bench/persons_agreement.py`)

The same frames and timestamps went through two backends, and the joints the
game would receive were compared (one dancer, 600 frames, full model):

| A vs B | person count differs | joint distance p50 / p95 (3D) | image-plane p50 / p95 |
|---|---|---|---|
| poses 2 vs poses 2 (control) | 0 | 0.0 / 0.0 mm | 0.0 / 0.0 mm |
| probe 0.5 vs poses 1 | 0 | 0.0 / 0.0 mm | 0.0 / 0.0 mm |
| poses 1 vs poses 2 | 0 | 24.7 / 68.6 mm | 14.2 / 40.2 mm |

- The output is different, not worse. With `num_poses 2` the detector's box
  re-seeds the landmark region on every frame. `num_poses 1` follows its own
  landmarks.
- **The scoring calibration (np-video-calib) was recorded at `num_poses 1`**
  (`scripts/video_calib/record_segment.py`). So the probe gives the live
  server the input the calibration measured. INFERRED: its one-player
  scoring is the calibrated one. The grid was not re-run here.
- Web GPU delegate vs the desktop reference (`scripts/web/pose_parity.mjs
  browser`, the 150 np-web-pose fixture frames on the rig): 150/150
  detections agree, joint error mean 22 mm, p95 52 mm, max 269 mm. On CPU it
  is mean 0.05 mm, p95 0.14 mm. A one-frame-shift sabotage reads 28 mm. The
  GPU delegate moves the input about as much as a frame of lag. Its effect
  on the score is not measured.

### Negative results

- **No capture cliff, so no grabber thread.** I expected OpenCV's 1-buffer
  V4L2 capture to lose a whole camera period whenever processing outlasts it.
  It does not, measured on the GPD's camera with `scripts/pose_bench/
  camera_probe.py`. The room light held that camera at 10–12 fps (YUYV,
  aperture-priority auto exposure, no frame-rate control).
  - At 120 ms of work, the sequential loop ran at 8.32 fps = 1000/120, and
    `read()` returned in 0.2 ms with a buffered frame.
  - The cost is staleness. Frame age at the loop was p50 100 ms sequential
    vs 77.6 ms with a newest-wins grabber thread (150 ms work: 129 vs 83).
  - When the work is shorter than a camera period (60 ms at 81 ms), both
    shapes read 41.5 ms (age p95 56 vs 46).
  - So a grabber thread only pays when inference outlasts the camera
    period. Then it saves up to one period of age. INFERRED: about 12 ms
    mean at the GPD's 45 ms frame on a 30 fps camera. With the probe, the
    frame is 28 ms, inside the period, so I did not build it.
- **No GPU delegate in Python.** The PyPI mediapipe 1.0.0 wheel says
  `ImageCloneCalculator: GPU processing is disabled in build flags`.
- **The picture's JS loops are not worth optimising.** RGBA→RGB is 0.32 ms
  and the ordinal mask 0.47 ms. The picture's cost is `drawImage` +
  `getImageData` (3.6–3.9 ms) and, on the GPU delegate, the mask readback
  (2 ms). A rewrite with 32-bit reads and lookup tables was written and not
  kept.
- **The camera clock adds latency of its own.** The GPD camera's buffers
  arrive 41–53 ms after their V4L2 timestamp at 10–12 fps. That is exposure
  plus USB, before any of our code runs.
- **`lite` alone barely helps with one dancer.** At `num_poses 2`, `lite`
  still runs the person detector on every frame, so it saves only 7 ms on
  the GPD (43.1 → 36.1).

### Reproducing

```bash
# desktop stage table (any clip; the GPD numbers used a 60 s, 640x480 excerpt)
.venv/bin/python scripts/pose_bench/stage_bench.py --video clip.mp4 --model full --num-poses 2
.venv/bin/python scripts/pose_bench/stage_bench.py --video clip.mp4 --probe 0.5
.venv/bin/python scripts/pose_bench/stage_bench.py --video clip.mp4 --slow infer=20   # instrument check
# accuracy
.venv/bin/python scripts/pose_bench/persons_agreement.py --video clip.mp4 --a 1 --b 2
.venv/bin/python scripts/pose_bench/persons_agreement.py --video duo.mp4 --a probe:0.5 --b 2 --blank left --join 3
# camera capture shapes (opens /dev/video0)
.venv/bin/python scripts/pose_bench/camera_probe.py --work 0,60,120,150
# web (serve a build first: python3 native/web/server.py --port 8451)
node scripts/pose_bench/web_pose_fps.mjs --port 8451 --camera clip.y4m "" "&pose_persons=1" "&pose_delegate=GPU"
```

## Frame fill (`np-pose-fill`, 2026-11-14)

The scorer was tuned on a 30 Hz Kinect. The GPD's camera delivers ~8.4 pose
frames a second, and at 8 Hz the same footage scores **0.39** of what it
scores at 30 Hz (table below). The loss is the scorer's time-error term:
`DetectFrame::AddError` keeps, per choreography frame, the best sensor frame
within reach, and at 8 Hz the nearest one is up to 60 ms away. More inference
speed (L-3) is the real cure. The fill is what the game does with the frames
it does get.

**Default: `extrap` at gain 1.0, which is deliberately generous.** Owner
decision, 2026-11-14: *"i would rather over credit and then calibrate
further. it feels fine to be a bit generous and then dial it back"*. The
default is therefore the mode that recovers the most score at 8–15 Hz.
- **It scores above a real 30 Hz camera** on the footage: 1.05–1.10.
- **The knob to dial it back is `DC3_POSE_FILL_GAIN`** (0..1). The
  [score-vs-gain table](#calibrating-dc3_pose_fill_gain) is the lookup: at
  8 Hz, gain 0.75 lands on a real 30 Hz camera's score.
- **`DC3_POSE_FILL=off|hold|interp|extrap|extrapc`** picks the mode. Unset
  means `extrap`; an unknown value is logged and the default used.
- `interp` is the conservative alternative. It never scores above real
  30 Hz (0.92 at 8 Hz), at the cost of a ~150 ms skeleton delay.

Code:
- `native/src/platform/PoseFill_Native.{h,cpp}`: the math and the schedule.
- `GestureMgr_Native.cpp` `BuildSensorFrame`: slots, levelling, recording.
- `Skeleton_Native.cpp` `Dc3SetPoseFillDelaySeconds`: interp's latency fold.

### What every mode shares

- **Engages only for a slow camera.**
  - It engages when the median of the last 15 camera-frame intervals,
    measured in song time, is above 1/24 s. It releases below 1/27 s.
  - A 30 Hz source never engages it, so at 30 Hz the stream is the camera's
    own (measured below).
  - A slow *game* (song loading, a box at load 150) also reads as a slow
    camera, because the rate is measured in song time. That is deliberate:
    what the scorer sees is frames per song second.
- **Is never recorded.** `DC3_POSE_RECORD` writes camera frames as they
  arrive, before the fill. Fill frames are not counted in `pose_fps`.
- **Does not bridge tracking dropouts.** Nothing is filled after a frame
  with nobody in it. So a detection dropout reaches the slot layer, P-12's
  1 s dropout hold and the 2 s tracker memory as the camera's own frames,
  exactly as with the fill off.
  `PoseFillSensorTest.ADropoutReachesTheHoldExactlyAsWithTheFillOff` counts
  held frames: 8 off, 8 interp, 8 extrap.
- **One log line** on engage, release, or an interp delay change ≥ 20 ms:
  `DC3 POSE FILL: extrap (gain 1.00) engaged at song 12.34 s (camera 8.4 frames/s, stream delay 0 ms added to the scorer's latency)`.

### What `extrap` (the default) does

Each camera frame is delivered the moment it arrives. Until the next one,
a frame every 1/30 s of song time steps it on by its joints' velocity from
the frame before, times the gain. It adds no delay, so the scorer's latency
is the camera's.

The guards are what keep the generosity aimed at the dancer:
- **Horizon: one source period, then nothing.** The period is the last
  pair's, but never more than the median interval, since a dropped frame
  doubles the pair's. After that the stream waits for the camera, so a
  stall stays a stall. The prototype (a3a9eccf6) held the stepped pose for
  0.5 s.
- **Speed clamp.** Each joint's velocity is clamped to 4 m/s. That is p99.6
  of the np-video-calib footage's joint steps at 8 Hz: p90 0.18 m, p99
  0.43 m per 125 ms.
- **Flip guard.** A person is not stepped at all when some L/R pair of the
  new frame sits closer to the *other* side's joint of the frame before
  than to its own, by more than half. A tracker's left/right swap is held
  where the camera put it, never amplified.
  - Measured on the same footage at 8 Hz, frame pair by frame pair, it
    fires on 0.1% of real motion (hands crossing) and on 99.7% of
    synthetic whole-body swaps.
  - Size alone cannot tell them apart: the L/R separation (median 0.37 m)
    is an ordinary 8 Hz step.
- **No guesses.** A joint not tracked in either frame is not moved.
  Inferred joints are moved.
- **No velocity across a camera gap** longer than min(2.5 × median, 0.5 s).
- **A person not in the frame before** is copied, not moved.

### What `interp` does (the conservative mode)

- **Runs D behind the camera.** At song time S the stream shows the moment
  σ = S − D, with D = 1.2 × the median interval, clamped to [1/30, 0.30] s,
  with 15 ms hysteresis. At 8 Hz, D ≈ 150 ms.
  - Every camera frame is delivered exactly, oldest first.
  - Between two camera frames, a fill frame on the 30 Hz σ-grid is their
    linear interpolation. So it lies on the segment between two real poses
    and cannot overshoot.
  - It fills only between frames with the same people (`SamePeople`), and
    nothing within half a step of the next camera frame.
  - The stream is never later than the newest camera frame.
- **Folds D into latency compensation.** D is added to whichever sensor
  latency wins `np-latency-harness`'s precedence: explicit
  `DC3_POSE_LATENCY_MS` > the source's declared latency > `scoring.dta`'s
  100. The call is `MoveDir::NativeSetLatencySeconds(resolved + D)`.
- **On release**, buffered camera frames not yet delivered (at most D's
  worth) are skipped.
- **Cost.** The skeleton that the gesture cursor, the player box and the
  `shot` heuristics read runs D behind the camera while engaged. The scorer
  compensates; they do not.

The other modes:
- `hold` re-sends the newest frame for up to 0.5 s; it equals
  `extrap` at gain 0, except for the horizon.
- `extrapc` is the prototype's step, clamped: half a period, tracked joints
  only, 10 cm a joint.

### Measured: footage fixtures (`native_song_sweep.py`, no GPU)

**Setup.**
- 8 songs: macarena, indaclub, boyfriend, 12step, starships, supersonic,
  scream, cupidshuffle.
- Medium, freestyle disabled, `DC3_FAST_TIME`, in-process performer
  replaying the np-video-calib footage (`replay:…/pose_songlead/{song}.dc3pose`,
  `DC3_POSE_PERFORM_REPLAY_SYNC=song`).
- `DC3_POSE_PERFORM_HZ` sets the camera rate. `DC3_AUDIO_GAIN=0`.
- **Box load (1-min) 25–84** across all grids (logged at every config's
  start and end).
- Under FAST_TIME the ratings are deterministic up to the scorer's own
  run-to-run spread: one rating flip, or ≤ 0.02% of score, between two
  `off` runs.

The tables give score as a fraction of the same footage at a real 30 Hz
with the fill off (`h30_off`, 14,607k over 8 songs, ratings
373/156/28/11). All MEASURED.

| camera | off | hold | **extrap g1 (default)** | extrap prototype | extrapc | interp |
|---|---|---|---|---|---|---|
| 8 Hz | 0.391 | 0.767 | **1.054** | 1.054 | 0.955 | 0.916 |
| 10 Hz | 0.557 | 0.854 | **1.098** | 1.101 | 1.029 | 0.940 |
| 15 Hz | 0.803 | 0.912 | **1.078** | 1.076 | 1.061 | 0.964 |
| 30 Hz | 1.000 | – | **1.000**, ratings 373/156/28/11 = `off` | – | – | 1.000 |

- **The guards cost nothing on clean footage.** Guarded extrap matches the
  prototype to 0.003.
- **Ratings at 8 Hz, default:** 375/164/21/8, against real 30 Hz
  373/156/28/11.
- **Per song**, extrap g1 at 8 Hz runs 0.93–1.13.
- **The 30 Hz runs:**
  - `h30_x` (default build) 373/156/28/11 and never engaged.
  - The `off` repeats: 373/156/28/11 and 372/157/28/11.
  - `h30_interp_c/d`: 372/157/28/11 and 373/156/28/11.
- **The 30 Hz run with 4 songs:** `h30_x4` had macarena time out on
  main_screen before gameplay, the same harness navigation flake as an
  earlier fill-off run (`h15_off`, macarena). The other 3 songs were
  identical to `off`.

### Calibrating `DC3_POSE_FILL_GAIN`

Score of extrap at each gain as a fraction of real 30 Hz with the fill off
(8 songs, MEASURED). Gain 0 is a hold of at most one period.

| gain | 8 Hz | ratings p/a/o/b | 10 Hz | 15 Hz | ratings p/a/o/b |
|---|---|---|---|---|---|
| (fill off) | 0.391 | 95/297/100/76 | 0.557 | 0.803 | 259/248/39/22 |
| 0 | 0.767 | 242/250/55/21 | – | 0.909 | 331/186/38/13 |
| 0.25 | 0.818 | 285/212/54/17 | – | 0.934 | 345/174/38/11 |
| 0.5 | 0.904 | 321/194/39/14 | 0.977 | 1.004 | 369/159/30/10 |
| 0.75 | **0.989** | 357/172/29/10 | – | 1.051 | 389/145/26/8 |
| **1.0 (default)** | **1.054** | 375/164/21/8 | **1.098** | **1.078** | 397/141/23/7 |
| (real 30 Hz) | 1.000 | 373/156/28/11 | 1.000 | 1.000 | 373/156/28/11 |

- **The score rises monotonically with the gain** at both rates, so "best"
  means the most score, and that is gain 1.0.
- **Parity with a real 30 Hz camera** is gain ≈ 0.75 at 8 Hz and ≈ 0.5
  at 15 Hz. The owner's later calibration down starts there.
- INFERRED cause of going above 1: the scorer rewards motion energy, and a
  stepped-on frame overshoots every turn of a limb. The noise rows below
  show the same property from the other side.
- **On the owner's real session (concern (c) runs, below)**, the
  *prototype* extrap scored 1.9× `off`. The guarded default was not
  re-run on that session.

### Measured: the controls (generous to a dancer, not to anyone else)

Fraction of the **right-dance** real-30 Hz score on the same 4 songs
(starships, macarena, indaclub, supersonic: 7,537k):

| input | real 30 Hz, fill off | 8 Hz, fill off | **8 Hz extrap (default)** | 15 Hz extrap | 8 Hz interp |
|---|---|---|---|---|---|
| **standing still** (`DC3_POSE_PERFORM=stand`) | 0 | 0 | **0** | 0 | – |
| standing still + NZ noise | 0.026 | 0.001 | **0.024** | – | – |
| **wrong dance** (`wrongfx`) | 0.011 | 0.005 | **0.019** | 0.017 | 0.009 |
| wrong dance + NZ noise | 0.049 | – | **0.036** | – | 0.021 |

- **Standing still earns nothing**, with or without the fill: every move
  rated bad, score 0. The fill steps on motion, and a still body has none.
- **With noise, standing still at 8 Hz plus extrap stays below the same
  noise at real 30 Hz** (0.024 vs 0.026).
- **The wrong dance stays near zero:** 1.9% of the right dance at 8 Hz.
  That is 1.6× what a real 30 Hz camera gives it (1.1%), against 1.05× for
  the right dance.
- **The wrong dance with noise is below the same noise at real 30 Hz**
  (0.036 vs 0.049).
- **The scorer's own jitter reward**, with the fill off, matters more than
  the fill here: noise multiplies a wrong dance's score by 4.3 at 30 Hz
  and gives a still body 2.6%.
  - The wrong-dance fixture swaps the footage: starships ↔ macarena,
    indaclub ↔ supersonic.

**Noise.** The performer injects it.
- `DC3_POSE_PERFORM_NOISE=sxy,sz,rho,seed` is per-joint jitter (metres,
  image plane and depth).
- `DC3_POSE_PERFORM_GLITCH=infer,untracked,flip,drop,seed` gives per-frame
  probabilities:
  - a joint marked INFERRED, 5 cm off;
  - a joint NOT TRACKED, 30 cm off;
  - the whole body's left and right swapped;
  - the frame dropped (not published).
- NZ = `NOISE=0.015,0.04,0,11`, `GLITCH=0.03,0.01,0.01,0.05,13`: 1.5 cm /
  4 cm jitter, 3% inferred, 1% untracked, 1% flipped, 5% of frames
  dropped.
- NL = `NOISE=0.005,0.015,0,11`, `GLITCH=0.01,0.003,0.003,0.02,13`.
- FLIP = `GLITCH=0,0,0.1,0,13`: 10% of frames body-swapped.

Fraction of the same noise at real 30 Hz, fill off (MEASURED):

| noise | 8 Hz off | **8 Hz extrap** | 15 Hz extrap | 8 Hz interp | 15 Hz interp |
|---|---|---|---|---|---|
| NZ (vs n30_off = 19,603k, 8 songs) | 0.241 | **0.827** | 0.992 | 0.757 | 0.920 |
| NL (vs nl30_off = 17,106k, 8 songs) | 0.338 | **0.954** | – | 0.825 | – |
| FLIP (vs f30_off = 8,428k, 4 songs) | 0.395 | **0.942** | – | – | – |

- **Noise does not blow up**: under every noise, extrap stays at or below
  the same noise at a real 30 Hz.
- **Clean, extrap is at 1.05–1.08.** Under noise it recovers less than it
  does clean.
- INFERRED: the clamp and the guess rule take out the outliers' step. Under
  10% body flips, extrap at 8 Hz scores 0.94, against 1.06–1.13 clean on
  those songs: the flip guard holds the swapped frames instead of stepping
  them on.
- **Noise raises the scorer's 30 Hz score**: NZ is 1.342× the clean
  footage, NL 1.171× and FLIP 1.12×. That is the scorer's property, not
  the fill's.

### Measured: concern (c), the owner-session timeouts

Two owner-session fill replays hit the 900 s timeout at load 138. The
re-run used four replays of `owner_bofa.dc3session` (betteroffalone,
beginner), run **concurrently**, with:
- GPU, under the gpu1 flock;
- `DC3_FAST_TIME`, the kitdrive route, `DC3_PERF_LOG=1`, `DC3_AUDIO_GAIN=0`;
- box load 50–70.

All four completed: 77 moves, 92 rating lines and 59 PERF windows each.
These were the builds before the guarded extrap, so `extrap` here is the
prototype.

| run | score | perfect/awesome/ok/bad | mean fps |
|---|---|---|---|
| off | 193,326 | 4/20/7/46 | 82.0 |
| interp | 245,896 | 7/22/6/42 | 81.3 |
| hold | 208,335 | 7/17/3/50 | 82.7 |
| extrap (prototype) | 362,665 | 10/21/4/42 | 80.0 |

- **The timeouts were load, not the fill.** Every fill mode runs at the
  frame rate of `off`, and the earlier runs managed 18–20 fps at load 138.
- The filler's cost is negligible: about 10k `Poll` calls run in the 0 ms
  unit tests.

### Tests

**`native/tests/test_pose_fill.cpp`**: math, schedule and latency, with no
game or GPU.
- `PoseFillMathTest`:
  - the default mode and the gain parse;
  - interpolation stays on the segment;
  - people and untracked handling;
  - `ExtrapolateGuarded`: the gain scales the step, the speed is clamped,
    a guess is not moved, an inferred joint is, a swap is held, crossing
    hands are not a swap, a new person is copied;
  - the prototype and extrapc.
- `PoseFillScheduleTest`:
  - 30 Hz is never filled; `off` never fills.
  - interp at 8 Hz is exact, at the right delay and rate.
  - A stall stalls the stream.
  - extrap stops one period after the newest frame, at gains 1 and 0.5.
  - No fill across a dropout (interp and extrap) or a long gap.
  - The clock running back and a faster camera are handled.
- `PoseFillLatencyTest`: interp's delay rides on whichever sensor latency
  wins.
  - default 100 + 160 = 260; declared 0 + 160 = 160; explicit 50 + 160 = 210;
  - on release, back to the explicit 50, then the default 100.

**`native/tests/test_native_pose_socket.cpp` `PoseFillSensorTest`**: the
real `BuildSensorFrame` path (slots, levelling, recording), with a test song
clock.
- extrap (the default) and interp each fill an 8 Hz camera to ~30 Hz.
- The session recording holds only the camera's own frames.
- extrap is never more than one period ahead, and it adds no latency.
- A dropout reaches P-12's hold exactly as with the fill off: 8 held
  frames in all three modes.
- A 30 Hz camera is the same stream with the fill on or off.

**Sabotages, every one caught; the source was restored and 22 of 22 pass.**

| sabotage | caught by | how it showed |
|---|---|---|
| interp's `SamePeople`/`AnyValid` guard → `if (false)` | sensor dropout test, schedule dropout test | held frames 8 (off) vs 32 (interp) |
| `DeliverFillFrame` also calls `Dc3PoseSessionRecord` | the recording test | 220 frames recorded vs 49 camera frames |
| extrap without the flip guard | `GuardedExtrapolation…` | 20 joint failures |
| extrap without the speed clamp | `GuardedExtrapolation…` | the absurd step goes through |
| extrap without the one-period horizon | `ExtrapStopsOnePeriod…` | 116 failures |
| extrap filling after a frame with nobody | schedule dropout test, sensor dropout test | held frames 8 vs 32 |

### Costs and limits

- **Generous by design.** At 8–15 Hz the default scores 5–10% above a real
  30 Hz camera on the footage. It also gives a wrong dance 1.6× a real
  30 Hz camera's credit, though that is still under 2% of the right
  dance's. Dial it with the gain.
- **A slow game engages the fill.** The engage log line says so, with its
  song time.
- **Not run on web.** The changed sources compile under em++
  (`-fsyntax-only` with dc3-web's flags). The fill has not been run in a
  browser.
- **Not validated live.** No owner session on the GPD with a real webcam
  has been taken, and the guarded default has not been replayed on the
  owner's recorded session.
- **The scores are from a loaded box** (load 25–84). FAST_TIME makes them
  frame-exact rather than wall-clock dependent, so a quiet-box rerun is
  expected to agree, but none was taken.

### Reproducing

```bash
# one config: footage at 8 Hz, the default fill at gain 0.75 (no GPU)
python3 scripts/native_song_sweep.py --out /tmp/fill/x8_g075 --no-gpu --jobs 3 \
  --songs macarena,indaclub,boyfriend,12step,starships,supersonic,scream,cupidshuffle \
  --difficulty medium --performer 'replay:<pose_songlead>/{song}.dc3pose' \
  --env DC3_POSE_PERFORM_REPLAY_SYNC=song --env DC3_AUDIO_GAIN=0 \
  --env DC3_POSE_PERFORM_HZ=8 --env DC3_POSE_FILL_GAIN=0.75 \
  --eval '{profile_mgr toggle_disable_freestyle}' --stall 60 --timeout 420
# controls: --performer stand (standing still); replay of another song's footage (wrong dance);
# --env DC3_POSE_PERFORM_NOISE=... --env DC3_POSE_PERFORM_GLITCH=... (noise)
native/build/milo-tests --gtest_filter='PoseFill*'
```

## Gaps

| # | Gap | Size | Platform |
|---|---|---|---|
| P-1 | **Crash on a motionless skeleton.** `RhythmDetector::AnalyzeData` divides by a variance that becomes zero → NaN → `SymbolKeys::SetFrame` segfault (`RhythmBattlePlayer::Poll`). A webcam trips it whenever joints are held (legs out of frame are held all song). | S–M | all |
| P-2 | Never run against a live webcam. **No-camera part done** (`np-pose-desk`): fast failure + on-screen notice (above); camera request, capture timestamps, HFOV and the lost-camera exit unit-tested against a fake capture (`tests/test_pose_server.py`). Left: a real webcam session. | S | all |
| P-3 | ~~Gesture menus~~ **Done (`np-gesture-menus`)**, see [Gesture menus](#gesture-menus). Left: a real person on a real webcam has not driven them; the depth image in the player box is P-4. | — | all |
| P-4 | Player on screen. **Silhouette done** (`np-player-view`): protocol v3 / `dc3_camera_push` carry the picture and a person mask, a `LiveCameraInput` stand-in feeds the decompiled `StreamRenderer`, and the cursor and gameplay boxes draw the player ([Player on screen](#player-on-screen-p-4)). Photos root-caused (`np-media`): the snapshot's alpha is its TexRenderer's clear, which the engine dropped; fix in engine branch `np-media` (`c810e49`; series also in `~/tmp/np-media-engine.patch`), not landed. Left: that engine change, `DepthBuffer3D`'s draw, the `pose_flash` entries (not camera), a live webcam. | M | all |
| P-5 | ~~Tests~~ **Done** (`np-pose-desk`): ctests `PoseSocketReaderTest`, `PoseCocoMappingTest`, `PoseSensorSlotTest` (no GPU, no game data); keypoint fixture + `PoseFixture.Python`; the gate runs the whole song and judges means, final scores and per-move distances (above). Open: standing still out-scores a different dance (P-7). | — | all |
| P-6 | ~~Remove the dead ncnn/YOLO path~~ **Done** (`np-pose-desk`). | — | all |
| P-7 | Scoring believability. **Answered** (`np-scoring`, [above](#scoring-believability-p-7-np-scoring)): the leniency is the original's. Real Kinect data (UTD-MHAD) scores like our inputs, and the scoring code is 100% matched. Added: the 30 Hz in-process performer + matrix, and the `DC3_POSE_LATENCY_MS` knob. **Video calibration** (`np-video-calib`, [above](#video-calibration-np-video-calib-2026-11-13)): 16 story-mode songs from a stream, replayed at Medium against the original's own results cards, 1162 = 1162 moves. Perfect+awesome 0.91 vs 0.96, but perfect 0.54 vs 0.79; score 74% of the original's (78% with a 10° camera-pitch correction). Still-body jitter ~1.7 mm (VIDEO mode). A causal One Euro filter costs 13–26% of the score, so there is no smoothing. Knob default stays unset: a recording cannot measure a live pipeline, and the score curve is flat over ±50 ms. Left: a live webcam's latency (P-2, L-5; `DC3_POSE_LATENCY_LOG` prints the capture → game part, and the fake-camera grid is in [Latency on the live path](#latency-on-the-live-path-l-4)); camera-pitch auto-levelling (whole-body lean +6.9° ± 1.4° over the reference on this camera); the perfect/awesome gap (arms carry the limb error). | S | all |
| P-8 | macOS: `/proc/self/exe` → `_NSGetExecutablePath`; camera permission goes to the Python child. See [MACOS.md](MACOS.md). | S–M | mac |
| P-9 | ~~Web pose~~ **Done in `np-web-pose`** (see [Web](#web)). Left: never run on a real webcam/permission prompt; GPU delegate and the lite model unmeasured on an idle box; no camera preview inside the game (P-4). | — | web |
| P-12 | ~~Mid-song pause with the dancer in view~~ **Done (`np-tracking`)**: a 1 s dropout hold plus 2 s tracker memory; a re-acquired dancer is bound and scored (W-10). See [Detection dropouts](#detection-dropouts-and-re-acquired-dancers-p-12-w-10-np-tracking). Left: a live webcam. | — | all |

## Gesture menus

DC3's shell is driven the way a 360 with a Kinect and no pad is (PLAN P-3,
branch `np-gesture-menus`). The controller-mode policy decides whether the
hands are listened to: the default `auto` is faithful while a pose provider
runs and forced while only the dummy skeleton does
([debugging/native.md](../debugging/native.md#controller-mode-policy)).

What the image's detectors need (read from `src/`, encoded in
`scripts/synthetic_kinect.py`):

| Gesture | Detector | Threshold |
|---|---|---|
| Engage a list | `DirectionGestureFilterSingleUser::IsValidSwipePosition` | hand outside an ellipse round the spine in the horizontal plane, hip frame: lateral 1.3 shoulder widths (1.1 once engaged), depth 0.9 (0.8). With 0.40 m shoulders, more than 0.52 m out to the side |
| Highlight | `HandHeightGestureFilter`, `HamNavList::DetermineHighlightedItem` | height = (shoulder.y − hand.y)/(shoulder.y − hip.y) + 0.15; item round(height·(n−1)), hysteresis 0.1 + half an item; scrolling lists (> 6 items) have 5 rows plus scroll zones |
| Select | `ArcDetector` (wrist round elbow) | amount = (path^1.5 − error/0.9)/threshold ≥ 1; threshold 0.1 m, 0.03 after 600 ms hover; capped at 0.5 for a ≤ 2-node path, 0.9 off-axis; 0.5 UI-s cooldown per skeleton |
| Back | the helpbar's `left_hand.hnl` with the left arm | lower item (index 1) |

Measured working: hover ≥ 0.6 s, forearm 0.24 m swept 110° in 0.3 s.

- **Desktop**: `scripts/gesture_menu_flow.py --engine native/build/dc3-native
  --workdir DIR --report DIR/report.json --all-tutorials-seen [--gpu --shots
  DIR/shots]` runs title → main → perform → YMCA → easy → start → the song →
  final results (continue) → complete (new song) → song select → back → back
  → main, reading only the screen and the highlighted item. It counts every
  select by source (`DC3 HamNavList: select ... selecting=0` is a gesture,
  `=1` a button). Measured: 10 gesture selects, 0 button selects, no
  controller-mode enter. `GestureMenusTest` runs it;
  `GestureMenusForcedTest` runs it under `DC3_CONTROLLER_MODE=forced` and
  requires it to fail (it stays on the title).
- **Timing**: a headless engine advances UI time 1/30 s per frame however
  fast it runs. The performer follows the engine's frame clock
  (`follow_engine`, `/api/frame/wait`) and the engine is paced
  (`DC3_HTTP_PACE_FPS=120`, `PUT /api/pace`) so it answers every frame; the
  song itself runs unpaced. Unpaced, a GPU-less engine ran ~3700 fps and the
  swipe peaked at 0.88.
- **Web**: `python3 native/web/server.py --port 8435 &` then
  `node scripts/web/gesture-menus.mjs --port 8435 --out DIR`. A fake camera
  cannot swipe and the web has no DTA eval, so the performer's joints go
  straight into `Module.dc3PosePush` (the camera pipeline's seam) from
  title_screen on. Measured: boot `auto` (forced), first push → faithful,
  `ControllerMode: exit (idle_ms=5015)`, then title, gameplay, perform and the
  helpbar back by gesture, 0 buttons. With `--env DC3_CONTROLLER_MODE=forced`:
  no handover, no select, exit 1.
- **What the player sees**: the highlighted item swells with an arrow; a
  swipe plays the item's select animation; the helpbar's LEFT HAND item fills
  with arrows when swiped. The player boxes at the top corners
  (`ui/gen/cursor.milo`: frame, empty-slot silhouette, edge-of-frame warnings,
  identification ring) now draw; the depth image inside the box is still
  hidden (P-4), so it shows the flat colour the original shows under Xenia.
- Not covered: a fresh console's tutorial screens (OKAY by gesture works the
  same way; the flow seeds tutorials-seen), scrolling to items below the 5th
  row of a long list.

## Testing without a webcam

- `scripts/synthetic_kinect.py --perform` — same wire protocol, replays the
  target. As a library, `point(i, side, h)` / `swipe(i, side, h)` navigate the
  menus (see [Gesture menus](#gesture-menus)). Wrap it to inject noise, latency, dropout.
  `--people 2` puts two performers on the sensor; as a library,
  `set_pose(i, "absent")` takes a person out of view and `"stand"` brings them
  back (`native/tests/test_native_twoplayer.cpp` drives Battle with 2, 1, 0).
- `pose_server.py --video` — real MediaPipe on a file; runs end to end but is not
  deterministic (wall-clock paced). Add `--frames 320x240` for the v3 picture +
  masks (the game passes it itself when it spawns the server).
- `DC3_POSE_PERFORM=choreo|wrong|halftempo|still|stand|replay:FILE` — the
  in-process performer (P-7). No socket, no second process, 30 Hz of song time,
  deterministic under `DC3_FAST_TIME`. It replays the same fixture files as
  `synthetic_kinect.py --replay`. See [Scoring believability](#scoring-believability-p-7-np-scoring).
- Camera picture without a camera (P-4): `synthetic_kinect.py --frames 320x240`
  sends protocol v3 with a picture and mask drawn from its own joints by
  `scripts/camera_fixture.py` (deterministic: capsule limbs + head disc, the
  Kinect's projection, striped background); `--offset 0.6` stands the person
  0.6 m to the right so a mirror bug shows. `camera_fixture.py --out DIR`
  writes the PNGs; `--grid 32x24` prints the mask sampled the way
  `GET /api/camera` samples. `native_assert_harvest.py --camera-frames 320x240`
  passes it through on any route.
- Deterministic keypoint fixture (P-5):
  `native/tests/data/pose/taye01_stand_dance.dc3pose` (139 KB, 240 frames,
  8 s) is MediaPipe's DC3-20 stream for
  `archive/screenshots-old/woot/taye01_stand_dance.mp4`, recorded offline by
  `scripts/pose_fixture_record.py` (frame-index timestamps, one rigid shift to
  stand the dancer at 2.5 m; re-recording is byte-identical).
  `synthetic_kinect.py --perform URL --socket S --replay FIXTURE` streams it
  keyed on song time (frame = `int(song * 30) mod 240`), so under
  `DC3_FAST_TIME` every run sees the same joints at the same song time;
  `--replay-still` holds frame 0 all song (same body, standing still).
  `tests/test_pose_fixture.py` (ctest `PoseFixture.Python`) checks the file
  and the keying without a GPU.
- On-disk video: rendered dance loops under `archive/screenshots-old/woot/`
  (MediaPipe detects the rendered dancer in 300/300 frames). No Kinect `.clp`
  recordings exist; for capturing them from a real console see
  [../tools/console/KINECT_CAPTURE.md](../tools/console/KINECT_CAPTURE.md).
