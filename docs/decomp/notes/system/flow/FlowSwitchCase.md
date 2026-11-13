# Lane notes: `src/system/flow/FlowSwitchCase.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?IsValidCase@FlowSwitchCase@@QAA_NPAVFlowNode@@PAVDataNode@@PBV3@_N@Z`

`FlowSwitchCase::IsValidCase` -- moved verbatim from `src/system/flow/FlowSwitchCase.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 83-94):

```text
        // The four relational arms go through CompareNumeric above (w7-bv,
        // 88.08 -> 100.0, 297/297 instructions).  Earlier notes here had
        // measured a named `const DataNode &to` (88.1), a per-arm or hoisted
        // `const DataNode *to` (77.9 both) and redundant scopes around either
        // branch (byte-neutral), and concluded the frame colouring -- the
        // kTransition branch's four Node() buffers at 0x58..0x70 and the six
        // switch-case buffers at 0x78..0xa0 -- was unreachable from source.  It
        // is reachable: every one of those spellings made `to` a scoped local,
        // which MSVC colours BEFORE the expression temporaries; passing the
        // Node() result straight into an inlined reference parameter keeps it
        // an expression temporary, colours it in statement order behind the
        // kTransition ones, and reads it through the returned pointer.
```
