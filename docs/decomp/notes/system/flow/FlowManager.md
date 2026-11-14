# Lane notes: `src/system/flow/FlowManager.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/flow/FlowManager.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 11-50; nearest following definition: `FlowManager::FlowManager`):

```text
// w8-e 2026-09-15: `merged_ObjPtrVecErase` (240 B, 0%) is NOT a target symbol.
// It is a SYNTHETIC ICF-group label invented by our own tooling:
// build/373307D9/icf_aliases.map:11045 declares the group at 823EA0B8 and lists
// four real members folded there -- ObjPtrVec<FlowNode|RndEnviron|RndLight|
// SpotlightDrawer, ObjectDir>::erase -- all tagged `icf_aliases.synthetic`, and
// config/373307D9/symbols.txt:123849 binds the made-up name to that address.
// Nothing our source can spell is named `merged_ObjPtrVecErase`, so this row
// can never pair and can never score.  The FlowNode spelling this TU really does
// emit (see the explicit instantiation above) is one of the four folded copies.
// Measured 0.0% and structurally unscoreable; not a missing body.
//
// w8-k 2026-09-15: RE-ANCHORED, and the row is now scoreable at 80.58%.
// Everything above is accurate about the artifact; "can never pair and can never
// score" was the one step too far.  A synthetic label is not a fact about the
// image, it is a choice in config/373307D9/symbols.txt, and the choice was
// wrong: ham_xbox_r.map names four REAL claimants at 0x823EA0B8, and the
// address-range test picks one without appealing to symbols.txt at all --
// splits.txt gives FlowManager .text [0x823E8318, 0x823EB8A0), which CONTAINS
// 0x823EA0B8, against LightPreset .text [0x8283DA50, 0x82850940), which does
// not, which is also why dtk carves the body into FlowManager.s.  symbols.txt
// now binds ?erase@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA?AViterator@1@V21@@Z
// there; LightPreset loses nothing, since its three spellings have no body in
// LightPreset.s either.  Whole-binary row diff: exactly 2 rows moved.
//
// RESIDUAL, 80.58% canonical / 80.0% raw, 240 B target vs 276 B base, 51 of 70
// instructions equal.  This is now ordinary decomp work, and the diagnosis is
// already done -- but the fix is NOT in this file and must not be made here:
//   * the target calls `bl ?Set@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAAXViterator@1@PAVFlowNode@@@Z`;
//     we call ?SetObjConcrete@?$ObjRefConcrete@VFlowNode@@... directly, with a
//     10-instruction inserted cluster at rows 45-54 that is the `!obj &&
//     mListMode == 0` null-check from Set's body.  MSVC inlined Set into erase
//     for us and did not for retail.
//   * residual beyond that is one r26<->r27 and one r27<->r28 callee-saved
//     register-permutation pair, which the canonical ruler forgives anyway.
// ObjPtrVec<T1,T2>::erase and ::Set are both in src/system/obj/ObjPtr_p.h
// (erase at :878, Set at :313), which is PCH-REACHED -- it is in decomp_pch.h's
// 178-header closure -- so any edit there rebuilds and re-scores every
// ObjPtrVec<T> instantiation in the binary, far outside this unit.  Whoever
// takes it: make the change there, run a FULL ninja, and diff the whole binary,
// because the blast radius is the whole binary.
```

File-level (was lines 52-93; nearest following definition: `FlowManager::FlowManager`):

```text
// w21-h (80.58, unchanged): the image is NOT uniform about inlining Set into
// erase -- three distinct erase bodies exist: FlowManager's (calls Set; ICF-folded
// with LightPreset's RndEnviron/RndLight/SpotlightDrawer copies), Font's RndMat
// and LightPreset's Spotlight (both inline Set).  Ours is the MIRROR image in
// two TUs: FlowManager inlines, LightPreset calls Set for all four types,
// CharClipGroup calls Set (image inlines).  In our objects the copy whose COMDAT
// is emitted FIRST of the erase/Set pair is the one that gets the other inlined
// (Font: erase sec 995 < Set 997, inlined; LightPreset: Set 1298 < erase 2214,
// not).  Measured inert here: dropping the explicit erase instantiation below,
// and replacing it with an explicit Set<FlowNode> instantiation -- section order
// (erase 563 < Set 585) and the inlining did not move.
// w18-d (80.58): an explicit instantiation of ObjPtrVec<FlowNode>::Set ahead of
// the erase instantiation does not stop Set being inlined into erase (inert).
// w31-e 2026-11-03: the COMDAT-order rule is now confirmed on BOTH SIDES of
// this row, which is what turns it from a correlation into a prediction.
// w21-d measured "erase<T> inlines Set<T> iff erase's COMDAT precedes Set's"
// across 19 of our TUs; it had the image side of LightPreset.  Adding the image
// side here: the dtk split target build/373307D9/obj/system/flow/FlowManager.obj
// numbers Set<FlowNode> at section 50 and erase<FlowNode> at 94, and its erase
// is 240 B (calls Set) -- exactly what the rule predicts.  LightPreset's image
// object is the mirror (erase 400 < Set 421, erase 276 B = inlined).  Ours is
// inverted in both: FlowManager erase 579 < Set 589 (276 B, inlined) and
// LightPreset Set 1301 < erase 2218 (240 B, calls).  So ONE mechanism, seen
// twice, predicts both rows and their mirror-image branch counts (9 delete / 6
// insert here, 6 / 9 there).  Re-measured inert this lane: deleting the
// explicit erase<FlowNode> instantiation below moves the pair 579/589 -> 563/569
// -- erase still precedes Set, still 276 B (this is w18-d's result reproduced on
// the current tree).  The order is therefore not reachable from this .cpp: it
// is set by which member the TU's earlier code demands first, and both orders
// the header makes available (`remove` -> erase -> Set, and an early push_back
// -> insert -> Set) are decided inside ObjPtr_p.h.
// w24-c2 (80.58, BLOCKED-PCH): residual unchanged -- rows 39-54, the inlined
// Set body (10 inserted rows) where the image does `bl Set<FlowNode>`, plus an
// r26/r27/r28 permutation. New observation, not built: in the image's
// FlowManager.s the address order puts Set<FlowNode> right after
// _Vector_base::~_Vector_base and before Node::RefOwner / Node(const Node&),
// i.e. among the earliest ObjPtrVec<FlowNode> COMDATs, next to where our obj
// numbers Node(ObjRefOwner*) and const begin()/end() (secs 329-331); ours
// numbers Set at 589, after erase (579) and remove (581). So whatever made the
// original reference Set early is in the push_back/insert/operator= path of
// obj/ObjPtr_p.h / Object.h (all three call Set in the image), not in this
// file -- a PCH-reached edit with whole-binary blast radius.
```


## `?Poll@FlowManager@@QAAXXZ`

`FlowManager::Poll` -- moved verbatim from `src/system/flow/FlowManager.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 258-276):

```text
        // Open (w25-gy): the image emits `fadds f0, f0(sample), f13(avg)`; we emit avg
        // first. c2rs §8.7 hypothesis: the avg operand is a memory leaf whose base is
        // the `this` temp V1122 (sid mod 4 = 2, key 0x18008), which outranks the sample
        // load temp (key 0x16bc0). The image order needs that base at sid mod 4 = 0 or 1.
        // 24 in-function spellings (locals, cursors, ctor forms, statement order before
        // the first `this` use) left V1122 unchanged; the open question is which
        // counter mints it before the body is lowered.
        // w26-n: the CQS sort dump confirms the note's keys exactly --
        //   CQS M6{00018008}[b V1122]{...} V1455/k02s03{00016bc0}
        // i.e. avg is a memory leaf on the `this` temp (sid mod 4 = 2) and the
        // sample is a load temp at sid 1455 (mod 1024 = 431).  The base-reference
        // lever was tried 13 ways and left the row unmoved, because the avg base
        // never re-binds: `float &avg = mAvgFrameTime;`, `float *avg = &mAvgFrameTime;`,
        // a `FlowManager *self = this;` and a per-iteration `float &avg` all fold
        // back onto V1122 and emit byte-identical code.  Re-binding the SAMPLE's
        // base does move its temp sid (`const float *s = ...; *s++` -> V1418,
        // key 0x16280) but never across 0x18008.  Flipping needs either V1122 at
        // sid mod 4 = 0/1 or the sample temp at sid mod 1024 in [513, 1023];
        // both are symbol-count questions, not operand-binding ones.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.978, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?erase@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`

`ObjPtrVec<FlowNode,ObjectDir>::erase`

w-low-world (lane B, 2026-11-13): value-audited against the listing at
0x823EA0B8: NOT A BUG.  Same body as ObjPtrVec<Spotlight>::erase except Set is
an out-of-line `bl Set<FlowNode>` with (this, begin+idx, lastObj).  Residual is
the mirror-image inlining decision (ours inlines Set), PCH-blocked.

