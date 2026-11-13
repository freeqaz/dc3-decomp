# Lane notes: `src/system/rnddx9/TexMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rnddx9/TexMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 10-18; nearest following definition: `DxRndTexMgr::OnReleaseResource`):

```text
// w8-l: the std::map<Hmx::CRC, RefRes<void> > insert_unique instantiated from
// this TU is 99.967220 normalized and is the only function short of 100% in
// this unit (15/16).  Four charged rows, [13]/[14] and [65]/[66], and each
// pair is ONE exchange: the two lwz's swap BOTH their destination register and
// their base register together (r11<->r27 with r30<->r6, and r26<->r27 with
// r30<->r29), the offsets following at -/+0x10.  objdiff's offset resolver
// says so explicitly -- "4 excluded as non-field (4 different base register on
// each side)" -- so this is a register permutation inside stlport's tree
// insert, not a wrong struct field.  Nothing in this file selects it.
```
