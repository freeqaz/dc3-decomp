# Lane notes: `src/system/rndobj/Bitmap.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Create@RndBitmap@@QAAXPAX@Z`

`RndBitmap::Create` -- moved verbatim from `src/system/rndobj/Bitmap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 255-260):

```text
        // w25-rc: `pixels` is seeded here, before `pixbytes` exists, so its
        // colour candidate is minted first (id 6, pixbytes id 7).  The image
        // emits both pointer adds with pixbytes as the first operand
        // (`add r25, r28, r11` / `add r25, r28, r25`); with `pixels` first
        // introduced as `mPixels + pixbytes` the ids were reversed and so
        // were both operand orders.
```


## `?PixelOffset@RndBitmap@@ABAHHHAA_N@Z`

`RndBitmap::PixelOffset` -- moved verbatim from `src/system/rndobj/Bitmap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 294-360):

```text
// w7-bt (83.17 -> 92.29 canonical): the two table selects are spelled with the
// SUBSCRIPT INSIDE EACH ARM, `if (odd) v = t13[idx]; else v = t02[idx];`, not
// as one subscript on a selected pointer.  MSVC hoists the subscript, which is
// very busy in both arms, ahead of the `beq` and cross-jumps the two `lbzx`
// into one after the join -- which is exactly the image's shape: in the 8bpp
// arm the whole `(y % 4) * 0x10 + (x % 16)` sits between the `clrlwi.` at
// 0x8267183C and the `beq .L_82671878` at 0x82671868, and the hoisted block is
// scheduled as a unit after returnBase rather than interleaved with it (the
// w7-am named-local spelling, 81.3, interleaved it).  With a single shared
// subscript the compiler instead sinks the index past the join (83.2).  The
// same spelling closes the nibble arm's 3 inserts / 3 deletes around
// 0x82671958..0x82671990 (89.05 -> 92.29): with `lookupIdx2` as a named local
// the index is scheduled as a peer of tiledBase; hoisted, it follows it.
// The arms must assign an `int` local: the ternary form of the same two arms
// (any table type, `char` or `unsigned char`, cast per arm or outside) leaves
// a `clrlwi r11, r11, 24` after the merged lbzx (88.56), because the widening
// is applied to the phi instead of inside each arm where it folds into the
// load.  Statement order inside the compressed arm is inert (nibble store
// moved before blockWidth, rowOffset moved before offsetMod: byte-identical).
// w21-bh: 92.29 -> 99.02.  The w7-bt "compressed arm schedule" residual was
// two source facts, both value-identical: (1) `width` is an int local, not an
// unsigned short, and `nibble = x & 1` comes after pixelScale -- that puts
// the image's `stb` at 0x82671A10 and its `divw r30, r5, r10` (y / blockSize)
// in place; (2) `x % blockWidth` is added at the END of the tiledBaseOffset
// expression rather than computed up front into a local -- the image's five
// divide-overflow traps run width/bw, y/bs, x/bw, y/bs, x/bw (0x82671AB8..
// 0x82671AF4), i.e. x % blockWidth is evaluated last.  (2) also cleared every
// r3/r7/r31 swap in the tiled arm.  The `_ref0`/`_ref3` reference aliases for
// mOrder/mHeight were inert and are gone.
// RESIDUAL (w21-bh, 99.02 canonical, 5 rows): three `mullw` operand orders
// (image width*pixelScale, mBpp*mod, mRowBytes*quotient; we emit each the
// other way round) and the second `divwu r8, r8, r27` two slots late.
// Measured inert in a standalone cl.exe probe: flipping each multiply's source
// operand order (all 8 combinations), unsigned/int variants of width,
// pixelScale, scaledWidth and mBpp, inlining scaledWidth, all 120 orders of
// the blockSize/width/blockWidth/pixelScale/nibble statements (12 tie at
// 99.02, the rest lower), and in the tiled arm all declaration and statement
// orders, `%` spellings and tiledBase operand orders.
// w26-r (98.88 -> 98.93 fuzzy, canonical unchanged at 99.02, 5 rows -> 4):
// `pixelScale` is now minted BEFORE `width`; this is a declaration-order
// change, not a statement-order one.  The c2 commutative sort keys a variable
// leaf 0x10000|sid<<5, orders descending and derives the sid from mint order,
// so `width * pixelScale` emitted pixelScale first while the image has width
// first (`mullw r27, r31, r9`).  Minting pixelScale earlier lowers its sid and
// that multiply now matches.  HYPOTHESIS for the 4 remaining rows (the
// mBpp*mod and mRowBytes*quotient operand orders, and the second `divwu` three
// slots late): the image's `tiledBaseOffset % scaledWidth` and `... /
// scaledWidth` reach the sort as TEMP LEAVES (0x10000|(sid<<6 & 0xffff), i.e.
// below the mBpp/mRowBytes memory leaf at 0x12012), whereas in our build both
// are single-use FOLDED temps and inherit their defining tuple's key
// (>= 0x01020000), which beats every memory leaf.  Every spelling that lifts
// them out of the folded class measured code-identical here (named locals,
// unsigned/int variants, casts, statement order): a single-use local is
// propagated straight back into the operand.  Open question: what keeps the
// two division results live in the image.
// w26-r addendum (measured on a synced scratch tree, baseline reproducing
// report.json exactly at 98.9268/99.0244): re-confirmed inert for ALL FOUR
// remaining rows -- naming `tiledBaseOffset % scaledWidth` and `... /
// scaledWidth` as `int`, `unsigned int` or explicit-cast locals; casting the
// mod to unsigned inside the multiply; writing the quotient multiply as
// `(quotient) * mRowBytes`; and computing `rowOffset` before `offsetMod`.  Every
// one is byte-identical to the current spelling, so the folded-temp class of the
// hypothesis above is not reachable by naming: c2 folds a single-use local back
// into its operand before the §8.7 sort runs, whatever the spelling.
// NEGATIVE RESULT (w7-am, 2026-09-14): writing the subscript on the left,
// `(idx)[cond ? bytes13 : bytes02]`, is byte-identical -- MSVC's operand
// evaluation order here is not reachable from the source spelling.
```


## `?DecodeDxt5Alpha@@YAXPAEHHAAE@Z`

`DecodeDxt5Alpha` -- moved verbatim from `src/system/rndobj/Bitmap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1004-1035):

```text
// w21-bh: 94.56 -> 98.41.  The w7-am "array-init store schedule" residual
// was not about the arrays: `byte` (the byteOffsets lookup) is an `int`, not
// an `unsigned char`.  As a uchar, MSVC coalesced the swizzle copy with it and
// speculated the `swizByte--` arm above the branch (`mr r11, r8; addi r8, r8,
// -1; bne`), which also reshuffled every table-init `stb`; as an int the copy
// stays `mr r10, r11` with both arms branched (image 82671EEC..EF8) and the
// 32 stores land in the image's order.  Value-identical: byte is 0..5.
// RESIDUAL (w21-bh, 98.41 canonical, 1 insert + 1 delete): one `clrlwi rN,
// rN, 24` -- we truncate at the swizzle copy (`mr r10, r11; clrlwi r10`), the
// image truncates `byte` at the head of the bit >= 6 arm instead (before its
// `addi r11, r11, 0x1`).  Measured inert in a standalone cl.exe probe: every
// type combination of byte/swizByte/next/bit over {uchar, int, uint, ushort}
// (int/uint/long byte all 98.41), in-place `byte++` for next, `next = byte;
// next++`, a uchar copy of an int byte, ternary / `-= 1` / test-swizByte
// spellings, inline Swiz helpers (by ref and by value), and all 90 orders of
// the a0/a1/array/lookup declarations (byteOffsets before bitOffsets is
// required; otherwise inert).
//
// w31-c (2026-11-03): DECODE RE-ADJUDICATED against the listing, no bug.  The
// two stack tables are byteOffsets at r1-0x60 (reached via r26, index
// i+(j<<2) from `add r25, r5, r4`) and bitOffsets at r1-0x50 (reached via r10,
// index i off the `slwi r5, r5, 2` bias), so the swizzle is on `byte`
// (`clrlwi. r5, r11, 31` with r11 from the r26 table) and the `>= 6` test is
// on `bit` (`cmplwi cr6, r8, 0x6`, r8 from the r10 table) -- exactly as
// written.  Endpoints: lbz r28,0x1(r3) feeds the code==0 store and
// lbz r27,0x0(r3) the code==1 store, so a0 = uc[1], a1 = uc[0], and
// `cmplw r9,r10` (r9 = a0, r10 = a1) + bgt selects the /7 arm when a0 > a1.
// Both interpolators match term for term and both are `divwu` (unsigned),
// matching the (unsigned int) casts: (6-code)*a0 + (code-1)*a1 + 2 over 5, and
// (8-code)*a0 + (code-1)*a1 + 3 over 7.  The single remaining row is the
// `clrlwi` placement described above, i.e. a same-value truncation (byte is
// 0..5), so this is a placement residual, not a decode error.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?DxtColor@RndBitmap@@ABAXHHAAE000@Z`

`RndBitmap::DxtColor` -- moved verbatim from `src/system/rndobj/Bitmap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1125-1136):

```text
            // BEHAVIOURAL FIX (w12-a): the 4-bit DXT3 alpha is masked to its nibble
            // BEFORE it is replicated -- `srw r11, r11, r10` / `clrlwi r11, r11, 28`
            // / `slwi r10, r11, 4` / `or` (0x826731AC..0x826731B8).  This used to
            // truncate to a byte instead, so the next texel's alpha nibble leaked
            // into the high nibble of `a`.
            // RESIDUAL (92.9 canonical): x/4 and y/4 are each computed twice (no
            // CSE with x%4 / y%4 -- the image recomputes them too), but the image
            // pairs the FIRST x/4 and y/4 with blockIdx and ours pairs them with
            // the remainders.  Inert/worse: naming both quotients, naming only
            // xQuotient, either operand order of the blockIdx add, declaring the
            // remainders before blockIdx (89.7), rb3's declaration order with an
            // inlined DecodeDxt3Alpha (88.5).
```

Added by lane w-low-rndobj, 2026-11-13:

```text
BEHAVIOUR VERDICT: AGREES (92.86 / 92.02, unchanged).  dxt = mOrder(0xc) & 0x38;
block = (mWidth(u16 0x4) >> 2) * (y/4) + x/4 with signed div/mod; DXT1 (8):
block*8, DecodeDxtColor(..., true, r,g,b,a); else block*16, colour from +8 with
a scratch alpha, then DXT3 (0x10) nibble-masked alpha replicated, or
DecodeDxt5Alpha(block, xr, yr, a).
```


## `?LoadDIB@RndBitmap@@QAA_NPAVBinStream@@I@Z`

`RndBitmap::LoadDIB` -- moved verbatim from `src/system/rndobj/Bitmap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1328-1335):

```text
// w16-a (stopped at 97.70 canonical): 23 rows -- an r26<->r27 swap between
// `bs` and paletteBytes (18 rows), plus the `lis Bitmap.cpp` / `clrrwi r25`
// order ahead of MemAlloc (4 rows).  Measured: a plain
// `for (p = pixels; p != pEnd; p++)` nibble-swap loop REMOVES the r26/r27 swap
// but becomes a CTR loop (93.6); `pEnd` declared before `p` with the do-while
// kept is inert; paletteBytes as a ternary also removes the swap but moves
// `li r26,0` into an else arm (96.0); hoisting `int paletteBytes;` to the top
// and `* 4U` are both inert.  The swap is liveness-driven, not declaration-driven.
```

Inside the body (was lines 1374-1390):

```text
    // RESIDUAL (w27-i, 98.16092 canonical / 97.61494 fuzzy): `bs` and
    // `paletteBytes` sit in the other way round from the image -- image
    // `bs`=r27 / paletteBytes=r26, ours `bs`=r26 / paletteBytes=r27 -- and
    // that one pair is 18 of the 23 remaining rows.  Both are pri -3 in
    // COLOR (ben 13/ref 11 for `bs`, ben 8/ref 8 for paletteBytes), so the
    // selector's first key is a tie and the `+0x44` tuple-visit ordinal
    // decides: id 5 (bs) tie 9, id 7 (paletteBytes) tie 66, sorted DESC, so
    // paletteBytes is selected first and takes the highest free register in
    // its interference-allowed set (r14..r27) -> r27; `bs` then gets r26.
    // Only `bs`'s ordinal exceeding paletteBytes' would flip it, and `bs` is
    // a parameter materialised at entry (ordinal 9) against a mid-function
    // definition (66), so no declaration move reaches it.  Refuted on this
    // row, best first: `palette` declared after `buf` 97.701, paletteBytes
    // at the top 97.011, palette at the top 96.552, explicit `else` arm
    // instead of `= 0` 96.431, `unsigned` paletteBytes 96.207, both at the
    // top 95.402, paletteBytes after `pixelBytes` 90.057, after `rowBytes`
    // 88.902.  No value, store or call changes in any of them.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
