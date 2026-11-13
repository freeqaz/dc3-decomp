# Lane notes: `src/system/rndobj/PropAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@RndPropAnim@@UAAXAAVBinStream@@@Z`

`RndPropAnim::Load` -- moved verbatim from `src/system/rndobj/PropAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 203-225):

```text
    // Open residual, 2 rows (99.988 normalized), no behaviour difference.  The
    // whole residual is inside this local's inlined ~ObjRefConcrete ->
    // ObjRef::Release ring unlink, whose two statements are
    //     prev->next = next;   // loads 0x88 then 0x84 -- we MATCH this
    //     next->prev = prev;   // image loads 0x84 then 0x88, we load 0x88,0x84
    // Same two values into the same two registers feeding an identical store;
    // only the issue order of the second statement's two `lwz`s differs.
    //
    // The image emits BOTH orders for this one inline body: an earlier census
    // of `lwz a / lwz b / stw _,0x4 / lwz b / lwz a / stw _,0x8` windows in
    // build/373307D9/asm gave 630 "crossed" (the image's order here) against
    // 314 "uniform" (ours); restricted to function-end windows on r31 it is 56
    // uniform to 5 crossed (w25-mb), and two of those crossed sites (a PushRev
    // and Spotlight::Save) already match in our build.  So the order is set by
    // the scheduler in the enclosing function, not by ObjRef::Release's
    // spelling (a PCH-reached header; editing it is not on the table).
    // Mechanism hypothesis (c2 P_DAG): the two loads are equal-height leaves,
    // so the pick falls to the tuple-index tie key; the open question is
    // whether the image's order comes from that tie in run 3 or from run 4
    // (sched0, after lowering), whose function-flag gates may skip it here.
    // Inert or worse (w25-mb): RndPropAnim as the ObjOwnerPtr template
    // argument, a named frame local for GetFrame(), and declaring `obj` after
    // the mLastFrame store (worse).
```


## `?ForeachKeyframe@RndPropAnim@@QAA?AVDataNode@@PBVDataArray@@@Z`

`RndPropAnim::ForeachKeyframe` -- moved verbatim from `src/system/rndobj/PropAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 777-781):

```text
                // w25-ma: read inline. A named Key<bool>& here was the only
                // difference from the image: it reordered the two loads of the
                // ObjectStage dtor's `next->prev = prev` unlink in the kObject
                // replace case below (same values, same stores). Hypothesis: one
                // upstream temp fewer shifts the later temp ids that c2 orders by.
```


## `?OnListFlowLabels@RndPropAnim@@MAA?AVDataNode@@PAVDataArray@@@Z`

`RndPropAnim::OnListFlowLabels` -- moved verbatim from `src/system/rndobj/PropAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 856-944):

```text
// RESIDUAL (w7-ai, 92.8%).  Every instruction of the body pairs; what is left
// is one EH-model difference and the uniform frame shift it causes.
//
//   - Our build allocates an EH STATE VARIABLE at 0x50(r31): zeroed at entry
//     and set to 1 (li r11,1 / stw r11,0x50(r31)) immediately after the copy
//     constructor that fills the sret, once in each branch.  The image has no
//     such variable -- its 0x50 is the new'd-pointer slot, zeroed at entry
//     (stw r28,0x50) and overwritten with the PoolAlloc result (stw r3,0x50).
//     Ours puts that pointer at 0x54 and never zeroes it.
//   - Because the image does not register the returned DataNode for unwinding,
//     it also does not have to home the sret pointer; we emit an extra
//     stw r3, 0xd4(r31) at entry and keep it in r26 as the image does.
//   - Those two cost 0x10 of frame (0xc0 vs 0xb0) and shift every body slot by
//     +4 or +8, which is all 26 of the diff_arg rows.
//
// Two spellings measured, byte-identical to each other:
//   (a) DataNode ret = flowArr;   (as written)
//   (b) DataNode ret(flowArr);    (the explicit two-arg ctor, which is what
//       RndTransformable::OnGetChildren -- same idiom, single return -- uses
//       at 100.0%)
// The registration is therefore not coming from how the DataNode is spelled.
// The remaining hypothesis is that the image reaches this shape with one fewer
// EH scope than a two-branch, two-return body can produce, which is not
// something the call sites here can steer; a single-return rewrite is refused
// because the image plainly has TWO DataNode slots (0x68 and 0x70), one per
// branch.
// w7-bt (still 92.77 canonical): the flag is tied to having TWO non-NRVO
// DataNode returns, and nothing tried removes it while keeping both.
//   - Discriminator: collapsing the else branch to `return 0;` drops the flag
//     AND the sret home store -- prologue then pairs 1:1 with the image
//     (pointer slot at 0x50, zeroed at entry, frame 0xa0), 67.0 only because
//     the second branch is gone.  So one class-typed return site: no
//     registration; two: registered.
//   - The image's six __unwind funclets after the function (159581..159589)
//     are exactly delete@0x50, ~DataNode@0x58, ~DataNode@0x68, delete@0x54,
//     ~DataNode@0x60, ~DataNode@0x70 -- both `ret` locals ARE registered,
//     the sret never is, with two copy-ctor sites into r26 (8267F570 and
//     8267F624) and the pointer slots written at 8267F4B0 (0x50, zeroed at
//     entry 8267F468) and 8267F5A8 (0x54).
//   - Byte-identical to the committed spelling (all 92.77): second branch at
//     function scope instead of `else`; `return DataNode(ret);` at both
//     sites.  Inverting the condition to put the empty branch first keeps the
//     flag and flips the layout (61.4).  A file-static helper
//     `DataNode ArrayNode(DataArray*)` is not inlined even under
//     __forceinline (82.0, WRONG_CALLEE), so an inlined-helper origin for
//     the temp+copy shape is ruled out too.
// What is left is a front-end EH-shaping decision (register the return
// object when there is more than one class-typed return site) that the
// image did not make from a source we have not found; every instruction of
// the body already pairs.
// w21-y (still 92.77, same 35 rows): behaviour re-checked against the image --
// identical calls, arguments and branch targets in both arms; the only
// difference remains our EH "sret constructed" flag (li 1 / stw 0x50 after
// each copy-ctor) and the +0x10 frame it costs.  Not re-measured: every
// spelling listed above.  Also inert (92.77, byte-identical): `const DataNode
// ret`, `DataNode ret(flowArr, kDataArray)` + `return DataNode(ret)`, and the
// second arm as a bare block after the first arm's return.  No new lever found in this wave's catalogue (local
// ref binding, early-return polarity and tail-merge placement do not touch
// the EH-state decision).
// w30-c (2026-11-02): re-read the residual from the target listing before
// touching it -- it is 5 instructions, all of them ours: `stw r3, 0xd4(r31)`
// (the sret home) plus `li r11, 1` / `stw r11, 0x50(r31)` after each of the two
// copy-ctor sites, which is MSVC's "sret constructed" EH flag and the +0x10
// frame that comes with the 4-byte slot it takes at 0x50.  Everything else
// pairs 1:1 (target 512 B / 128 instructions, ours 532 / 133).
//
// NEW NEGATIVE this lane: the tail-duplication hypothesis -- one `return ret`
// reached from both branches, with the duplicated copy ctor and the two ret
// slots produced by the duplicator after the EH analysis -- is REFUTED.  Both
// spellings (flowArr assigned in each branch then one `DataNode ret = flowArr;
// flowArr->Release(); return ret;`, once with `DataNode ret(flowArr);`) measure
// 78.875 against 92.773, i.e. the merged body is a different function, so the
// image really does have two class-typed return statements and still has no
// flag.  Together with w7-bt's discriminator (`return 0;` in the else arm
// drops the flag) that pins the difference to a front-end EH decision, not to
// a spelling of the DataNode.
// w47-a (2026-11-06): re-read from the listing, not inherited.  The whole
// residual is the EH return-object state -- the RELOCATION-SYMBOL multiset is
// EQUAL (no wrong callee, constant or global), and the opcode+displacement
// multiset differs only by the +0x10 frame and the two `li 1`.  The image does
// have EH funclets for this body (`__unwind$159581` = `lwz r3, 0x50(r31); bl
// operator delete`), so this is not "no EH at all": the image simply has no
// state VARIABLE, while ours writes 1 to 0x50 after each copy-ctor into sret
// and homes the sret pointer to the write-only 0xd4(r31).  Eight spellings
// re-measured this wave, all INERT at 92.77344: direct-init `DataNode
// ret(flowArr)` in each arm, `const DataNode ret` in each arm, `return
// DataNode(ret)`, `DataNode ret(flowArr, kDataArray)`, and an explicit-type
// arm 2.  `if (!mFlowLabels.empty())` is 90.38281 -- worse.  No source handle
// on the state variable was found.
```
