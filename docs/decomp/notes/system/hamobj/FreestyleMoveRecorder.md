# Lane notes: `src/system/hamobj/FreestyleMoveRecorder.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@FreestyleMoveRecorder@@QAAXXZ`

`FreestyleMoveRecorder::Poll` -- moved verbatim from `src/system/hamobj/FreestyleMoveRecorder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 188-195):

```text
            // Open (w25-gu): the image reloads texels into r10 and the take
            // index into r11; ours is the other way round.  In c2's COLOR
            // pass texels (pri 18, tie 61) finds r11 free, and the take-index
            // load is a regasg temp, so it takes r10.  The image fits a take
            // index that is a COLOR candidate (pri >= 18), interfering with
            // texels and coloured first.  No spelling tried makes that load a
            // candidate: named locals are demoted (one block, one def), and
            // declaration order is inert.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.916, 5 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?OnReadCreated@FreestyleMoveRecorder@@CA?AVDataNode@@PAVDataArray@@@Z`

`FreestyleMoveRecorder::OnReadCreated` -- moved verbatim from `src/system/hamobj/FreestyleMoveRecorder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 647-677):

```text
    // RESIDUAL (w8-i, 97.67 canonical / 97.20930 fuzzy): 4 rows of 44, ONE cause,
    // and it is a destination-register choice rather than anything the source
    // says.  The last two statements share one load of sInstance in r11; the
    // image computes the take address with `add r10, r10, r11`, leaving r11 (the
    // sInstance pointer) alive, and then reaches both `lwz r10, 0xb8(r11)` and
    // `stw r10, 0x20(r11)` off it.  We emit `add r11, r10, r11`, which kills the
    // pointer, so MSVC has to reload it from the global -- the one extra `lwz`
    // that makes our body 176 bytes against the target's 172.  Note MSVC does
    // NOT reuse the index either side: 0xb8 is re-loaded after the store on both,
    // so the store really is treated as possibly aliasing the member but not the
    // pointer global.
    // w26-x, from the c2 tap: the image carries THREE versions of the
    // sInstance k07 global (loads at idx 17, 23, 30); our IL carries FOUR --
    // the `stw` through the take pointer re-versions the global, so `RT 41`
    // re-loads it.  That is what makes `RT 39 add` the last use of r11, so the
    // add's destination takes r11 (first free) instead of the image's r10.
    // Measured in w26-x: a `FreestyleMoveRecorder *&inst = sInstance;`
    // reference gives 37/44 rows and a `FreestyleMoveRecorder *inst` copy
    // 37/44 with an added `mr 9, 11` (the pointer then survives the store in
    // r9, which is the right shape, but the copy costs a register and the add
    // takes r11); swapping the two statements is 32/44 (worse).  The open
    // question is what stops the global re-versioning at that store.
    // REFUTED: `FreestyleMoveRecorder *inst = sInstance;` for these two
    // statements only is WORSE (96.7 raw -- it buys a `mr r9, r11` copy and
    // shifts r9->r8), and `FreestyleMove &take = sInstance->mTakes[...];` is
    // exactly inert (the subscript already forms the address).
    // w19-b: an inline member on sInstance doing both stores (name ours) DOES
    // stop the sInstance reload -- 43 of 44 rows equal -- but MSVC then emits
    // a dead `addi r10, r11, 0xb8` (&mCurrentTakeIndex) the image lacks; same
    // canonical score, so not kept.  Variants with a const int& parameter, a
    // FreestyleMove& local, or the second store in its own inline: identical.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?CompareDisplacementVectors@FreestyleMoveRecorder@@ABAXABVVector3@@H0HAAM1@Z`

`FreestyleMoveRecorder::CompareDisplacementVectors` -- moved verbatim from `src/system/hamobj/FreestyleMoveRecorder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 702-709):

```text
    // w25-oa: v1y and v2z only move two fmuls operand lists (c2 commutative
    // key, C2RS-BRIDGE 8.7). As plain v1.y / v2.z the loads are memory leaves
    // on base temps 228 (key 0x10008, below invLen1) and 239 (239 & 3 == 3:
    // key 0x1c008, above invLen2); the image wants n1y load-first and n2z
    // invLen2-first. Through a reference the base is the reference variable,
    // keyed (sid & 7) << 13: v1y is sid 11 (0x16008 > invLen1 0x101e0) and v2z
    // sid 16 (0x10008 < invLen2 0x10280). The declaration positions fix those
    // sids. Measured: v1y declared next to v2z falls back onto temp 228.
```


## `?CompareSkeletonJointDisplacement@FreestyleMoveRecorder@@ABAMPBUFreestyleMoveFrame@@HPBVBaseSkeleton@@AAM@Z`

`FreestyleMoveRecorder::CompareSkeletonJointDisplacement` -- moved verbatim from `src/system/hamobj/FreestyleMoveRecorder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 810-815):

```text
    // Clamped prev-frame index: max(frameIdx - 1, 0).  The constant sits on
    // the LEFT of the compare: `0 < x` keeps the 0 in a register through the
    // branchless mask idiom (`li r10,0` / `subfc r3,r11,r10` / `srwi r10,r10,31`
    // / `subfe r10,r10,r6` at 8252439C..825243D0), whereas `x > 0` lets the
    // backend fold it to `subfic`/`addme` (4 rows, w7-aq).  `Max(x, 0)` is a
    // third shape again (`subi r10,r10,1`).  -- w7-bv, 89.74 -> 100.0
```

Inside the body (was lines 820-826):

```text
    // A plain rotated `for` over size(): the entry test the rotation copies out
    // of the loop condition is `srawi. r9,r9,2; beq` (825243DC) with _M_start
    // loaded before _M_finish, where a hand-written `if (size() != 0)` guard
    // around a do/while is peepholed to `clrrwi.` under every spelling w7-aq
    // tried.  The frame pointers are loop-invariant and get hoisted into the
    // preheader after the beq (825243F4..82524400), which is why they are
    // declared inside the body.  -- w7-bv
```


## `?CalcFrameScore@FreestyleMoveRecorder@@QBAXAAUFreestyleFrameScores@@PBUFreestyleMoveFrame@@HPBVBaseSkeleton@@M@Z`

`FreestyleMoveRecorder::CalcFrameScore` -- moved verbatim from `src/system/hamobj/FreestyleMoveRecorder.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 917-922):

```text
        // w25-gk: a plain indexed for loop over windows.size(). The earlier
        // hand-rotated do-while (count local, explicit guard, cursor) compiled to
        // the same instructions but gave the count a COLOR priority 6 lower, which
        // renumbered r21-r25. Naming CompareSkeletonJointDisplacement(...) *
        // twWeight (one product, same operands) makes it a variable leaf in
        // c2's commutative sort, so outTotalWeight comes first in the fmuls.
```
