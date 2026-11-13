# Lane notes: `src/system/rndobj/VelocityBuffer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetXfms@RndXfmCache@@ABA_NPIBVRndMesh@@IIAAPBM@Z`

`RndXfmCache::GetXfms` -- moved verbatim from `src/system/rndobj/VelocityBuffer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 41-64):

```text
    // 83.33334 canonical / 83.125 fuzzy (w31-c).  The two mMeshPtrs tests are
    // written END first, which is the order the image forms them: 826B16BC
    // `slwi r11,r11,2` on endIndex precedes 826B16C0's start-index form, and the
    // end slot is reached as `lwz r11,-0x4(r11)`.  Swapping those two operands
    // (pure reorder of two side-effect-free compares; the short-circuit is
    // unchanged) is 75.0 -> 83.33334 canonical, 73.958 -> 83.125 fuzzy, and it
    // is the whole of the gain -- the earlier 75.0 readings were taken with the
    // start index written first.
    // RESIDUAL (2 instructions, scheduling): the image issues BOTH loads before
    // the first `cmplw` (`lwzx r10,r10,r3` then `lwz r11,-0x4(r11)`, then
    // `cmplw r10,r4` / `bne` / `cmplw r11,r4` / `bne`); we still compare-and-
    // branch on the end read before forming the start address.  Re-measured
    // inert from this order: both reads into named locals inside an `else`
    // guarded by the bounds test (83.33334 either way), and
    // `(a != mesh) | (b != mesh)` (73.33 -- it lowers to subf/subfe/or).
    // The instruction multiset and every constant already agree, so what is
    // left is c2 emitting the second load behind the first branch.
    // w34-a (2026-11-04, behaviour verdict): AGREES, re-derived from both
    // listings.  Both sides test endIndex against 0x1b580, form
    // &mMeshPtrs[endIndex] and mMeshPtrs[startIndex], load both slots, and
    // return the same &unk1f40[startIndex * 12] (mulli 0x30, addi 0x1f40) or a
    // null pointer with false.  The 2I/2D rows are the two identical slwi/lwzx
    // pairs exchanging position with the end-index load, so no value, offset,
    // bound or callee differs.
```


## `?CacheXfms@RndXfmCache@@AAA_NPIBVRndMesh@@PIBMIAAI@Z`

`RndXfmCache::CacheXfms` -- moved verbatim from `src/system/rndobj/VelocityBuffer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 103-132):

```text
        // w7-bt (82.2 -> 96.3 canonical): the image's mesh-pointer loop is a
        // counted `for` -- it carries the CTR pass's own dead zero guard
        // (0x826B1658 `cmplwi r6, 0x0` / 0x826B165C `beq` INTO the index
        // loop's body, unreachable because the 0x826B1644 `cmplwi cr6` guard
        // already excluded zero) where a do/while gets no guard at all.  The
        // index loop is the `subic.`/`bne` down-counter (0x826B1670), which it
        // only stays if `m` is declared BEFORE the `for` (declared between the
        // loops it takes the CTR instead, 90.6); the post-increment `*indices++`
        // keeps the strength-reducer's own `subi r8, r10, 0x4` (0x826B164C)
        // where an explicit `--indices` folds into `addi ..., 0x658f`.
        // Refuted: two `for`s in one `if` (83.2, loop 2 takes CTR with an
        // unfolded guard), a pointer-compare `for` (68.7, never CTR),
        // `while (m--)` (90.6), indexing `mMeshPtrs[startIndex + i]` with no
        // local (85.6).
        // RESIDUAL (w7-bt, 96.3 canonical): the image schedules that `subi r8`
        // before the mesh-pointer loop and we sink it to the index loop's
        // preheader (1 insert + 1 delete), and r9/r10/r11 rotate from the
        // 0x826B15F4 reload of unk1b580 onward.
        // w26-z (2026-11-02): the c2rs regasg tap puts the cause on LOOP-
        // STRENGTH-REDUCTION PREHEADER BLOCK PLACEMENT, not on an expression.
        // Our build has two separate preheader blocks -- b8 `addi r10, r9, -4`
        // (meshPtrs) and b13 `addi r10, r3, -4` (indices) -- while the image
        // carries BOTH decrements, `subi r8, r10, 0x4` and `subi r9, r9, 0x4`,
        // inside the one dominating setup block, before the mesh loop. The
        // `-4` is the reducer's own, not the front end's: writing
        // `&unk19640[startIndex] - 1` folds it into the address constant
        // (`addi r10, r11, 0x658f`, 95.259). 13 variants measured. Byte-
        // identical and so no help: pointer-walking the mesh loop, dropping the
        // scope braces, `*indices = idx; indices++; idx++;`. Worse: counted
        // `for` for the index loop (86.9), `idx` declared first (92.6).
```


## `?Draw@RndVelocityBuffer@@QAA_NPAVRndCam@@AAV?$ObjPtrList@VRndDrawable@@VObjectDir@@@@@Z`

`RndVelocityBuffer::Draw` -- moved verbatim from `src/system/rndobj/VelocityBuffer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 329-355):

```text
        // The cache index must be read INLINE in both subscripts, not through a
        // named `int cacheIdx` local.  The local is what made MSVC sink
        // prevXfm's address past the nine intervening calls and rematerialise it
        // at the SetPConstant site from a callee-saved copy of the index PLUS a
        // callee-saved copy of the 0x36bec base -- two registers where the image
        // spends one, which pushed the prologue from `__savegprlr_16` to
        // `__savegprlr_15`, the frame from 0xe0 to 0xf0, and rotated every
        // callee-saved register in the function by one.  That single local was
        // worth 48.74 -> 99.0 canonical.
        // RESIDUAL (w7-as, 99.0 canonical): the last 8 rows are the two address
        // chains emitted in the opposite ORDER inside this one basic block.
        // Image: cur.slwi, prev.slwi, cur.add(r31), prev.xori, cur.add(r8)->r3,
        // prev.add(r31), prev.add(r8)->r25.  Ours starts with prev.slwi and
        // finishes cur one slot later.  Swapping these two declarations is
        // exactly byte-identical, so the order is the MSVC block scheduler's,
        // not the source's.
        // w26-r: the tap names the mechanism.  The two shifted indices are
        // candidates id 37 (sym 63, the cur offset) and id 36 (sym 64, the prev
        // offset); both are pri 30 tie 0, so the pop order is descending id and
        // cur takes r11 (the image has cur in r10, prev in r11).  The ids come
        // from the "code" pass CMINT log, which numbers them DESCENDING in
        // creation order (CMINT 91 -> id 37 sym 63, CMINT 92 -> id 36 sym 64),
        // so the flip needs cur created after prev.  Every source spelling that
        // could move that creation order was measured code-identical or worse
        // here (declaration swap identical; naming either index 48.7; memcpy
        // first 91.5), i.e. the creation order is not the declaration order.
        // Open question: which pass orders the two address chains' symbols.
```
