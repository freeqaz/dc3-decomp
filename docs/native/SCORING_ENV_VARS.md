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
| `DC3_SCORING_DEBUG` | off | Once-per-second liveness counters (archive lookups, frame gating, slot reassignments). Parsed with `Dc3EnvFlag(..., false)`. |

## Deterministic dummy: `DetectFrac ~0 is CORRECT

When NO pose provider is running (`DC3_POSE` unset), `GestureMgr_NativePoll`
fills a **static, TRACKED dummy skeleton** in slot 0. This is the intended
default-run scoring input: it exercises the entire pipeline every frame and
produces a deterministic `DetectFrac ~0` — the honest "player standing still"
signal, **NOT a scoring bug**. Its exact end-of-song value is a blessed
regression signal; do not "fix" a near-zero score, and do not `MarkUntracked`
the dummy (that would break `ShellInput::HasSkeleton()` / `SkeletonChooser::Poll`
and silently collapse scoring coverage to the `errors=1.0` short-circuit).

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
