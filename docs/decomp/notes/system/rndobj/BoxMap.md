# Lane notes: `src/system/rndobj/BoxMap.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CacheData@BoxMapLighting@@QAA_NAAULightParams_Spot@1@@Z`

`BoxMapLighting::CacheData` -- moved verbatim from `src/system/rndobj/BoxMap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 233-256):

```text
    // w30-c (2026-11-02): target 79 instructions, ours 75, and the whole delta
    // is FOUR instructions: the image RELOADS mBeamLength/mTopRadius/
    // mBottomRadius (0x50/0x54/0x58) after the branch instead of keeping the
    // condition's loads live, and forms `&spot.mApex` with `addi rN, r4, 0x20`.
    // Everything else differs only in registers.  No behaviour difference: the
    // same three fields, the same f3/f1/f2 algebra and the same six stores in
    // both, re-derived from the target listing.
    // w34-b (2026-11-04): the three reloads are REACHED, and the binding is the
    // lever.  Naming the operands as plain `float` locals is byte-inert (c1
    // folds them back onto the condition's loads), but the same three names as
    // `const float &` survive copy propagation and each becomes its own load in
    // the body -- which is exactly the image's three reloads.  83.22785 ->
    // 97.87342 canonical, 80.0 -> 95.658226 fuzzy; the load, store and callee
    // multisets become IDENTICAL side for side, and the object goes 300 -> 312
    // B against the image's 316.  Same under functionRelocDiffs=all, so the
    // gain is not an immediate artifact, and behaviour is unchanged (a
    // reference to a member reads the same field at the same point).
    // What is LEFT is one dead `addi rN, r4, 0x20`: the image materialises
    // &spot.mApex and never uses it; binding `spot.mApex` to a local reference
    // to imitate it COSTS 5 points (92.8481), so it is a c1 artifact of the
    // original's expression tree, not a spelling to chase.
    // Also measured this lane and worse: reversing the radius comparison
    // (83.139), merging the two `if`s into one `&&` chain (byte-identical), and
    // hoisting the colour test above the radius test (79.722).
```
