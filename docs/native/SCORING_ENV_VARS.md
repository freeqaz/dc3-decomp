# Native Move-Scoring Environment Variables

Reference for the native/web move-scoring pipeline env gates. As of the
2026-07 default-on flip (branch `t41-defaulton`), the scoring pipeline and the
real `move_passed` path both run by DEFAULT; the env vars are now **opt-outs**.

## Value parsing: `Dc3EnvFlag`

All of these are parsed by `Dc3EnvFlag(name, defaultOn)` (defined in
`native/src/platform/System_Native.cpp`, declared in
`native/src/platform/NativeSettings.h`):

| Env value                              | Result       |
|----------------------------------------|--------------|
| unset / empty                          | `defaultOn`  |
| `0`, `false`, `off`, `no` (any case)   | `false`      |
| anything else (`1`, `yes`, `true`, …)  | `true`       |

This is a deliberate change from bare `getenv()` truthiness: previously
`DC3_NATIVE_SCORING=0` still read as "set" and so *enabled* scoring. Now `=0`
disables it. **Scripts that set any of these vars to `0` will now DISABLE the
feature** — audit before relying on old semantics.

## The vars

| Var | Default | Meaning |
|-----|---------|---------|
| `DC3_NATIVE_SCORING` | **on** | Per-frame `TheGestureMgr->Poll()` (pose → skeleton → FilterQueue → MoveDir callback fan-out). `=0` restores the pre-flip baseline bit-exactly (no poll, `has_skeleton` false). Gate sites: `src/App.cpp` RunOneFrame (web loop) and RunWithoutDebugging (native headless loop), both inside `#ifdef HX_NATIVE`. |
| `DC3_REAL_MOVE_PASSED` | **on** | Genuine Xbox `move_passed` DTA path (`Game::SetHamMove` → `MetaPerformer::OnMovePassed`), byte-identical to the Xbox `#else`. `=0` suppresses `move_passed` emission on native. Gate site: `src/lazer/game/Game.cpp` inside `#ifdef HX_NATIVE`. Landed as a separate commit so it can be reverted independently. |
| `DC3_POSE_SELFTEST` | off | Feeds the choreography's OWN reference pose as the player (perfect self-mimicry → `DetectFrac` → ~1.0). Still OR'd into the `App.cpp` scoring gate, so it forces the poll even under `DC3_NATIVE_SCORING=0`. Parsed with `Dc3EnvFlag(..., false)` at `FilterQueue.cpp` and both `App.cpp` sites, so `DC3_POSE_SELFTEST=0` can never accidentally enable it. |
| `DC3_POSE` | unset | Provider select: `external` (unix-socket `pose_server.py`) or `internal` (ncnn). Also overrides the `MILO_HEADLESS` skip in `GestureMgr_NativeInit`. |
| `DC3_POSE_NO_SPAWN` | unset | Connect-only; do not fork `pose_server.py`. |
| `DC3_POSE_MODEL` | `native/models/pose_landmarker_full.task` | MediaPipe pose landmarker `.task` path passed to the spawned server (`lite`/`full`/`heavy` tiers ship in `native/models/`). The AGPL YOLO backend and its weights were retired after ground-truth measurement (`tools/pose_corpus/bench_model_z.py`). |
| `DC3_POSE_SOCKET` | `/tmp/dc3_pose.sock` | Unix socket path shared with the pose server. |
| `DC3_POSE_CAMERA` | `0` | Camera index passed to the spawned server. |
| `DC3_POSE_HFOV` | unset (server default 58.51°) | Real horizontal FOV of the webcam, degrees. Absolute depth scales with the assumed focal length: the Kinect-default 58.51° measured +0.76–1.73 m depth bias on real 65–72.5° cameras. Pose SHAPE is FOV-invariant, so the default is safe for scoring; calibrate for correct absolute positions/displacements. |
| `DC3_SCORING_DEBUG` | off | Once-per-second liveness counters (archive lookups, frame gating, slot reassignments). Parsed with `Dc3EnvFlag(..., false)`. |

## Deterministic dummy: `DetectFrac ~0 is CORRECT

When NO pose provider is running (`DC3_POSE` unset), `GestureMgr_NativePoll`
fills a **static, TRACKED dummy skeleton** in slot 0. It exercises the entire
pipeline every frame. Do not `MarkUntracked` the dummy — that would break
`ShellInput::HasSkeleton()` / `SkeletonChooser::Poll` and silently collapse
scoring coverage to the `errors=1.0` short-circuit.

> **CORRECTION (2026-07-17).** This section previously claimed the dummy's
> `DetectFrac ~0` was "the honest standing-still signal, NOT a scoring bug", and
> told agents not to fix it. **That was wrong, and it masked two real bugs** —
> a standing pose vs. a moving reference should score low-but-nonzero, never
> identically 0. Live instrumentation showed all 33 error nodes pinned at
> exactly 1.0 because `mCamBoneLengths` was never populated on native (the
> `norm_bones` divisor was 0, so every `PositionNode`/`DisplacementNode` took
> its max-error path). `DC3_POSE_SELFTEST` hid it by substituting a
> `DancerSkeleton`, which computes its bone lengths lazily.
>
> Treat a *degenerate* score — identically 0.000 or identically 1.000,
> regardless of input — as a BUG, not a baseline. The useful regression
> assertion is a **differential** one: selftest must score near 1.0, a static
> dummy must score clearly below a real dancer, and neither may be exactly
> 0 or exactly 1 for every move.

## `move_passed` arg semantics (so future agents don't "fix" it)

`Msg.h` maps `operator[](i)` to `Node(i+2)`, and the handler reads `Int(4)` /
`Float(5)`. So `move_passed[2]=frac` is consumed as `ratingIndex=(int)frac` and
`move_passed[3]=b3` (a bool) is consumed as `detectFrac=Float(5)`, stored in
`HamMoveScore.mDetectFrac`. This is **byte-identical to the authentic Xbox
`#else` path** in `Game::SetHamMove` — a faithful port, not a native bug.

## Opt-out to reproduce the pre-flip baseline exactly

```sh
DC3_NATIVE_SCORING=0 DC3_REAL_MOVE_PASSED=0   # faithful rollback:
                                              #   has_skeleton stays FALSE,
                                              #   no move_passed fires,
                                              #   DetectFrac identically 0
```

## Landing record (2026-07-17, merge 4c6cd81b)

Both flips landed default-on after the full runtime gate battery on 18K–60K
frame headless `betteroffalone` runs:

- **G1** landing config (`DC3_REAL_MOVE_PASSED=0`): clean, scoring active,
  zero `move_passed`.
- **G2** selftest: clean, **`frac=1.000`** flowing through `move_passed` on
  reference poses — end-to-end proof of real detection values (dummy runs
  stay at the expected `0.000`).
- **G4** full opt-out: clean, zero scoring output (legacy behavior).
- **G5** both-flips 60K frames: clean past song end; `move_passed` fired
  **150 times total** = once per move boundary per player.
- **Gate A** real-video pose chain (`pose_server.py --video`): 93.1% archive
  hit rate, zero skeleton failures.

Two engine bugs were found and fixed by these gates before landing:

1. **`DataNode::Equal` string mis-decomp** (`d3100a62`): `kDataString` glob
   pointer compared as chars → all DTA string equality false → the hud
   `move_interp` `$new_move` debounce fired every frame → `set_cur_move` /
   `post_move_finished` storms (67,475 `move_passed` in 18K frames).
2. **`DanceRemixer::PostMoveFinished` OOB** (`79e47f0a`): wall-clock-derived
   `moveIdx` indexed one past `mRoutineMeasures` at song end → SIGSEGV in
   `FindObject`. HX_NATIVE bounds guard.

Gate-horizon lesson: 18K-frame runs end **inside** the song; both crashes only
manifested past song end. Long-run gates must idle past the last move boundary
(60K frames for this song).

Web (`__EMSCRIPTEN__`) note: default-on there too (`CheckForSkeletonLoss`
early-returns on web); build verified green at landing. During-song web frame
time not yet measured — if scoring cost shows up on wasm profiles, flip the
web default at the `App.cpp` gate sites.
