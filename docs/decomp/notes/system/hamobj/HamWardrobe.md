# Lane notes: `src/system/hamobj/HamWardrobe.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnSetVenue@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z`

`HamWardrobe::OnSetVenue` -- moved verbatim from `src/system/hamobj/HamWardrobe.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 573-609):

```text
    // Venue()'s result is an UNNAMED temporary, not a named `Symbol venue`.
    // A named local pins its 4-byte slot for the whole scope; the temporary
    // dies at the end of the full expression, so the image reuses that slot
    // (0x64) for the MILO_ASSERT line number and again for Symbol("crew01").
    // Naming it shifted every later local by 4 and cost 33 instructions.
    // w17-b (stopped at 99.906): the whole residual is that the image puts
    // Venue()'s result in the SAME slot (0x64) as the return-value EH flag
    // (`stw r30, 0x64` before the Obj<> call), the MILO_ASSERT line temp and
    // Symbol("crew01"), and reads venueName back from that slot (`lwz r20,
    // 0x64(r31)`, after the gNullStr load).  Ours gives the Venue temp its own
    // slot (0x68) and every later local shifts.  Measured, all no better: a
    // block-scoped `Symbol venue` (reads from the slot, but venue at 0x64 and
    // the flag pool moves to 0x68: 99.2), function-scope `Symbol venue` (same
    // 99.2), `const Symbol &venue = Venue()` (99.2), `Symbol venue; venue =
    // Venue();` and `static_cast<Symbol>(Venue()).Str()` (both = this, 99.906),
    // `gNullStr == venueName` operand order (inert).
    // w21-l (value-scan row, ARTIFACT): the scan reads the image's `lwz r20,
    // 0x64(r31)` as the flag's 0 stored there earlier; Venue() writes that slot
    // through its hidden return pointer (`addi r3, r31, 0x64`), so it is the venue.
    // w21-ax (stopped at 99.9058, same 35 rows: 12 regswap + 23 frame offsets):
    // frame-cell reading of the target -- 0x64 {flag, Venue, line, crew01},
    // 0x68 {"venues", Symbol(venueName)}, 0x6c {crew02}, 0x70[8] {substr temp,
    // emilia01}, 0x78[8] {worldPath, mo01}. Ours opens two extra 4-byte cells
    // (Venue at 0x68 is never reused; crew02/emilia01 get fresh 0x70/0x74), so
    // worldPath lands 8 higher. Inert here: the Venue statement in its own
    // `{}` block, `Symbol(TheGameData->Venue()).Str()`, and
    // `SetDir(a->Obj<ObjectDir>(2))` with no `dir` local. Named `Symbol venue`
    // re-measured: venue 0x64, flag/line pool 0x68, worldPath still +8 (98.4 raw).
    // w26-e: both named-Symbol readings re-measured in a splice harness --
    // `Symbol venue = TheGameData->Venue(); const char *venueName =
    // venue.Str();` and the `Symbol venue(...)` ctor form each compile to 159
    // instructions against the image's 138 (the out-of-line Symbol copy
    // constructor), so the image's `lwz r20, 0x64(r31)` is an in-place
    // temporary re-read, NOT a named local.  The open item is a c1 frame-slot
    // one: the image lets the Venue temp reuse the exception-state cell at
    // 0x64; our front end gives it its own cell at 0x68 and every later cell
    // shifts by 4 or 8.
```
