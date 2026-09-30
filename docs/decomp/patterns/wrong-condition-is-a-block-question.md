# A wrong condition is a question about BLOCKS, not mnemonics

**Taxonomy classes 2 (inverted / missing / extra condition, 128 historical bugs)
and 3 (loop bound / off-by-one, 33).** Instrument:
`scripts/analysis/cond_semantics_scan.py` (`--selftest`, `--explain <mangled>`,
`--show-dropped <reason>`). Written 2026-09-30.

## The mechanism

A wrong condition always costs match points — the branch mnemonic, the compare
opcode or the compare immediate changes — so unlike a wrong field it can never
hide at an exact `match_percent_normalized` of 100.0. Every instance is already
an objdiff row. The problem is that the rows are drowned, and **a flipped
mnemonic is not evidence of a flipped test**:

| What objdiff shows | What it usually is | Why it is not a bug |
|---|---|---|
| `beq` vs `bne`, same compare | **block placement** | MSVC put the other arm inline: the test AND the successors both swapped (`CacheResource` row 28: the image places the `MovieExtension` arm out of line at `0x289c`) |
| `cmpw a,b; blt` vs `cmpw b,a; bgt` | operand order | same predicate |
| `cmpwi x,3; ble` vs `cmpwi x,4; blt` | immediate re-spelling | `x<=3` ≡ `x<4` (`Object::OnAddSink`, objdiff's one `COMPARISON_STYLE` row) |
| `cmplwi x,0; bgt` vs `bne` | unsigned zero test | `x>0` ≡ `x!=0` unsigned |
| `cmpwi` vs `cmplwi` before `beq` | declared type | signedness is irrelevant to equality |
| `cmpwi x,7; blt` vs `ble`, fall arm `li x,7` | a clamp | `x<7?x:7` ≡ `x<=7?x:7` (libvorbis `seed_curve`) |
| `subic. r,r,1; bne` vs `cmpw i,n; blt` | loop lowering | down-counter vs bound compare |

A real inversion flips the **test** and leaves the **arms** where they were; block
reordering flips both. So the decision has to be made on *aligned successors*, and
successors are blocks — a row alignment cannot do it, because LCS pairs `li r3,1`
against `li r3,0` as a `diff_arg` and happily lines up the two wrong arms.

## What the scanner does

1. **Predicate, not mnemonic.** Each side's CR producer (`cmp*`, `fcmpu`, record
   form `x.`) is found on that side's own fall-through chain; producer + branch
   become the set of outcomes (LT/GT/EQ, plus UN for floats) on which the branch
   is taken. A register-vs-immediate compare is widened into the exact set of
   32-bit **values** — which is what makes `x<=3`/`x<4`, unsigned `x>0`/`x!=0`, and
   signedness-on-equality all come out equal without special cases.
2. **Successors on blocks.** Both bodies are cut into basic blocks and paired by a
   register-blind, **stack-slot-blind** signature (unique exact, then shared
   identical rows, then mutual-best fuzzy). Stack-blind matters: `RhythmBattle::
   OnBeat`'s frame is shifted +4 against the image, and with raw slot offsets the
   pairing matched the image's `0xb0` arm against *our* `0xb0` in the *other* arm.
3. **A second opinion from what the arms DO.** The tokens that distinguish one arm
   from the other must follow the orientation. `CacheXbox::ThreadGetDir`'s two arms
   are both "destroy the Strings" and differ only in `li r3,-1` vs `li r3,8`; bulk
   pairing matched our `return 8` arm to the image's `return -1` arm and read an
   INVERTED test. The arm effects disagree, so the row becomes
   `ORIENTATION-CONFLICT` (a lead), never a finding. When blocks do not pair at all,
   a *decisive* arm-effects reading (≥2 tokens one way, 0 the other) orients the
   row by itself; a non-agree verdict reached that way is `UNPAIRED-DIFFERS`, a lead.
4. **Operand order by value provenance**, never by register name or by row
   alignment: two loads scheduled in opposite order are paired crosswise by objdiff,
   and the first cut of this tool "proved" an operand swap on STLport
   `_Rb_tree::insert_unique` whose two compares are byte-identical text.

## What it found (2026-09-30, worktree `det-cond`)

Universe **48,365** report.json functions: 31,352 dropped as exact-100 (a proof of
absence for these shapes — see the control below), 16,173 not built, **840
examined**. Branch rows: **11,125**, **10,995 classified (98.83%)**, 130 dropped with
reasons (64 successor blocks unpaired and arm effects not decisive, 23 successors on
a third block, 21 producer not on the fall-through chain, 19 CTR-conditional, 3
producer not found).

| Function | Bucket | Verdict |
|---|---|---|
| `XboxContentMgr::PollRefresh` | INVERTED | **real** — `if (allDone)` where the image skips on allDone (`8f88a8719`) |
| `AllocAlign` (Memory_Xbox) | OTHER-PREDICATE (switch bound 0xf vs 0xb) | **real** — and the bound was only the tip: the byte jump table mapped every `XALLOC_PHYSICAL_ALIGNMENT_*` code to the wrong size (`65a5f01d2`) |
| `CacheXbox::ThreadGetDir` row 143 | ORIENTATION-CONFLICT | refuted: both return 8 when disconnected, -1 when connected |
| `CacheResource` row 28 | agree | block placement (confirmed independently by the parent lane) |
| `seed_curve` | clamp → agree | `min(choice, 7)` either way |
| `JoypadPollCommon` row 165 | SELECT | `min` spelled `a<b?a:b` vs `b<a?b:a`; ties select equal values |
| `MoveDir::UpdateOverlay` 911/1049 | OTHER-PREDICATE | refuted: `it != end` merged into `it < end` (lower_bound never passes end) |
| `SkeletonFrame::Create`, `SkeletonViz::DrawJoints`, `TrigTableInit`, `fft_altivec` ×3 | SIGNEDNESS | refuted: loop counters from 0 and same-object pointer compares; the `TrigTableInit` floor is already documented in source |
| `DepthBuffer3D::AddAttachment` | PRODUCER-SHAPE | refuted: find-returning-NULL vs iterator-vs-end |
| `BSPFace::Update` row 124 | PRODUCER-SHAPE | refuted: a materialised `isZero` bool vs direct `fcmpu` chain |
| `PlayBack::Set`, `XMemDelete` | LOOP-LOWERING | refuted: guarded down-counter |
| `RndPropAnim::OnListFlowLabels` row 20 | (first cut: INVERTED) | refuted — an artifact of stack-slot-sensitive pairing, fixed in the tool |
| `PartyModeMgr::PickNextPlayer` row 93 | dropped (unpaired) | refuted: `idx = a>b ? 0 : 1` with the default and override values swapped (`li 1 … ble … li 0` vs `li 0 … bgt … li 1`) |
| `CharClipGroup::GetClip` row 15 | dropped (unpaired) | refuted: `mIndex = min(n-1, mIndex)` as an unconditional store of a select vs a conditional store of `n-1` |
| `ShouldWaitForRecovery`, `GestureMgr::PostUpdate`, `CalcShaderOpts`, `Skeleton::Displacements` | (objdiff `BOOLEAN_NEGATION`; not branches) | refuted: mask-idiom spellings; the image's extra `and` consumes a register provably holding 1 |

ONE-SIDED leads: 204 rows in 68 functions. Hand-checked 11 (`RhythmBattlePlayer::Poll`,
`ChoosePlayerSides`, `SyncProperty@HamCharacter`, `TypeProps::Save`,
`GetTweakedAutoexposure`, `Sound::SetSpeed`, the 13 `ObjPtrVec::operator=`, …): all
tail merges, loop rotations, a dead test on a value that cannot occur, or
materialised bools. None was a bug. **That is a sample, not the population.**

Cross-check against objdiff's own detectors (scan 20): all 5 `BOOLEAN_NEGATION`
functions are branch-free mask idioms (see below) and all 5 are already
adjudicated in source or here as equivalent; the one `COMPARISON_STYLE` row reads
`agree`; of the 103 `CONTROL_FLOW` functions' inverted-mnemonic rows, 42 were in this
scanner's drop buckets on its first revision — which is why the arm-effects
orientation exists.

## What it CANNOT see — and the manual recognizer for each

* **A condition that is not a branch.** MSVC if-converts into masks. The real bug
  `UIManager::IsGameScreenActive` (`4b1bffd5a`) had no branch at all. Read:
  `subfic t,x,0; subfe t,t,t` = `(x!=0) ? -1 : 0`; `subic t,x,1; subfe d,t,x` =
  `(x!=0) ? 1 : 0`; `cntlzw; extrwi 1,26` = `(x==0)`. A replace row between two of
  these is a POLARITY question — decide it with those three identities, and check
  whether an `and` the image has and we lack is folded away because the other
  operand is provably 1 (`ShouldWaitForRecovery`, `GestureMgr::PostUpdate`).
* **Jump-table contents.** A switch bound difference (`cmplwi idx,N; bgt default`)
  is only the visible tip. MSVC byte jump tables here are **unscaled offsets**
  from `func+K` (`lbzx r0; add r12,r12,r0; bctr`): decode each entry to its case
  label and compare the mapping, not the bound. That is how `AllocAlign` was found.
* **Restructured arms** — the 64 + 23 drop rows. `--show-dropped successor-blocks-unpaired`.
* **Tail merges and cross-jumping** land in ONE-SIDED: our `b` into a shared
  `beq` tail against the image's inline test. Follow the `b` before believing it.
* **Signedness that cannot matter** — loop counters starting at 0, compares of two
  pointers into one object. SIGNEDNESS rows need the value's range, which the
  scanner does not know.
* **The CTR trip count** (`mtctr` operand) and **a wrong value under the right
  test** (class 1 — `this_offset_scan.py`).

## Controls

* **Two-sided sabotage**, run on the final revision: `MILO_ASSERT(MainThread())`
  → `MILO_ASSERT(!MainThread())` in `RandomInt()` (a 100.0 function). Clean 8
  finding rows (11,016 B) → sabotaged 9, the new row `INVERTED` for
  `?RandomInt@@YAHXZ`, which left the exact-100 set at 99.4643 → reverted,
  output **byte-identical** (sha256 `cf2c6137…`).
* **Script mutation**, run from a freshly created empty directory: deleting the
  `swapped`-successor negation fails exactly the two selftest pins that exist to
  catch it; the unmutated copy passes.
* `determinism_check.py --only cond_semantics_scan`: SAME across PYTHONHASHSEED 1/7
  on 11,016 B.
