# Lane notes: `src/system/obj/Data.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `DataArray::GetObj()`

`DataArray::GetObj` -- moved verbatim from `src/system/obj/Data.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `DataArray::GetObj` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 472-476):

```text
    // w21-ab NEGATIVE: `return Node(i).Obj<Hmx::Object>(this);` (the extra
    // inline level that closed DataReplaceObject) does not compile here --
    // Hmx::Object is incomplete in Data.h, so non-PCH TUs (Memory_Xbox,
    // HttpGet) fail C2680 on DataNode::Obj's dynamic_cast.  Spell it at the
    // call site instead, as DataReplaceObject does.
```
