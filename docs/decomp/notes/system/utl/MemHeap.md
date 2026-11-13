# Lane notes: `src/system/utl/MemHeap.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Print@MemHeap@@QAAXAAVTextStream@@_N@Z`

`MemHeap::Print` -- moved verbatim from `src/system/utl/MemHeap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 96-106):

```text
    // w17-d: 96.601 -> 100 (modulo register permutation). The image reads
    // mSizeWords (0xc) then mStart (0x4) AFTER the MakeString("\n") write:
    // `curPtr = mStart` sits below that write AND endPtr re-reads the member
    // as `mSizeWords + mStart` (CSE'd with curPtr's load). w7-aq sank only
    // the curPtr assignment, keeping `endPtr = curPtr + mSizeWords` (89.8).
    // curPtr is `int *`, not `unsigned int *`: MakeString takes every argument
    // by const reference, so the image passes `&curPtr` itself (`addi r4, r1,
    // 0x50`, idx 111) and therefore keeps the variable live in slot 0x50 --
    // written on loop entry (idx 72) and after every increment (idx 152).  A
    // `(int *)curPtr` cast at the call site materialises a *temporary* instead,
    // which is why our build spilled once, just before the call.
```


## `?Init@MemHeap@@QAAXPBDHPAHH_NW4Strategy@1@H2@Z`

`MemHeap::Init` -- moved verbatim from `src/system/utl/MemHeap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 223-277):

```text
    // RESOLVED (w7-bn, 2026-09-15): 83.0 -> 100.0 canonical, 78 rows all
    // equal, 312 B both sides.  The lever is the OPACITY of the first
    // mStart write, not a read-back.  `int **pStart = &mStart; *pStart =
    // start;` is a store the early optimizer will not equate with
    // this->mStart, so (a) the later direct `mStart = alignedStart` cannot
    // dead-store-eliminate it (827F87BC survives), (b) the adjacent direct
    // read `int *rawStart = mStart;` stays a real load through the early
    // passes and is forwarded LATE by the machine-level stw->lwz peephole,
    // which keeps lwz's zero-extension -- that is the `clrrwi r10, r30, 0`
    // at 827F87C0 -- and (c) the stack-argument load `lbz r8, 0xf7(r1)`
    // (827F87C4) cannot hoist above an opaque store, which is what pins the
    // whole store window (827F87BC-827F8810) into the image's order.
    // Measured on the way (each with run_objdiff in this worktree):
    //   direct store + read through *pStart, adjacent          89.7 (clrrwi
    //     present, both sides 312 B, but lbz hoisted above the store);
    //   *pStart store + *pStart read, adjacent                  89.0 (read
    //     forwarded early, no clrrwi, dead `addi r11, r31, 0x4`);
    //   *pStart for both stores and the read, 3 stores between 66.4 (read
    //     stays a real lwz);
    //   direct store + direct read + aligned store via *pStart  74.1 (read
    //     forwarded early as bare r30);
    //   mAllowTemp moved between the stores; `*(unsigned int *)&mStart`
    //     read; (unsigned long long) casts on the subtraction; reading
    //     (mStart - rawStart) after the aligned store: all byte-identical
    //     to their parent spelling.
    // The paragraphs below are the history that led here; they are kept
    // because every negative in them is still true of a DIRECT read-back.
    // RESIDUAL (w7-aq, 83.013 canonical): the image writes mStart TWICE --
    // 827F87BC stores the raw `start`, 827F87E8 overwrites it with the
    // 16-byte-aligned pointer -- and also carries a `clrrwi r10, r30, 0` copy
    // of `start` (827F87C0).  Our build dead-store-eliminates the first write,
    // and the two missing instructions drag the whole store-scheduling window
    // (idx 27-56) out of alignment; the rest of the function is exact.
    // REFUTED (w7-aq): `auto &ref = mStart` around both writes (the spelling
    // that was here before), computing the aligned pointer from `mStart`
    // rather than from `start`, and routing it through an `int *rawStart =
    // mStart;` local -- all three still DSE the first store, all three read
    // 83.013 to five decimals.
    // NEGATIVE RESULT (w7-bi, 2026-09-14): a fourth spelling, aimed at the
    // OTHER missing instruction.  `clrrwi r10, r30, 0` (827F87C0) is a
    // zero-extended COPY of `start`, and its only consumer is the mSizeWords
    // subtraction at 827F87EC -- so it is the SIZE, not the alignment, that
    // the image computes from a value which went through a 32-bit conversion.
    // Routing only that subtraction through a read-back (`int *rawStart =
    // mStart;` placed after `mStrategy` and before the aligned store, with the
    // alignment still taken from `start` so 827F87B8 `subi r11, r30, 0x4`
    // keeps pairing) is ALSO dead-store-eliminated: identical 83.0 canonical,
    // identical 83-row table, same 2-instruction deficit (312 B target vs
    // 304 B base).  MSVC forwards the store to the read and then removes it,
    // so no read-back spelling can keep it alive.  The entire residual is
    // those 2 absent instructions plus the member-store reshuffle they cause
    // (idx 27-56); idx 0-26 and 57-82 are exact on both sides.
    // Opaque first write + direct read-back: see the RESOLVED note above.
    // Do NOT "simplify" to `mStart = start;` -- that spelling is DSE'd and
    // reads 83.0.
```


## `?TryAlloc@MemHeap@@QAAPAHHHAAH@Z`

`MemHeap::TryAlloc` -- moved verbatim from `src/system/utl/MemHeap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 421-444):

```text
// w7-bu (2026-09-15): 90.007 -> 93.3 canonical.  Three of w7-aq's four
// residual rows were spellings, not allocation: the default-arm tail merge
// (block starts null, default arm `break`s), the split header built as a
// register FreeBlock temp and stored whole (loads-before-stores + the
// forwarded-reload `clrrwi r8, r11, 0` at 827F89B0), and `totalUsed =
// blockSize` at the top of its arm (`mr r27, r28` at 827F8A08).  See the
// in-body comments for the measurements.
// RESIDUAL (w7-bu, 93.3 canonical, 45 rows): one register plan.  The image
// puts `this` in r26 and sizeWords in r27 (827F88A4/827F88AC), we get the
// pair the other way round (15 rows); it loads info.mBlock straight into r31
// and copies the OLD block to r29 inside the split arm just before the
// in-place `stwux r28, r31, r10` (827F89A0/827F89A8), we keep the loaded
// value as the survivor and copy the advanced pointer at the top (`mr r31,
// r30`, +`add`/`stwx` instead of `stwux`); it keeps padWords in r30 and
// copies it into r5 for the call (827F89A4), we load into r5 and copy the
// survivor out.  Measured inert: `sizeWords += padWords` in place of the
// totalUsed local (byte-identical), swapping the two header-load
// declarations (byte-identical), a `newBlock` local with `block = newBlock`
// after the call (identical rows), reassigning prevBlock before the call
// through an oldPrev temp (88.8).  The remaining 0x4/0x8 load+store order
// (image next@0x8 first, ours ascending) rides on the same stwux plan: with
// the base register updated in place the scheduler orders the two stores
// after it differently.  Declaration order is not a lever -- both swapped
// values are parameters.
```

Inside the body (was lines 480-519):

```text
        // The split block's header is built in a register-held FreeBlock
        // temp and stored as a WHOLE STRUCT, not field by field: that is the
        // only spelling that (a) hoists both header loads above every store
        // (827F8990/827F899C precede the `stwux` at 827F89A8) and (b) makes
        // the timestamp argument a store-forwarded reload -- the
        // `clrrwi r8, r11, 0` at 827F89B0 is MSVC zero-extending a value it
        // forwarded out of the struct store, which a plain `timeStamp` local
        // never needs (w7-bu, measured: locals 90.0, temp+struct store 93.3).
        // A memory-copied temp (`saved = *block`) keeps a dead load of the
        // old mSizeWords and spills it (91.4); assigning the three fields is
        // what keeps the temp in registers.
        // w26-l (2026-11-02): 89 combinations of the remaining free spellings
        // here (null-check form; `block += padWords`; a named `newBlock`; a
        // direct `*(FreeBlock*)block = saved`; blockSize hoisted to an
        // initialiser or removed from its declaration; a marker comment after
        // the kFirstFit arm) are all inert or worse -- the best is the current
        // body. What they cannot move is the ROOT, which the colour tap names
        // directly: `block` is TWO candidates here (id 7 sym8/k04 pri 67 ->
        // r30, and id 10 sym11/k04 pri 94 ref 16 -> r31, i.e. the extra
        // `mr r31, r30` that follows our `lwz r30, 0x60(r1)` of info.mBlock
        // and has no counterpart in the image), and that second version takes
        // the top nonvolatile rung. With it present every later rung shifts, which is
        // why `this`/`sizeWords` (ids 3 and 6, pri 25 and 13) come out r27/r26
        // against the image's r26/r27 and blockSize lands in r29 instead of
        // r28. The same extra version is why the store is `add` + `stwx`
        // rather than the image's single `stwux`. Collapsing `block` to one
        // live range is the lever; nothing above does it, and the obvious
        // guess -- ONE `block = info.mBlock;` after the switch instead of one
        // per arm, which is legal because info.mBlock starts null and the
        // default arm runs no fit -- is REFUTED: 91.6 -> 87.6 (151 rows), so
        // the image has the four-way phi and the extra version is not it.
        // Nor does any spelling of the update itself fuse the store: an
        // `int*`-walk `words += padWords`, a `char*` + padWords*4, a named
        // `newBlock`, a direct `*(FreeBlock *)((int *)block + padWords) = saved`
        // and a reference alias are all byte-identical to the current `add` +
        // `stwx` pair (13 more combinations, none better than 91.6).
        // The two orderings below are load-bearing, not stylistic: next-before-
        // time and padWords-before-prevBlock each mirror an ordering in the
        // image's own sequence (w45-b: canonical unmoved at 93.25874, fuzzy
        // 91.68532 -> 91.82517; every other field order is 91.75524 or worse).
```


## `?AttemptMerge@FreeBlock@@QAA_NPAV1@H@Z`

`FreeBlock::AttemptMerge` -- moved verbatim from `src/system/utl/MemHeap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 624-629):

```text
// w19-d: fuzzy 98.125 -> 100 (byte-identical).  Two levers: the timestamp is
// Max() (the image loads next's stamp first and keeps the result in r10, the
// inline's `x < y ? y : x` shape), and the size is summed from a `thisSize`
// local read before the adjacency test (RB3's spelling), which fixes the add's
// operand order.  The hand-rotated do/while below is still needed: a plain
// `while` costs 26 points here.
```
