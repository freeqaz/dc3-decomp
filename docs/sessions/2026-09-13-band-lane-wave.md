# Band-lane wave, 2026-09-10 to 09-13

~50 lane merges. `matched_functions` 30,677 → **30,944**, `matched_code_percent`
45.988 → **47.567** (+267 functions, ~179 KB). Native gate green on a settled tree
after every behaviour-changing merge (437 executed / 437 passed / 69 skipped
against a budget of 69).

**No worklists here, per [REMAINING_WORK.md](../decomp/REMAINING_WORK.md).** What
follows is the method, the mechanisms found, and the refutations — the things that
do not rot.

## Method

Slice the non-XDK under-100 frontier into bands by `match_percent_normalized`, one
worktree lane per band: 99.9-99.999 (one row from 100), 99.0-99.9, 96-99 by size,
94-99 ≥1500 B, 88-96 ≥900 B, 78-88 ≥700 B, 0-88 ≥400 B, and exactly-0%. Plus
single-target lanes for the two largest functions and research lanes for
cross-cutting mechanisms. Six concurrent; the coordinator rebases, **re-measures
every claim with `run_symbol_sweep` before merging**, merges `--no-ff`, and gates
natively. Each band lane generates its successor's list from what it did not reach.

Three coordinator rules learned the hard way:

- **Re-measure before editing.** Worklist percentages came from
  `decomp.db.current_percent` (the fuzzy ruler) and were several points off
  canonical in both directions. One sub-lane made two genuine instruction-level
  fixes against a wrong baseline and had to revert them.
- **`git rev-parse --abbrev-ref HEAD` before every merge.** A merge issued while the
  shared checkout is parked on another branch lands inside that branch.
- **Commit each measured result immediately.** Six lanes died together on one rate
  limit; everything committed was recoverable and everything uncommitted was lost,
  including a finished whole-binary A/B whose author never got to write it down.

## The certificates were worthless

The ~1,000 `AT_LIMIT` certificates written in one bulk event on 2026-08-19 are not
evidence. Every lane that re-read them found real bugs. Two examples stand for the
class: `RhythmBattle::OnBeat` (16.5 KB) was certified "LINKER_MERGED unfixable" at
38.7% and went to 99.45 on nine real logic bugs; `BustAMovePanel::OnBeat` (12 KB)
was certified "unicorn_equivalent_high - unfixable compiler noise" and went to
**100** on three. One lane's UI slice hit 6 of 7 wrong; another hit 8 of 8,
including an `artifact:orig_error` on a function that then reached 100 and a
`permuter_exhausted` on a plain off-by-one.

## Mechanisms worth reusing

- **A wide flags word built with shift-and-or can never match.** MSVC/Xenon
  allocates bitfields **MSB-first**; only assigning a bitfield member emits the
  target's `and ~mask` + `rldimi` and merges consecutive writes. A relational
  expression assigned *directly* to a bitfield lowers to a branch; routed through a
  named local it lowers branchless. Five `CalcShaderOpts` overrides, 13-47% → 100.
  Probes: `scripts/analysis/slotprobe/bf1..bf4.cpp`.
- **Stack-slot sharing is a source lever, not a flag.** See
  [stack-slot-sharing.md](../decomp/patterns/stack-slot-sharing.md). MSVC packs
  sibling-scope locals onto one slot unless the local's address reaches an inlined
  callee. A temporary takes the highest free slot of its size class and lives to the
  end of its full-expression, so the target's `(r1)`-store sequence decodes directly
  into `bs <<` chain structure.
- **A call result must land in a local before the member it updates is read.**
  `m = mOther; m += Call();` reads `mOther` twice; the target reads it once. 8pp on
  one function.
- **Read the split target assembly** in `build/373307D9/asm/**/*.s` rather than
  iterating on 30-row diff windows. Decisive on every function one lane moved.
- **A `MILO_ASSERT`'s stringified expression is part of the match, and the default
  ruler cannot see it.** One function read 100.0 all-equal at the default ruler
  while `report.json` had it unmatched at 99.964. The assert text also tells you
  what a local was named in the original.

## Refutations — do not re-chase

- **`Symbol` must not declare a copy constructor.** Two codegen tells suggested the
  original had one. Whole-binary A/B: 9 functions up, **3,409 down**, 3,181 leaving
  100, headline −12.5pp. It is an ABI change — a class with a user-declared copy
  ctor is never passed in registers here, and every class *containing* a `Symbol`
  loses trivial copyability with it. Details in
  [TECHNICAL_NOTES.md](../decomp/TECHNICAL_NOTES.md).
- **`Key<float>`'s default ctor is right.** `Key() {}` gives `DancerSequence::Load`
  +2.00 against 25 functions / 9,324 B regressing, worst −25.1.
- **The nine `ASSERT_REVS` relocation rows are MSVC's CSE anchor pick**, and 613 of
  645 sibling `Load`/`PreLoad` functions are already at 100 with our spelling, so
  the only lever reaching all nine breaks the 613.
- **A relocation row naming a different global on each side is usually the same
  variable**: MSVC anchors one `lis/addi` on a neighbour and reaches the real one by
  displacement. Two such rows were handed to a lane as bugs and both were artifacts.
  The sweep now classifies them (`ANCHOR_DISPLACEMENT`).
- **`Invert(Matrix4)` does not yield to its own diagnosis.** The target holds twelve
  cofactors live across ~250 calls; implementing exactly that makes MSVC schedule
  the call block *first* and the function drops 70.7 → 32.8. A next attempt needs a
  construct that pins the *schedule*, not another expression rewrite.
- **`CSHA1::Transform`'s round-macro lead is closed.** The FIPS-180 accumulation
  order is real but MSVC canonicalises the add chain before scheduling, so the
  rewrite is byte-identical. The prologue state-load order is a genuine defect whose
  fix still nets −0.6pp because the 80-round body reschedules.
- **The 0% band is 61 of 64 unreachable**, split between ICF naming artifacts (dtk
  names a folded address with a different group member than the one we emit) and
  invisible odr-use (the link is `/OPT:NOREF`, so a template COMDAT survives after
  the code that instantiated it was optimised away). Instruments:
  `scripts/analysis/{coff_defined_symbols,missing_instantiations,zero_pct_cause}.py`.

## Trades taken deliberately

- `HamDirector::ReactToCollision` committed at a **0.39pp canonical loss** to fix
  three evidenced behaviour bugs (an inverted comparison, a round-up that had lost
  its `* 4.0f`, two calls passing the wrong beat). Correct behaviour over the metric.
- Restoring the two hand-inlined `PropSync(T*&)` calls takes four 0% rows to 100
  (+1,032 B) but would drop the eight `PropSync(ObjPtrVec<T>&)` functions (5,376 B)
  to ~68% permanently, because MSVC at `/O1` refuses to inline that template where
  the target inlines it. **Refused.**

## Two instrument facts established late, both worth more than a crossing

- **Comments are inert here.** `MILO_ASSERT` takes its line as an *explicit
  argument*; `__LINE__` survives in 4 of 1,188 source files. Whole-binary probe (one
  comment line prepended to every `.cpp`/`.c`): **one** function moves, 36 bytes. See
  [comments-are-inert-except-at-__LINE__.md](../decomp/patterns/comments-are-inert-except-at-__LINE__.md).
  I had briefed a lane on the opposite and edited the standing brief before checking;
  the grep that settles it takes ten seconds.
- **Enumerations over history fail open.** `git show <merge>` yields an EMPTY diff for
  a merge commit; a bad pathspec matches nothing; `git log --grep` in a shell loop
  mangles subjects containing quotes. All three return a clean-looking zero. Measured
  here: asking "which source files did this wave touch" through merge commits returns
  **0**, while the control returns **202**. Run a does-this-match-anything control
  before believing any count, above all a zero.

## A sample of the real bugs

Every finger curled twice as far as retail (doubled curl-quaternion angle). Every
mesh collided through its own back faces (hard-coded backface-cull argument). The
standing-still tolerance was half its intended radius. Partially-weighted elbows
were over-rotated (identity-quaternion shortfall folded into the total). A
list-scroll "settled" test that could never settle. A `Handle()` returning the
integer 6 instead of `DATA_UNHANDLED`. A voice command that never stored the spoken
mode. Outro songs read from the intro array. Hair stiffness toggling every point. A
use-after-erase in a flow-trigger loader. A crossfade audio stream constructed with
a different flag than the primary. `*++rbegin()` where the original had
`back()+pop_back()`. A `DataNode*` passed as a `char*`. An unreachable timer reset
every path jumped over, so a gesture's raise timer never reset. A loader poll with
no re-entrance guard at all. A progress bar divided by a millisecond duration *and*
scaled, a million times too small. Editor picking that reported a hit only when a
new proxy was allocated, so it worked once and went dead. A save path that never
wrote three of six players' colours. A nav setter called with its index and its
on/off flag transposed. A directory-terminator scan that accepted a partial marker
followed by arbitrary bytes.

## Committed at a deliberate canonical LOSS — named, so a census can find them

Each of these went **down** on the ruler to fix behaviour the target's own bytes
prove wrong. A future pattern scan will see regressions here with no way to
distinguish them from rot, and a commit body is not where anyone looks first.
Values as of `1b54119e5`:

| symbol | before → after | what it bought |
|---|---|---|
| `?ReactToCollision@HamDirector@@IAA_NM@Z` | 88.86 → **88.47** | inverted comparison; a round-up that had lost its `* 4.0f`, dividing the beat count by four instead of rounding to a measure; two calls passing the wrong beat |
| `?Poll@XboxContent@@UAAXXZ` | 91.49 → **90.98** | backup-complete states 0/1 where the target uses 7/8, which re-armed the mount state machine |
| `?ReadEditorDirDead@@YAXAAVBinStream@@@Z` | 96.0 → **91.5** | the 20-byte terminator must arrive *consecutively*; ours matched each index independently, so a partial marker plus arbitrary bytes plus the rest was accepted |
| `?OnSetEnabled@…` / `?OnSetHidden@HamNavProvider@…` | 92.00 → **88.00** | `SetEnabled(int index, bool enabled)` was called with the DTA flag as the index and the label index as the flag |

`?SetCrossfadeJump@HamAudio@@QAAXMMM@Z` (86.15) belongs with them: its inverted
"begins before start of song" test was fixed at a flat score, and the three
`HamAudioCrossfade` tests that then failed were themselves asserting the pre-fix
semantics — see the `__LINE__`/test-adjudication note above.

The metric is the instrument, not the goal.
