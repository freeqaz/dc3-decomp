# The local-static scope ordinal, and what it says about retail's `OBJ_SET_TYPE`

2026-08-12. Lane `local_static_scope_ordinal` of the
`functionRelocDiffs=name_check` residual — 395 of dc3's 1,101 exposed
functions, and 362 of those were one macro.

## What the ordinal is

MSVC mangles a function-local static as `?<var>@?<ord>??<enclosing fn>`.
`<ord>` is a **running count of the lexical scopes opened in the function**, up
to and including the one holding the static — one digit for 0-9, otherwise
base-16 digits `A`-`P` terminated by `@`. It is not the nesting depth: two
statics at the same function top level get *different* ordinals if a
scope-introducing statement sits between them.

That makes it a witness to source *structure* that survives into the object
even when every instruction matches. When our object and retail's disagree on
`<ord>` while `functionRelocDiffs=none` says the code is byte-identical, the
ordinal is the only thing left telling us we wrote the function differently
from the way it was written.

## The calibration table

Measured by appending probe functions to a real TU, building it, and reading
each probe's ordinal straight out of the COFF —
`tools/calib_scope_ordinal.py <repo> [name-substring]`, about a second per
round behind the PCH. dc3, MSVC PPC, `/EHsc`, `src/system/ui/UIList.cpp`.

A static at function top level with nothing before it is **1**. Everything
below is the delta a construct adds.

| construct | delta |
|---|--:|
| a bare `{ }` before it | +1 |
| `if (c) S;` before it (unbraced substatement) | +2 |
| `if (c) ;` before it | +2 |
| `if (c) { }` before it | +3 |
| `if (c) S; else S;` before it | +3 |
| `if (c) S; else { }` before it | +4 |
| `if (c) { } else S;` before it | +4 |
| `for` / `while` / `do` / `switch` with a braced body, before it | +2 |
| `for (;;) break;`, `for (...) S;`, `switch (c) S;` before it | +1 |
| `switch (c) { case 0: S; }` before it | +2 |
| `try { S } catch (...) { }` before it | +2 |
| **inside** `if (c) {` | +3 |
| **inside** the `else {` of `if (c) { } else {` | +5 |
| **inside** the `else {` of `if (c) S; else {` | **+4** |
| **inside** the `else {` of `if (c) ; else {` | +4 |
| **inside** `else if (c) {` of `if (c) S; else if (c) {` | +6 |
| **inside** `for (...) {`, a `switch` `case`, `catch (...) {` | +2 |
| **inside** `try {` | +1 |
| one extra nesting level inside any of the above | +1 |
| `MILO_ASSERT(c, line)` before it — `do { if (!c) { } } while (0)` | +5 |
| `MILO_NOTIFY` / `MILO_WARN` / `MILO_LOG` before it | 0 |
| a destructible local (`String t;`), a temporary | 0 |
| a `const Symbol &` bound to a temporary | 0 |
| an inlined callee — with a bare block, a loop, a `try`, its own static, or used as the static's initialiser | 0 |
| a ternary initialiser, a comma expression | 0 |
| `if (a && b)` vs `if (a)`, redundant parentheses | 0 |
| `#pragma warning(push/pop)` | 0 |
| a declaration-in-condition, a `goto` label | 0 |

The costs are purely additive and compose: `if (c) { MILO_ASSERT(...); static }`
is 1 + 3 + 5 = 9, measured 9; `if (c) S; else { if (c) { } static }` is
1 + 2 + 2 + 3 = 8, measured 8.

Two things it is worth reading off this table. **Only scopes lexically BEFORE
the static count** — restructuring anything after it is free, which is why the
inner `if (found)` arms of `OBJ_SET_TYPE` are unwitnessed. And **an unbraced
substatement is a whole scope unit**, so `if (c) S;` and `if (c) { S }` are
distinguishable in the object file even when they generate identical code.
That last one is the entire finding below.

## `OBJ_SET_TYPE`: retail wrote the null case first

`src/system/obj/Object.h`. Retail's ordinal for the `static DataArray *types`
is 5; ours was 4. 362 functions, 112,520 bytes — the largest single lane in the
dc3 residual, and every one of them already byte-identical at `none`.

We had the non-null case first, braced, with the static inside it: 1 + 3 = 4.
The table says exactly one shape gets to 5 without a construct nobody writes —
**being in the braced `else` of an unbraced `if`**: 1 + 2 + 2 = 5.

```c
if (classname.Null())
    SetTypeDef(nullptr);
else {
    static DataArray *types = SystemConfig("objects", StaticClassName(), "types");
    ...
}
```

This is not a shape fitted to the counter. rb3
(`/home/free/code/milohax/rb3`, `src/system/obj/ObjMacros.h`, the matched
`#else` arm) is the same Milo macro reconstructed **independently, for a
different compiler**, against a different game's retail objects — and it is
written that way: `if (classname.Null()) SetTypeDef(0); else { ... }`. Two
reconstructions that never saw each other agreeing on the control flow is the
corroboration; the ordinal is what says which of the two spellings dc3 retail
used.

rb3 differs in one respect: it hoists the static to the function's top level,
above the `if`. dc3 retail did not — a top-level static would put the guard
check on the function-entry path unconditionally, which is code-visible, and
our conditional placement is already byte-identical to retail at `none`. So the
static is inside the branch and the branch is the `else`.

### Result

| | before | after |
|---|---|---|
| `none` (control) | 43.727554% / 4,973,320 B | 43.727554% / 4,973,320 B |
| `name_check` | 40.560802% / 4,613,152 B | 41.550125% / 4,725,672 B |

`tools/none_guard.py` held the control across code, data and the fingerprint of
all 28,680 `??_C@` string COMDATs. 362 functions went complete, totalling
exactly the +112,520 bytes `name_check` gained, so nothing regressed to pay for
it — and 361 of the 362 are `SetType`, the odd one out being a 32-byte
`fn_824EFBC4` in `HamPartyJumpData`.

## Ruled out

Recorded so the next attempt does not re-derive them.

- **A bare `{ }` spliced into the macro** reaches ordinal 5 at 100% `none`-fuzzy
  and is the wrong answer. It moves the counter without being source anyone
  writes; taking it would have been training the metric.
- **rb3's exact shape** (static hoisted above the `if`) is not dc3 retail's —
  see above.
- **Dropping the dead `DataArray *def;`** (rb3's macro has no such
  declaration) is both ordinal-neutral and byte-neutral. There is no witness
  either way, so it was left alone.
- **rb3's inner spelling** (`if (found != 0)` with an unbraced then-arm) is
  likewise ordinal-neutral, because those scopes open after the static.
- Everything in the 0-delta rows of the table: inlining, temporaries,
  destructible locals, ternaries, `#pragma`, `goto` labels, declarations in
  conditions, and all three of `MILO_NOTIFY` / `MILO_WARN` / `MILO_LOG`.

## Two checks that the table reads real source

- `OBJ_CLASSNAME`'s `static Symbol name(#classname)` sits at the top of
  `StaticClassName()`, so the table predicts ordinal 1 — and that macro has
  **zero** `name_check` charges across the whole tree, so retail is 1 too.
- `MonthToken` (`src/system/os/DateTime.cpp`) has a `MILO_ASSERT` before its
  `static Symbol month_symbols[12]`: predicted 1 + 5 = 6, and our object says 6.

## Still open in this lane

739 functions remain exposed at `name_check` after this fix, of which the
scope-ordinal ones are:

- **`MonthToken`** (14 sites): retail says **1** where we say 6. The table
  makes that a flat statement — retail's `MILO_ASSERT` cannot have come before
  that static. But the assert generates code, and we are byte-identical to
  retail at `none` with it there, so the resolution is not simply "delete it".
  Unresolved.
- **`DirLoader::FixClassName`**: deltas of +1, +2, +4 and +5 that *grow with
  position*, which no single constant construct explains. Our version is a flat
  chain of `if (mRev >= X) goto ret;` guards; retail almost certainly nested
  them, so each successive static is behind one more scope than ours. Needs its
  own probe run.
- The `_dw` (−10), `msg` (−5, −3), `_s` (−20) families, where retail's ordinal
  is *lower* than ours — we opened scopes retail did not.

## Instruments

- `tools/calib_scope_ordinal.py <repo> [substr]` — the table above, ~1s/probe.
- `tools/probe_scope_ordinal.py <repo> [--apply <name>]` — scores a candidate
  `OBJ_SET_TYPE` body by ordinal *and* `none`-fuzzy in one second.
- `tools/coffsyms.py <obj> [substr]` — COFF symbol names, stdlib only.
- `tools/none_guard.py --baseline b.json` / `--check b.json` — the control.
- Lane worklist and the triage that sized this:
  `<bench-bank>/archive/runs/namecheck-lane-triage-and-fixers-20260812/`.
