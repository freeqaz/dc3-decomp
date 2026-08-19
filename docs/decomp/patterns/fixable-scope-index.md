# Local-static scope indices: reading a function's block structure off its symbol names

**dc3-decomp (title 373307D9).** Applies to any MSVC PPC target; the numbers
below were measured against this build's own compiler,
`build/compilers/X360/16.00.11886.00/cl.exe` with `/O1 /Oi /EHsc /TP`.

## What the number is

A function-local static mangles as

```
?<name>@?<scope>??<enclosing function>@4<type>A
```

`<scope>` is an MSVC number: a single digit `d` encodes `d+1`, anything else is
a base-16 string over `A`..`P` terminated by `@` (`?BA@` = 0x10 = 16, `?9` = 10).
It is **a per-function counter of scopes opened so far**, sampled at the point of
declaration. It is not an identity — a static declared after an inner block keeps
that block's number, and statics in the same scope share one.

That makes it a fingerprint of the enclosing function's block structure that
survives into the shipped binary. When our index differs from the target's, our
source has a different number of lexical scopes before that declaration. Under
the graded ruler (`functionRelocDiffs=name_check`, the one `report.json` uses)
**every relocation naming that static is charged**, so a single missing brace can
cost a 2,500 B row.

## The cost table

| construct | cost |
|---|---:|
| `if (c) stmt;` | 2 |
| `if (c) { stmt; }` | 3 |
| `else stmt;` | +1 |
| `else { stmt; }` | +2 |
| `else if (c) { stmt; }` | +4 |
| bare block `{ ... }` | 1 |
| `while` / `for` / `do` (+ its braced body) | 2 |
| `switch (x) { ... }` (+1 per braced case) | 2 |
| `MILO_ASSERT(c, line)` | 5 |
| ternary, `&&`, `||` | 0 |

`MILO_NOTIFY_ONCE` / `MILO_WARN_ONCE` cost 1 — the macro body is a bare block,
and `_dw` is declared inside it, so the macro's own block is the scope the
static lands in. `START_AUTO_TIMER` costs 0: under `MILO_DEBUG` it expands to
two *declarations* (`static Timer *_t`, `AutoTimer _at`), and a declaration
opens nothing.

**The counter starts at 2 because the function body itself is scope 2.** The
doc used to say "a function's first construct starts at 2", which is the same
statement read backwards and invites an off-by-two when you walk a function by
hand. The direct evidence is `?_t@?1??DrawShowing@WorldCrowd@@UAAXXZ@...`:
`?1` decodes to 2, and `_t` is `START_AUTO_TIMER`'s static, declared at the top
of the body before any construct. So: **start at 2, add the table in source
order, and the value you are standing on when you open the static's scope is
the index.** Verified to the digit on all five `_dw` in
`RndTexBlender::DrawShowing` (9, 15, 26, 41, 56) and both in
`WorldCrowd::DrawShowing` (42, 54).

`MILO_ASSERT`'s 5 is just the table applied to its expansion: `do`(1) +
block(1) + `if`(2) + block(1).

Worked example — `_SYNC_PROP_BITFIELD` in `system/obj/Object.h` predicts 39 for
the gap between two `_s` statics in `CamShot::SyncProperty`, and 39 is what the
compiler emits.

## Reading the two sides correctly

This bit the census twice, and both traps are easy to walk into by hand as well.

* **Our indices come from `?<name>@?<scope>??<fn>@4<type>A` data symbols in
  `build/373307D9/src/**.obj`, never from `??__F<name>@...` atexit helpers.**
  `scripts/obj_atexit_scope_patcher.py` rewrites the atexit names in our objects
  to whatever the target says, so objdiff can pair the bodies. Reading them back
  tells you the target's structure with our filename on it. (It also leaves the
  pre-patch string behind in the COFF string table, so `strings` shows both.)
* **The target's indices are the other way round.** dtk leaves most of the
  `.data` objects as bare `lbl_<addr>`, so `symbols.txt` names only a minority of
  the target's statics — but it names *every* `??__F<name>@?<scope>??<fn>@YAXXZ`,
  and that helper carries the same counter. The atexit list is the only complete
  enumeration of a target function's statics. `RndTexBlender::DrawShowing` has
  one named `_dw` in `.data` and three atexit helpers; the three are the truth.
* **A function can declare many statics under one name** — one `_dw` per
  `MILO_NOTIFY_ONCE`, one `msg` per static `Message`, one `_s` per `SYNC_PROP`.
  Compare the sorted *lists*. If the lengths differ, the count is the finding:
  we invented or dropped a declaration, and no amount of brace-shuffling will
  reconcile it.
* **objdiff's relocation pairing tells you which static is which.** Run
  `run_diff_inspect mode=clusters` and read the `lis`/`addi` pairs: it lines our
  `?_dw@?DG@` up against the target's `?_dw@?CK@`, which is how you learn that
  two indices that happen to be equal belong to different declarations.

## Using it

1. `python3 scripts/analysis/scope_index_census.py` — whole-build census of
   target vs our indices, grouped by enclosing function. After the 2026-08-19
   multiset fix: 1,259 functions agree and 60 do not. (The older "1,289 / 30"
   figures came from a census that kept only the last static parsed under each
   name; 33 of the 60 were hiding behind that collapse, and most of them are
   `COUNT tgt/ours=(2,1)` — a warn-once the target has and we do not.)
2. For one function, decode both sides and walk the source with the table. The
   delta localises to the exact stretch between two statics.
3. Fix the source, rebuild the one `.obj`, and read the new names back with
   `strings -a <obj> | grep '??<function>'`. The obj patchers do not touch scope
   indices, so a single-object build is enough for this check (it is NOT enough
   for a match percentage — see BUILD_SYSTEM.md).
4. Confirm with `run_objdiff` that the instruction stream is untouched. **If an
   edit moves a single instruction it is the wrong edit**: these are pure
   structure, and the compiler emits the same code for braced and unbraced
   bodies, for `if (a) if (b)` and `if (a && b)`, and for a branch and its
   inversion with the arms swapped.

## What the deltas mean in practice

Every fix landed on 2026-08-17 was one of four shapes:

* **A missing brace pair.** `ClipCollide::SyncWaypoint` (+1: the early-out
  `if (!mChar || !mWaypoint) return;` is braced in the target),
  `DirLoader::FixClassName` (each rung of the rev ladder is a block, +1 each).
* **A brace pair we invented.** `IsUselessLoad` (-2: two single-statement bodies
  in the dance_battle branch are bare), `DataMergeFilter::Filter` (-1).
* **Two nested ifs that are one short-circuit condition.**
  `Automator::OnCheatInvoked` (-3).
* **An inverted branch with the arms swapped.** `Leaderboards::Text` (-2): the
  static lives in the arm that our source made the `else`. `a && b` with the
  arms one way and `!a || !b` with them the other compile to the same branch
  layout, so only the index can tell them apart.

  `KeylessHash<T1,T2>::Insert` (2026-08-19) is the largest of these so far, -10
  across all three instantiations that carry a `MILO_NOTIFY_ONCE`. Everything
  before the static is identical on both sides, but our source had

  ```c++
  if (mOwnEntries) { MILO_ASSERT(mSize, 0xB3); Resize(...); ... return Insert(val); }
  else             { MILO_NOTIFY_ONCE("Hash table half full (%d)", mSize / 2); }
  ```

  and the original had the test inverted with the arms swapped, which puts the
  notify-once block at 30 instead of 40 *and* moves `MILO_ASSERT(mSize, 0xB3)`
  after the static so its 5 stops counting. -10 is exactly two asserts' worth,
  which is a trap: the obvious reading ("we have two asserts the target lacks")
  is refuted by the instruction stream, which was already 100% with every
  instruction equal and carries all four asserts' explicit line numbers as
  immediates. The second reading ("we invented brace pairs") is refuted by
  arithmetic — unbracing every body that legally permits it, including
  collapsing the two nested `if`s onto a dangling `else`, bottoms out at 39.
  **Compute the floor of the brace-only explanation before you go looking for a
  brace.** All three bodies stayed at 212/209/201 instructions "all equal"
  afterwards; MSVC lays the arms out identically because one of them ends in a
  `return`.

## When the skew is not a scope fix at all

If the counts differ, the function is missing or inventing a *declaration*, and
the index is a symptom of ordinary decomp divergence. Do not touch braces; the
neutrality gate ("if an edit moves an instruction it is the wrong edit") does not
even apply, because the function is not matching in the first place. Two worked
examples, left open on purpose:

* **`RndTexBlender::DrawShowing`** (92.5%). Target has 3 `_dw`, we have 5. The
  relocation pairing shows the target's near-list and far-list loops both
  reference **`?_dw@?P@??DrawBlendList@...`** — i.e. the original called
  `DrawBlendList(nearList, ...)` / `DrawBlendList(farList, ...)` and the compiler
  inlined them, keeping the static's name attached to `DrawBlendList`. Our source
  hand-wrote both loops inside `DrawShowing`, which manufactures two statics that
  cannot exist in the target. The remaining three need +3 before the first
  notify, -3 between the first and second, +1 between the second and third —
  consistent with the outer `if (a && b && mOutputTextures)` being two nested
  `if`s and the two validity warnings being siblings rather than `if`/`else`.
  Note `DrawBlendList` is itself only 92.5%, and its `state != 2 ? mNearMap :
  mFarMap` disagrees with the near loop's `mRenderedStates |= 2`, so that body
  wants checking before anything is routed through it.
* **`WorldCrowd::DrawShowing`** (85.9%). Both sides have 2 `_dw`, but they are
  offset by different amounts (+8 on the collider warning, +12 on the environ
  warning) — the equal-looking 42 on each side is our *first* static against the
  target's *second*. So we are 8 scopes long before the collider loop and 4 more
  between it and the environ check; the `if (n != 0) { do { } while }` around the
  bounding-rect reduction (5) where a plain `for` would be 2 is the most likely
  three of those four.

## The one that is not brace-shaped: assert spelling

`MILO_ASSERT`'s do/while form (cost 5) is right for the overwhelming majority —
changing `system/os/Debug.h` to the expression form
`((cond) || (TheDebugFailer << ..., 0))` moved 50 functions OFF a correct
numbering and cost 7,716 B of matched code; the bare-`if` form (cost 3) cost
8,600 B. Both were measured whole-build and reverted.

But a residue of functions can only be reconciled if their asserts cost **3**
(`CampaignMqCrewProvider::Text` and `::UpdateList`, two asserts each, both
land exactly; `Automator::FillButtonMsg`; `CampaignSongProvider::Text`) or
**0** (`ShellInput::Init`, three asserts; `MonthToken`;
`Hmx::Object::ExportPropertyChange`), while `Accomplishment::Configure` and
~1,280 others require **5**. The instruction streams of all of these match, so
the assert code is present on both sides in every case; only the block structure
around it differs. Either the original had more than one assert spelling with
the same message format, or those functions differ from ours in some other way
that happens to be a multiple of the assert cost. **Do not "fix" these by
rewriting asserts longhand or by adding a second assert macro** — the evidence
does not identify which, and a whole-build measurement is the only thing that
has ever settled a question in this class.

The expression form is also refuted on its own terms wherever the assert
condition builds a temporary: in `TexLoadPanel::LoadMoggClip` the target
destroys the `String` temporary BEFORE the failure branch, which only the
statement form does.
