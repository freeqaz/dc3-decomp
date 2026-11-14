# Lane notes: `src/system/rndobj/Font3d.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CharWidthAdvanceMesh@RndFont3d@@QBA_NGAAM0PAPAVRndMesh@@@Z`

`RndFont3d::CharWidthAdvanceMesh` -- moved verbatim from `src/system/rndobj/Font3d.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 143-182):

```text
            // w9-c 2026-09-30: 96.875 canonical / 96.875 fuzzy, 2 rows of 65, and
            // they are the SAME instruction on both sides -- the image emits
            // `lwz r11, 0x30(r30)` / `stw r11, 0x0(r27)` / `li r3, 0x1`, while
            // MSVC fills the load-use gap for us and emits `li r3, 0x1` BETWEEN
            // the load and the store.  Instruction sets identical, sizes
            // identical (256 B both sides), rows 0-55 100% equal.
            // Mechanism: the `li` materialises the return value, and the block
            // scheduler chooses whether to fill the lwz->stw latency slot with
            // it.  Measured inert (w26-b, one c2 compile each, 62/64 rows every
            // time): binding the mesh to a local first (`RndMesh *m =
            // info->mMesh; *mesh = m;`), writing the out-parameter as
            // `mesh[0] = ...`, parenthesising the loaded member, `return 1;`,
            // and a `bool result` local set inside the arm and returned after
            // the `if` (that last one re-schedules the block: 43/64).  Moving
            // the store ahead of the advance computation is worse still (46/64).
            // What is left to move is the block's scheduling region, not any
            // statement in it.
            //
            // NOT A TEMPLATE MISMATCH: objdiff's "Function Call Diff" reports the
            // image's `_M_find` instantiated over `pair<const unsigned short,
            // RndFont::CharInfo>` where ours is over `pair<const unsigned short,
            // RndFont3d::CharInfo *>`.  `_M_find`'s body depends only on the KEY
            // type and the node layout, so every unsigned-short-keyed map folds to
            // one body: both names resolve to 82703860 in icf_aliases.map.  The
            // map below really is `map<unsigned short, CharInfo *>` -- the image
            // loads a POINTER out of the node at 0x14 and then dereferences it at
            // 0x10/0x20/0x30 -- and the listing's template argument is the ICF
            // survivor's, not this call site's.
            // w26-z (2026-11-02): the mechanism is sched3, not sched0. The c2rs
            // regasg tap shows this block's schedule order is ALREADY
            // `lwz(info->mMesh)` / `li r3,1` (def r3, no use) / `stw *mesh`, so
            // the `li` is placed ahead of the store by the list scheduler and
            // sched0 never moves it back. Its height outranks the store's
            // because it feeds the `#2f8` return pseudo-op (tap RT 61). The
            // image's `lwz, stw, li` needs that def to rank no higher than an
            // unused store, and no spelling of the return reaches it: 14
            // measured, every one exactly 96.875 (named `ok` before and after
            // the store, `return (*mesh = info->mMesh), true;`,
            // `RndMesh *&slot = *mesh;`, `RndMesh **out`, `mesh[0]`, `(*mesh)`,
            // `!false`, `1 != 0`, `mMonospace || true`).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
