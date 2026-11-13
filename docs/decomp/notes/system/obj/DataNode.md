# Lane notes: `src/system/obj/DataNode.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Evaluate@DataNode@@QBAABV1@XZ`

`DataNode::Evaluate` -- moved verbatim from `src/system/obj/DataNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 143-149):

```text
        // PLATFORM (w25-n1 audit): Property(fail=true) returns null only after
        // MILO_FAIL_DTA (Object.cpp, FindArray(propKey, fail) / "property %s not
        // found"). The image derefs it bare (Property at 0x8259D5DC feeds UseQueue
        // at 0x8259D5E0, no test) because its fail never returns there: inside
        // MILO_TRY it throws, outside it halts. Native throws inside a try too
        // (w23-dta), so this arm is reached only from a native fail outside a
        // try, which warns and returns -- the stand-in for the image's halt.
```


## `?Equal@DataNode@@QBA_NABV1@PAVDataArray@@_N@Z`

`DataNode::Equal` -- moved verbatim from `src/system/obj/DataNode.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 718-723):

```text
        // w21-s: spelled on the fields (first.mType / second.mType), not the
        // firstType/secondType locals -- same values.  With the locals MSVC
        // jump-threads the Int/Object/Symbol arms' fall-outs past the
        // `firstType == 6` test; the image does not (its bne at 0x8259EBE4,
        // 0x8259EC70, 0x8259ECB0 all land on 0x8259ED08, the first test), and
        // the field spelling also gives the image's type registers (r9/r10).
```
