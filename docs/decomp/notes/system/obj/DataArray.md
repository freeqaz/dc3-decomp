# Lane notes: `src/system/obj/DataArray.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@DataArray@@QAAXAAVBinStream@@@Z`

`DataArray::Load` -- moved verbatim from `src/system/obj/DataArray.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 527-531):

```text
// w15-b (97.863 -> 100): every MemPushTemp/Resize/MemPopTemp triple is a
// MemDoTempAllocations scope.  w7-r traced the residual to seven missing dead
// stores of node.Type() into the address-escaping 0x58 temp (and 0x10 of
// frame); with the RAII scopes the function carries an EH state and MSVC keeps
// all fourteen stores, exactly as the image does.
```

Inside the body (was lines 568-575):

```text
        // This chain must stay an if/else-if chain in SOURCE ORDER. The target
        // tests 0x24, 0x20, 0x25, 0x7, 0x23, 0x8, 0x9, 0x21, 0x22 in exactly that
        // sequence, which is declaration order, not numeric order.
        // REFUTED (w7-l, 2026-09-14): rewriting this as `switch (node.Type())`
        // with the same case order -- the shape ../og-dc3-decomp and ../rb3 both
        // use -- makes MSVC sort the cases and emit a binary search (`bgt` on
        // 0x21 first). 97.863 -> 55.5 canonical, 10 mismatch rows -> 172. Do not
        // retry.
```
