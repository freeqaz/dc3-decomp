# Lane notes: `src/system/math/kdTree.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FindSplit_Mean@kdTreeNode@?$kdTree@VTriangle@@@@QAA_NABVBox@@ABV?$list@PAVTriangle@@V?$StlNodeAlloc@PAVTriangle@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`kdTree::kdTreeNode::FindSplit_Mean` -- moved verbatim from `src/system/math/kdTree.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Inside the body (was lines 192-204):

```text
            // The image tests begin() != end() with the iterator it then walks
            // (lwz r29, 0x0(r25); cmplw r29, r25); `!items.empty()` + FOREACH
            // loads begin twice-over through r11 and adds a mr.
            // w26-c: the two EDGE vectors are named references.  The image
            // orders every component sum by descending displacement (x: 0x10
            // then 0x0, y: 0x14 then 0x4, z: 0x18 then 0x8); with the frame
            // read inline, c2's post-lowering commutative sort keys bound
            // `origin.x + frame.x.x` off the *temp* it mints for `cur`, and
            // only the x pair (disp 0x0 vs 0x10) came out ascending.  Naming
            // the edges gives the memory operands a variable base, whose real
            // address hash (not just the temp sid mod 4) puts all three pairs
            // in the same order.  w14-a's 4-row residual, closed; swapping the
            // source operands stays inert, as does ::Add(origin, frame, v).
```
