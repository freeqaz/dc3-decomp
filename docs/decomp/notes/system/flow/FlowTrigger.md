# Lane notes: `src/system/flow/FlowTrigger.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetEventEditorDef@FlowTrigger@@QAAPAVDataArray@@VSymbol@@@Z`

`FlowTrigger::GetEventEditorDef` -- moved verbatim from `src/system/flow/FlowTrigger.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 218-227):

```text
    // Each arm computes `->Array(1)` ITSELF and MSVC cross-jumps the four
    // identical FindArray/Node/Array tails into .L_82424B20 (w7-bn, 99.35 ->
    // 100.0).  That makes `a` a phi of four definitions at 82424B38, and a
    // phi'd null test is `cmplwi cr6`; a single-def call result tested by an
    // `if` is `cmplwi r3` (cr0), which is what a shared `eval->Array(1)`
    // after the chain produced.  Same rule closed XboxContentMgr::
    // StartRefresh.  Refuted at 99.35 (bg + bn, full ninja each): a separate
    // `found` local, `if (a && (a = ...))`, a bool temp, reusing the
    // multi-def `eval` for the tail, a second separate `if (a)`; flattened
    // early returns / `return a` / `found ? found : nullptr` -> 98.5.
```
