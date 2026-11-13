# Lane notes: `src/system/rnddx9/Rnd.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawLargeQuad@DxRnd@@UAAXABULargeQuadRenderData@@ABVTransform@@PAVRndMat@@W4ShaderType@@@Z`

`DxRnd::DrawLargeQuad` -- moved verbatim from `src/system/rnddx9/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 181-203):

```text
    // CLOSED (w26-f7, fuzzy 99.357796 -> 100.0, canonical 100.0).  The residual
    // was ONE move, not a 3-cycle: the six callee-saved GPRs held, in the
    // image, r26 = TheShaderMgr's address half, r25 = tf, r24 = s, r23 = the
    // BaseMaterial RTTI, r22 = the RndMat RTTI, r21 = the vtable-pointer load
    // `lwz r21,0x0(r29)`; ours had the same six in the same order except that
    // the vtable-pointer load popped FIRST and took r26, pushing the other
    // five down one.  Tap (`c2rs stage cands --regasg`): the vtable load is
    // id10 pri26 code#32 and TheShaderMgr's address half id11 pri9 code#31, so
    // the vtable load had to stop outranking every other one of the six.
    //
    // The fix is a reference binding, not a reshape -- a reshape of the
    // loop/declaration block costs more than the cycle is worth (measured
    // 96.83-97.52, w26-f2).  Binding the GLOBAL OBJECT as `RndShaderMgr &mgr`
    // emits the identical instruction stream (c1 folds the binding away) but
    // the vtable load is no longer the same IL temp: it drops to the bottom of
    // the pop order, and the five behind it each move up exactly one register.
    // Naming it as a value or a matrix temporary instead costs 6 points
    // (`Hmx::Matrix4 mtx(tf)` 93.30) because it moves the ctor ahead of the
    // call setup.  Note this refutes the "global reference is inert" negative
    // for a global OBJECT: it is a global *pointer* binding that folds.
    //
    // w26-f2 had the mapping one param off (r25 is tf and r27 is mat); the
    // register set and the reversed priority relation were both right.
```


## `?CreateLargeQuad@DxRnd@@UAAXHHAAULargeQuadRenderData@@@Z`

`DxRnd::CreateLargeQuad` -- moved verbatim from `src/system/rnddx9/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 227-239):

```text
// CLOSED (w26-f8, fuzzy 99.76744 -> 100.0, canonical 100.0).  w20-m's note was
// right about the symptom: the index loop's two strength-reduced row offsets
// landed the other way round (ours r8 = y*w1 / r7 = y*width, image r7 = y*w1 /
// r8 = y*width), and because the tail emits the three row updates in register
// order that also rotated them.  The two offsets are COLOR candidates at equal
// priority (tap: id31 pri22 tie89 -> r8, id30 pri22 tie88 -> r7), so the tie
// key decided and the two were one step apart.  The image computes the VALUE
// row offset once per row, in the outer loop: `unsigned int rowV = y * width;`
// hoisted before the x loop.  That puts its definition where the image's is,
// which flips the tie and the update order together.  Naming the same value
// INSIDE the x loop instead costs 4 points (95.95) -- the position is the
// lever, not the name.  Tried in w20-m (inert or worse): x-first operand
// order, an `int base` temp, an int index instead of a pointer (94.9).
```


## `?PushClipPlanesInternal@DxRnd@@UAAXAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z`

`DxRnd::PushClipPlanesInternal` -- moved verbatim from `src/system/rnddx9/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 321-335):

```text
// w18-c: 86.44 -> one row (a commutative `add` in the node address, r28+r11 in
// the image).  The clip plane is a Plane built from the transform's position and
// its negated z axis (`Scale(m.z, -1.0f, n)`, the image's three `fmuls .., f31`
// with f31 = -1.0f): Plane's ctor stores a,b,c before d, which is the image's
// store order; the Vector4 copy goes through Multiply and back with Set().
// Reading planes[i] twice instead of through `trans`: 99.4, worse.
// w26-f10 stop note, fuzzy 99.901 / canonical 100.0, one row left: idx 32,
// `add r11,r28,r11` (image) vs `add r11,r11,r28` (ours) -- the address of
// `planes[i]`, i.e. stlport's `_M_start + i` inside vector::operator[] in the
// PCH-reached header, with r28 the strength-reduced `i * 0x14` induction value.
// Both operands are leaves in c2's commutative sort (8.7), one a load temp for
// _M_start and one the loop's byte offset; the source reaches neither, and the
// only way to re-key them is to edit stlport, which is PCH-reached and would
// move all 574 PCH TUs.  The other row (idx 64, the inlined Plane ctor's
// fnmadds) is fixed below by binding `normal`.
```


## `?D3DFormatForBitmap@DxRnd@@QAA?AW4_D3DFORMAT@@ABVRndBitmap@@@Z`

`DxRnd::D3DFormatForBitmap` -- moved verbatim from `src/system/rnddx9/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 389-426):

```text
// 100.0% (w7-aq, 2026-09-14).  Two structural facts, both read off the target
// listing rather than guessed:
//
// 1. D3DFMT_UNKNOWN is -1, not 0xff (fixed in src/xdk/d3d9i/d3d9types.h).  The
//    image initialises the result with `li r30, -0x1`, and it emits the
//    `fmt != D3DFMT_UNKNOWN` fail block with NO comparison in front of it --
//    which only happens if the condition folds to compile-time FALSE on a path
//    where the result still holds its initialiser.  With 0xff we emitted a
//    spurious `cmpwi r31, 0xff; bne`, an extra callee-saved GPR and
//    `bl __savegprlr_29` instead of the inline std/stwu prologue.
//
// 2. There is ONE MILO_ASSERT, after the if/else -- not one per default arm.
//    MSVC constant-folds the check per predecessor: on the four dxt cases and
//    the four bpp cases `fmt` is known non-negative so the check is true and
//    the case bodies branch straight to the epilogue (`b 0xea0`, target idx
//    96-107); on the two default paths `fmt` is still -1 so the check is false
//    and they fall into the fail block.  The compare itself disappears.  That
//    leaves a SINGLE addressable line-number temp, which is what lets the two
//    default arms tail-merge: the bpp arm sets up `addi r4, r1, 0x54` and then
//    `b 0xe4c` straight into the dxt arm's `bl MakeString<_D3DFORMAT>`.
//    Writing MILO_ASSERT inside each `default:` gives TWO temps, coloured
//    independently at 0x50 (reusing `order`'s home) and 0x54 (reusing `bpp`'s);
//    the tails then differ by that one stack slot, MSVC refuses to cross-jump,
//    and it duplicates 45 instructions -- 380 bytes vs the target's 340, 4.4%.
//
// NEGATIVE RESULTS recorded so they are not re-run:
//   - Declaring `order`/`bpp` as `int` with the casts moved to the MILO_FAIL
//     call sites: 16.4%.  The `const D3DFORMAT&` parameter is what forces both
//     values into memory at 0x50/0x54 before the first branch; as ints they
//     stay in registers and the two homing stores vanish.
//   - "our assert expression text is wrong, find the 34-char one" is REFUTED.
//     The target's MakeString instantiation names char[19]/char[5] where ours
//     names char[8]/char[22], but every MakeString<char[N], int, char[M]> in
//     the binary folds to one body (icf_aliases.map) and the literal both
//     sides reference is ??_C@_0BG@PPIAGPFI@fmt?5?$CB?$DN?5D3DFMT_UNKNOWN?$AA@
//     -- _0BG = 22 = "fmt != D3DFMT_UNKNOWN" + NUL.  The text is correct.
//   - The `return`-per-case shape (as in ../og-dc3-decomp) is 0.2%: it
//     dissolves the r30 result register the image keeps across both Fail calls.
```
