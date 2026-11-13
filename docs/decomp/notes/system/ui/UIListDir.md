# Lane notes: `src/system/ui/UIListDir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?BuildDrawState@UIListDir@@QBAXAAUUIListWidgetDrawState@@ABVUIListState@@W4State@UIComponent@@M_N@Z`

`UIListDir::BuildDrawState` -- moved verbatim from `src/system/ui/UIListDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 335-352):

```text
// w31-e 2026-11-03 (98.916, unchanged): the residual is a pure ALLOCATION
// residual and the instruction multiset already agrees -- 371 rows both sides,
// 19 register-only diff_args, and the four non-register rows are two
// `stw rN, 0x54(r1)` pairs that the two builds place at different points
// (image 18/110, ours 24/64; identical opcode, displacement and count).  The
// frame is identical too (0x220 both, 18 callee-saved GPR / 13 FPR both), and
// the stack diff reports exactly one user slot, 0x54, PERMUTED (same offset,
// different variable: image spills the call result there, we spill
// fadeCountEnd).  c2rs tap on the contested registers, from a live run of
// `regasg.sh src/system/ui/UIListDir.cpp BuildDrawState@UIListDir`:
//   id 38 sym38/k04 pri 12   tie 199 -> r28   (also r28 in the image)
//   id 12 sym13/k04 pri -100 tie 58  -> r17
//   id 10 sym11/k04 pri -114 tie 122 -> r15
//   id 57 sym14/k04 pri -86  tie 71  -> r14
// The three that disagree with the image are separated by 14 to 74 priority
// units, not by 1, so this is not the "add or remove one merge point" lever
// (C2RS-BRIDGE 6.2b) -- it is sched0/first-fit over a 371-instruction body, the
// class the w28 lessons say to stop spelling at.  Left as measured.
```

Inside the body (was lines 519-568):

```text
        // NOTE (w7-ai, 97.90): retail's frame is 0x220 and ours is 0x1e0 because
        // MSVC gives the `data == -1` branch's UIListElementDrawState its own
        // 0x40 slot at 0xe0(r1) while ours is coloured onto the main `elem` at
        // 0xa0(r1).  Scoping the main `elem` up to loop-body level or moving
        // `Vector3 elemPos` into the loop changes nothing.
        // w7-bj (98.88, 4 rows): declaring `elem` at FUNCTION scope does give
        // frame parity (0x220, 1476/1476 bytes, the 0x24c/0x25f homes) but the
        // canonical ruler charges it (97.90 -> 97.40; with the Min shape below
        // 97.67) because it also flips a pair of fnmsubs and the loop-guard
        // placement, so the 0x1e0 frame is kept.  What did pay: spelling
        // fadeCountStart as `halfDisplay < mFadeOffset ? halfDisplay :
        // mFadeOffset` (reproduces `mr r15,r10 / mr r15,r11` at 0x82789088,
        // 97.90 -> 98.17) and defining `prevData = 0` AFTER the StepPercent call
        // (the `mr r27, r23` lands at 0x8278924C, 98.17 -> 98.88).  A named
        // `scrollGap` local and `totalGap - scrollOffset * firstGap` were inert.
        // w18-d: frame parity now comes from the loop-scope `emptyElem` above
        // without moving `elem`; every 0x40 offset row is gone.
        // Residual (98.916, 23 rows, all regalloc): one allocator choice -- retail splits numDisplayWithData
        // (homed at 0x54(r1) at 0x82789080/0x827891E8, r28 time-shared with
        // `showing`, reloaded at 0x82789554) and keeps fadeCountEnd in r17,
        // where we keep numDisplayWithData in r15 and split fadeCountEnd (r14,
        // freed for the Vector3-copy scratch) onto 0x54(r1) instead.
        // w21-z (stop at 98.916, same 23 rows): behaviour re-checked -- all 38
        // branches land on the same aligned row on both sides and every
        // mismatch is register/home-slot only.  Measured: guarded do/while
        // loop (inert, 98.916); fadeCountEnd re-spelled as the min expression
        // (98.1); halfDisplay computed before NumDisplayWithData() (96.0 --
        // NumDisplay() inlines differently).
        // w26-s: four more spellings MEASURED INERT (byte-identical, 98.916) --
        // a named `count = numDisplayWithData` feeding `reserve` and the loop
        // bound, the same count feeding only the loop, the increment written
        // `numDisplayWithData = numDisplayWithData + 1`, and
        // `for (int i = 0, count = numDisplayWithData; ...)`.  Why the split is
        // forced in the image and not here: `showing` holds r28 in BOTH builds
        // (the `mr r28, r3` after Display2Showing is an EQUAL row), so the
        // image's numDisplayWithData necessarily time-shares r28 and must home
        // it, while ours has r15 free and never needs to.  The open question is
        // the priority order, not the loop spelling: ours ranks
        // fadeCountStart(-100) > numDisplay(-102) > numDisplayWithData(-114).
        // w27-f (2026-11-02): the C2RS-BRIDGE 8.8 binding lever does not reach
        // this row.  Measured byte-inert at 98.91599 canonical / 98.63144
        // fuzzy: `const int &numDisplay = state.NumDisplay()`, `const float
        // &elementSpacing = mElementSpacing` (used at both fade distances),
        // `const int &halfDisplay = numDisplay / 2`.  Worse: `const int
        // &fadeOffset = mFadeOffset` (98.59078) -- the ternary's short-circuit
        // is what makes the two `mr r15/r17` rows match, and a named reference
        // breaks it.  `numDisplayWithData` cannot be reference-bound at all
        // (it is incremented, so a const reference does not compile).  The
        // 23 rows are branch-aligned 1:1 on both sides, so this stays a
        // priority-order question, not a spelling one.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/ui/UIListDir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 657-700; no definition follows it):

```text
// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- UIListDir::BuildDrawState,
// canonical 98.91599 / fuzzy 98.63144.  369 instructions on BOTH sides and
// 1476 B = 1476 B (348 equal / 19 diff_arg / 2 delete / 2 insert), so this is
// not an instruction-count artifact.
// Placed at END OF FILE deliberately.  MILO_ASSERT takes an explicit line
// LITERAL rather than __LINE__ (src/system/os/Debug.h:93), but nothing above
// this line is touched at all, so the certificate cannot perturb its own row.
//
// BEHAVIOUR: no divergence.  All 38 branch and call rows align 1:1 on both
// sides and every mismatch is a register name or the home slot of a value that
// is live either way.  The four `bl` rows that read like wrong callees
// (?Fail@NetStream, ?Parent@Node<ObjPtrList<CharLookAt>>,
// ?UncompressedSize@AsyncFile against our real names) are the documented
// ICF-alias carve-out: same folded address, and
// scripts/check_alias_map_soundness.py gates that map on a relocation-charged
// body test.
//
// MECHANISM: three long-lived values are one register low.  Decoded from the
// listing -- image (numDisplayWithData, fadeCountStart, fadeCountEnd) =
// (r28, r15, r17); ours = (r15, r17, r14).  c2rs tap:
//   id 12  pri -100 tie 58  ben 9 ref 9 sym 13/k04 -> r17  [fadeCountStart]
//   id  9  pri -102 tie  8  ben 6 ref 6 sym 10/k04 -> r16  [numDisplay]
//   id 10  pri -114 tie 122 ben 6 ref 6 sym 11/k04 -> r15  [numDisplayWithData]
//   id 57  pri  -86 tie 71  ben 1 ref 5 sym 14/k04 org split1#95 allowed {r14}
// `showing` holds r28 in BOTH builds (idx 173 `mr r28, r3` is an EQUAL row).
// The image puts numDisplayWithData there as well, TIME-SHARED with showing --
// which is exactly why the image must home it (`stw r3,0x54` at idx 18 and
// `stw r28,0x54` at idx 110, reloaded at idx 329) and why that value, and not
// fadeCountEnd, is the one c2 splits.  Ours has r15 free for it and never
// needs a home, so we split fadeCountEnd instead.
// The nearest competitors are 12-14 priority units apart, so the
// C2RS-BRIDGE 6.2b "add or remove one merge point" lever (worth about one
// unit) cannot reach the ordering; and which value c2 splits in the first
// place is the split pass, not a source spelling.
// 26 spellings measured across lanes, all inert or worse.  This wave added:
// swapping the numDisplay / numDisplayWithData declaration order 96.74797,
// a named count feeding reserve and the loop bound (byte-identical), and
// `unsigned int numDisplayWithData` 98.57723.  Already on record: the guarded
// do/while, fadeCountEnd as a min expression, halfDisplay moved before
// NumDisplayWithData, four named-count forms, four const-reference bindings,
// elem at function scope, the ternary fadeCountStart, prevData placement,
// scrollGap, and the totalGap rewrite.
// ---------------------------------------------------------------------------
```
