# Native-impact bug taxonomy — 2026-07-15 → 2026-09-15

**Dates covered:** 2026-07-15 → 2026-09-15 (3,596 commits on `main`; ~1,800 scanned for this inventory)
**Why this document exists:** the native x86_64 port compiles the *same* `src/` tree as the PPC target, so a semantic divergence in the decomp is a live bug in the playable build. This is the list of those, separated from the objdiff match-percentage work that dominates the commit count but changes no behaviour.
**What it is for:** hunting the ones still in the tree. Each class below carries its **recognizer** (how you spot another instance), its **detector** (the tool that finds them at scale, or the explicit absence of one), and what is **still unhunted**.

> **The count.** 524 net distinct behavioural bugs; class-sum 567. The gap is real, not an error to reconcile: ~30–40 commits fix bugs in more than one class (`TypeProps::Save` `01e1922ff` alone has 3 spanning classes 1/3/6). Dedup applied: chunk_03 (~32 items) dropped as a confirmed full re-chunking of sep14_part2 (~20 hashes cross-checked); orphan_merge (13) dropped as a confirmed duplicate of sep13_c5 items 15–27. This supersedes the earlier ~627 headline, which had unverified cross-chunk dedup.
>
> **Labeled gaps in the count**, stated rather than reconciled away: ~35–40 items in three buckets (aug21–31 guards ×17, sep1–11 conditions ×16, sep1–11 arithmetic ×13) are counted from their reports' own per-category totals but **not individually itemized** — their hashes did not survive compaction. 14 items in sep13_c5 have recovered hashes but lost defect descriptions, so their class placement is best-guess. Sibling lanes (`a0556c709e7db8414` and four other agent IDs) were never merged in and could move the number either way.

---

## The taxonomy

| # | Class | Count | Detector status |
|---|---|---|---|
| 1 | Wrong struct field / wrong offset | 121 | ❌ none (lead-only tool, known false positives) |
| 2 | Inverted / missing / extra condition | **128** | ⚠ `cond_semantics_scan.py` (added 2026-09-30) — branches only; mask idioms and jump tables are manual |
| 3 | Wrong loop bound / off-by-one / container | 33 | ⚠ `cond_semantics_scan.py` (bounds/strictness on branches; CTR trip counts not traced) |
| 4 | Arithmetic: sign, width, dropped term | 68 | ❌ none verified |
| 5 | Wrong `.data` constant | 54 | ⚠ partial — `mutable_float_audit.py`, blind spot measured below |
| 6 | Wrong callee / vtable slot / dispatch | 97 | ✅ best covered |
| 7 | Save/Load field-order desync | 14 | ❌ none |
| 8 | Lifetime / ownership / refcount | 22 | ⚠ `null_deref_sweep.py` covers one sub-shape |
| 9 | Buffer overflow / OOB write | 9 | ❌ none |
| 10 | Uninitialized read / missing store | 16 | ⚠ `bss_initializer_scan.py` — **examines 20%** |
| 11 | Entire missing function body | 2 | ✅ `fake_impl_scan.py`, `stub_flag_audit.py`, `zero_pct_cause.py` |
| 12 | Dead / no-effect property | 3 | ❌ none |
| 13 | Wrong access specifier *(added 2026-09-15)* | 6 rows / 4 decls | ✅ `access_specifier_scan.py` (new) |

**The headline for hunting:** classes 1+2+3+4 are **350 of 524 bugs — 67% — with no detector between them.** Every one was found by a human or an agent reading a target listing against our source. That is where the remaining population is.

---

## Per-class recognizers

### 1 — Wrong struct field / wrong offset (121)
**Tell:** an objdiff offset-mismatch row rendered as "source accesses X, target accesses Y" — frequently a neighbouring field, or one struct-layout generation off after a member reorder. Structurally: `lwz`/`stw`/`lfs` at a plausible-but-wrong immediate; or a `dynamic_cast` result's owner field assigned instead of that object's own.
**Most dangerous sub-shape:** same-width, same-shape wrong field **scores 100%**. `RndFlare::Load` and `CharUpperTwist::Load` both hid under a displayed 100.0 (`docs/decomp/patterns/rounded-100-hides-real-bugs.md`).
**Detector:** none. ⚠ `run_analyze_function`'s "Offset Mismatches (resolved)" block **ignores the base register** and renders a pure `(r1)` stack-slot diff as a wrong-field finding — it manufactured exactly that story for `FxSendChorus::Load`. Treat as a lead, never a finding.

### 2 — Inverted / missing / extra condition (128) ← largest class, no tooling
**Tell:** a branch whose sense is flipped relative to the target, or one that exists on only one side. This repo's commits routinely cite the exact bit (`fcmpu` + bool materialization for an inlined `IsNaN`, `beq`/`bne` polarity, `cmplwi` vs `cmpwi`).
**The single most productive recognizer across all 11 source reports** was grepping commit *subjects* for `invert|guard|missing|extra|backwards` and reading the body for the disassembly proof.
**Sub-shape worth its own hunt:** guards **we invented** that the image does not have (`RndText::OnComputeCharWidths`, `DepthBuffer3D::DrawShowing`, `CharDebug::DisplayObject`, `CharEyes::LidTrackAndClampingUpdate` — whose else-arm dereferences unconditionally anyway, so it never guarded anything). See `pattern_decompilation_introduced_semantics.md`.
**Detector:** `scripts/analysis/cond_semantics_scan.py` (added 2026-09-30) — decides a `beq`/`bne` row on aligned successor BLOCKS, not the mnemonic, so block placement stops reading as inversion. First run: 2 real bugs (`XboxContentMgr::PollRefresh`, `AllocAlign`). Blind spots and their manual recognizers: `docs/decomp/patterns/wrong-condition-is-a-block-question.md`.

### 3 — Wrong loop bound / off-by-one / wrong container (33)
**Tell:** a bound off by one register or immediate (`cmplwi r0,N` vs `N-1`); `.begin()` where the target splices at `.end()`; `push_back` vs `push_front`; an increment on the wrong side of a `continue` edge. Often **invisible under the normalized ruler** — the bound compiles to identically-shaped code with a different literal.
**Detector:** `scripts/analysis/cond_semantics_scan.py` (added 2026-09-30) for the branch-bound shapes (OFF-BY-ONE / STRICTNESS buckets, value-set comparison so `x<=3`≡`x<4` is not flagged). ⚠ "Invisible under the normalized ruler" applies to the ROUNDED display only: the exact `match_percent_normalized` charges a changed literal (measured by that scanner's immediate-sabotage control). Container shapes (`.begin()` vs `.end()`, `push_back` vs `push_front`) are a wrong-callee/argument question, not a branch — still no detector.

### 4 — Arithmetic: sign, width, dropped operation (68)
**Tell:** a cited instruction substitution — `fnmsubs` vs `fmsubs`, `lwa` vs `lwz`, `divw` vs `divwu`, a missing `fmuls`/`fadds` term, an explicit double-rounding claim.
**Exclusion rule that kept this class honest:** reassociation-only claims with no stated observable difference were excluded throughout. Xenon MSVC defaults to `/fp:fast` and reassociates freely, so float term order is usually inert — `571722546` records a byte-identical object from a deliberate z/w swap.
**Detector:** none verified.

### 5 — Wrong `.data` constant (54)
**Tell:** an `lfs`/`lis`+`lfs` loading a named or `lbl_*` static where our source has a bare literal, **or** where the two differ in the 2nd–5th significant digit. The plausible-looking wrong number is the dangerous case: `MicXbox::Poll` had 3 of 4 resample constants wrong in the *5th* digit. Gross errors (20×, 8×, sign flips) also occur — `sBeamBrighten` 2.0 vs 0.1, `sFogScale` 1.0 vs 0.125.
**Detector:** `scripts/analysis/mutable_float_audit.py`. **Read its limits below before trusting a count.**

### 6 — Wrong callee / vtable slot / missing dispatch (97) ← best covered
**Tell:** a relocation-**name** mismatch invisible under the default `normalized` ruler. Per CLAUDE.md a wrong callee can cost **exactly zero** normalized points. A function reading 100.0% with 0 mismatch rows under `normalized` is **not** evidence of a correct callee.
**Detectors:** `reloc_name_gate.py` (+ `--selftest`), `callee_emitted_anywhere.py` (7 real defects in 58 rows, 0 false positives), `pattern_census.py --ruler name_check`, `vtable_dispatch_scan.py`, `run_symbol_sweep(kind="vtable_slots")`, `unnamed_callee_scan.py`, `callee_reloc_scan.py`.
**Use `diff_mode="name_check"`, never `normalized`.**

### 7 — Save/Load field-order desync (14)
**Tell:** a rev-gated `BEGIN_LOAD`/`BEGIN_SAVES` block whose field order doesn't match the target's byte layout. Both sides do "the same shape" of reads; the **wire bytes land on the wrong member**. `StreamRenderer::Save` wrote `mPlayer1/2/3DepthColor` twice and lost players 4–6; `CharFeedback::Load` skipped a 4-byte int on the rev<6 path, desyncing everything after it.
**Detector:** none. **278 paired Save/Load classes, full gap.** ⚠ A naive offset-keyed detector rebuilds the documented `FxSendChorus::Load` false positive, because the `this` register varies (r31 in Flare/CharUpperTwist; r30 in Text, where r31 holds a member object). There is also **no live positive** to validate a selftest against — `CharUpperTwist`'s permutation is the shipped one.

### 8 — Lifetime / ownership / refcount (22)
**Tell:** a hand-inlined pool-alloc/placement-new replacing a straight `new`/`delete` (losing an EH cleanup region); an `ObjRef`/`ObjOwnerPtr` built against the wrong owner; `.erase(it); ++it;`; a `Deactivate()` standing in for `delete` so a container never drains.
**Detector:** `null_deref_sweep.py` covers the null-`this` sub-shape only. ⚠ Xbox maps guest page 0 readable/writable/zeroed, so a null-`this` small-offset store **silently no-ops on console and SIGSEGVs on Linux** — this class is strictly worse on native than on the target.

### 9 — Buffer overflow / OOB write (9)
**Tell:** a bound crossed by exactly one element on a *peeled* loop iteration, or an index computed from a transposed (row,col) pair on a non-square array. Commit bodies distinguish "a real OOB write" from the far more common benign OOB read into zeroed neighbours.
**Detector:** none.

### 10 — Uninitialized read / missing store (16)
**Tell:** a member the target's ctor zeroes/sets that ours does not; or a computed value assigned to a scratch local nothing reads back, leaving the real destination stale.
**Detector:** `bss_initializer_scan.py` — **examines 20% of its population.** See below.

### 13 — Wrong access specifier (added 2026-09-15)
**Tell:** MSVC encodes member access **in the mangled name** — the char after the final `@@`: `A`–`H` private, `I`–`P` protected, `Q`–`X` public. A member we declare `public` that the image declares `protected` is a *different symbol*, so objdiff never pairs it and it costs zero under every ruler.
**Detector:** `access_specifier_scan.py` (new). Found 2 previously unrecorded instances (`GroupSeqInst::Poll`, `StreamReceiver360::SendDoneImpl`). Full write-up: `docs/decomp/patterns/access-specifier-is-part-of-the-mangled-name.md`.

---

## Instrument defects measured 2026-09-16 — read before quoting any zero

**`bss_initializer_scan.py` examined 20% and printed a bare 0.** Its universe is 16,238 symbols we place in `.bss`; it examined **3,295 (20.29%)** and discarded 12,939 through an uncounted `continue`, under a summary line counting *object pairs* rather than symbols. The pattern doc's claim — *"Ten is the whole binary… if it ever returns more, someone added a declaration without its initializer"* — rested on that. Two real bugs walked through the other 80%, **both after the verdict**: `Game::Poll::sLastBeat` (`88c3d9c20c`, target side is an unnamed `lbl_82F1A524` so there is no name to join on) and `GainEffect::sGain` (`df13adcd1c`, our definition sat in the wrong TU, and the pairing is per-object). Now instrumented (`0ac5d5924`); the blind spots are **counted, not closed**. ⚠ Note `12cff454d` justifies closing this class with *"The scan now returns zero hits over 980 object pairs"* — that is this instrument at 20% coverage.

**`mutable_float_audit.py` assumes the target's addressing mode.** It walks REFLO relocations as if each were a load site. The target emits `lfs f0, lbl@l(r11)`; **our MSVC emits `lis`/`addi` + a displaced load**, so REFLO lands on an `addi` and the displacement field is 0 pre-link. Measured on `IsValidSwipePosition`: three REFLO relocs, only **one** on an actual load; the other three constants are reached by `lfs r0,12(r11)` / `lfs r13,8(r11)` / `lfs r0,4(r10)` carrying **no relocation at all** — structurally invisible. Across all four DISAGREE rows, REFLO lands on `addi/addis` as often as on a load (8/6, 2/4, 7/2, 10/4), with 25 / 13 / 3 non-relocated `lfs` sites unseen. **Its DISAGREE and MISSING counts are not trustworthy as bug counts.** Its *named-symbol* join (33 both sides, 0 disagreements) needs no pairing inference and is sound. Denominators repaired in `6c6a2ad45`; the addressing-mode gap is **open**.

**Two candidates from it, both refuted on inspection** — recorded so they are not re-raised:
- `IsValidSwipePosition` — **correct**. All four constants present with matching values (0.9/1.3/0.8/1.1), right branch assignment, ellipse test matching the target's `fdivs`/`fadds`/`fcmpu`.
- `GetMoveRating@BustAMovePanel` — **not a bug**. Our `0.85f/0.70f/0.4f` literals match the target's values; only runtime tunability differs.

---

## Corrections to this document's own earlier verdicts

⚠ **Two of the three claims this doc previously recorded as "did not survive verification" were themselves wrong.** That is the worse direction of error: a false correction reads as *verified* and tells future lanes to stop looking at a real bug.

1. **`Hmx::operator*(Matrix4, Matrix4)` computed `b*a` — REAL. My "not supported" verdict was false.** Commit `571722546` (2026-08-23). The removed code dotted `b`'s rows against `a`'s columns; the replacement is `Dot4(b.Col4(j), a.row_i)`. 18.3% → 95.9% canonical. The commit states it "was certified COMPLETE by the objdiff `base_size=0` defect and never measured." Live callers named: RndCam view-projection, `CheckShadow`'s shadow-texture matrix, `RndShaderVelocityCamera::Select`. **How I got it wrong:** I reached for `7694afdfd`, which is `Hmx::operator*(**Transform**, Matrix4)` — a different overload — and concluded the claim was a garbled merge. It was one overload away.
2. **`Locale::mInitialized` — REAL. My "not supported" verdict was false.** Commit `12cff454d`. The image holds `0x01` at `TheLocale+0x1c`; ours read `.bss` zero, so `if (mInitialized)` skipped the **entire** locale load — every language file, `mSymTable`/`mStrTable` — leaving `mSize` 0 and every `Localize()` missing. Current source: `Locale() : mInitialized(true) {}`. Independently corroborated by the census table in `docs/decomp/patterns/dropped-static-initializer.md`. It is **not** superseded by `GetSystemLanguage`: there are **three distinct real locale bugs** — `12cff454d` (gating), `a3aed0f17` (Nordic/Spanish), `317bad0b4` (comparator is `FastSort<3>`, verified: the image's `qsort` names it outright and `LocaleChunkSortFunc` is in no map).
3. **The 627 total** — superseded by 524 with stated dedup, not refuted.

The lesson generalizes: **a refutation needs the same evidence standard as a claim.** Both bad verdicts came from grepping for a nearby symbol, finding a plausible neighbour, and concluding the original was confused — without running `git show` on the hash. `feedback_history_queries_fail_open.md` already records that history queries fail *open*.

---

## Worked examples (unchanged, verified)

### Gameplay-breaking
| Bug | Commit | What it was |
|---|---|---|
| `MoveFrame::Load` never wrote `mNodesInverseScale` | `d57bc744d` | Zeroed every Ham2 scoring weight — **any dance move scored perfectly**. 76.8% → 100%. |
| `ObjectDir::Iterate` gated on the wrong symbol | `4e4cf8513`, `2a1b14b6f` | Every `{$dir iterate ...}` in the game's DTA was a silent no-op. 54 call sites went live at once. |
| `Rand::Seed` signed shift (`srawi`) | `5a0c71f52`, test `752712aef` | Sign extension corrupted ~half the global RNG table behind every `RandomInt`/`RandomFloat`. |
| `GetSystemLanguage` wrong language | `a3aed0f17` | Nordic and Spanish consoles selected the wrong language. 98.48 → 100.0. |
| Kinect gesture filters miscalibrated | `1aa8cd9b5` | `HighFiveGestureFilter` all three thresholds wrong, one with the wrong **sign**. |
| Pose scoring degenerate | `a7be9ff49`, `e94ec17f9` | Bone lengths never populated; hip-root never set (`IsValid()` permanently false); X-axis mirrored; gesture arrays bound per *hardware slot* not per *player*. |
| `HamDirector::PollEnabled` accidental override | `389f21dea` | Same signature as a virtual it did not mean to override. |

### Crashes, hangs, memory corruption
| Bug | Commit | What it was |
|---|---|---|
| Null-`this` deref class, 9 sites | `ebe9d3a99`, `4e829662f`, `9b294fc25`, `607239f44` | Silent no-op on Xbox (page 0 mapped), SIGSEGV on Linux. |
| `~Object` skipped `ReplaceRefs` in a cascade | `9d3fc20f8` (red test `bceef97ed`) | Every object destroyed as cascade collateral left a live dangling reference. |
| `TaskMgr::Poll` freed an already-freed Task | `184b5f06d`, `615572c35` | Intermittent SIGSEGV, reproduced 1-in-8 runs. |
| Ogg decode thread, six live bugs | `ec12f4215` | Includes the store `~VorbisReader()` spin-waits on — without it the destructor never returns. |
| Bloom allocator wrote past its object | `b93f589b8` | Ran off the end of `sBloom` **and** asked for 16× the pixels. |
| `TrigTableInit` OOB write | `25955293b` | Wrote `gBigSinTable[513]` of a 512-float table; read the delta slot as the sine slot. ⚠ Earlier drafts cited `90084bd8a`/`fcb2aebe7`; both wrong. |
| DTA worker stack ported verbatim | `f0304916c` | Xbox's literal 64 KB; clang inlines `MakeString`, making frames ~17.6× larger. |
| `DataArray::SortNodes` PPC32 stride | `8c73183dd` | Hardcoded 8-byte qsort element; `DataNode` is 16 bytes on LP64. |
| 26 undefined symbols hidden by the linker | `eb62d26a1` | `--unresolved-symbols=ignore-all` masked crash-on-first-call landmines. |

### Rendering, audio, input
Unchanged from the prior revision: `~RndTex` cache invalidation (`5cae2299e`), `Movie::IsOpen` const/vtable (`8ae44c2a3`), DXT5 alpha swizzle (`803fd4836`), 105 missing DATA definitions of which 71 were inside an `#ifdef HX_NATIVE` (`537a5e52e`), `DspAllocate` mangling (`ac5bf312c`), `TransformNormal` dropped transpose (`12f02213b`). Kinect/webcam remains the largest single cluster: `return 0` colour stub (`23ffaf52d`), LP64 texel-pointer truncation (`616c49625`, `4b45c8ed2`), 16:9 forced through a 4:3 box (`7a1974ead`), 0.83 m depth bias (`05ca6d094`).

**Two live in the native port right now:** `FillCompressedVertex` (`c08641a00`) packs colour as ABGR instead of ARGB (red/blue swapped) via an ODR-violating dual definition, and truncates UVs to `unsigned short` instead of half-float encoding.

---

## Progress over the window

| Metric | Start | End |
|---|---|---|
| Matched functions | 30,677 | 31,310 |
| Authorable matched (canonical) | 96.80% | 97.05% |
| XEX headline | 48.747% | 48.856% |
| Authorable complete units | 623/967 | 629/967 |

Heavily back-loaded: 8 commits in W29 against 1,274 in W38. Sept 13–14 alone account for ~360 of the bug count — deliberate hunt days, not a representative rate.

---

## Standing caveats on the instruments

- **The 2026-08-19 bulk AT_LIMIT certificates are not evidence.** Every lane that re-read them found real bugs — `RhythmBattle::OnBeat` 38.7% → 99.45% on 9 bugs; `BustAMovePanel::OnBeat` "unfixable" → 100% on 3. One UI slice was 6-of-7 wrong, another 8-of-8.
- **A displayed 100.0% hid real bugs.** The canonical ruler forgives register permutation, and until the 2026-08-20 fork fix forgave wrong callees entirely. Apply "a divergence on a 100% function is an artifact" **only to a zero-mismatch instruction count, never to a displayed number**.
- **Do not quote ctest as "N/N passed"** — it counts skips as passes. Quote executed/passed/failed/skipped with a date.
- **`MILO_ASSERT` is non-fatal on native**, so a clean exit code is not evidence of correctness.
- **Static review is insufficient for native behavioural fixes** (`feedback_runtime_verify_native_fixes.md`).
- **Every "class exhausted" verdict is a claim about an instrument.** Two were disproved today. Before believing a zero, read the tool's coverage block; if it has none, that is the finding.

## Sources

Commit history on `main` 2026-07-15 → 2026-09-15 (~1,800 commits scanned across 11 survey reports, deduped as described above); `docs/sessions/2026-09-13-band-lane-wave.md`, `2026-09-14-wave6-instrument-defects.md`, `2026-09-14-wave7-fable-retries-and-bug-tells.md`, `2026-09-15-wave8-zero-percent-climb.md`; `docs/decomp/patterns/{rounded-100-hides-real-bugs,relocation-names-are-unmetered,dropped-static-initializer,access-specifier-is-part-of-the-mangled-name}.md`; `docs/native/SCORING_ENV_VARS.md`, `MISSING_DEFINITIONS.md`, `HACK_AUDIT.md`; `../milo-native-engine` history over the same window.
