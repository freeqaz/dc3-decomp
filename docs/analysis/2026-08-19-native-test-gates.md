# Native test gates — follow-up to the 2026-08-19 toolchain audit

Closes items **9c**, **9d**, **11** and **12** of
[the toolchain audit](2026-08-19-toolchain-audit.md).

Four questions, in the audit's own order. Two were real bugs, one was a real
bug plus a second one found while fixing it, and one was a fixture artifact —
which is stated here as plainly as the bugs, because "this one isn't real"
saves the next lane the trip.

| # | audit finding | verdict |
|---|---|---|
| a | `RandSeed.NoSignExtensionPoison` is vacuous | **CONFIRMED** — and the obvious replacement is vacuous too |
| b | intermittent `SIGSEGV` in `TaskMgr::Poll()` | **REAL BUG, fixed** — use-after-free, root-caused from a live core |
| c | 130.6-unit L-ankle jump | **FIXTURE ARTIFACT** — a rigid character reposition, not IK. A second, worse artifact found next to it |
| d | `ctest` reports 79 skips as passes | **CONFIRMED** — 9 skips recovered, the rest made loud and budgeted |

---

## (a) A guard that could not fail — twice over

Reproduced rather than taken on faith. Deleting `(unsigned int)` from
`Rand::Seed` (`src/system/math/Rand.cpp:37`) and rebuilding leaves
`NoSignExtensionPoison` **green** while four sibling golden-sequence tests go
red. Exactly as the audit recorded.

Two independent reasons, and **the second one also kills the obvious fix**:

1. It probed the **draw stream**. `Int()` returns `table[i1] ^ table[i2]`, and
   XOR destroys any high-half signature — two poisoned words cancel to
   `0x0000xxxx`, and one poisoned against one clean word yields the complement
   of the clean half, which is `0xFFFF` only if that half was `0x0000`. Over
   5 seeds × 16 draws it never happened.

2. **The `0xFFFFxxxx` signature the test's comment describes does not occur in
   the current source at all.** `Rand::Seed` combines the two fields with `+`,
   not `|` — deliberately, because a disjoint `|` folds to `rlwimi` and costs
   ~10 pp of PPC match (the reasoning is in `Rand.cpp`). Under `+`, sign
   extension adds `0xFFFF0000` and simply **carries**: the word comes out as
   `correct - 0x10000`.

   ```
   seed 12345, table[0]:   correct 0x2704D3DC     buggy 0x2703D3DC
   ```

   Bit 31 stays clear. The high half stays ≤ `0x7FFF`. The low half is
   byte-identical. So *every* structural invariant of the form "no `0xFFFF`
   high word" or "bit 31 clear" — including the first rewrite attempted here,
   which is a genuinely true property of the correct algorithm — is **also**
   vacuous against the real defect. That was caught only by rebuilding under
   the sabotage a second time.

**What actually discriminates** is comparing the production table against an
independent reference for the target's semantics. The rewritten test carries
its own negative control: it computes both the `srwi` and `srawi` reference
variants, asserts they **differ** on ≥ ¼ of the 256 entries for each seed
(proving the seed set reaches the defect), and only then asserts
production == `srwi`. Weakening the seed set now fails the control instead of
silently turning the test into a no-op.

Table access is a new `HX_NATIVE`-guarded `Rand::TableWordForTest`.

Also fixed, same class, both named by the audit: `RandSeed.Deterministic` and
`Sha1.Deterministic` asserted only self-consistency, so they passed on broken
code. Both now pin a golden value (the SHA-1 digest cross-checked against
coreutils `sha1sum`).

> **Pattern worth keeping.** A "true invariant of the correct algorithm" is not
> the same thing as "an invariant the bug violates". The only way to know which
> you have written is to rebuild with the bug in place.

---

## (b) `TaskMgr::Poll()` SIGSEGV — real, and now root-caused

### Reproduction

The audit's own binary (`dc3-decomp/native/build/dc3-native`, built 07:44)
crashes **~1 run in 8** of the scripted flow, always at frame ~50 on the
`autosave_warning_screen → title_screen` transition.

| binary | runs | crashes |
|---|--:|--:|
| audit binary, bare | 8 + 16 | 1 |
| audit binary, under gdb | 2 | 2 |
| audit binary, ASLR disabled (`setarch -R`) | 5 | 0 |
| **audit binary, total** | **31** | **3 (≈10 %)** |
| fresh build from `713bcaa5c` | 12 serial + 18 under 6-way load + 5 ASLR-off | 0 |

The audit's "0 crashes under gdb" was a single run — it faults under gdb fine.
Disabling ASLR does not make it deterministic.

### Root cause

`addr2line` on the audit's backtrace lands on `Task.cpp:459`,
`delete unk84[i].Ptr()`, and `objdump` shows the faulting instruction is
`call *0x8(%rax)` — the virtual destructor dispatched through the vptr just
loaded from the object. Caught live under gdb:

```
rdi  = 0x555563248eb0    -> inside [heap]        (block still mapped)
rax  = 0x55503178da68    -> in NO mapped region  (vptr is garbage)
Task::IsLive((Task*)rdi) == false
unk84.size() == 1, entry 0: mObject = the dangling pointer, mOwner = null
```

So the Task's destructor **had** run and its block had been recycled, yet the
queue still held a non-null `ObjPtr` to it.

Why the `ObjPtr` outlives its target:

- `Hmx::Object::~Object` deliberately **skips `ReplaceRefs(nullptr)`** while
  `ObjectDir::InDeleteObjects()` is true.
- The cascade's Phase-0 `NullifyAllRefs` only walks objects reachable from the
  dir being torn down. `TheTaskMgr` is a global, not a dir member.
- Producer: `AnimTask::~AnimTask() { TheTaskMgr.QueueTaskDelete(mBlendTask); }`
  firing during `UIScreen::UnloadPanels`.

`UnloadPanels` already carries a native workaround for this exact hazard
(`ClearTimelineTasks` before the cascade, with a comment naming the
use-after-free) — but it covers `mTimelines`, not the deferred-delete queue.
`QueueTaskDelete` likewise already refuses to enqueue *during* a cascade for
the same reason. Neither helps an entry enqueued *before* it started.

### Fix

Consult the same `Task::IsLive` registry `TaskTimeline::Poll` has consulted
since it hit this class of bug, and drop the reference with `NullifyObj()`
(clears `mObject` and self-loops the ring without touching freed memory).
Plain `unk84.clear()` is *not* sufficient on its own — `~ObjRefConcrete` calls
`mObject->Release(this)` whenever the cascade / `sRingsDirty` guards happen to
be false.

### Evidence it is fixed

The 30 clean post-fix game runs are **not** the proof, and are not offered as
such: the pre-fix build did not reproduce either (0 dangling entries in 38
runs, with the detection path instrumented and confirmed live — the queue is
exercised ~400–600 times per run, the dangling window just never lands).

The proof is a **deterministic** regression test,
`ObjectLifetimeTest.PollSkipsQueuedTaskDestroyedByDeleteObjectsCascade`:

- with the guard compiled out (`if (false && ...)`) it dies with **SIGSEGV at
  `Task.cpp:497`, `delete unk84[i].Ptr()`** — the same source line as the
  production backtrace, confirmed from the core dump;
- with the guard in, 25/25 `ObjectLifetimeTest` pass.

It asserts the dangling **precondition** directly, so it cannot quietly stop
covering the crash. It must delete through `~ObjectDir`, not `DeleteObjects()`
directly — only `~ObjectDir` bumps `sDeleteObjectsDepth`, and it is that depth
which makes `~Object` skip `ReplaceRefs`. Calling `DeleteObjects()` by hand
runs the cascade at depth 0 where refs *are* nullified and the bug cannot
occur; that is how the first draft of the test came up green for the wrong
reason.

PPC codegen: everything is inside `#ifdef HX_NATIVE`; `TaskMgr::Poll` is
**123 of 123 instructions equal**.

> **Incidental.** `Hmx::Object::New<T>` on an unregistered class hits
> `MILO_FAIL`, which is non-fatal on native, and the caller then spins forever
> printing `Couldn't instantiate class Object`. Another instance of the
> recorded "MILO_ASSERT is non-fatal on native → fall-through" class.

---

## (c) The 130.6-unit ankle jump is **not** flying feet

Telemetry at the jump (audit's own dump, frame 1070):

```
lAnkle      (30.0, -21.6) -> ( 57.9, -149.1)   delta 130.55
rAnkle      (-4.5, -27.5) -> ( 24.3, -152.1)   delta 127.95   <- same vector
lHand       (18.0, -37.7) -> ( 40.8, -164.0)                  <- same vector
rHand       (-9.4, -28.7) -> ( 14.7, -154.0)                  <- same vector
ankleSeparation   35.0 -> 33.8    (its normal 33-35 band)
pelvisToLAnkle    29.5 -> 30.2    (unchanged)
lAnkleZ/rAnkleZ   4.2/3.9 -> 4.2/3.9   (floor height unchanged)
lAnkleLocal      18.48/0/0 -> 18.48/0/0   (bit-identical)
charClipLayers       2 -> 1
```

Every internal body relationship is preserved, both hands move by the same
vector, floor height does not change. The character was **rigidly translated in
XY**, once in 9050 frames, coincident with a clip layer being dropped — and the
position it moves *to* is where the dancer stays for the remaining ~800
samples, so the pre-jump position is the transient one.

The three causes the test names in its own failure message — stale
`mLocalXfm`, a constraint target changing, `NeutralWorldXfm` garbage — would
all **distort** the body. None translates it.

The measurement is the defect: `lAnkleWorldDelta` is a **world-space** delta,
so it cannot distinguish "the foot moved relative to the body" from "the body
moved and took the foot with it". Replaced with a classifier — a jump is
*articulated* (fails) only if the two ankles disagree by > 15 % of the larger
delta, or ankle-separation / pelvis-to-ankle changed by > 5 units; otherwise it
is a *rigid reposition*, reported and bounded at ≤ 1.

**Negative control:** tightening the ratio from 0.15 to 0.001 reclassifies the
same event as articulated and the test goes red with the flying-feet message.
Both branches are live. Observed agreement is 2 % against a 15 % threshold.

### A worse artifact found next door

Fixing that turned `HandBonesNotFlyingDuringGameplay` red — the same event seen
through the hands. It asserted `|world coordinate| ≤ 200` on the premise,
stated in its own comment, that *"character is at world origin"*. The game
violates that premise, so the test measured **where the dancer stands**, not
whether their hands are attached:

| metric | p50 | p99 | max |
|---|--:|--:|--:|
| world-space max-abs coordinate | 160.8 | 191.0 | **199.5** (limit 200) |
| body-relative hand distance | 44.5 | 74.4 | **79.6** |

It was passing with **0.5 units of margin out of 200** — 0.25 % — and it went
red here on a rerun where the reposition landed slightly further out. Rewritten
to measure hand distance from the character's own body (ankle midpoint), limit
150 against an observed max of 79.6.

After both fixes: **48/48** `GameplayTelemetryTest` pass; articulated jumps 0;
rigid repositions 1; worst hand-to-body 80.0 of 150. Re-run three times — the
reposition lands at a different frame each time (1070 / 2570 / 2010), which
also rules out a fixed-frame harness artifact.

**Left open, and deliberately not claimed as fixed:** the reposition itself is
a visible one-time teleport of the dancer at the start of the routine,
coincident with `charClipLayers 2 → 1`. That is a character-placement /
clip-layer-handoff question for a lane that owns that code. It is now labelled
as such instead of being misfiled under IK.

---

## (d) Skips are no longer counted as passes

Baseline on this tree: **442 registered, 84 skipped, 358 executed**, and
`ctest` printing `100% tests passed`.

Why each group was skipping, and what was done:

| n | suite | gate | outcome |
|--:|---|---|---|
| 48 | `GameplayTelemetryTest` | `DC3_GAMEPLAY_TESTS` | left OFF — 48 tests behind one ~2 min 9050-frame engine run. `--all-gates` runs it. |
| 7 | `DtaFlowTest` | `DC3_DTA_FLOW_TESTS` | **ON** — needs the game assets, which the existing `MILO_TEST_ASSET_DIR` probe already proved present. All 7 pass. Pure lost coverage. |
| 2 | `CharClipGroupTest` | `MILO_LIB` | **ON** — `GetMiloLibRoot()` already defaults to where the library lives, so these skipped next to sibling tests reading the same files. Both pass in 45 ms. |
| 16 | audio / Mogg / Bink | `DC3_AUDIO_TESTS` | left OFF — real audio device, contends under `ctest -j`. |
| 1 | `HeadlessBootTest.LongRunStability` | `MILO_LONG_TEST` | left OFF — ~5 min. |
| ~5 | `MiloViewerScreenshot` | viewer binary + GPU | unchanged. |

Gates are **probed** at configure time, never assumed: without the assets the
probe fails, the gate stays off, and the suite degrades to *skipping* rather
than to *failing*.

Three instruments, each falsified before being trusted:

- **`scripts/native_test.sh`** — the documented entry point. Prints registered
  / EXECUTED / passed / FAILED / **SKIPPED** separately plus a per-suite skip
  breakdown, and enforces `native/tests/skip_budget.txt` as a two-way ratchet.
  Verified: budget 70 → **exit 2** ("coverage shrank"); budget 80 → **exit 3**
  ("coverage improved, lock it in"); budget 74 → **exit 0**.
- **`TestGates.EnabledGatesReachTheTestProcess`** — an instrument check on the
  instrument. The gate list CMake decided is compiled into the binary and
  compared against the live environment, because a mistake in the ctest
  `ENVIRONMENT` property would have no symptom except tests quietly skipping
  again. Verified with `env -u MILO_LIB -u DC3_DTA_FLOW_TESTS`: goes red naming
  the missing gate.
- **`TestGates.ReportDisabledSuites`** — prints the remaining holes and why, on
  every run.

Writing the wrapper immediately found a bug **in the wrapper**: `ctest` prints
`100% tests passed out of N` with no failure clause when nothing fails, so a
regex written against only the failing form reported `registered: 0,
EXECUTED: -74`. It now parses both forms and refuses to print a number at all
(exit 4) if it cannot parse the summary line.

**Result: 445 registered, 371 execute and pass, 74 skip, 0 fail.**

The `371` coinciding with CLAUDE.md's long-stale `371/371` is a coincidence —
that figure was stale for a different reason (441 registered, 362 executing).

---

## Files touched

| file | why |
|---|---|
| `src/system/math/Rand.h` | `TableWordForTest` accessor (HX_NATIVE) |
| `src/system/obj/Task.cpp` / `.h` | the liveness guard + `DanglingQueuedTasksSkipped` (HX_NATIVE) |
| `native/tests/test_rand_seed.cpp` | falsifiable guard + reference implementation + goldens |
| `native/tests/test_sha1.cpp` | golden digest |
| `native/tests/test_object_lifetime.cpp` | the cascade regression test |
| `native/tests/test_gameplay_telemetry.cpp` | jump classifier + body-relative hand metric |
| `native/tests/test_gates.cpp` | new — gate visibility + plumbing check |
| `native/tests/skip_budget.txt` | new — the ratchet |
| `scripts/native_test.sh` | new — honest summary |
| `native/CMakeLists.txt` | configure-time gate probing |
| `CLAUDE.md` | current numbers; point at the wrapper |

Three `src/` files changed, all inside `#ifdef HX_NATIVE`. Verified with a
same-worktree A/B against `main`'s versions of those three files:
**0 of 48,344 functions differ** on either the normalized or the fuzzy ruler.
`../milo-native-engine` was **not** modified.
