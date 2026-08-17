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

A function's first construct starts at 2. `MILO_ASSERT`'s 5 is just the table
applied to its expansion: `do`(1) + block(1) + `if`(2) + block(1).

Worked example — `_SYNC_PROP_BITFIELD` in `system/obj/Object.h` predicts 39 for
the gap between two `_s` statics in `CamShot::SyncProperty`, and 39 is what the
compiler emits.

## Using it

1. `python3 scripts/analysis/scope_index_census.py` — whole-build census of
   target vs our indices, grouped by enclosing function. As of 2026-08-17,
   1,289 functions agree and 30 do not; the 30 are the worklist.
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
