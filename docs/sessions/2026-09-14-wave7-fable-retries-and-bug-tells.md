# Wave 7 — band lanes, Fable retries of Opus floors, and ~100 behavioural bugs

**Dates:** 2026-09-14 → 2026-09-15
**Method:** ~75 lanes (`w7-a` … `w7-by`) in CoW worktrees under `~/tmp/w7-*`, one coordinator. Each lane got a worklist derived from `report.json` (canonical band × size × "no in-source note yet"), an off-limits list, and the lever/refutation list from the previous lanes. The coordinator verified every returning lane against the target listings and the latest main snapshot, rebased, ran a full `ninja`, merged `--no-ff`, pushed, re-snapshotted and ran the native gate. 77 merges, 1,095 commits, no `Co-Authored-By` trailers.
**Headline (baseline `d27301d3c` → `bae633077`):** 30,967 → **31,228** matched functions (+261), 5,427,300 → **5,544,916** B (+117,616), 47.713% → **48.747%** (+1.03 pp); fuzzy 55.252 → 55.459.

> **Read the headline last.** Two things mattered more than the percentage this wave: (1) a Fable retry of an Opus-certified "floor" busted roughly half of them and found a real bug on one row in ten, so an Opus AT_LIMIT note is now a *lead*, not a verdict; (2) around a hundred behavioural divergences were fixed, most of them invisible to every ruler in this project until the function was re-read against the listing.

## Per-function delta vs the baseline (as of `bae633077`)

| | count |
|---|---:|
| functions UP (> 0.001 pp) | 488 |
| functions crossed to 100.0 canonical | 265 (157,856 B of function size) |
| functions DOWN (< −0.001 pp) | 16 — every one deliberate or pairing noise, listed below |
| functions added / removed | 0 / 0 |

⚠ The 265 crossings sum to 158 KB of function size but `matched_code` moved +118 KB: `matched_functions` is credited on the canonical ruler, `matched_code` on `fuzzy_match_percent`, and a canonical 100 carrying register-permutation rows adds zero bytes. Same ruler split as wave 6; quote the report delta, never the sum of sizes.

### The 16 DOWN rows, all adjudicated

| function | before → after | why |
|---|---|---|
| `Game::OnSetShuttle` | 73.18 → 55.83 | two behavioural bugs fixed; faithful spelling scores lower (w7-aw) |
| `SkeletonClip::PrevSkeleton` | 88.26 → 70.95 | out-params of the second `RecordedFrameAt` were swapped; object now the target's exact size (w7-am) |
| `RndMatAnim::LoadStage` | 92.80 → 73.28 | the keys are THIS object's, not the owner's (w7-ae) |
| `DingoSvrXbox::Poll` | 94.57 → 88.87 | second `MILO_NOTIFY` the image has restored (w7-ar) |
| `NgLight::SphereConeTest` | 65.64 → 63.15 | the image computes a reciprocal, not a divide (w7-am) |
| `UIManager::IsGameScreenActive` | 97.50 → 95.00 | the screen equality was inverted (w7-ag) |
| `MCContainerXbox::Mount` | 88.43 → 87.08 | content size was in the wrong half of the quadword (w7-af); four later spellings held |
| `fft_matrix_inverse_columnwise` | 89.03 → 87.71 | a permuter win advanced the malloc'd temp itself and freed the advanced pointer (w7-ay) |
| `DirLoader::WriteTypeMemDump` | 95.94 → 94.92 | an `if (file)` guard the image does not have dropped (w7-ai) |
| `CacheResource(const char*)` | 71.56 → 71.49 | rb3's duplicated `return cacheFile;` merges the String dtor (w7-bo) |
| `JoypadPollCommon` | 96.726 → 96.699 | `mConnected == true` → `cmplwi 1`, 125 → 123 rows; canonical-ruler trap (w7-bn) |
| `MemcardXbox::ShowDeviceSelector` | 88.125 → 88.109 | stop reassigning the `i3` parameter (w7-af) |
| `MemTracker::MemTracker` | 93.134 → 93.127 | STLport `allocate(n, allocated_n)` write-back reproduced (w7-bu) |
| four `fn_827F1C*` funclets in `NetCacheMgr` | 100 → 99.8/99.9 | `fn_<addr>` EH funclets are paired by byte signature; `AddLoaderRef` was worked in the same TU |

## What a Fable retry of an Opus floor is worth

Every Opus lane left an in-source note on the rows it could not cross (`w7-<lane>`, `RESIDUAL`, `NEGATIVE`). From lane `w7-ay` onward, a Fable lane was given batches of those rows with the notes and the recording lane's commits, told not to re-derive a recorded negative, and asked to find the lever the note did not try.

| Fable lane | rows | raised | crossed to 100 | real bugs on "certified" rows |
|---|---:|---:|---:|---:|
| w7-ay | 12 | 7 | 2 | 2 (transposed particle force; heap-corrupting permuter win) |
| w7-ba | 12 | 13 moves | 7 | 4 (SetLeg Cross order; pelvis offset; Name() in a notify; assert line) |
| w7-bj | 6 | 4 | 1 | 2 (BuildNGCone cap faces, cross-section angle; Synapse unsigned counts) |
| w7-bm | 6 | 5 | 3 | 0 (new tail-merge lever) |
| w7-bn | 16 | 11 | 9 | 0 (+ PCH-struct COMDAT-reorder lesson) |
| w7-br | 19 | 14 | 7 | 2 (GetProgress missing `subf`; LE_A8R8G8B8 front buffers) |
| w7-bv | 12 | 12 | 9 | 3 (CamShotVOData outro chain; DumpSongLayout pair order; FlowPickOne default arm) |
| w7-bu | 12 | 11 | 0 (+1 funclet) | 1 (StartVoiceThreadEntry pooled delete) |
| w7-bt | 12 | 10 | 2 | 0 |
| w7-bs | 18 | 14 | 4 | 1 (ComputeFaceTangentBasis notify on degenerate faces) |
| w7-by / bw / bx | 25 | 21 | 5 | 2 (RenderConeDefs fog constant slots; UtilDrawPlane dropped sixth parameter) |

The pattern held across every batch: the residual an Opus lane wrote up as "pure scheduling", "register-allocator floor" or "frame-pointer reservation" was, roughly half the time, a source shape the lane had not tried — and in about one row in ten the shape it had not tried was the *correct* one and the existing source was wrong. The lever list that accumulated is in `memory/project_w7_levers_and_bug_tells.md` and, per lever, in the in-source notes; the reusable ones:

- **A "frame-pointer reservation" floor was C++ EH.** Our object carries `except_record_*` + `__unwind$` funclets destroying a `MemDoTempAllocations` RAII object where the source had bare `MemPushTemp`/`MemPopTemp` (LoadVertices, WrapText).
- **A "regalloc floor" was the loop's shape.** Zero-based `k` with `i = k - 2` keeps `i` as the induction variable; `kWeights[k]` not a walked pointer (BlurShadowRT 92.7 → 100). A hand-rotated `if(p!=e){do{}while(p!=e);}` suppresses CTR conversion per loop (LoadDIB). A counted `for` gets the CTR pass's dead guard (CacheXfms 82 → 96).
- **A "pure scheduling" residual two lanes certified was one missing `subf`.** A branch that lands ON an arithmetic instruction means both arms perform it (GetProgress dropped `mLoopStart` whenever a loop end was set).
- **Statement splitting across a call boundary**: bind each call result to its own named local (GetClipStartAndEndBeats → 100); hoist `T &ref = *it` above the call that first uses it or MSVC homes `&*it` as a dead `stw`.
- **Same-TU callee register summaries.** A non-inline same-TU callee lets MSVC keep caller values in volatile registers; mark it `inline` (ComputeFaceTangentBasis). A `__forceinline` TU-local helper keeps an sret result as an expression temporary (IsValidCase 88 → 100); plain `inline` was not inlined.
- **Tail-merge lever for state-machine exits.** Write the full exit tail at every state site so MSVC cross-jumps them (StandingStillGestureFilter::Update → 100); a block written after an if/else chain where the image's earlier arms branch to the destructor belongs INSIDE the final else (CamShotVOData).
- **Aggregates.** Four adjacent floats = one `Vector4` via inlined `Set()`; three `Vector3` locals = one `Hmx::Matrix3`; a struct assignment `m = *it` yields the second hoisted pointer; `memset` vs `= {0}` splits the aggregate clear differently.
- **Opaque first store.** When the image stores a member twice, every plain spelling is DSE'd; an opaque `int **` first write survives (MemHeap::Init 83 → 100).
- **Refuted from both ends, do not re-probe:** stlport `vector::size()` load order (call site and header: 4 UP / 388–720 DOWN).
- **MSVC's CSE is one pass.** A product of a re-derived subexpression is not unified with a product of the named local: `loopStartNorm + loopRangeNorm * loopProgress` spelled inline at each position sum keeps the image's per-site `fmadds` (LoopVizCallback::UpdateOverlay 93.5 → 99.996; two earlier notes had blamed the stack allocator and an extra callee-saved FPR).
- **Two zero-store sites inside a loop** make MSVC hoist the constant into a callee-saved register; one store site with the rejecting tests as `continue` rematerialises `li` like the image (LinkRibbonDrawState 85.7 → 100). An `erase`/`insert` pair is `vector::resize` (AllocateMeshes 87 → 100).
- **Commutative operand order across a whole body flips with the declaration position of ONE named constant** (MoveParticles): a global value-numbering artefact, not a per-expression floor — try moving the declaration before certifying.
- **Floors that are genuinely not source:** the image CALLS a helper that the same TU inlines elsewhere (FinishPostProcess); curl's `mprintf` anchors the highest-addressed `.rdata` static, and reordering the tables reaches 99 but changes the `.rdata` layout (dprintf_formatf, rejected); a local escaping by address to an out-of-line callee fixes the frame (SkeletonViz::SetCamera).
- **Canonical-ruler trap:** a spelling that removes 60 register rows and adds 6 offset rows LOWERS canonical (RndMeshDeform::Load, JoypadPollCommon). Read the full-precision number for any sub-0.1 pp comparison.

## Behavioural bugs

About a hundred were fixed. Each is described with its proving listing address in the merge commit that landed it (`git log --merges d27301d3c..bae633077`); the extract in the wells directory lists the 89 that merge messages itemised under a "bugs" heading. A cross-section, by what the tell was:

- **A single `diff_arg` row on an argument register** = wrong out-param or the wrong one of two similar arguments: `SkeletonClip::PrevSkeleton` (second `RecordedFrameAt` out-params swapped), `UILabel::CenterWithLabel` (widths swapped), `SuperEasyRemixer::DumpSongLayout` (variant pair order), `MetaPerformer::CalcCharacters` (primary/secondary), `ClipPlayer` running player 1 as player 0.
- **A guard we added or dropped**: `NailedMovesInRoutinePct` short-circuit, `DirLoader::WriteTypeMemDump`'s `if (file)`, the NaN guard the image has (w7-u), `AnnotateClip`'s null test on a pooled `new DataArray`, `FlowPickOne` activating an uninitialised `chosen` on the default arm.
- **Wrong callee / wrong instantiation**: `StartVoiceThreadEntry` freeing envelope params through the global `operator delete` instead of the header's pooled one; `MakeString<char>` vs `<unsigned char>`; a `void *` error code passed to `%u` (GetFreeSpaceSync).
- **Serialisation**: `RndEnviron::Load` rev gate desynchronising every rev 3..15 file; `CharFeedback::Load` under-reading by 4 bytes on revs 3–5; `FlowCommand::Load` resolving against the wrong ObjectDir; `RndMatAnim::LoadStage` reading the owner's keys; `CharDebug` writing UVs over bone indices.
- **Control flow read from the listing**: `CamShotVOData`'s win-level block belongs inside the final else (every BATTLE/CAMP/HYPE category notified spuriously and overwrote `charSym`); `ComputeFaceTangentBasis` printing "has bad UVs" for degenerate faces; `MoveDir::Poll` quadrupling its smoother on measure 3 instead of beat 3; `XboxEnumeration::Poll` stopping after its first batch; `JsonToDta` losing case 0; the DTA lexer skipping `//` comments.
- **Math**: a transposed particle force rotation (`MoveParticles`); `RenderConeDefs` uploading its fog constant with the components in the wrong slots (image: `(0, halfDist, 0, 1/far)`); `UtilDrawPlane` dropping the bool it forwards to all four `DrawLine` calls; a ribbon bend in the wrong plane; `WorldCrowd` impostor right-vector with x and z transposed; a wrong matrix element in the photo spotlight; `SetLeg`'s Cross operand order; a hip-centre pelvis offset; `EQEffect::SetParameter` band-3 coefficients 4× too large; `NgLight::SphereConeTest` reciprocal.
- **Sizes and formats**: `DxRnd::InitBuffers` front buffers are `D3DFMT_LE_A8R8G8B8` (`ori 0x106`); a compile-parameters struct 0x14 bytes short; a 64-byte buffer; `XboxAllocator` 4-byte-aligning every VMX float vector; `XALLOC_ATTRIBUTES` bit layout (MSB-first bitfields); `DingoJob::AddContent` under-sizing and never terminating every telemetry POST body; curl's `%s` printing nothing.
- **Index / bookkeeping**: a hash-table index shifted twice; `AppendWeights`' dedup scan running backwards; `FlowPickOne`'s use-index arm incrementing `mIndex`; a step-key index never recorded; `HamDriver` polling off its own output; `CDReader`'s pending-file bookkeeping; `SampleInst360::GetProgress` loop length.
- **Diagnostics that named the wrong thing**: a missing-glyph message printing `ClassName()` instead of `TextToken()`; `MemTracker::DiffDump` labelling allocs and frees backwards; `GetSystemLanguage` wrong for Nordic and Spanish consoles; three wrong assert lines in `LightPreset::Animate`.

Two deliberate fidelity trades were taken *against* the score because the image does the thing: `DingoSvrXbox::Poll` keeps its second notify (−5.7 pp) and `Game::OnSetShuttle` keeps both fixes (−17 pp).

## Native gate

Green at every merge, including the final one at `bae633077` (506 registered / 437 executed / 0 failed / 69 skipped, budget 69). A `--all-gates` run at `29a8e7494` (gameplay, audio, long tests on) was also green: 492 executed / 0 failed / 14 skipped. Three lanes broke the native build with PPC-neutral iterator spellings and were repaired in `w7-bc`, `w7-bd`; the rule is now in every brief: stlport's `vector::iterator` is a raw `T *`, libstdc++'s is not — write `(char *)(T *)v.begin()` and `std::vector<T>::iterator(p)`. One native test (`test_dxt5_alpha`) had encoded the endpoint-order bug that `w7-am` fixed and was corrected in `w7-be` — after a behavioural decomp fix, adjudicate the failing *test* against the target listing too.

## Instrument notes (measured, not guessed)

- `measure_progress.sh` has no `--min-diff`; an unknown flag falls into the baseline-ref position and dies as a git "ambiguous argument". Use `scripts/analysis/compare_progress.py <a> <b> --functions --min-diff 0.001`, or the inline compare the coordinator used (both `report.json` into `{(unit,name): (float(match_percent_normalized), size)}`; `match_percent_normalized` is a *string* in some rows).
- `run_diff_inspect(mode="mismatches")` caps at 30 rows; use `run_objdiff(concise=false, full_listing=true)` for the whole table. `mode="attributed"` fails on `rndobj/Utl.cpp`; `mode="asm_listing"` fails on `rndobj/PropAnim` ("unknown target … PropAnim.obj").
- `run_symbol_sweep(kind="functions")` measures objects as last built; `ninja` first.
- `run_objdiff`'s "Offset Mismatches (resolved)" block ignores every non-`this` base register, not only `(r1)`.
- Top-level units (`default/Memory_Xbox`) have their listing at `build/373307D9/asm/<Unit>.s`, not under `asm/src/…`.
- A `fn_<addr>` EH funclet moving 100 → 99.8 in an untouched TU is UNVERIFIABLE_PAIRING noise; confirm with `run_objdiff build=false`, do not count it.
- Frontier derivation must scan the WHOLE function body (definition line to the next column-0 `}`) plus ~60 lines above for a note; a ±60/25 window mis-reported three noted rows as fresh.
- Lane reports truncate long mangled names with `…`; read the real name from the worktree's `report.json` before sweeping. Lanes `w7-ao/an/am` cited addresses that did not align with the listing while the semantic claim was right — verify the claim against the listing, not the address.
- A PCH-reached header struct edit reordered small COMDAT `.text` in 18 unrelated objects (measured against a same-source control); keep XDK structs TU-local when one TU uses them.
- Do not merge into main during a gate's compile phase, do not create a worktree while main's `ninja` is mid-run, and map ninja PIDs to their cwd via `/proc/<pid>/cwd` before deciding main is busy.

## Where the frontier stands

After `bae633077`, every row in the 80–99.99 canonical band at ≥ 200 B carries an in-source note and has been Fable-retried at least once, and the 40–80 band at ≥ 300 B has no un-noted row. The lever this wave found — a second model re-reading a recorded floor — has been applied to the whole list, so the wave-7 frontier is exhausted and the next wave needs a different derivation (the 200–300 B rows below 80, the `WRONG_CALLEE` census, or the stub families in `docs/decomp/REMAINING_WORK.md`).

Coordinator artefacts (worklists, merge messages, per-merge snapshots `report-interim-<sha>.json`, gate logs, `tooling-notes.md`, `bugs-extract.txt`) are in `~/tmp/dc3-wells/w7/`.
