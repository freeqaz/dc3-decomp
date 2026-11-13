# Lane notes: `src/system/rnddx9/MultiMesh.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateGeometryBuffers@DxMultiMesh@@AAAXXZ`

`DxMultiMesh::UpdateGeometryBuffers` -- moved verbatim from `src/system/rnddx9/MultiMesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 98-142):

```text
// RESIDUAL (w7-bs, 100.0 canonical / 99.69 raw, 652 B, 163/163, 5 rows):
// only target indices 73-77, the 0x8007000E mask after
// D3DDevice_CreateVertexBuffer, where the image allocates r10/r11 and we
// allocate r11/r12 (w7-bp's cluster (1)).  Everything for the preceding 72
// instructions is equal and the operand order of the `and.` is the same, so
// this is volatile numbering, not a source shape.
//
// STILL OPEN (w26-f7, fuzzy 99.724, 5 rows).  The mechanism is the reader-side
// allocator (C2RS-BRIDGE 8.1), not COLOR: `subic`/`subfe`/`ori`/`lis` are all
// regasg temps, first-fit over the sched3 order in the list r11 r10 ... r3
// r31 ... r14.  Ours takes r11 for `subic` first and then r12 for `lis`, so
// r10 was already blocked when `lis` reached regasg; the image has `lis` on
// r11 and `subic` on r10, i.e. the constant is materialised BEFORE the mask
// and r10 is free.  Nothing about the mask expression changes that: 6
// respellings measured this pass (`== 0 ? 0x8007000E : 0` 97.09, a named
// `s32 mask` local, `~(x != 0) & k`, `-(x == 0) & k` 98.50, an if/else arm
// 97.09, a `(s32)` cast on the constant) leave 5 rows or make it worse; the
// named local and the cast are byte-inert.  Open question: which value holds
// r10 across this block in sched3 order -- it is dead in the emitted code by
// index 59 -- since that, not the mask's spelling, is what blocks the `lis`.
//
// BUSTED (w7-bs, 93.98 -> 100.0).  w7-bp's cluster (2) -- the three DEAD home
// stores of `*(mGeomOwner + 0x148)` at 0x82622F50 / 0x82622F6C / 0x82622FA0
// and the tail's `lwz r11, 0x148(r30)` / 0x110-0x114 / `divw` re-derivation
// at 0x82622F90-AC -- IS the accessor's real spelling: `owner->Faces()` is an
// inlined `return mGeomOwner->mFaces;` (Mesh.h), and MSVC homes the returned
// reference once per source MENTION while CSE-ing the loads.  Three mentions
// = three homes: the loop bound `owner->Faces().size()` (home 0x58, evaluated
// at entry and again in every tail because the bound is not hoisted), the
// subscript `owner->Faces()[i]` (home 0x5c), and `indexCount` before the
// null test.  The tail compare is `cmplw cr6, r9, r8` / `bne cr6` at
// 0x82622FAC-B0, so the bound is spelled `!=`, not `<` (`<` costs one
// diff_op row, 99.97).  The `beq .L_82622FB4` at 0x82622F5C is the
// zero-trip test the counted `for` produces on its own.
//
// Kept from the raw-offset era, still measured against THAT spelling and
// superseded by the accessor form above -- do not resurrect either:
//   (w7-aj) re-evaluating the raw (0x114-0x110)/6 bound in a hand-rotated
//       do/while tail regressed 88.4 -> 86.0 and 91.5 -> 90.3;
//   (w7-bp) post-increment `*dst++ = a; *dst++ = b; *dst++ = c;` cost
//       94.0 -> 93.0 (MSVC split the induction variable into r6+r3);
//       `*++dst` was worth +0.7 over `dst[0]/dst[1]/dst[2]`.
// The `*dst` / `*++dst` / `*++dst` / `dst++` form below is what the image's
// `stw 0x0(r3)` / `stwu 0x4(r3)` / `stwu 0x4(r3)` / `addi r3, r3, 4` at
// 0x82622F7C-94 lowers from.
```


## `?DrawBatchedNewGfx@DxMultiMesh@@AAAXXZ`

`DxMultiMesh::DrawBatchedNewGfx` -- moved verbatim from `src/system/rnddx9/MultiMesh.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 278-284):

```text
        // w19-c (99.15 -> 100 modulo register permutation): device, buffer
        // and the call result each in a named local before the call, as in
        // DxMesh's own draw path.  With the VertSize()/GetMultimeshFaces()
        // call left inside the argument list MSVC materialises the constant
        // arguments in the wrong order (`li r8, 1` last instead of first, 8
        // rows); with the device fetched inside the argument list it is
        // loaded after the call instead of held across it.
```
