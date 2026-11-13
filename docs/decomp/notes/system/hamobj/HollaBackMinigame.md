# Lane notes: `src/system/hamobj/HollaBackMinigame.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetDefaultShot@HollaBackMinigame@@QAAXXZ`

`HollaBackMinigame::SetDefaultShot` -- moved verbatim from `src/system/hamobj/HollaBackMinigame.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 440-469):

```text
// Lane w7-ag, 2026-09-14: the 4 remaining inserts are one MSVC anchoring
// choice, repeated once per branch.  The image keeps only the HIGH half of
// ?TheHamDirector@@3PAVHamDirector@@A in r30 and loads through it everywhere,
// including the GetVenueWorld() call after the if/else:
//   build/373307D9/asm/system/hamobj/HollaBackMinigame.s
//     824EA530  lis r30, ?TheHamDirector@@...@ha
//     824EA540  lwz r11, ?TheHamDirector@@...@l(r30)     ; in-branch
//     824EA78C  lwz r3,  ?TheHamDirector@@...@l(r30)     ; after the join
// We materialise the FULL address instead -- `lis r11, @ha; addi r30, r11, @l`
// in every one of the four branches, then `lwz r3, 0x0(r30)` at the join --
// so each branch carries one extra addi.  Splitting the tail into
// `WorldDir *venue = TheHamDirector->GetVenueWorld();` + Find<> is completely
// inert (identical 19 rows), and there is no expression in this function whose
// spelling chooses between the @ha anchor and the full address: the four
// branch bodies and the join already read the global exactly once each, which
// is what the image does.  Permuter-class residual; 98.09524.
// w21-k (still 98.10, same 19 rows: 4x lis/addi/lwz anchor triples + the
// join `lwz r3, 0x0(r30)` + one f1/f2 fmr order pair in the Animate call):
// behaviour re-checked against the listing (same four shots, same
// Handle slot through the virtual base, Animate(0, false, 0, nullptr,
// kEaseLinear, 0, false)).  decomp-synth beam search (3 depths, ~310
// builds, 1480 s) found nothing above baseline; a `static inline
// ForceShot(const char *)` helper was not inlined (26.6).  Stopped here.
// w21-au (still 98.10, same 19 rows): six more spellings, all byte-inert
// (98.09524): a __forceinline ForceShot helper (it does inline -- same
// four-block shape, same full-address PRE into r30); the tail in a
// __forceinline helper; `HamDirector *&dir = TheHamDirector;` for every
// access; `RndPropAnim *anim;` hoisted to function scope; the whole tail
// chained in one expression (98.095 now, not the 90.2 w16-b measured).
// Behaviour re-checked: same four shots, same Animate args.  Stopped.
```

Inside the body (was lines 488-519):

```text
    // w16-b (98.10, 19 rows): the image keeps only `lis r30, TheHamDirector@h`
    // and loads `@l(r30)` where we materialise the full address in r30, in all
    // four Handle arms. Chaining this lookup without the `anim` local is 90.2.
    // w27-e (still 98.095, the same 19 rows, 13 spellings): mechanism -- the two
    // forms c2 can pick are `lis @h` into a callee-saved GPR with a symbol-
    // relative `lwz sym@l(rN)` at each use (the image), and a MATERIALISED
    // address (`lis` + `addi`) that the allocator rematerialises in every block
    // that spans a call (ours; the `addi` is dead in three of the four arms and
    // the fourth feeds `lwz r3, 0(r30)` at the GetVenueWorld site).  Inert on
    // all of it: reference-binding the global pointer at function scope, at the
    // arm scope, and just before the final lookup; a named copy (-0.9pp);
    // `((HamDirector *)TheHamDirector)`, `TheHamDirector[0]`, a named venue
    // pointer, and hoisting `anim`'s declaration.  Open question: which IL shape
    // makes c2 keep the @h half symbolic instead of materialising the address.
    // w28-a: the delta is exactly four `addi r30, r11, TheHamDirector@l` plus the
    // `lwz 0(r30)` form at the GetVenueWorld site (census: surplus addi:4, lwz:1).
    // MEASURED CAUSE -- they are PRE-inserted on the four arms because the TAIL's
    // `TheHamDirector` needs an address at the merge point: replacing the tail
    // with `RndPropAnim *anim = nullptr;` deletes all four and leaves the arms in
    // the image's `lis @h` + `lwz @l(rN)` form (in a volatile register, since
    // nothing then needs @h across the arms -- the image's r30 differs).  The
    // image keeps the @h half in r30 across the arms and loads `@l(r30)` at the
    // tail, so it needs no addi at all.  Splitting the tail at a statement
    // boundary (`WorldDir *venue = TheHamDirector->GetVenueWorld();`) is
    // byte-identical; a `HamDirector *dir` copy there is -0.5pp and at function
    // scope -8.8pp.
    // w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG.  The four
    // `addi r3, r31, 0x78/0x88/0x98/0xa8` (r31 = frame pointer: the Handle
    // return DataNode temp) are on BOTH sides -- objdiff aligned ours one row
    // later.  The ours-only instruction is `addi r30, r11, TheHamDirector@l`,
    // the w28-a anchor.  Handle's receiver is the same vbase-converted
    // TheHamDirector on both sides.  98.1 / equal 195, diff_arg 15, ins 4.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED by listing.  Player(1)->
// IsPlaying() false -> p1 shot; Player(0)->IsPlaying() false -> p2; Side()
// == 1 (kSkeletonRight) -> p21, else p12; each Handle(Message("force_shot",
// shot), true) through the vbase (slot 0x14, r6 = 1).  Tail: Find<RndPropAnim>
// ("bid_start_character_faded_out.anim", true) then Animate with f1=f2=f3=0,
// r5 = false, r7 = nullptr, r8 = kEaseLinear, r10 = false -- the only diff
// there is the order of two `fmr` copies of the same 0.0.  Unicorn h4, fresh:
// EQUIVALENT/high via the zero-fill path (IsPlaying stub -> p1), both sides
// then fault identically in the virtual Handle; weak evidence.  No native
// test (needs TheGameData + TheHamDirector).  Code unchanged.
```
