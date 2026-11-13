# Lane notes: `src/system/gesture/SpeechMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@SpeechMgr@@QAAXXZ`

`SpeechMgr::Poll` -- moved verbatim from `src/system/gesture/SpeechMgr.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Inside the body (was lines 639-683):

```text
            // w15-a (97.98 canonical): the image strength-reduces TWO pointers, one at
            // &event[i].pResult (addi r31, r1, 0x70) and one at &event[i] (subi r30,
            // r31, 0x10), costing r28 in the prologue; we use one.  Indexing event[i]
            // directly is byte-identical; a `switch (eventId) { case 0x40: case 0x80: }`
            // drops the image's `cmplwi 0x20 / ble` (94.1, an empty `case 0x20:` is
            // folded away), so the explicit `> 0x20` test stays.
            // w21-z (stop at 97.98, 11 rows: r29/r30 regswap x5 + the +0x10 IV pair):
            // behaviour re-checked against 8243BAC0..BB88 -- same tests, same order,
            // same args.  15 standalone cl.exe spellings of the loop all keep ONE
            // pointer at &event[i]: direct event[i], EV &/const EV & ref, EV *pe,
            // RR *&r = .pResult, pResult/id hoisted to locals (loads at the top),
            // pointer-stepped e++, `int i`, do/while, event + i, a named union,
            // nested switch with/without the <=0x20 cases, /Ox.  None reproduce
            // the image's second (pResult-based) induction pointer.
            // w21-bb (97.98, same 11 rows): 3 more probe spellings keep the one
            // &event[i] pointer -- pResult loaded into a local BEFORE eventId,
            // fully nested ifs, and an extra `cur.pResult = 0` store after the
            // destroy (not shipped; it only adds a store).
            // w28-c (97.98, same 13 charged rows).  The image's base is
            // r1+0x70 = &event[0].pResult, ours r1+0x60 = &event[0], and the
            // image derives the element as `subi r30, r31, 0x10` INSIDE the
            // loop; that single choice is the whole residual (both sides walk
            // r31 by 0x18, and every load/store resolves to the same field).
            // It is the c1 address-formation question, not colour.  Measured
            // and REFUTED here, ten spellings: C2RS-BRIDGE 11's "first field
            // read" does NOT explain it -- the image tests eventId FIRST
            // (cmplwi 0x20 / 0x40 / 0x80 precede the pResult test), so its
            // first read sits at the NON-base offset, and forcing pResult
            // first in the `&&` chain costs 4.37pp (93.61) by changing the
            // branch order.  Also inert/byte-identical: a named
            // `NUI_SPEECH_RECORESULT **pp = &cur.pResult`, a `cur` pointer,
            // direct `event[i]` everywhere, a base-pointer local, a
            // pre-stepped element pointer.  Worse: reading pResult into a
            // named local first (96.82 -- it hoists the load above the
            // eventId test).  Decisive probe: writing the destroyed address
            // explicitly as `(NUI_SPEECH_EVENT *)((char *)&cur.pResult -
            // 0x10)` is FOLDED BACK to `&event[i]` and scores identically,
            // so c1 canonicalises on the first element address formed and
            // no expression of the address re-biases it.  Open question:
            // what makes c1 form &elem.pResult before &elem.
            // w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG -- the
            // image's `subi r30, r31, 0x10` is &event[i] derived from
            // &event[i].pResult (a stack array, not a this-adjustment); the
            // address passed to NuiSpeechDestroyEvent is the same on both
            // sides.  Re-measured 98.0 / equal 38, diff_arg 12, del 1.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED by listing only.
// mEnabled (0x3c) false -> CurrentLine() = literal; else memset(r1+0x60, 0,
// 0x78 = 5 events), NuiSpeechGetEvents(5, events, &fetched) (r3/r4/r5 in that
// order), SUCCEEDED = `cmpwi 0 / blt`, fetched != 0; loop: eventId > 0x20 and
// (== 0x40 or == 0x80) and pResult (+0x10) -> ProcessRecoResult(this,
// pResult); NuiSpeechDestroyEvent(&events[i]) every iteration; fetched
// reloaded each trip, unsigned compare.  Unicorn h4, fresh: DIVERGENT
// data_layout, input_sensitive -- first mismatch is the String::operator=
// literal address on the disabled path; nothing past it is compared.  No
// native test: NuiSpeechGetEvents is a generated stub (returns 0, no events)
// on native, so the loop is unreachable there without new injection plumbing.
// Renamed event/cur/res -> events/event/hr: byte-identical.
```

## `?AddDynamicRuleWord@SpeechMgr@@QAAXVSymbol@@PBD1PAPAX2@Z`

`SpeechMgr::AddDynamicRuleWord` -- moved verbatim from `src/system/gesture/SpeechMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 593-603):

```text
        // BUG FIX (w15-a): c3 is the transition's SEMANTIC VALUE.  The image
        // converts it into its own wide buffer and points the
        // NUI_SPEECH_SEMANTIC passed as the last argument at it:
        //   0x8243B5B4  bl   mbstowcs_s            ; r4 = r31+0x80
        //   0x8243B5B8  addi r11, r31, 0x80
        //   0x8243B5C0  stw  r11, 0x58(r31)        ; s.pcwszValue = semantic
        //   ...
        //   0x8243B5E8  addi r10, r31, 0x58        ; pSemantic = &s
        //   0x8243B600  bl   NuiSpeechAddWordTransition
        // We converted c3 and then never used it, handing the recognizer an
        // uninitialised pcwszValue.
```

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/gesture/SpeechMgr.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 43-48; nearest following definition: `SpeechMgr::Grammar::operator==`):

```text
    // VoiceInputPanel reads mOverlay as an LVALUE (`TheSpeechMgr->mOverlay->
    // Print(MakeString(...))`): MSVC re-derives `this` from the global after
    // the argument call and keeps only the vptr across it (0x82957BCC), which
    // the rvalue `Overlay()` accessor cannot produce (it homes the pointer in
    // a callee-saved register).  Byte-identical with the member, 93.4 with the
    // accessor.  -- w7-bv
```
