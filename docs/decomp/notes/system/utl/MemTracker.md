# Lane notes: `src/system/utl/MemTracker.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0MemTracker@@QAA@HH@Z`

`MemTracker::MemTracker` -- moved verbatim from `src/system/utl/MemTracker.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 160-205):

```text
// RESIDUAL (w7-ai, 93.1%): every remaining row lives in target rows 41-65, the
// inlined AllocInfoVec(y) member init, and all of them are downstream of ONE
// missing instruction pair:
//
//     [57] clrlwi r5, r27, 2      ; r5 = y & 0x3fffffff, i.e. (unsigned)(y*4)/4
//     [65] stw    r5, 0x54(r31)   ; spilled before the first String ctor,
//                                 ; then overwritten with 0 at row 100 -- DEAD
//
// 0x54 is the slot that later holds the `(AllocInfo *)0` const-ref temp for the
// KeylessHash ctor, so MSVC coloured a dead scalar onto it. With that extra
// post-call work absent from our stream, MSVC has nothing to fill the pre-call
// slots with and hoists the `this + 0x18180` anchor ABOVE the DebugHeapAlloc
// call into callee-saved r26 (rows 41/43), where the image recomputes it into
// volatile r8 afterwards (rows 46/51) -- that scheduling difference is the
// other 8 rows. One root cause, not two.
//
// MEASURED NEGATIVE: spelling the division into AllocInfoVec's ctor as
// `mEndOfStorage(mStart + size * sizeof(AllocInfo *) / sizeof(AllocInfo *))`
// is BYTE-IDENTICAL -- MSVC folds the round trip, and it would in any case have
// had to show up in MemTracker::DiffDump's inlined copy of the same ctor, which
// has no clrlwi on either side (checked). So the division is NOT in the shared
// ctor; it is something in this function's own source that we are missing, and
// it must be live across the three String member ctors to get spilled at all.
//
// Also adjudicated and NOT a bug: the MakeString instantiation pair
// (`<const char(&)[19], int, const char(&)[5]>` target vs `<[15], int, [9]>`
// ours) is an ICF fold -- rows 82-85 reference the SAME
// ??_C@_0P@KDMJFNCI@MemTracker?4cpp and ??_C@_08LAKGMMIJ@mHashMem string
// symbols on both sides, so the array-size triple is just the fold
// representative's name.
//
// w26-l (2026-11-02): rows 41-66 are ONE decision, and it is not a spelling of
// this function. The image materialises `this + 0x18180` (mFreedInfos) AFTER
// `bl DebugHeapAlloc` into r8 and keeps nothing across the call; we hoist the
// same `addis/subi` pair ABOVE the call into r26, which is what turns the 3
// vector stores into `stw r3,0x0(r26) / 0x4 / add r7,r28,r3` against the
// image's `stw r3,0x0(r8) / 0x4 / 0x8` and drags the `clrlwi` result from r5
// to r8 (`stw r5,0x54(r31)` vs `stw r8,0x54(r31)`). The stores are inside
// AllocInfoVec's __forceinline ctor (utl/AllocInfo.h, a PCH-reached header),
// so the only free variables here are its arguments; 47 combinations of them
// (y / (unsigned)y / (size_t)y / y&0x3fffffff, int vs unsigned vs size_t
// hashSize, x<<1, `2*...`, `hashSize*4`) are byte-identical to the current
// body. Mechanism: c2's sched0 placement of an address-materialisation pair
// across a call. 122 of 138 rows are already equal and every surviving
// difference is inside that block, so this is a scheduling row, not a colour
// row -- no register assignment anywhere else in the function moves.
```


## `?DiffDump@MemTracker@@QAAXAAVTextStream@@@Z`

`MemTracker::DiffDump` -- moved verbatim from `src/system/utl/MemTracker.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 545-569):

```text
// RESIDUAL at 95.37 canonical (w7-at, 2026-09-14). 110 of 137 instructions are
// equal and the structure is right; what is left is 16 callee-saved register
// renames (r23<->r27, r24<->r28) plus ONE three-instruction reordering at target
// rows 49-59, where the image materialises the DebugHeapAlloc result into a
// callee-saved register and stores THAT to both 0x50 and 0x54 --
//   mr r27,r3 / add r10,r30,r3 / mr r24,r3 / stw r27,0x50 / mr r29,r3 /
//   stw r27,0x54 / mr r3,r11 / lwz r4,0(r11) / stw r10,0x58
// -- so its AllocInfoVec words land in declaration order 0x50, 0x54, 0x58 and
// the image already holds allocIt (r29) and the Free() pointer (r24) before the
// push_back loop even starts. We store r3 straight to 0x50 and emit 0x58 in the
// middle. Same instruction multiset, different schedule.
//
// TWO NEGATIVE RESULTS, both byte-identical output:
//  - hoisting `allocIt`/`allocEnd` above the sorts and passing them AS the sort
//    arguments (so the image's reuse of the sort's argument registers as the
//    loop variables has a source spelling): MSVC already CSEs this.
//  - rewriting AllocInfoVec(int) from a member-init list to three body
//    assignments in declaration order, to force the 0x50/0x54/0x58 store order:
//    MSVC canonicalises the two forms. (AllocInfo.h is PCH-reached, so this was
//    measured with a full ninja; since the objects did not change there is no
//    binary-wide delta to report.)
// Adjudicated and NOT a bug: the "alloc"/"free" literals look swapped at rows
// 89-93 but the uses at rows 119/128 swap back -- both sides pass "alloc" on the
// allocIt arm and "free" on the freedIt arm. It is only which of r25/r26 holds
// which literal.
```


## `?ReportMemoryUsageOverview@MemTracker@@QAAXPBD@Z`

`MemTracker::ReportMemoryUsageOverview` -- moved verbatim from `src/system/utl/MemTracker.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 716-730):

```text
        // NOTE (w7-ai): the five locals share one declaration with the
        // MemFreeBlockStats call precisely because the image's physical-heap
        // arm writes into TWO OF THEM rather than into fresh locals. Slot map
        // from the target listing -- the call passes r4=0x58, r5=0x60, r6=0x50,
        // r7=0x5c, r8=0x54 -- so `free` is 0x50 and `lfrags` is 0x58, and those
        // are exactly the two slots the physical arm stores to. Declaring
        // `used` as a private local instead let MSVC dead-code the whole
        // `mFreePhysMem - PhysicalUsage()` computation away (9 deleted
        // instructions); it only survives because these locals have had their
        // address taken in the sibling arm.
        //
        // BEHAVIOUR, faithful to the image and deliberately preserved: the
        // physical arm never assigns `biggest`, so PhysLargest prints an
        // uninitialised slot. The image reads r1+0x54 (the call's LAST
        // out-param) having only written r1+0x50 and r1+0x58.
```
