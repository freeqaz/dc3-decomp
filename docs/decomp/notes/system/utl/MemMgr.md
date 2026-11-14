# Lane notes: `src/system/utl/MemMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 104-114; nearest following definition: `operator new`):

```text
// RESOLVED (w13-d): the "anchor + displacement" rows that earlier lanes
// (w8-g, w9-d) measured as an MSVC-internal floor in MemPushHeap, MemPopHeap,
// MemPushTemp, MemPopTemp, AddHeap, MemAllocSize, MemFindHeap, MemFree,
// MemTruncate and MemPrintOverview were a LINKAGE question, not codegen: MSVC
// only reaches one global from another by a displacement when both have
// internal linkage, and gNumHeaps / gHeaps / gNewOperatorAlign had been
// declared extern on the strength of hand-authored mangled names in
// symbols.txt.  Making them static (and filling the three unknown ints above
// so the displacements are the image's) took seven of those rows to 100.
// The earlier padding experiment moved nothing only because the globals were
// still external then.
```


## `?MemTruncate@@YAPAXPAXHPBDH1@Z`

`MemTruncate` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 252-291):

```text
// w-memtrunc (2026-11-10): 97.26667 canonical (name_check, objdiff 4.2.9), 76
// rows, equal 73 / diff_arg 1 / insert 1 / delete 1 -- and REFUTED at the
// source level.  The three rows are ONE scheduler slot, not a missing or extra
// statement: the image's preheader is `srawi r26, r10, 2` (allocSize) BEFORE
// the `li r29, 0` / `li r28, 0` pair (truncated = nullptr, i = 0), while MSVC
// sinks our srawi one slot later, between the two `li`s.  The DAG is identical
// on both sides; nothing is dropped or added.
//
// Mechanism: the `nullptr` init is always hoisted to the top of the block, so
// no declaration order or init spelling moves it -- measured inert (all still
// 97.26667, 73/1/1/1): i60 moved last; allocSize declared first; `int i = 0;`
// at its declaration with `for (; i < gNumHeaps; i++)`; truncated's zero as a
// statement instead of a declaration initializer.
//
// Spellings with measured counts (equal / diff_arg / replace / insert / delete):
//   baseline                                             97.26667  73/1/0/1/1
//   (size + 3) / 4                                       98.60000  74/1/0/1/0
//       -- srawi lands in the image's slot, but MSVC emits it as
//          `srawi r11 + addze r26` (round toward zero).  The image has a bare
//          srawi, so the original is `>> 2`; also a real semantic change for
//          negative size.  Rejected.
//   (size + 3) >> 2 inlined at both use sites (no local)  88.53333  60/11/1/5/3
//       -- LICM puts addi+srawi inside the loop; 80 instrs vs 300 bytes.
//   int sizeBytes = size + 3; int allocSize = sizeBytes >> 2;
//                                                       100.00000  71/4/0/0/0
//       -- the insert/delete pair is GONE: all 75 of the image's instructions
//          align, in order, same opcodes.  Residual is a pure r10<->r11
//          recolour of the four address-materialization instructions
//          (lis r11, addi r10 r25+3, addi r30 r11+gHeaps@l, srawi r26 r10 2).
//          NOT LANDED: `equal` falls 73 -> 71, which is the false-gain shape
//          even though every other count improves.  Invariant under 8 further
//          orderings/types of the same two-temp form (truncated first, i60
//          last, temps last, `long`, scoped block, `3 + size`, comma decl,
//          split decl) -- all exactly 100.00000 / 71/4/0/0/0.
//   dead-init probe `int allocSize = 0; allocSize = (size+3) >> 2;`  folded back
//                                                        97.26667  73/1/0/1/1
//
// So: the image's preheader order is reachable, but only by spending one extra
// live vreg, and that vreg flips the r10/r11 parity.  Both halves are colouring
// ties; the source cannot have the ordering without the recolour.  Reverted.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.733, 4 non-equal row(s): NOT A BUG.
Every row is register-only; `scripts/analysis/regperm_equivalence.py` raised 1 lead(s),
each read by hand against the listing: pure register permutation, no operand swap.
(CALLARG? leads read by hand: MemHeap::Truncate takes r3-r6; the r10 lead is a dead
leftover.)

## `?MemFindAddrHeap@@YAHPAX@Z`

`MemFindAddrHeap` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 330-335):

```text
// w8-g: 87.29% (normalized, full ninja). The residue is address materialization:
// the target loads &gHeaps once and reads gNumHeaps as a displacement off that same
// base (gHeaps is +0x950, gNumHeaps +0xbe4 in .bss -- delta 0x294), while we
// materialize both. REFUTED: rewriting the loop with a walking `MemHeap *heap`
// (the style used at lines 210-211 of this file) does NOT produce the shared
// anchor -- measured 71.46%, a 15.8pp regression. Reverted.
```


## `?MemAlloc@@YAPAXHPBDH0H@Z`

`MemAlloc` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 498-527):

```text
        // w7-bu (2026-09-15): 92.8 -> 94.3 canonical. The pop loop is a
        // do/while whose CONDITION is the AllowTemp test and whose depth
        // check is a top-of-body break: the image's only back-edge is the
        // conditional `mr. r11, r10 / beq .L_827CC1AC` at 827CC1FC-827CC200,
        // jumping to the top `cmpwi cr6, r9, 0x0 / ble exit` (827CC1AC), and
        // there is NO reload of 0x40(r30) at the bottom. The former
        // `while (mSize > 0) { ...; if (AllowTemp()) break; }` gave a
        // rotated loop with two bottom tests (`cmplwi r9,0 / bne exit /
        // lwz 0x40(r30) / cmpwi / bgt body`), 4 rows; `for (;;)` with a top
        // break measured identical to it (92.8), and a loop-carried
        // `curSize = --mSize` local regressed to 92.1.
        // The earlier NEGATIVE RESULT (a hoisted `int curSize` local, 92.9
        // either way) was measured against that rotated shape and is
        // superseded.
        // RESIDUAL (94.3, 131 rows): (1) the image saves r15-r31, we r16-r31
        // -- it re-masks `temp` into a FRESH register at the test
        // (`clrlwi. r27, r28, 24`, 827CC178) and reads r27 for the
        // `temp ? kLastFit` select (827CC358); we mask r28 in place. A second
        // bool copy (`bool useTemp = temp`) is copy-propagated away (94.3,
        // same rows). Every low callee-saved register is off by one from
        // that. (2) r29/r30 heap vs allocated_mem are flipped, and because
        // the image's temp-path result lands in r29 (heap's register, dead
        // there) while the common exit returns r30, it emits the
        // CritSecTracker dtor TWICE (827CC278-827CC28C: `cmplwi cr6, r15 /
        // beq / mr r3, r15 / bl Exit / mr r3, r29 / b .L_827CC44C`); ours
        // tail-merges into the single exit at 827CC438. Declaring
        // `allocated_mem` above `heap` does not flip them (94.3, same rows).
        // (3) The .bss anchor rows (gTinyHeapReady at -0x14(r26), gHeaps at
        // -0x294(r26): idx 53, 69-72, 86-95, 141) -- see the note at the top
        // of this file.
```


## `?MemInit@@YAXXZ`

`MemInit` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 628-640):

```text
    // RESOLVED (w7-bu, 2026-09-15, 97.2 -> 98.4 canonical): the byte-slot
    // permutation w7-r recorded (0x54..0x58 in a different order, refuted as a
    // declaration-order lever) was not a slot-order problem at all -- it was
    // two missing statements. The image stores disableMgr AGAIN after the heap
    // block (`stb r18, 0x54(r31)` at 827CD488, a dead store to an
    // address-taken local), which extends disableMgr's live range past the
    // block and is what puts it at 0x54; and `sDefaultHeap = 0` sits INSIDE
    // the `!disableMgr` arm (see below). With both, every slot lands.
    // RESOLVED (w13-d): the remaining rows were the .bss anchor+displacement
    // pattern -- the image reaches gCheckConsistency / gNumHeaps /
    // gTinyHeapReady as +0x17 / +0x13 / -0x1 off r21 = &gInitted (827CD308,
    // 827CD330, 827CD3B8).  Closed by giving those globals internal linkage
    // and the image's layout (see the declarations at the top of the file).
```

Inside the body (was lines 702-710):

```text
        // RETRACTS w7-bu (2026-09-15), which read the image here as passing an
        // UNDEFINED 8th argument ("it sets r6..r9 to 0 and never writes r10, so
        // in the shipped game allowTemp is whatever byte that was").  There is
        // no 8th argument and nothing is undefined: AddHeap takes SEVEN
        // parameters (see the proof above its definition), so r6..r9 at
        // 0x827CD390..0x827CD3A0 are exactly `false, (Strategy)0, 0, false` --
        // a complete argument list.  The three spellings w7-bu measured for a
        // phantom r10 (`bool t;`, an uninitialised int, a literal) were all
        // answers to a question that does not exist.
```


## `?MemPushHeap@@YAXH@Z`

`MemPushHeap` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 835-846):

```text
// RESIDUAL (w14-l, 99.00): 4 rows, all one ANCHOR PICK.  Both globals are
// static now, so MSVC does co-address them -- but the image anchors r30 on
// &gInitted (lbz gInitted@l(r11), then gNumHeaps as 0x13(r30) and the
// MakeString arg as `addi r5, r30, 0x13`), while we anchor on &gNumHeaps
// (`lbz -0x13(r30)`, `lwz 0x0(r30)`, `mr r5, r30`).  Not linkage: MemFindHeap
// and MemInit pick gInitted in both builds.  Tried: `if (gInitted &&
// gNumHeaps > 0)` without the bool local -> 91.6 (loses the materialised
// bool).  Same family as the gRev/gAltRev anchor pick (RhythmBattlePlayer.cpp)
// and System.cpp's gUsingCD/gSystemConfig pair.
// w18-d (99.00): reading gNumHeaps through MemNumHeaps() at both sites is
// inert -- the anchor still lands on gNumHeaps (unlike System.cpp, where routing
// the gUsingCD WRITES through SetUsingCD() moved it).
```


## `?ThreadMemStack@@YAAAVMemHeapStack@@_N@Z`

`ThreadMemStack` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 964-1011):

```text
    // MEASURED NEGATIVE (w9-d, 2026-09-30): 98.63946 -> 98.60545, reverted.
    //
    // The image's write-back is genuinely CONDITIONAL and this spelling is not:
    // two paths branch straight past `.L_827CB878` (`stw r30, 0xbd4(r29)`) to
    // the `addi r11, r29, 0x48` at .L_827CB87C -- the gNumThreads == 0 arm
    // (`b .L_827CB87C` at 0x827CB6B4) and the cache hit (`beq cr6, .L_827CB87C`
    // at 0x827CB6D4, after `cmplw cr6, r11, r3` compares gThreadIds[idx]
    // against the current thread id).  Only 0x827CB734 and 0x827CB824 reach the
    // store.  Both skipped paths had just READ idx out of
    // gThreadBufCurrentIndex, so the store is a self-assignment there and the
    // VALUE is identical either way; the difference is that an unconditional
    // store cannot be branched over, so MSVC hoists the `addi` above it and we
    // carry one extra instruction (base 588 B, 1 insert + 1 delete).
    //
    // Moving the store inside the `gThreadIds[idx] != currentThreadId` block
    // and returning `gThreadBuf[idx]` DOES fix that ordering exactly -- and
    // costs two instructions elsewhere, for a net loss.  With the two return
    // paths no longer separated by the store, MSVC cross-jumps the two
    // CritSecTracker `bl Exit` sites (0x827CB724 and 0x827CB894 in the image)
    // into one and lets `return gNullMemStack` fall into the shared
    // `mr r3, r30`: base drops to 580 B with 1 diff_op (`bl` vs `b`) and 2
    // deletes.  That merge is only available because our r30 holds the .bss
    // anchor where the image's holds idx -- the r29<->r30 swap this function
    // carries -- and `add r30, r10, r11` then kills the anchor.  Declaring
    // `idx` after the CritSecTracker to shift that allocation is BYTE-IDENTICAL
    // (measured), as docs/decomp/patterns/fixable-declarations.md says decl
    // reorder is for stack slots, not register-only swaps.
    //
    // OPEN (w27-b, 2026-11-02), and it is ONE colour decision, not a floor.
    // The c2 candidate dump puts the .bss anchor first among the nonvolatiles
    // and idx third:
    //   id 35  sym 29/k03  pri 30  tie 13   ben 29 ref 17  -> r30   (.bss anchor)
    //   id 3   sym  7/k04  pri 25  tie 143  ben 10 ref 10  -> r29   (idx)
    //   id 8   sym 11/k04  pri 20  tie 86   ben  9 ref  9  -> r29
    // The selector takes the first register in list order once costs tie, so
    // whichever pops first takes r30.  The image has idx in r30 and the anchor
    // in r29, i.e. idx popped first: either its priority reaches 30 (then tie
    // 143 > 13 decides it) or the anchor drops below 25.  Priority is
    // refs-in-block x block pressure summed over blocks, so the lever is one
    // more or one fewer machine reference to one of the two in a block of the
    // right weight -- nothing in the source order moves it.
    // Measured inert this round (no change to any of the 33 swap rows):
    // `return gThreadBuf[idx];`, dropping the named `result`, and the image's
    // conditional write-back (store moved to the end of the
    // `gThreadIds[...] != currentThreadId` block) -- the last re-confirms w9-d
    // and now reads identical rather than net-negative.  Replacing the named
    // `currentThreadId` with a second GetCurrentThreadId() call costs
    // 98.64 -> 95.92.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual:
gNullMemStack/gThreadIds named lbl_ on target side; one addi in a different register.


## `?MemPrintOverview@@YAXHQAD@Z`

`MemPrintOverview` -- moved verbatim from `src/system/utl/MemMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 1069-1087):

```text
// w20-d: 100 normalized, fuzzy 99.06: p/i trade r30/r31 as a consistent
// bijection (21 diff_arg rows); every MakeString/MemFreeBlockStats slot,
// compare and branch checked against the image.  Fuzzy also charges the target's
// unnamed lbl_82F189CC for sMinPhysFree (no map name).  Inert: hoisting `i`.
//
// w26-f: the residual is ONE bijection -- the cursor `p` takes r31 and the
// sMinPhysFree address temp takes r30, where the image has `p` in r30
// (`mr r30, 4` at 0x14) and the address temp in r31 (`lis r31, 0` at 0x38).
// c2rs tap: `p` is pri 17 tie 5; the address temp is pri 15 tie 0.  The
// image's order therefore needs the temp +3 or `p` -2, and priority is
// refs x block pressure over the PRE-colouring candidate set.  Measured inert
// with nsearch under report.json's ruler (canonical held at 100.0 on every
// variant): inlining `name`, inlining either `str`, inlining both, inlining
// `usage`, inlining the two physical-branch KB locals, folding the four loop
// KB locals into the MakeString call, collapsing either or both `strlen`
// temporaries, `&buf[0]` for the cursor, and 2x2 pairs of those.  A volatile
// cursor (control) does move the bytes, so the harness is live.  Every
// spelling that preserves the instruction sequence preserves the candidate
// set, and the candidate set is what sets priority.
```
