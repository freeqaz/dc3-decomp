# Two months of bugs that reach the native port — 2026-07-15 → 2026-09-15

**Dates covered:** 2026-07-15 → 2026-09-15 (3,596 commits on `main`)
**Method:** three parallel surveys — a commit-level sweep of the 2,490 `src/` commits, a sweep of the 69 `native/` commits plus the 17 in `../milo-native-engine`, and a digest of the wave close-out docs. Headline claims were then re-verified individually against `git log`; three did not survive and are recorded at the bottom.
**Why this document exists:** the native x86_64 port compiles the *same* `src/` tree as the PPC target, so a semantic divergence in the decomp is a live bug in the playable build. This is the list of those, separated from the objdiff match-percentage work that dominates the commit count but changes no behaviour.

> **Read the count last.** The bottom-up commit sweep put the total near 627. The project's own wave documents tally differently — wave 7 records "~105 bugs" and wave 3 "~60 real gameplay bugs." Those are not contradictory (waves are subsets of this window) but they count at different granularity, and the 627 came from a seven-way agent fan-out whose cross-chunk de-duplication was not verified. **Treat "several hundred" as sound and any exact figure as soft.** Sept 13–14 alone account for roughly 360 of it; those were deliberate bug-hunt days, not a representative rate.

## Scope: what counts as a bug here

Counted: our C++ did something semantically different from the shipped Xbox binary — wrong field, inverted condition, wrong loop bound, dropped arithmetic, wrong callee or vtable slot, Save/Load field-order desync, lifetime error, missing virtual dispatch, wrong `.data` constant.

Not counted: register allocation, declaration/stack-slot reordering, orphan template instantiation, COMDAT placement, ICF renaming. These move match% and nothing else. They are the large majority of the 3,596 commits.

## Gameplay-breaking

| Bug | Commit | What it was |
|---|---|---|
| `MoveFrame::Load` never wrote `mNodesInverseScale` | `d57bc744d` | Zeroed every Ham2 scoring weight — **any dance move scored perfectly**. 76.8% → 100%. Core game loop. |
| `ObjectDir::Iterate` gated on the wrong symbol | `4e4cf8513`, `2a1b14b6f` | Every `{$dir iterate ...}` call in the game's DTA was a silent no-op. 54 call sites went live at once. 96.6% → 99.4% → 100%. |
| `Rand::Seed` signed shift (`srawi`) | recorded `5a0c71f52`, test `752712aef` | Sign extension corrupted roughly half the global RNG table that every `RandomInt`/`RandomFloat` reads. |
| `GetSystemLanguage` wrong language | `a3aed0f17`, merge `e2b8a366f` | Nordic and Spanish consoles selected the wrong language; the image returns out of both switches. 98.48 → 100.0. |
| Kinect gesture filters miscalibrated | `1aa8cd9b5`, merge `fd3224648` | `HighFiveGestureFilter` had all three thresholds wrong, one with the wrong **sign**; part of a batch of thirteen wrong constants "invisible to every ruler." |
| Pose scoring degenerate | `a7be9ff49`, `e94ec17f9` | Bone lengths never populated (every error node pinned at 1.0 → scoring always zero); skeleton hip-root never set (`IsValid()` permanently false); pose X-axis mirrored; gesture arrays bound per *hardware slot* instead of per *player*, so two-player scoring could never work. |
| `HamDirector::PollEnabled` accidental override | `389f21dea` | Same signature as a virtual it did not mean to override; `SetPollEnabled(false)` dropped the director out of the poll list entirely instead of early-returning inside `Poll()`. |

## Crashes, hangs, memory corruption

| Bug | Commit | What it was |
|---|---|---|
| Null-`this` deref class, 9 sites | `ebe9d3a99`, `4e829662f`, `9b294fc25`, `607239f44` | Xbox maps guest page 0 readable/writable/zeroed, so a null-`this` small-offset store silently no-ops on console. Linux never maps page 0 — identical code SIGSEGVs. |
| `~Object` skipped `ReplaceRefs` in a cascade | `9d3fc20f8` (red test `bceef97ed`) | Root cause of dangling `ObjPtr`s: every object destroyed as cascade collateral left a live dangling reference. |
| `TaskMgr::Poll` freed an already-freed Task | `184b5f06d`, `615572c35` | Intermittent SIGSEGV at the autosave-warning → title-screen transition, reproduced 1-in-8 runs. |
| Ogg decode thread, six live bugs | `ec12f4215`, merge `e421d16c0` | Splice, uncleared field, inverted progress flag. 78.96% → 99.41%. Includes the store `~VorbisReader()` spin-waits on — without it the destructor never returns. |
| Bloom allocator wrote past its object | `b93f589b8`, merge `19e4fc1b1` | The allocation loop ran off the end of `sBloom` **and** asked for 16× the pixels. Real heap corruption. |
| `TrigTableInit` out-of-bounds write | `25955293b` | A peeled last entry wrote `gBigSinTable[513]` of a 512-float table, and read index 511 (the delta slot, not the sine slot). An `HX_NATIVE` guard had been papering over the OOB by skipping the write on native. ⚠ Earlier drafts of this table cited `90084bd8a` and `fcb2aebe7`; **both are wrong** — `90084bd8a` is a next-day pure-codegen negative result that never describes the OOB, and `fcb2aebe7` is a merge restating the already-applied fix (`git merge-base --is-ancestor 25955293b fcb2aebe7` → yes). |
| DTA worker thread stack ported verbatim | `f0304916c` (engine `f1a2d7f`) | Xbox's literal 64 KB. Clang inlines `MakeString` where MSVC keeps it out-of-line, making native frames ~17.6× larger per DTA nesting level — overflowed after ~4 levels vs Xbox's ~130, on real shipped content. |
| `DataArray::SortNodes` PPC32 stride | `8c73183dd` | Hardcoded 8-byte qsort element size; `DataNode` is 16 bytes on LP64, so every native sort corrupted or crashed. |
| 26 undefined symbols hidden by the linker | `eb62d26a1` | `--unresolved-symbols=ignore-all` was masking guaranteed crash-on-first-call landmines (12 Bink entry points, 3 Kinect NUI calls, a mismatched `XNotifyCreateListener`). |
| DTA call-stack overflow into BSS | `36873b0ff`, `26cc0088b` | 150-deep nesting via the debug HTTP endpoint overflowed `gCallStack[100]` into adjacent BSS, corrupted the `gPreExecuteFunc` pointer, then called it. |
| `FreestyleMoveRecorder::Poll` buffer overrun | `909f44538` | The recording scratch is a whole `FreestyleMoveFrame`; the old shape wrote past a depth buffer. |

## Rendering and audio

| Bug | Commit | What it was |
|---|---|---|
| `~RndTex` never invalidated the GPU texture cache | `5cae2299e` | A new texture at a recycled address rendered the *previous* texture's image. Also a GPU-memory leak. |
| `Movie::IsOpen`/`IsLoading` missing `const` | `8ae44c2a3` (engine `a0ea2b1`, `4c9ff4a`, `f8e4175`) | MSVC vtable slot mismatch meant `BinkMovieImpl`'s overrides never took effect — movie state read as hardwired `false`. |
| DXT5 alpha endpoint swizzle | `803fd4836` | Endpoint bytes need the same Xbox byte-swizzle as index bytes; alpha decoded wrong. |
| `RndMesh::DrawShowing` LOD name-match | engine `138e160`, `9898a63` | Name-matched `"_lod"` and deleted whole characters that have no higher-detail sibling. |
| 105 missing DATA definitions | `537a5e52e` | 71 of them were written but wrapped in `#ifdef HX_NATIVE`, starving the PPC build; 9 carried a wrong native-side *value* — `RndPostProc::sBloomLocFactor` shipped `0` instead of `1.0f`, multiplying bloom by zero. |
| XAPO base classes, wrong vtable length | `8ae44c2a3` | `HeadsetXferEffect` never even paired (namespace-mismatched vtable name); `SynapseAPO::DoProcess` had an unreachable wrong-signature override. |
| `DspAllocate` parameter-type mismatch | `ac5bf312c` (engine `6c27ac3` → `d62700c`) | The real allocator body was unreachable via mangling — `DelayEffect`/`FlangerEffect` ran on uninitialized buffer pointers and later `delete[]`'d garbage. |
| `TransformNormal` dropped the transpose | `12f02213b`, merge `c41bcd7a1` | A normal transform that omitted its transpose. |
| `TypeProps::Save` restored every pair transposed | `01e1922ff` | The EditorDir extraction loop could also never advance. |

## Input — Kinect / webcam pipeline

Largest single cluster alongside memory-lifetime. Beyond the scoring bugs above: the colour feed was decoded by a `return 0` stub (`23ffaf52d` — solid black camera image, `YUVtoRGB` declared in an anonymous namespace and never defined); `D3DLOCKED_RECT` left uninitialized when no frame was available (`0ff96a7b5`); `LiveCameraInput::LockStream`/`UnlockStream` compiled out by a wrong `HX_NATIVE` guard (`1ce9169c5`); LP64 pointer truncation wrapping a texel pointer ~8–16 GB forward (`616c49625`, `4b45c8ed2`); the wrong `UnlockRect` overload on all four buffer-update paths (`4b45c8ed2`); 16:9 webcam aspect forced through Kinect's 4:3 box for a 33% vertical skew (`7a1974ead`); systematic depth bias up to 0.83 m in monocular root recovery (`05ca6d094`); torso joints computed as naive midpoints instead of Kinect-calibrated fractions (`ef2172f3f`).

## Serialization and dispatch

- **Save/Load field-order desync** — five real serializer bugs out of 35 candidates examined, 30 benign (`a6de2f191`, merge `541048cb4`). `CharUpperTwist::SyncProperty` had to have the shipped *permuted* property-to-member pairing restored (`834bcfc7f`); two of the five were hiding under a rendered 100.0% (`148e97f09`).
- **Missing/extra virtual dispatch** — the linker map proved 21 missing virtual overrides, "including Enterables that never ran `Enter()`" (`49ad7cfd5`). A later whole-binary vtable adjudication added 6 more missing virtuals and fixed 3 wrong/extra declarations (`e2278333c`, `389f21dea`), with one proposed removal reverted because it broke the shared engine.
- **`DataNode::Equal`** — fixed in two passes (`6ba456674` Aug 4, 96.5% → 98.0%; `54f87c528` Sep 13, 98.18 → 99.19). The `kDataString` mis-decomp that made String/Symbol equality garbage in the DTA layer is recorded in `docs/native/SCORING_ENV_VARS.md` as caught by the native runtime gate before the 2026-07-17 default-on landing.

## Progress over the window

| Metric | Start | End |
|---|---|---|
| Matched functions | 30,677 | 31,310 |
| Authorable matched (canonical) | 96.80% | 97.05% |
| XEX headline | 48.747% | 48.856% |
| Authorable complete units | 623/967 | 629/967 |

Activity was heavily back-loaded: 8 commits in W29 against 1,274 in W38.

## Three claims that did not survive verification

Recorded because they were produced by the survey and would otherwise be repeated.

1. **"`Hmx::operator*(Matrix4, Matrix4)` computed `b*a`, transposing every view/projection matrix"** — *not supported*. The nearest real commits are `7694afdfd` (`Hmx::operator*(Transform,Matrix4)` reaching 99.69% via the Dot3 row-first idiom — a **matching** fix whose residual 8 instructions were probed and refuted as a source lever) and the genuine dropped-transpose bugs `12f02213b` / `01e1922ff`. The claim appears to be a garbled merge of those. Do not cite a global matrix-multiply order bug without re-deriving it.
2. **"`Locale::mInitialized` defaulted to 0, so every non-English language file failed to load"** — *not supported*. The verified locale bugs are `GetSystemLanguage` returning the wrong language for Nordic and Spanish consoles (`a3aed0f17`) and the chunk sort comparator being `FastSort<3>` rather than `LocaleChunkSortFunc` (`317bad0b4`). `Locale::Init` work in this window (`844ce3cf2`, `f035c8d2e`, `ec0a4bc4e`, `7a6826c6c`) is scope/slot residual, not a gating bug.
3. **The 627 total** — see the note at the top. The order of magnitude is sound; the figure is not a measurement.

This is the same failure mode the wave docs already warn about: a confident restatement is not a measurement. `docs/sessions/2026-09-13-band-lane-wave.md` records that history-enumeration queries **fail open** — `git show` on a merge returns an empty diff, a bad pathspec matches nothing, and `git log --grep` in a shell loop mangles subjects, all three returning a clean-looking zero.

## Standing caveats on the instruments

- **The 2026-08-19 bulk AT_LIMIT certificates are not evidence.** Every lane that re-read them found real bugs — `RhythmBattle::OnBeat` 38.7% → 99.45% on 9 bugs, `BustAMovePanel::OnBeat` "unfixable" → 100% on 3. One UI slice was 6-of-7 wrong, another 8-of-8.
- **A displayed 100.0% hid real bugs** — the canonical ruler forgives register permutation, and until the 2026-08-20 objdiff fork fix it forgave wrong callees entirely. `CharUpperTwist`'s Save/Load desync sat under a rendered 100.0.
- **Do not quote ctest as "N/N passed"** — it counts skips as passes. Quote the executed/passed/failed/skipped split with its date.
- **`MILO_ASSERT` is non-fatal on native**, so a clean exit code is not evidence of correctness.
- **Static review is insufficient for native behavioural fixes** — `feedback_runtime_verify_native_fixes.md` records a fix adversarially confirmed on static reasoning and a green suite that produced zero behavioural change at runtime and had to be reverted.

## Sources

Commit history on `main` 2026-07-15 → 2026-09-15; `docs/sessions/2026-09-13-band-lane-wave.md`, `2026-09-14-wave6-instrument-defects.md`, `2026-09-14-wave7-fable-retries-and-bug-tells.md`, `2026-09-15-wave8-zero-percent-climb.md`; `docs/native/SCORING_ENV_VARS.md`, `MISSING_DEFINITIONS.md`, `HACK_AUDIT.md`, `CONSOLE_HW_FINDINGS.md`; `../milo-native-engine` history over the same window.
