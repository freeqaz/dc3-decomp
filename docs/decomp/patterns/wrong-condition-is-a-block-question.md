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

ONE-SIDED leads: 204 rows in 68 functions (all since read -- see "The ONE-SIDED pile, read in full" below). Hand-checked 11 (`RhythmBattlePlayer::Poll`,
`ChoosePlayerSides`, `SyncProperty@HamCharacter`, `TypeProps::Save`,
`GetTweakedAutoexposure`, `Sound::SetSpeed`, the 13 `ObjPtrVec::operator=`, …): all
tail merges, loop rotations, a dead test on a value that cannot occur, or
materialised bools. None was a bug. **That is a sample, not the population.**

## The ONE-SIDED pile, read in full (2026-09-30, worktree `leads-onesided`)

On a re-run at `731b6137f` the pile was **201 rows in 65 functions** (the tree
had moved three functions since the 204/68 count). Every function was traced
against its listing. **No real bug.** One decompilation-introduced guard was
found, and it was dead (`MoveDir::UpdateOverlay`). Removing it measured
87.3115 -> 87.5363 (`3ae5a6ad5`), but that change was reverted in
`a870924b1` and handed over rather than landed, because the MoveDir unit was
held by a concurrent wave. The refutations fall into nine classes. The
first four now have a recogniser in the scanner (see the next section).

| Class | Functions (native-reachable unless marked Xbox) | How to tell |
|---|---|---|
| **Stub body** | `mmioWrite/OpenW/Advance/Read/Seek/StringToFOURCCW` (xdk, Xbox) | ours is `li r3,0; blr`; a class-11 question, not a condition |
| **Relocated test** | `UIListState::Scroll`, `FileMerger::Clear`, `DxMesh::DrawFur` (Xbox), `RndXfmCache::GetXfms`, `HamListRibbon::PostLoad`/`Draw`, `Trie::store`, `CacheResource`, `MCContainerXbox::Mount` (Xbox), `DingoSvrXbox::Poll` (Xbox), `Voice::UpdateMix` (Xbox, a moved `bdnz` block), `DecodeDxt5Alpha`, `FlowSlider::UpdateActivations` | the same producer + branch sits a few rows away, or in a moved block. LCS alignment pairs one of them with a neighbour, so a relocated test often shows up as TWO one-sided rows, one per side |
| **Cross-jump / tail merge** | `ChoosePlayerSides`, `MemAlloc`, `UIListState::Scroll` rows 135/149 | one side's `b` lands on the other copy of the test. Follow the `b` before believing it |
| **Rotated loop** | `RndText::ConstructMeshes` x2, `RndFont::CharWidthAdvanceCoords`, `RndSoftParticleBuffer::DoPost`, `SkeletonHistory::PrevFromArchive`, `HamCamShot::FlipTargetAnimGroups`, `Game::OnSetShuttle`, `FileMerger::Clear` row 126, `Sound::SetSpeed`, `WordWrap` (a strlen loop lowered two ways) | a guard + top test on one side against `b` to the bottom latch on the other |
| **Re-test / dead test** | `UIFontImporter::GetMatVariationName` (`x>0` then `x!=0`), `CharEyes::Poll` (jump threading), `CharLipSyncDriver::UpdatePlayback`, `ThreeDSound::CalculateFaderVolume` (re-reads `mShape` after `MILO_FAIL`; its case is already decided), `RndText::FitTextScroll` (the only effect is a dead stack store), `fft_recursive` row 62 (Xbox: `err != 0` at a join where the arm is a constant), `DumpHolmesLog` (ours null-checks `delete log` on a pointer already dereferenced), `MoveDir::UpdateOverlay` (**ours**, dead; removal handed to the lane that holds MoveDir) | the value was already tested, or is already known on every path |
| **If-conversion / materialised bool / select** | `CacheWav` (`subic/srwi/subfze/and` = `r3>0 ? 0 : x`), `Geo::Intersect` (`return f() ? 1 : 0`), `HamCharacter::SyncObjects` (re-normalising a 0/1 bool), `BSPFace::Update`, `CharInterest::ComputeScore`, `Spotlight::BuildNGCone`, `DecodeDxt5Alpha` row 57 | one side branches over 1-2 `li`/`mr`/`fmr`; the other computes the same value without a branch, or speculates it before the branch |
| **Upcast null guard** | `CharLipSync::Print`, `DirLoader::WriteTypeMemDump` | `addic. r,base,off; bne; li r,0`: MSVC's `p ? p+off : 0` for a derived-to-base conversion of a pointer that is never null (`&vec[i]`, `this+0x10`) |
| **Inlining difference** | `ObjPtrVec<FlowNode>::erase` | ours inlines `Set()`; the image calls it, and the image's out-of-line `Set` at `0x823E8A38` holds exactly our inlined `obj \|\| mListMode` test |
| **Out of scope** | `DepthBuffer3D::DrawShowing` (29 rows, 68.9%: the whole body is under `#else` of `HX_NATIVE`, so native runs an empty stub), `fft_altivec` (13 rows, 46.9%, Xbox VMX), `FindMITargetTypeInstance` (xdk CRT) | not read row by row. The DrawShowing rows sampled were `fsel`-vs-branch selects, a hoisted `has1&&has2&&has3` bool and duplicated assert compares |

The prior lane's 11 (the 13 `ObjPtrVec::operator=` instances, `RhythmBattlePlayer::Poll`,
`SyncProperty@HamCharacter`, `TypeProps::Save`, `Sound::SetSpeed`) still stand.

## The ONE-SIDED recognisers

Four recognisers each move a ONE-SIDED row into a named **artifact** bucket.
They move a row only after finding the other side's test, and three of them
re-run the full `classify_pair` predicate + successor analysis on that
counterpart and accept only `agree`. A counterpart that tests the complement
leaves the row ONE-SIDED.

| Bucket | Rows moved | Rule |
|---|---|---|
| `STUB-BODY` | 69 | the other side has <= 4 instructions and no branch, call, store or `b`, and this side is >= 4x longer. Kept strict on purpose: `RandomInt()` with its assert deleted compiles to a tail call (`b Int`), and that must stay a lead |
| `agree-relocated` | 29 | same producer signature at another address; the count of branches with that signature is equal on both sides; `agree`. A CTR latch counts too when its whole block signature matches |
| `agree-via-jump` | 8 | the other side has an unconditional `b` within 4 rows whose destination (after <= 4 non-branch instructions) is a conditional branch. Its producer sits between the destination and the branch, or before the `b`; `agree` |
| `RETEST` | 9 | this side compared the same register(s) the same way (same immediate or operand pair) earlier, found by a LINEAR walk with no redefinition in between. **This is an artifact label, not a dominance proof.** The row still lists under `--show-recognised` |

ONE-SIDED went from **201 to 86 rows** (65 to 38 functions) at `d511959ff`. All
10,857 other rows and the drop table stayed byte-identical. The
UpdateOverlay guard is still in the source, so its row still counts under
RETEST (9). What is left is
mostly out-of-scope rows (DrawShowing 29, fft_altivec 13), the
if-conversion/select class, and rotated loops whose guard's producer is not
on its own fall-through chain.

**Controls.** Selftest fixtures cover all four recognisers. Each fixture reads
ONE-SIDED with the recognisers off (`recognise_one_sided=False`) and reads its
new bucket with them on. Each recogniser also has a negative control that must
stay ONE-SIDED: a complemented counterpart, a register redefined between the
two tests, and a short real body that lost its only guard. Four live rows are
pinned.

For the script-mutation control, 9 mutants were run from a freshly created
empty directory: each recogniser switched off, each `agree` gate widened to
"any verdict", the RETEST redefinition check deleted, and the stub
call/store rule deleted. Each mutant failed exactly its own check. The stub
rule mutant was first missed because a length ratio masked it, and the
fixture was fixed.

For the live two-sided control, the scanner ran from a fresh empty directory
after two sabotages in `src/system/math/Rand.cpp`: an invented `if (i1 == i2)
return i1;` in `RandomInt(int,int)`, and the `MILO_ASSERT` deleted from
`RandomInt()`. ONE-SIDED went from 86 to 88, with one ours-only row and one
target-only row, and neither was absorbed by a recogniser. After a revert and
rebuild the output was byte-identical (sha256 `754c75e7…`).
`determinism_check.py --only cond_semantics_scan` reads SAME. `honesty_lint`
shows no new findings.

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
* **Immediate sabotage** (the class-3 shape): `TickFormat`'s `if (tick >= 0)` →
  `tick >= 1`. The function left the exact-100 set at **99.9677** — a number the
  rounded display prints as `100.0` — and landed in `OFF-BY-ONE` ("predicates
  differ on exactly one value (immediate 0 vs 1)"); reverted, byte-identical
  again. This is the measurement behind the exact-100 proof-of-absence drop: a
  changed literal IS charged by `match_percent_normalized`; "invisible under the
  normalized ruler" is true only of the rounded rendering.
* **Script mutation**, run from a freshly created empty directory: deleting the
  `swapped`-successor negation fails exactly the two selftest pins that exist to
  catch it; the unmutated copy passes.
* `determinism_check.py --only cond_semantics_scan`: SAME across PYTHONHASHSEED 1/7
  on 11,016 B.
