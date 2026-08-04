# Lever 5 — process, signature, and census (dc3-decomp, 2026-08-04)

Follow-up to `b79fecaf` (`LabelShrinkWrapper::UpdateAndDrawWrapper`, **dc3-decomp**,
68.1% → 99.9%), written up as [Lever 5](../decomp/patterns/fixable-liveness.md#lever-5--name-the-temporaries-so-they-are-built-up-front-and-frame-packed).

Three questions: (1) could we have *copied* that fix from a sibling repo? (2) what
exactly is the diagnostic signature, mechanically? (3) which other functions have it?

> **Every number below names its repo.** dc3-decomp, og-dc3-decomp, rb3 and rb3-xenon
> share the Milo engine and have *identical mangled symbol names*; an unattributed
> figure gets refuted against the wrong binary. All dc3-decomp percentages are direct
> `mcp__orchestrator__run_objdiff` readings taken in the worktree
> `/home/free/code/milohax/dc3-lever5-census` (branch `lever5-census`, off `main`
> `86357b58`) on 2026-08-04. **Not** from `report.json`, **not** from `decomp.db`, and
> **not** from a commit subject line.

---

## Part 1 — "Do we just copy over the code?"

### The short answer: no. There was nothing to copy, and all three siblings would have made it worse.

The function exists in four trees. Here is what each actually contains.

| Repo | Body | Measured | Note |
|---|---|---|---|
| **dc3-decomp** | borders + `mBounds*`, four named `Vector3` corners, `SetLocalPos(const Vector3&)` | **99.9%** (`run_objdiff`, 141 instrs) | derived in-tree, 68.1 → 90.6 (`3bb49c53`) → 99.9 (`50d75d6d`) |
| **og-dc3-decomp** | **stub** — `MILO_ASSERT` + `SetWorldXfm`, no corner logic at all | **31.75%** (its own `build/373307D9/report.json`, mtime 2026-05-31) | 564 bytes, same as dc3-decomp's |
| **rb3** (Wii, MetroWerks, `SZBE69_B8`) | `InqMinMaxFromWidthAndHeight` + four `SetLocalPos(x,y,z)` | **100.0%** (its own `report.json`) | **452 bytes — a different function** |
| **rb3-xenon** (Xbox 360, `45410914`) | a port of the rb3 Wii body | **no entry for this symbol in its `report.json`** | missing, not 100%, not any number |

What each copy would have bought you:

- **og-dc3-decomp** → a two-line stub. It compiles (its header has the border members),
  and it is a *regression* to roughly the pre-work state. og-dc3 is a first-class port
  source, but "first-class" is not "correct" — it has shipped real bugs before, e.g. the
  `ObjectDir::Iterate` class/type bug.
- **rb3 (Wii)** → **does not compile in dc3-decomp.** `UILabel::InqMinMaxFromWidthAndHeight`,
  `GetDrawWidth`, `GetDrawHeight` and the `SetLocalPos(float,float,float)` overload all
  exist in rb3 and none of them exist in dc3-decomp (`Trans.h:99` has only the
  `const Vector3&` overload; `Text.h:337` has `GetAlignment()`, not `Alignment()`). Its
  100.0% is against a different binary, a different compiler, and a 452-byte function
  where DC3's is 564. It is the **wrong shape**, not a lower-percentage version of the
  right one.
- **rb3-xenon** → same wrong shape (it *is* the rb3 Wii port), same compile failures,
  and its header has had the border members deliberately deleted.

The traffic actually ran the other way. rb3-xenon's own source comment records that it
*used to* carry the DC3 border-based shape and that it was **removed** as wrong-for-retail:

```
// NOTE(laneBS1): ported from the rb3-Wii RB3 oracle
// (../rb3/src/system/ui/LabelShrinkWrapper.cpp:49). The previous body derived the
// corners from RndText bounds plus the four mLeft/Right/Top/BottomBorder floats;
// retail RB3 has no such members (see the header note), so it cannot be that shape.
```

And the identifiers the 99.9% body is written against (`mBoundsLeft` … `mBoundsBottom`)
came into existence in dc3-decomp on 2026-02-21 in `4a834046`, *after* the
og-dc3-decomp fork. They exist in exactly one of the four repos.

> **Same name, different binary.** rb3-xenon has a `LabelShrinkWrapper::UpdateAndDrawWrapper`
> too. Do not read a percentage off it and attribute it to dc3-decomp — the two have
> different headers, different class layouts (rb3-xenon's is 32 bytes smaller: no
> `mResourceDir`, no borders) and a different function body.

### So what *is* the process?

It is derivation from the target's stack frame, not transcription. Here it is as a
procedure. Steps 1-4 are mechanical and scriptable. Step 5 is the judgement call.

#### Step 1 — Measure, and distrust every number you did not just produce

```
mcp__orchestrator__run_objdiff(symbol, project_dir=<your worktree>)
```

`project_dir` is not optional — without it you measure the main repo, not your edits.
`decomp.db`'s `current_percent` is stale in both directions (worst observed 65pp,
because the `SYNC DB` step bumps `updated_at` without writing the percentage).
`report.json` is a build artifact of whatever was last compiled, and in this census it
ran **0.3 – 1.6 pp optimistic** on every one of the nine functions re-measured.

The one-line `**Stack:**` summary at the bottom of `run_objdiff`'s output is the
cheapest possible triage and is the thing to read first:

```
**Stack:** frame Δ -0x10 (structural) | 7 DIFFER, 1/0 TGT/BASE-only.
```

#### Step 2 — Read the frame delta, and split it from the callee-save delta

```
mcp__orchestrator__run_diff_inspect(symbol, mode="stack-layout", project_dir=...)
```

The header is the whole decision:

```
  Frame size:          TGT 0xc0      BASE 0xb0      Δ -0x10
  Callee-saved GPRs:   TGT 1       BASE 1       Δ +0
  Callee-saved FPRs:   TGT 4       BASE 5       Δ +1
  → Callee-saved Δ = +0x8; structural Δ remaining = -0x18.
```

**Structural** is the number that matters. If the whole frame delta is explained by
callee-save counts, this is a register-pressure problem and you are on Lever 1/2, not
Lever 5. `stack_layout.py` flags that case as AT_LIMIT on its own.

#### Step 3 — Read the slot table's `TGT_ONLY` / `BASE_ONLY` counts

That is the discriminator (Part 2 below). The ground-truth Lever-5 case reads:

```
    0x70       —  TGT_ONLY   float  sz=4  L=0  S=1  A=2   [62..92]
    0x74       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [70..70]
    0x78       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [68..68]
    0x80       —  TGT_ONLY   float  sz=4  L=0  S=1  A=2   [77..137]
    0x84       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [79..79]
    0x88       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [81..81]

  Summary (user slots):  DIFFER 3 | TGT_ONLY 6 | MATCH 1
```

Six target-only 4-byte slots, **zero** base-only, grouping into two 16-byte homes.
Our slots are a *strict subset* of the target's.

#### Step 4 — Count the target's slots by hand and group them into 16-byte homes

```
mcp__orchestrator__run_objdiff(symbol, full_listing=true, project_dir=...)
```

Read every `stfs`/`stw`/`stfd` with an `r1` base out of the **target** column and bucket
by 16. Here: `0x60,0x64,0x68` · `0x70,0x74,0x78` · `0x60,0x64,0x68` again · `0x80,0x84,0x88`
→ **four values, three homes, all written before the first call.** That count is the
specification you are trying to reproduce.

#### Step 5 — The inference (this part is not mechanical)

Everything above tells you *how many* simultaneously-live aggregates the target had, and
*that they were all materialized in the first basic block*. It does not tell you what
they were called or what expression built them. Going from

> "the target holds four aggregates live at once, in three homes, all built up front"

to

> "declare `topLeft`/`topRight`/`bottomLeft`/`bottomRight` as named locals immediately
> after `SetWorldXfm` and pass them by name"

is a judgement call. Specifically:

- **Mechanical:** the frame delta, the deficit's divisibility by the aggregate size,
  the slot count, the block in which each home is first written, and the fact that
  unnamed const-ref temps collapse to one home. All of that is derivable, and the two
  scripts in this session derive it.
- **Inference:** *that naming is the right lever.* Widening a live range by naming is
  one of several source edits that would produce N live homes — you could also hoist the
  construction, introduce an array, or change the callee's signature. Naming is the
  one that is both behaviour-preserving and idiomatic for the original code.
- **Inference:** *how many* names. The correct answer is "one per value the target holds
  live", **not** "one per home". Four names produced three homes because the packer
  coalesced two. Trying to hand-produce three homes with three names plus a mid-function
  `left.Set(...)` gives 86.2% — right home count, wrong block. See "Do Not Half-Fix It"
  in the pattern doc; both intermediate shapes are traps that buy real points and stall.
- **Inference:** *the order.* Natural TL/TR/BL/BR fell out correct here, but nothing in
  the binary forced that choice in advance.

A useful smell that is halfway mechanical: **a lone named temp whose only purpose is to
reorder calls.** The 80.4% state this replaced had an `auto _tmp0 = Vector3(...)` hoisted
above the block to force the TopLeft call to run last. That is the shape of a hack that
found half of this lever by accident — naming one temp bought one extra home. Grep for
`auto _tmp` / `_tmp0` and treat every hit as a Lever-5 lead.

#### Step 6 — Verify and do not stop early

Re-measure after every edit. Confirm the frame size now matches exactly
(`stwu r1, -0xc0`) and that `TGT_ONLY` has gone to zero, before you spend any time on
the residual register swaps — they are downstream and usually evaporate on their own.
Here they did: the entire FPR assignment (`f31=minX f30=minZ f29=maxX f28=maxZ`, `f0`
for the zero constant) and the whole instruction schedule fell out with no further edit.

---

## Part 2 — The signature, mechanically

Four candidate signals were proposed. Measured against the real data, they are not equal.

| Candidate signal | Verdict | Evidence |
|---|---|---|
| **Frame-size deficit, base < target, deficit ≡ 0 mod 16** | **necessary, far from sufficient** | fires on **183** functions in dc3-decomp, 182 of them below 100%. Only 15 have any unnamed aggregate temp at all. |
| **`TGT_ONLY > 0` and `BASE_ONLY == 0`** (our slots a strict subset) | **the strongest single signal** | ground truth reads 6/0. The two functions that also pass the source test read 4/0 and 6/2 — and the 4/0 one is the better candidate. |
| **Unnamed ctor temps passed by const-ref, ≥2 in consecutive statements** | **necessary, and cheap** | fires on 14 sub-100% functions across the whole tree. |
| **Register permutation global rather than block-local** | **not usable as a discriminator** | register swaps were symptoms in 100% of the 31 functions triaged in the [regswap AT_LIMIT sweep](2026-08-04-regswap-atlimit-sweep.md); they fire on essentially every AT_LIMIT function regardless of cause. |

**The conjunction is what is specific.** Deficit ∧ source-temps takes 182 candidates
down to 2. Neither half works alone:

- The deficit alone is not it. `-16` is also what a single missing scalar local looks
  like after 16-byte stack alignment; it fires on 128 functions.
- The source tell alone is not it. A temp whose callee takes it **by value** costs no
  stack. `Symbol` is the trap here — `class Symbol { const char *mStr; }` is four bytes
  and travels in a GPR, so `Symbol(x)` in an argument list never occupies a home. It was
  in the first version of the type table and produced one clean false positive
  (`FlowNode::DuplicateChild`, dc3-decomp). Same reasoning excludes every handle and
  pointer-wrapper type.

### Tools

Two scripts, both committed on this branch, both validated against the ground-truth case:

**`scripts/analysis/frame_deficit_census.py`** — the binary half. `stwu r1, -N(r1)`
encodes to `0x9421____` with N in the signed 16-bit displacement, so the frame size can
be read straight out of both COFF prologues. **No objdiff run needed**: 979 unit pairs,
~1.9k functions, ~40 seconds. Because it reads the objects rather than `report.json`, it
is immune to the staleness that makes `report.json` and `decomp.db` untrustworthy.

```
python3 scripts/analysis/frame_deficit_census.py                    # every delta
python3 scripts/analysis/frame_deficit_census.py --max-percent 99.999   # unmatched only
```

Baseline reading on dc3-decomp `main` `86357b58`: **1588 exact, 183 deficits, 119
surpluses**, deficit histogram `-16: 128, -32: 22, -48: 5, -64: 2, …`.

**`scripts/analysis/lever5_score.py`** — the source half. Resolves each sub-100%
function's body from `report.json`'s `demangled_name`, strips comments and literals, and
counts aggregate constructor calls in **argument position** (preceded by `(` or `,`,
which excludes declarations), plus the longest run of consecutive statements containing
one. Cross-references the deficit and reports whether the source-side temp budget is
large enough to explain it.

### Validation against ground truth

`LabelShrinkWrapper::UpdateAndDrawWrapper` was round-tripped back to its pre-fix
four-unnamed-temps form, rebuilt, measured, and restored. Both detectors fire:

```
run_objdiff     68.1% normalized (67.9% raw), 162 instrs, 21 insert / 21 delete
stack-layout    TGT 0xc0 / BASE 0xb0, structural -0x10; 3 DIFFER, 6 TGT_ONLY, 0 BASE_ONLY
frame census    0x00c0 0x00b0  -16
source scorer   4 temps, longest_run=4, budget 64B, 0 named aggregates
                (post-fix: 0 temps, 4 named aggregates)
```

Two defects the validation shook out, both now fixed:

1. `--max-percent` defaulted to `100.0` against a **half-open** interval, so a function
   sitting at a stale `100.0` in `report.json` was silently dropped — which is exactly
   the function you are testing right after you edit it. Default is now `101.0`.
2. `Symbol` in the aggregate table (see above).

### Three buckets, not one

The census makes it clear that "frame differs + registers permuted" is at least three
distinct problems, and `TGT_ONLY` vs `BASE_ONLY` separates them:

| Bucket | Reading | Meaning | Lever |
|---|---|---|---|
| **Deficit** | structural Δ < 0, `TGT_ONLY > 0`, `BASE_ONLY == 0` | the target holds live values we collapse | **Lever 5** — name the temps |
| **Surplus** | structural Δ > 0, `BASE_ONLY` dominant | we hold live values the target collapses | **Lever 4** / inverse — narrow the scope, or *un*-name |
| **Permuted** | Δ ≈ 0, `PERMUTED` rows dominate, `TGT_ONLY == BASE_ONLY` | same set of homes, different assignment | neither — see below |

That third bucket is the one behind "the packing is lifetime-based and renaming locals
apart changed nothing". It is **not** a puzzle: `PERMUTED` means both sides use the same
homes at different program points, i.e. the same *count* with a different assignment.
Renaming cannot change a count, so renaming is *expected* to be inert there. The
`stack-layout` tool says so in as many words:

> 47 user slot(s) PERMUTED — both sides use the same slot at different program points,
> i.e. the SAME SET of slots with variables assigned differently. This is MSVC
> temporary/slot allocation shaping, not a declaration count difference.

If you are in the permuted bucket, stop reordering declarations. The lever, if there is
one, is scheduling — *where* a value is materialized relative to its consumer — not
naming.

---

## Part 3 — Ranked candidate list

All dc3-decomp, all re-measured with `run_objdiff` in the census worktree on 2026-08-04.
`report.json` column shown only to document how far off it is.

| # | Function | report.json | **measured** | structural Δ | TGT/BASE-only | temp run | Bucket | Confidence |
|---|---|---|---|---|---|---|---|---|
| — | `LabelShrinkWrapper::UpdateAndDrawWrapper` *(reverted for validation)* | — | **68.1%** | −0x10 | **6 / 0** | 4 | **Lever 5** | ground truth |
| **1** | `LoopVizCallback::UpdateOverlay` `src/lazer/game/GamePanel.cpp:120` | 93.5 | **92.9%** | −0x10 | **4 / 0** | 6 (`Hmx::Color`, `Vector2`) | **Lever 5** | **high** |
| **2** | `CharEyes::LidTrackAndClampingUpdate` `src/system/char/CharEyes.cpp:967` | 96.1 | **95.6%** | −0x10 | 6 / 2 | 5 (`Hmx::Color`) | Lever 5 (impure) | medium |
| 3 | `ArcDetector::UpdateOverlay` `src/system/gesture/ArcDetector.cpp:384` | 73.6 | **72.0%** | **+0x20** | 2 / 6 | 5 | **surplus / inverse** | medium — biggest gap on the list |
| 4 | `RndTexRenderer::DrawToTexture` `src/system/rndobj/TexRenderer.cpp:240` | 92.3 | **91.6%** | 0 | **3 / 19** | 3 | **surplus / inverse** | medium |
| 5 | `Spotlight::UpdateTransforms` `src/system/world/Spotlight.cpp:887` | 94.3 | **93.0%** | **+0x30** | 0 / 1 | 2 | surplus | low-medium |
| 6 | `CharLookAt::Highlight` `src/system/char/CharLookAt.cpp:344` | 92.1 | **91.3%** | +0x10 | 1 / 0 | 3 (`Vector3`) | mixed | low |
| 7 | `ThreeDSound::Highlight` `src/system/synth/ThreeDSound.cpp:137` | 94.2 | **93.3%** | 0 | 3 / 3 | 6 (`Hmx::Color`) | **permuted** | low |
| 8 | `CharIKHand::Highlight` `src/system/char/CharIKHand.cpp:583` | 97.7 | **97.4%** | +0x10 | 0 / 0 | 6 | permuted | low |
| 9 | `DisplayEvents` `src/system/midi/DisplayEvents.cpp:6` | 95.2 | not re-measured | +0x10 | — | 2 | surplus | low |
| 10 | `RndSpline::PrepareShader` `src/system/rndobj/Spline.cpp:341` | 98.7 | not re-measured | 0 | — | 2 (`Vector4`) | permuted | low |
| 11 | `SongSequence::OnSongLoaded` `src/lazer/game/SongSequence.cpp:283` | 98.7 | not re-measured | 0 | — | 2 (`String`) | permuted | low |
| 12 | `Debug::DoCrucible` `src/system/os/Debug.cpp:448` | 99.5 | not re-measured | 0 | — | 4 (`DataNode`) | permuted | very low |
| 13 | `ParseNode` `src/system/obj/DataFile.cpp:70` | 99.5 | not re-measured | 0 | — | 2 (`DataNode`) | permuted | very low |

**Candidate 1 is the one to work.** `LoopVizCallback::UpdateOverlay` is the only function
in the tree besides the ground truth that carries the full signature — negative
structural delta, target-only homes with **zero** base-only homes, and a six-statement
run of `Hmx::Color(...)` temporaries in argument position. Its stack table shows the
target's homes shifted uniformly by +0x10 relative to ours with one extra 16-byte home
at `0xd0`-`0xdc`:

```
    0xd0       —  TGT_ONLY   float  sz=4  L=0  S=1  A=2   [555..571]
    0xd4       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [556..556]
    0xd8       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [558..558]
    0xdc       —  TGT_ONLY   float  sz=4  L=0  S=1  A=1   [560..560]
```

— exactly one `Hmx::Color` (4 floats). The target round-robins its Color temporaries over
**two** homes (`0x80` across instrs [171..420], `0x90` across [265..504]); we collapse
both into one (`0x80`, [171..503]). It is not a five-minute fix — 582 instructions and
~18 Color temporaries — but the diagnosis is unambiguous.

**Not attempted, and why.** No fix was landed on candidate 1 in this session: the
mechanism is "which two of eighteen temporaries are simultaneously live", which the
stack table constrains but does not determine, and `mode="attributed"` — the tool that
would map instruction index back to source line — **crashes** on this function
(`/FAs` compile exits −11, "failed to generate assembly listing"). That is worth fixing
before anyone works this candidate; without it, step 5 has to be done by hand.

### Known re-entry points from the prior sweep

`HamNavList::Poll`, `HamDirector::OnPopulateMoves` and `RndText::Load` were flagged
separately. Frame readings from the bulk census (dc3-decomp, `86357b58`):

| Function | target frame | base frame | Δ |
|---|---|---|---|
| `HamNavList::Poll` | 0x120 | 0x120 | **0** |
| `HamDirector::OnPopulateMoves` | 0x1350 | 0x1260 | **−0xF0** |
| `RndText::Load` | 0x1c0 | 0x1b0 | −0x10 |

`HamNavList::Poll` has no frame delta at all, so whatever its four "target-only home-slot
writes" are, they are not a missing local — home slots are the caller's parameter save
area and live *outside* the `stwu` allocation. That is a spilled-incoming-parameter
difference, a fourth thing again, and not Lever 5.

---

## Reusable takeaways

1. **The frame size is readable without objdiff.** `stwu r1, -N(r1)` is one fixed-form
   instruction. Any question of the form "is our frame the same size as the target's"
   is a 40-second whole-tree scan, and it is immune to `report.json` staleness.
2. **`TGT_ONLY` vs `BASE_ONLY` is the routing decision**, and it is a better one than
   the frame delta's sign alone (candidate 4 has zero frame delta and 3/19).
3. **Re-measure before citing.** Nine functions re-measured here, nine `report.json`
   figures optimistic by 0.3-1.6 pp. That is small, but it is the same failure mode that
   put a fabricated 99.1% for `RndText::SizeCheck` into six documents across three repos.
4. **A sibling repo at 100% is not a source unless it is the same function.** rb3's
   `UpdateAndDrawWrapper` is 100% and 452 bytes; dc3-decomp's is 564. Check the size and
   the header before you check the percentage.
