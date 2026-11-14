# Lane notes: `src/system/os/System.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/os/System.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 73-84; nearest following definition: `CheckForArchive`):

```text
// ⚠ An objdiff row in InitSystem / PreInitSystem reading "we reference
// gUsingCD where the target references gSystemConfig" is NOT a wrong-variable
// read.  Both functions materialise ONE address register (r30) for this pair
// and reach the other member at ±0x8; the relocation names only the anchor.
// The target anchors on gUsingCD and reads gSystemConfig at -0x8(r30); we
// anchor on gSystemConfig.  Measured 2026-09-11: that choice is NOT driven by
// the layout (it survived this reorder unchanged), nor by reading gUsingCD
// directly instead of through UsingCD(), nor by binding a `int &` to gUsingCD
// to force its address temp.  The residual rows are codegen -- do not "fix"
// them by renaming a global.  w18-d: what DOES drive it is the writes --
// InitSystem/PreInitSystem store through SetUsingCD() in the image; with plain
// `gUsingCD = ...` stores MSVC anchors on gSystemConfig instead.
```


## `?SetSystemArgs@@YAXPBD@Z`

`SetSystemArgs` -- moved verbatim from `src/system/os/System.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 577-583):

```text
                // RESIDUAL (w7-al, 96.7 canonical): 19 rows.  16 are one
                // callee-saved ranking -- the image puts the literal zero in
                // r30 and the sCommandLineBuffer address in r29, we do the
                // reverse -- and the other 3 are the scheduler hoisting
                // `cntlzw` above `stb` and materialising the toggle through an
                // extra `mr r28, r11` that we fold into the `extrwi.` itself.
                // Moving this assignment above the `*ptr = 0` is byte-inert.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual:
sCommandLineBuffer is named lbl_82F653B0 on target; one stb uses a different register.
