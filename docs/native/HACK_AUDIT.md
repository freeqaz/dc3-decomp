# Native Port Hack Audit

**Date**: 2026-03-16 (updated 2026-03-17)
**Scope**: All `#ifdef HX_NATIVE` / `#ifndef HX_NATIVE` guards in `src/` and `include/`

## Guiding Principles

**PPC decomp parity is sacred.** All changes use `#ifdef HX_NATIVE` to preserve Xbox
match percentages. We can make any changes we want inside `#ifdef HX_NATIVE` blocks.

**Guards must be for real platform divergences**, not crash suppression:
- **GOOD**: async vs sync loading, 64-bit vs 32-bit pointer logic, missing hardware
  (Kinect), STL implementation differences, endianness
- **BAD**: null checks that mask uninitialized data, early returns that skip
  initialization, MILO_WARN downgrades that hide missing objects

**Silent failure is worse than a crash.** Masking a crash with a null check or early
return leads to state corruption that's harder to debug downstream. If something is
broken, it should fail loudly (MILO_FAIL) so we can fix it. Only guard after MILO_FAIL
to prevent null derefs when MILO_FAIL is non-fatal on native.

## Executive Summary

~745 `HX_NATIVE` preprocessor blocks exist across the codebase. Most are legitimate
platform differences (LP64, endianness, Kinect removal, STL compat). This audit
identified **14 high-severity hacks** masking real bugs. **7 have been fixed** and
**17 audited as KEEP** (all in PPC decomp functions where unconditional would break
match%). See Changes Applied below.

### Changes Applied (2026-03-16)

| # | Change | Files | Verified |
|---|--------|-------|----------|
| P0 | **Removed ObjRef ring corruption guard** — root cause fixed 18 sessions ago, guard silently skipped ref updates | `Object.cpp` | 3000 frames, no crash |
| P1 | **Fixed ObjPtrList::sort for NULL-terminated lists** — root cause of CharHair crash was sort loop assuming circular list | `ObjPtr_p.h` | 2000 frames + GPU render |
| P1 | **Re-enabled CharHair::Poll and Enter** — hair/cloth physics now work on native | `CharHair.cpp` | GPU render, no crash |
| P1 | **Cleaned up null-this UB guards** — removed MILO_WARN noise, kept guards with comment explaining they can't be fixed (callers are decomp-matched PPC source) | `Object.cpp` | 500 frames fatal mode |
| P2 | **Unified TaskMgr timing** — removed unconditional wall-clock override, use `mAutoSecondsBeats` guard (menus/loading get audio time, gameplay falls back to wall-clock since Game::Poll is stubbed) | `Task.cpp` | 3000 frames |
| P2 | **Deferred purge for gSuppressRefErase** — ReplaceNode registers affected ObjPtrVecs for cleanup; ReplaceList purges nulls at outermost exit. Nulls never escape into consumers. | `Object.h`, `Object.cpp`, `ObjPtr_p.h` | Build + 227 tests |
| P0 | **Removed OriginalChoreoRemixer::Init blanket early return** — was skipping entire choreography pipeline, leaving `mTotalMeasures` uninitialized. DTA `num_rated_measures` while-loop ran forever against garbage. Now tries real pipeline; falls back with MILO_FAIL + `mTotalMeasures=0` only if MoveParents genuinely empty | `OriginalChoreoRemixer.cpp` | Native boot clean, PPC 100% |
| P1 | **Removed all CharClipGroup null guards** — 4 `#ifdef HX_NATIVE` guards (GetClip, Copy, DeleteRemaining, FindClip) no longer needed after deferred purge fix | `CharClipGroup.cpp` | PPC match preserved |

---

## CRITICAL: Masking Real Bugs

These hacks silently skip operations or swallow errors. Fixing the underlying issues
would allow removing them and improve correctness.

### 1. ObjRef Ring Corruption Guard (`Object.cpp:300-316`)

```cpp
// Validate ref ring before walking — corrupt rings crash during merge
ObjRef *probe = mRefs.next;
int count = 0;
while (probe != &mRefs && probe != nullptr && count < 100000) {
    if (!probe->next || !probe->prev) {
        fprintf(stderr, "DC3 Native: ReplaceRefs skipping corrupt ring for '%s'\n", Name());
        return;  // SILENTLY SKIPS ENTIRE OPERATION
    }
    ...
}
```

**Problem**: When ring corruption is detected, `ReplaceRefs` silently returns. Objects
that should have been re-pointed now reference stale/freed memory. Downstream consumers
get dangling pointers.

**Root cause**: The `ObjDirPtr(C*)` double-AddRef bug was fixed (Session 59), but this
validation guard was kept "pending crowd/audio/song revalidation." That revalidation
happened (Sessions 67-77) — crowd and audio work. **This guard should be tested for
removal.**

**Status**: **FIXED** — guard removed. 3000-frame smoke test + 2000-frame GPU render
clean. No ring corruption detected after 18 sessions of stability.

---

### 2. Null-`this` Checks (`Object.cpp:363,371,381,388`)

```cpp
if (!this) { MILO_WARN("RemoveSink called on null object"); return; }
if (!this) { MILO_WARN("GetOrAddSinks called on null object"); return nullptr; }
if (!this) { MILO_WARN("AddSink called on null object"); return; }
if (!this) { MILO_WARN("AddPropertySink called on null object"); return; }
```

**Problem**: `if (!this)` is undefined behavior in C++. GCC doesn't optimize it away
(unlike MSVC), so it "works," but it masks callers passing null objects into the sink
system. The real bug is upstream — something is calling `nullObj->AddSink(...)`.

**Root cause**: During FileMerger's `MergeSinks` phase, objects being merged may already
be freed. The merge walk calls `AddSink` on the dead object.

**Status**: **REDUCED** — removed MILO_WARN noise, kept silent guards with explanation.
Investigation found 18 unguarded callers (BustAMovePanel, HollaBackMinigame,
HamProviderPrinter, EventDialogPanel, etc.) — all in decomp-matched PPC source that
can't be modified. Long-term fix: initialize null globals (TheMaster, TheUIEventMgr,
etc.) to lightweight stubs instead of leaving them null.

---

### 3. CharHair Poll/Enter Bypass (`CharHair.cpp:109-113, 140-145`)

```cpp
void CharHair::Poll() {
#ifdef HX_NATIVE
    return;  // Hair physics entirely disabled
#endif
    ...
}

void CharHair::Enter() {
    mReset = 1;
    RndPollable::Enter();
#ifdef HX_NATIVE
    return;  // Hookup entirely skipped
#endif
    Hookup();
}
```

**Problem**: All hair/cloth physics are disabled. The root cause is null `CharCollide`
entries in the `ObjPtrList` during `SortCollides()`. The Hookup function iterates
`ObjDirItr<CharCollide>` which may include uninitialized colliders during venue Enter.

**Root cause**: On Xbox, `Enter()` is called after all objects are fully initialized.
On native, `Enter()` may fire while some CharCollide objects haven't completed their
own Enter yet (async loading order).

**Status**: **FIXED** — root cause was `ObjPtrList::sort` assuming circular linked list
(PPC Link creates circular, native Link creates NULL-terminated). Fixed sort loop
condition with `#ifdef HX_NATIVE` (`outer != nullptr` vs `outer != sentinel`).
Both `Poll()` and `Enter()` guards removed. Hair/cloth physics now work on native.

---

### 4. CharClipGroup Null Clip Guards (`CharClipGroup.cpp:107-118, 71-73, 205-208`)

Three locations purge or skip null clip pointers, plus a `DeleteRemaining` latent crash.

**Root cause (corrected)**: NOT "ObjPtr::Load creates null entries" — `ObjPtrVec::Load`
with `kObjListNoNull` explicitly skips null entries (line 278 of ObjPtr_p.h). The actual
cause is `gSuppressRefErase` during native `ReplaceList`. When clips are deleted by
`Merger::Clear`, `ReplaceRefs(nullptr)` walks the ring via the snapshot approach, but
`gSuppressRefErase=true` prevents the ObjPtrVec from erasing the now-null entry. On
Xbox, `ReplaceList` erases immediately (no snapshot/suppression), so null entries never
persist.

**Status**: **FIXED** — all 4 guards removed. `ReplaceList` now uses a deferred purge
mechanism: `ReplaceNode` registers affected ObjPtrVecs for cleanup instead of silently
leaving null entries, and `ReplaceList` purges all registered vectors at the outermost
exit. Nulls never escape into consumers. See `docs/sessions/2026-03-17-deferred-purge-refactor.md`.

---

### 5. ObjRef ReplaceList Erase Suppression (`Object.cpp:16-47`)

```cpp
bool gSuppressRefErase = false;

void ObjRef::ReplaceList(Hmx::Object *obj) {
    bool oldSuppress = gSuppressRefErase;
    gSuppressRefErase = true;
    // ... snapshot refs into vector before replacing ...
    gSuppressRefErase = oldSuppress;
}
```

**Problem**: This is a global flag that suppresses ObjPtrVec node erasure during
ReplaceList walks. The comment explains why — "Replace handlers can move refs between
rings, corrupting the ring walk." But a global suppression flag is fragile. If any
code path sets this and crashes before resetting it, all subsequent erase operations
are silently skipped.

**Root cause**: The ring data structure doesn't support concurrent mutation during
iteration. The snapshot approach is correct but the global flag is a hazard.

**Status**: **FIXED** — converted to `SuppressEraseScope` RAII guard in `Object.h`.
Flag is now automatically restored on exception, early return, or normal exit.

---

## HIGH: Masking Incomplete Implementations

### 6. DataNode::GetObj Warn-vs-Fail (`DataNode.cpp:543-557`)

```cpp
#ifdef HX_NATIVE
    MILO_WARN("GetObj: %s not found in %s\n", str, msg);
#else
    MILO_FAIL_DTA(kNotObjectMsg, str, msg);
#endif
```

**Impact**: Xbox shows a clear error dialog. Native silently warns and returns null.
Song animations that reference objects (HUD elements, stage props) get null and
silently skip their animation targets. Characters may not animate certain props.

**Acceptable?**: Yes, for now. Many objects genuinely don't exist on native (Kinect UI,
Xbox-specific HUD). But this also masks **real missing objects** that should exist.

**Action**: Add a diagnostic mode (`MILO_STRICT_GETOBJ=1`) that fatals on GetObj
failures, to audit which missing objects are bugs vs expected gaps.

---

### 7. Object Property MILO_WARN Downgrades (`Object.cpp:469-474, 491-499, 535-540`)

Three property lookup paths downgrade `MILO_FAIL` to `MILO_WARN` on native:

```cpp
#ifdef HX_NATIVE
    MILO_WARN("%s: property %s not found", ...);
#else
    MILO_FAIL("%s: property %s not found", ...);
#endif
```

**Impact**: Property lookups that fail are silently ignored. DTA scripts that set
properties on objects (animation targets, UI state) fail without notification.

**Acceptable?**: Same as above — needed for now, but masks real issues.

---

### 8. ObjDirPtr::Replace Null Guard (`Dir.h:65-74`)

```cpp
#ifdef HX_NATIVE
    if (!ObjRefConcrete<C>::mObject) return;  // Skip replace on freed object
#else
    MILO_ASSERT(ObjRefConcrete<C>::mObject, 0x70);
#endif
```

**Impact**: When an ObjDirPtr's target is already freed, Replace silently returns.
The pointer remains null/stale instead of being updated to the replacement object.

**Root cause**: Same as #1 — object lifecycle during merges.

---

### 9. NewObject Post-Assert Return (`Object.cpp:634-638`)

```cpp
MILO_ASSERT_FMT(it != sFactories.end(), "Unknown class %s", name);
#ifdef HX_NATIVE
    if (it == sFactories.end()) return nullptr;
#endif
return (it->second)();
```

**Impact**: In non-fatal mode, the assert warns but continues. Without the nullptr
return, the code would dereference `end()` and crash. This guard is **correct for
non-fatal mode** but reveals that `MILO_FATAL_FAILS=0` can reach dangerous paths.

**Action**: Keep. This is defense-in-depth for non-fatal mode.

---

## MoveGraph / Remixer Pipeline (31 guards, audited 2026-03-16)

The choreography/remixer pipeline (`MoveMgr`, `MoveGraph`, `DanceRemixer`,
`OriginalChoreoRemixer`, `SuperEasyRemixer`) had 31 `HX_NATIVE` guards. Many were
crash-masking null checks added during early native bring-up when MoveGraph loading
was broken. MoveGraph loading has since been fixed — these guards need revisiting.

### KEEP: Null-check guards in PPC decomp functions (audited 2026-03-16)

All 9 null-check guards and all 3 MILO_FAIL-return guards are `#ifdef HX_NATIVE` — they
add code that does NOT exist in the PPC target binary. Making them unconditional would
inject branches/returns into the PPC .obj and break decomp match percentages.

The `#ifndef HX_NATIVE` guards (MoveMgr asserts) are the inverse — they keep PPC asserts
that should NOT appear on native (MILO_ASSERT is fatal by default on native).

| File | Line(s) | Guard type | Verdict |
|------|---------|------------|---------|
| `MoveMgr.cpp` | 299-305 | `#ifdef` null dirGraph | KEEP — adds branch to PPC |
| `MoveMgr.cpp` | 444-446 | `#ifndef` assert mMovesDir | KEEP — removes assert from native |
| `MoveMgr.cpp` | 455-458 | `#ifndef` assert PropKeys | KEEP — same |
| `DanceRemixer.cpp` | 186-192 | `#ifdef` null moveDir/detector | KEEP — adds 2 returns to PPC |
| `MoveVariant.cpp` | 29-31 | `#ifdef` null mVariant | KEEP — adds branch to PPC |
| `SuperEasyRemixer.cpp` | 61-66 | `#ifdef` empty MoveParents | KEEP — adds early return to PPC |
| `SuperEasyRemixer.cpp` | 113-119 | `#ifdef` empty difficulty track | KEEP — adds loop to PPC |
| `SuperEasyRemixer.cpp` | 225-227, 232-234 | `#ifdef` return after MILO_FAIL | KEEP — dead return adds PPC code |
| `OriginalChoreoRemixer.cpp` | 123-128 | `#ifdef` fallback + return | KEEP — dead code adds PPC code |
| `OriginalChoreoRemixer.cpp` | 160-162 | `#ifdef` return after MILO_FAIL | KEEP — same |

### KEEP: Real platform divergences

| File | Line(s) | Guard | Why it's needed |
|------|---------|-------|-----------------|
| `MoveMgr.cpp` | 37-44 | Skip `SetType("easeup_remixer")` | Config lookup fails on native — needs objects.dta fix, not code fix |
| `MoveMgr.cpp` | 409-416 | Guard `SetDefaultReplacer` on song anim availability | Timing: native may not have song loaded yet. Real async vs sync divergence. |
| `MoveMgr.cpp` | 400-405 | Allocate default `SongLayout` | Init ordering difference (could be made unconditional after testing) |
| `MoveMgr.cpp` | 607-611 | Different null-check for `moveDir` pointer | 32-bit `(unsigned int)` cast vs direct null check. LP64 divergence. |
| `MoveDir.cpp` | 2168-2170 | Empty `PostUpdateFilters` stub | Kinect-specific filtering not applicable on native. |
| `HamDirector.cpp` | 337-351 | ~~Inline `Enter()` logic~~ | **REMOVED** (Phase 2, 2026-03-17): FileMerger pipeline now wires mMerger, full Enter() path runs |
| `HamDirector.cpp` | 591-595 | ~~`GetWorld` returns `mVenue` vs `mMerger->Dir()`~~ | **REMOVED** (Phase 2, 2026-03-17): mMerger wired via change_files, GetWorld() works naturally |
| `HamDirector.cpp` | 1060-1084 | Player presence / crew reconstruction | Single-player flow vs Xbox DTA multi-user pipeline. |
| `HamDirector.cpp` | 2531-2538 | Merger erase via accessor vs pointer arithmetic | Struct layout / access pattern difference. |
| `HamDirector.cpp` | 2606-2614 | `SyncCamera` world selection | Same `mVenue` vs `mMerger->Dir()` divergence. |
| `HamDirector.h` | 120-122 | ~~`SetNativeVenueWorld()`~~ | **REMOVED** (Phase 4, 2026-03-17): dead code, no callers |
| `HamDirector.cpp` | 3114-3117 | Skip STL exception throw | Exception handling difference. |

### KEEP: HamDirector debug/fallback guards (audited 2026-03-16, updated 2026-03-17)

Guards add code that does NOT exist in the PPC target binary — cannot be made
unconditional without breaking decomp match. `DebugWorldLoad()` was removed in Phase 4
(2026-03-17) — logging is now unconditional inside `#ifdef HX_NATIVE`.

| File | Line(s) | Guard | Verdict |
|------|---------|-------|---------|
| `HamDirector.cpp` | ~990 | Debug logging in `OnLoadSong` | KEEP — unconditional (was behind DebugWorldLoad, now always logs) |
| `HamDirector.cpp` | ~1176 | Debug logging in `OnFileLoaded` | KEEP — same |
| `HamDirector.cpp` | 1281-1295 | `video_recorder.srec` stub | KEEP — Kinect-specific native workaround |
| `HamDirector.cpp` | 1951-1990 | Non-fatal `Find()` for HUD objects | KEEP — PPC uses fatal Find + no null checks |
| `HamDirector.cpp` | 2355-2363 | Camera shot fallback to `Area1_WIDE` | KEEP — adds retry logic not in PPC |

---

## MEDIUM: Acceptable Workarounds (Tech Debt)

### 10. PoseFatalities Full Disable / Remaining Cosmetic Gaps

- `PoseFatalities::Poll()` — strike-a-pose disabled (LP64 struct mismatch)
- `CharHair::Poll()` — **FIXED** (see #3)
- `PlayCrowdAnimation` — **FIXED** (Session 73: null purge + FastInt UB + auto-loading)
- `OriginalChoreoRemixer::Init` — **FIXED** (Session 74: blanket return removed)

**Acceptable?**: PoseFatalities is the only remaining cosmetic disable. Low priority.

### 11. Task.cpp Wall-Clock Timing (`Task.cpp:482-499`)

**Status**: **FIXED** — removed `#ifdef HX_NATIVE` unconditional wall-clock override.
Now uses the same `if (mAutoSecondsBeats)` guard as Xbox. Menu/loading screens get
audio-synced timing (their `SetSecondsAndBeat` calls set `mAutoSecondsBeats = false`).
Gameplay still falls back to wall-clock (Game::Poll is stubbed, never calls
`SetSecondsAndBeat`). Full fix requires un-stubbing Game::Poll.

### 12. HamDirector Property Pre-Init (`Ham.cpp:189-214`)

Pre-initializes properties that DTA scripts normally set. Correct for native's
different init order. Not a bug — just a platform difference.

### 13. MergeSinks Int Cast (`Object.cpp:395-401`)

```cpp
#ifdef HX_NATIVE
    if (o && o->mSinks) {
#else
    if (o && (int)o->mSinks) {
#endif
```

PPC uses `(int)` cast for `cmplwi` codegen. On 64-bit native, casting a pointer
to `int` truncates. The native version is actually more correct.

---

## LOW: Legitimate Platform Differences (Keep)

These are correct and should remain:
- LP64 pointer size fixes (26+ instances)
- Endian conversion (`CharBonesSamples`, `Mesh`, `AsyncFile`)
- Kinect/MoveDir null safety (12+ instances)
- STLport-vs-libstdc++ namespace differences
- `__fsel` PPC intrinsic replacement
- Controller mode force (`GestureMgr`)
- DTA screen flow bridges (`UI.cpp`, `HamNavList.cpp`)
- Audio threading guards (`Game.cpp:297-310`)
- Non-fatal `MILO_FAIL_DTA` (correct for debug-build-origin)

---

## Roadmap Impact

### Removal Priority (Updated 2026-03-16)

| Hack | Status | Notes |
|------|--------|-------|
| ReplaceRefs ring guard (#1) | **DONE** | Removed, 3000-frame clean |
| Null-this guards (#2) | **REDUCED** | Callers are PPC decomp targets; init globals to stubs long-term |
| CharHair sort fix (#3) | **DONE** | ObjPtrList::sort fixed, hair physics re-enabled |
| CharClipGroup guards (#4) | **DONE** | All 4 guards removed — deferred purge prevents null leakage |
| gSuppressRefErase RAII (#5) | **DONE** | SuppressEraseScope + deferred purge mechanism |
| Wall-clock timing (#11) | **DONE** | Unified with mAutoSecondsBeats guard |
| GetObj strict mode (#6) | OPEN | Add `MILO_STRICT_GETOBJ=1` env var |
| Remixer Init blanket return | **DONE** | Pipeline runs for real; MILO_FAIL if MoveParents empty |
| Remixer null-check guards (12) | **KEEP** | All in PPC decomp functions; unconditional would break match% |
| HamDirector debug guards (3) | **KEEP** | video_recorder stub, non-fatal Find, camera fallback remain |
| HamDirector debug logging (2) | **SIMPLIFIED** | DebugWorldLoad() removed; logging now unconditional in `#ifdef HX_NATIVE` |
| HamDirector Enter/GetWorld (2) | **DONE** | Removed Phase 2 — FileMerger pipeline wires mMerger |
| SetNativeVenueWorld | **DONE** | Removed Phase 4 — dead code, no callers |
| gNativeHudDir hack | **DONE** | Removed Phase 5 — DirLoader parent chain + FindObject ProxyDir fallback. 461→7 warnings |
| ObjPtrVec::Node::RefOwner bug | **DONE** | Fixed Phase 5 — `static_cast<Hmx::Object*>(mOwner)` → `Owner()` indirection |
| Game::IsLoaded bypasses (2) | **DONE** | Removed Phase 3 — PollForLoading gates on conditions first |
| GamePanel::StartIntro block | **DONE** | Removed Phase 3 — engine pipeline handles all setup |
| Song::SyncState guard + stub | **DONE** | Unguarded Phase 3 — sync-wait loop has native guard |
| MoveMgr SetType config fix | OPEN | Fix objects.dta for `easeup_remixer` type |

### What Was Unblocked

- Hair/cloth simulation now works on all characters
- ObjRef ring fix from Session 59 confirmed complete (no corruption in 18+ sessions)
- Menu/loading screens use audio-synced timing (gameplay still wall-clock fallback)
- Choreography/remixer pipeline now attempts real initialization on native
- Web infinite hang fixed (DataWhile loop on uninitialized `mTotalMeasures`)
- 7 `HX_NATIVE` guards removed or restructured (2026-03-16)
- 14 more guards removed in FileMerger convergence Phase 3+4 (2026-03-17): Game.cpp bypasses (2), GamePanel StartIntro block, Song::SyncState guard + stub, HamDirector SetNativeVenueWorld + DebugWorldLoad (2 files) + Enter/GetWorld (2)
- Phase 5 (2026-03-17): gNativeHudDir removed (~330 lines loading+drawing), DirLoader parent chain + FindObject ProxyDir fallback added, ObjPtrVec::Node::RefOwner pre-existing bug fixed

---

## The inverse audit: native fixes that were never guarded (2026-08-19)

This document audits guards that exist. The inverse defect is a native fix written
**without** a guard, so it compiles into the Xbox plane too and silently changes the
code the decomp is trying to match. It is nearly invisible: the match% moves by a few
instructions, which reads as ordinary residual noise, and nothing about the source
says "native".

Two confirmed instances so far:

| Function | Introduced by | Cost | Fixed |
|---|---|---|---|
| `HamIKSkeleton::SetBone` — `if (!t2) return;` | `5d19777db` *native: venue rendering* | 6 instructions, 92.4% → 99.0% | `866ba1082` |
| `DelayEffect::Process` — `if (!mBuffer) return;` | `f8a417405` *native: v0xE mogg audio pipeline*, whose own body says "Null guard in DelayEffect::Process for freed mBuffer" | instruction table 160 instrs / 26 mismatches (20 diff_arg, 1 replace, 1 delete, 4 insert; base size 636) → 156 / 16 (all diff_arg, 0 insert/replace/delete; base size 624 = target size) | this audit |

The `Cost` column quotes instruction-table counts, not the rendered percentage. The
percentages for that row would be 95.7 % → 99.4 % normalized, but a rendered percentage
rounds and a mismatch count does not; see
[docs/decomp/patterns/rounded-100-hides-real-bugs.md](../decomp/patterns/rounded-100-hides-real-bugs.md).

### Detector

```bash
python3 scripts/analysis/native_guard_leak_scan.py --repo . --signal all
python3 scripts/analysis/native_guard_leak_scan.py --self-test   # negative control
```

Four signals, reported as separate tiers so each can be judged on its own:

- **blame** — line still attributed (`git blame -w -M`) to a commit whose *subject*
  marks it native-port work, and outside any guard-macro conditional.
- **interpolated** — that commit owns ≤10 lines and ≤40% of a function body somebody
  else decompiled. Authoring a whole function is not leaking.
- **guard-shape** — the line is the defensive idiom this document's Guiding Principles
  already name: a null/empty check whose body is `return`/`continue`/`break`.
- **shape-static** — every unguarded defensive guard in `src/`, ignoring history
  entirely. The blame signal cannot see a leak landed under a subject like
  `progress: ...`; this one can, at the cost of being an upper bound.

`--self-test` is the negative control: it re-injects `if (!t2) return;` into
`HamIKSkeleton::SetBone` verbatim as `5d19777db` left it — no comment, so the content
signal cannot fire for free — on a scratch commit, asserts the scanner reports it, and
asserts the currently-guarded site stays silent. It refuses to run on a dirty tree
because its rollback is a hard reset.

Every run opens with a provenance banner naming the commit it scanned, the
`report.json` it filtered with, whether `ninja -n` still has work to do for that
report, and the file denominators. **Every number below is relative to a commit** —
see the next section for why that is not pedantry.

### The result that matters most

**A hit is not a verdict, and the false-positive rate is high by nature.** The Xbox
build is full of genuine null checks, empty checks and early returns.

Counts, each with the tree it came from (they are not interchangeable — the guard this
audit fixed was itself one of the 814, so the merge-base and the branch head disagree
by one before you change anything else):

| Tree | Criterion | TIER S | in sub-100% fns | TIER S-lead (first 2 statements) |
|---|---|---|---|---|
| `eda64e956` (merge-base) | original regex | 814 | 107 | — (never a script output; hand-counted as "24") |
| `eda64e956` | corrected regex + attribution | 688 | 156 | 42 |
| branch head on `00cf7aa4d` | corrected regex + attribution | 686 | 154 | **41** |

The subset is now a flag, not a hand count: `--leading-stmts 2` prints it and lists it.
The count rose from ~24-27 to 41 because the attribution fix resolves enclosing
functions the old walk-backwards heuristic missed (a multi-line signature used to
charge every hit in its body to the *previous* function), not because more guards
appeared.

All 41 are target-faithful. **Exactly one real leak in the whole tree, and it was
`DelayEffect::Process`.**

### Do NOT use the `insert` count as the discriminator

An earlier revision of this section said the cheap discriminator for a leaked guard is
objdiff's `insert` count, reasoning that a leaked guard is a load/compare/branch our
side emits and the target does not. **That is refuted.** Measured over the 41
adjudicated target-faithful sites above:

| Screen | Fires on target-faithful sites | False-positive rate |
|---|---|---|
| `insert > 0` | 31 / 41 | **76 %** |
| `insert > 0` at instruction index ≤ 12 | 4 / 41 | 10 % |
| our-side-only compare/branch in the first 20 instructions | 0 / 41 | 0 % |

`insert > 0` is *necessary-ish and nowhere near sufficient*. Anyone who reads this
table and starts deleting null checks because a function has inserts will remove code
the Xbox build genuinely has — a correctness regression dressed up as a match
improvement.

The "low index" refinement does not rescue it either, and it fails on this lane's own
flagship example. `LiveCameraInput::NuiAudioDataCallback` was cited as proof of
target-faithfulness — the target has all three of its chained null tests — and it
carries inserts at indices **5 and 8**:

```
[5] insert  TGT: ---   SRC: addi  r8, r11, 0x1444
[8] insert  TGT: ---   SRC: lwz   r10, 0x0(r8)
```

That is address recomputation the target folds into a displacement, nothing to do with
a guard.

The screen that does discriminate is the third row: an **our-side-only compare/branch**
in the prologue region — an `insert` whose SRC is a `cmp*`/`b*`, or a `replace` whose
SRC is one and whose TGT is not. That is the shape a leaked early-out actually takes.
Positive control: at the merge-base, `DelayEffect::Process` showed

```
[ 9] insert  TGT: ---   SRC: cmplwi  cr6, r11, 0x0
[10] insert  TGT: ---   SRC: beq     cr6, 0x274
```

It fires on that one real leak and on none of the 41. Even so it is a screen, not a
verdict — the thing that settles a candidate is still the target disassembly: read the
function's prologue in `build/373307D9/asm/`.

Two further calibrations of what a hit is worth: five sites handed to this audit as
"reported unguarded" were *all* target-faithful, and two of them come straight back out
of this scanner's own TIER S.
