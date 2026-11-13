# Lane notes: `src/system/hamobj/HamMove.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `Dc3W8bHamMoveProbe()`

`Dc3W8bHamMoveProbe` -- moved verbatim from `src/system/hamobj/HamMove.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `Dc3W8bHamMoveProbe` in report.json (unit); filed under the source name

Above the definition (was lines 27-34):

```text
// w8-b: orphan-instantiation anchor.  ham_xbox_r.map line 43563 puts
// ??$MakeString@PBDPBDH@@YAPBDPBDABQBD1ABH@Z at 0x824AFEA8 in hamobj:HamMove.obj,
// but no surviving call in HamMove reaches a MakeString("...%s...%s...%d..."):
// the caller was /OPT:REF'd away and only the instantiation was linked.  An
// unreferenced EXTERNAL function re-creates it.  `static` does NOT -- measured on
// this exact probe: adding `static` costs exactly 1 function / 92 B binary-wide
// (31246/5546312 -> 31245/5546220), i.e. MSVC discards a static before it
// instantiates anything it mentions.  Deleting it likewise takes the row to 0%.
```


## `?Copy@HamMove@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

`HamMove::Copy` -- moved verbatim from `src/system/hamobj/HamMove.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: definition is macro-generated (BEGIN_COPYS); symbol matched by the name the macro expands to

Inside the body (was lines 493-498):

```text
        // w21-k (97.77 -> 100): iterate through a named reference to the
        // list.  With FOREACH straight over the member, MSVC hoisted the list
        // head load (`lwz r29, -0xf8(r31)`) between the two byte copies above;
        // the image finishes both copies first (w7-w/w7-ag/w13-b had certified
        // that 4-row scheduling residual as a floor).  FOREACH_CONST over the
        // member, which binds a const reference the same way, also measures 100.
```
