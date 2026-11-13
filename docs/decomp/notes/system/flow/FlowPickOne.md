# Lane notes: `src/system/flow/FlowPickOne.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Activate@FlowPickOne@@UAA_NXZ`

`FlowPickOne::Activate` -- moved verbatim from `src/system/flow/FlowPickOne.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 42-117):

```text
// w7-bv: 95.80 -> 98.6 canonical (98.4 raw, 9 of 323 rows), on top of w7-bh's
// 95.19 -> 95.80 kChoiceUseIndex fix (no mIndex++ in that arm -- keep it).
// Levers, each measured on this function:
//   * Per-arm ActivateChild(...) calls, no shared `chosen` local.  With one
//     `FlowNode *chosen` assigned in every arm and activated after the switch,
//     MSVC spilled it to 0x58(r31) across the arms (16 rows, idx 280-295).
//     Written per arm, the identical tails cross-jump into the image's shared
//     blocks at 0x82406090 / 0x8240609C / 0x824060A0 (bctrl-free `bl
//     ActivateChild`) and 0x824060E4 (the incrementing tail).
//   * `while (int remaining = items.size())` is the only spelling that keeps
//     the refill test a SIGNED shift: 0x82405FAC / 0x82405FD0 are
//     `srawi. r11, r11, 2`.  `items.size() != 0`, `(int)items.size() != 0`,
//     `items.size()`, `items.end() - items.begin() != 0` all fold to MSVC's
//     mask peephole `clrrwi. r11, r11, 2`; `!items.empty()` is a pointer
//     compare (`cmplw`) and wrong outright.
//   * kChoiceOrdered reads mIndex into a named `int index` before the range
//     test: written on the member directly, MSVC copied it (`clrrwi r10, r9,
//     0`) and took r9 for the 0x14 divisor (4 rows at 0x824060BC-0x824060C4).
//   * `lastChosen == mChoiceHistory[0]`, operand order as in the image
//     (`cmplw cr6, r25, r11` at 0x82405FE4); the member on the left swaps it.
//   * `FlowNode *lastChosen = nullptr; if (!empty()) lastChosen = back();`
//     -- the ternary form inverts the branch (bne + `mr r25, r24` + b).
// RESIDUAL (9 rows, r26 cluster): the image materialises r26 = &mChoiceHistory
// .mNodes at 0x82405E90, one instruction BEFORE the `mIndex >= 0` test, and
// addresses the vector r26-relative for size() (0x82405E9C/EA0), empty()
// (0x82405EC4/ECC), clear() (`mr r3, r26`), [0] (0x82405FD8) and [mIndex] --
// yet reloads _M_finish r30-relative for back() (`lwz r11, 0x64(r30)` at
// 0x82405ED8).  We form r26 lazily at the clear() call, fold the earlier
// sites to r30+0x60/0x64, and CSE the empty() load into back() (`clrrwi r11,
// r10, 0`).  mNodes is private to ObjPtrVec, so nothing in this TU can name
// +0x60: `ObjPtrVec<FlowNode> &history = mChoiceHistory;` gives r26 =
// this+0x5c, an extra `addi r27, r26, 4`, one more callee-saved register
// (r23, frame 0x10e0) and 95.1 -- refuted.  The split (vector methods through
// an explicit `this` temp, back() through the member) lives in how the
// ObjPtrVec accessors were spelled in the original header, which is
// PCH-reached and out of scope for this row.
// w21-p (98.55, same 9 rows): behaviour re-read against the image -- every
// branch lands on the corresponding block (targets differ only by the
// constant function-base delta, plus -4 inside the r26 insert/delete
// window), and the r26 rows are all the same address, this+0x60.  Note the
// same shape exists for mChildNodes in BOTH builds: r29 = this+0x18 is
// materialised at 0x82405D44 between the two displacement loads of
// empty(), and all later uses go through r29.  For mChoiceHistory the image
// does the same at the top of the >1 arm; we only materialise it for the
// clear() call.  Not chased further: the lever is in ObjPtrVec's accessors.
// w21-an (98.55, same 9 rows): two more spellings for the r26 placement,
// both worse -- `ObjPtrVec<FlowNode> &history = mChoiceHistory;` for every
// jukebox use (96.47; lever (a) binds this+0x5c, not &mNodes), and a named
// `int historySize = mChoiceHistory.size();` before the bool (98.11: the
// size loads hoist above the `mIndex >= 0` test and r26 is still lazy).
// w24-c3 (98.55, same 9 rows): not re-chased beyond reading the image.  The
// image's back() at 0x82405ED8 goes through r30 (`lwz r11, 0x64(r30)`) while
// every other mNodes access uses r26 -- i.e. back() and the size/empty/clear/
// [] accessors reach mNodes through DIFFERENT inline paths in the original
// ObjPtrVec header (PCH-reached, whole-binary blast radius).  Out of scope for
// a 5-function task; the next lane should A/B an ObjPtrVec accessor spelling
// with a full ninja + whole-binary row diff.  Hypothesis: the image's
// &mNodes is ONE address temp, CSE'd and hoisted to the head of the >1 arm
// because several inlined accessors materialise it explicitly; ours folds
// each access into a displacement and only forms the temp for _M_erase's
// `this`.  Open question: which accessor's inline body forms the explicit
// address.
// w31-e 2026-11-03 (98.555, same 9 rows): nothing new to add on the r26
// placement, but the residual's SHAPE is worth stating precisely, because it is
// not pure placement -- the instruction multiset differs by exactly two rows.
// Aligned against the image: target has one `addi r26, r30, 0x60` at 158 that
// we emit at 172 (the base address moved late), our three `lwz rX, 0x4(r26)`
// become `0x64(r30)` displacement loads, and at 178 the image RELOADS
// `lwz r11, 0x64(r30)` where we CSE the already-live _M_finish pointer into
// `clrrwi r11, r10, 0`.  So the image refused a CSE we perform, and that is
// downstream of the same missing address temp: with r26 formed at the head of
// the >1 arm, the back() reload is a distinct memory operand, and without it
// the load is CSE'd.  Four lanes (w7-bv, w21-p, w21-an, w24-c3) have already
// spent the spellings that reach this from FlowPickOne.cpp; the address temp
// belongs to ObjPtrVec's inline accessors in ObjPtr_p.h (PCH-reached).  Left
// as measured rather than respelled a fifth time.
```

Inside the body (was lines 183-188):

```text
                // back() + pop_back(), not a walking pointer: the image's loop
                // body at 0x82405FB8 writes items._M_finish back to memory
                // after the push_back (`stw r29, 0x64(r31)`) and re-derives the
                // count with `subf r11, r27, r29` / `srawi. r11, r11, 2` -- a
                // signed shift.  Only a named int in the condition keeps it
                // signed; see the w7-bv note above.
```
