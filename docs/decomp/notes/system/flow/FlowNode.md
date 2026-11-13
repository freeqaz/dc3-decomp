# Lane notes: `src/system/flow/FlowNode.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/flow/FlowNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 99-105; nearest following definition: `FlowNode::Load`):

```text
// w16-d: file-scope INIT_REVS + ASSERT_REVS (the image's lbl_820250D8 is the
// 8-byte gRev/gAltRev pair) replaces a function-local gRevs[4] array; that
// fixed the alt-version MakeString argument order (3 rows). Stopped at 99.5
// with ONE row left: the image stores r29 (&mDrivenPropEntries) to the dead
// home slot 0x60 just before `bl reserve`. Tried (inert): a named
// `ObjVector<DrivenPropertyEntry> &entries` reference for clear/reserve/push_back.
// w21-ae: closed to 100 -- see the std::vector reference at the reserve call.
```


## `?DuplicateChild@FlowNode@@SAPAV1@PAV1@@Z`

`FlowNode::DuplicateChild` -- moved verbatim from `src/system/flow/FlowNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 296-300):

```text
        // Copy dynamic property values from old flow to new flow
        // w16-d: 99.926 -> 100. The NewObject/dynamic_cast is ONE expression;
        // a named `Hmx::Object *newObj` local let the StaticClassName() Symbol
        // temp share arr's frame slot (frame 0xd0 vs the image's 0xe0, 24
        // offset rows).
```
