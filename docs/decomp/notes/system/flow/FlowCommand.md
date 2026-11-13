# Lane notes: `src/system/flow/FlowCommand.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@FlowCommand@@UAAXAAVBinStream@@@Z`

`FlowCommand::Load` -- moved verbatim from `src/system/flow/FlowCommand.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 54-59):

```text
    // w16-d: 99.966 -> 100. The image declares symbols FIRST and drives the
    // SetProperty loop off `sit != symbols.end()` (0x8241DA98 compares r29, the
    // list<Symbol> walker, against &symbols at r31+0x70). The slot pair follows
    // the loop's controlling list, not declaration order -- which is why the
    // declaration swap alone (w7-aa, w8-l) only moved the ctor/dtor order.
    // Same trip count either way: both lists are pushed in lockstep above.
```
