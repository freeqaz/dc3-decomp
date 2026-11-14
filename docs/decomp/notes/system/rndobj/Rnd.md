# Lane notes: `src/system/rndobj/Rnd.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?TestPoint@Rnd@@QAAXABVVector3@@PAVRndFlare@@@Z`

`Rnd::TestPoint` -- moved verbatim from `src/system/rndobj/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 614-619):

```text
            // BEHAVIOURAL FIX (w12-a): a queued point test is NOT marked
            // occlusion-ready here -- the image's success path branches
            // `b .L_826688FC` (0x826688E8) straight to the epilogue, past the
            // `stb r11, 0x148(r27)` at 0x826688F8 that only the two
            // SetVisible(false) arms reach.  Readiness comes later, from the
            // point-test results.
```

Inside the body (was lines 626-638):

```text
    // w26-f13 stop note (fuzzy 99.92063, canonical 100.0).  The two charged
    // rows are the epilogue's `li r10, 0x0` / `stb r10, 0x102` pair: the image
    // puts the false in r10, we put it in r11 (the true that follows is r11 on
    // both sides).  Both constants are regasg temps, so the register is first
    // fit (r11, r10, ...) over sched3 order: ours reaches the false's def
    // first, so it takes r11 and the later true reuses it.  The image's false
    // in r10 means r11 was blocked when it was assigned -- either the true's
    // def precedes it in sched3 and sched0 then reorders the pair, or the
    // false is a colour candidate rather than a regasg temp.  Measured inert
    // (9 spellings): literal 0/1, named bool/const bool at the use site, a
    // named bool at the merge point, a function-scope bool, a raw `RndFlare *`
    // local for the store base.  Open question: which IL shape orders the
    // true's def ahead of the false's in sched3.
```


## `?DrawTimers@Rnd@@IAAMM@Z`

`Rnd::DrawTimers` -- moved verbatim from `src/system/rndobj/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1179-1183):

```text
        // w25-ma: the row cursor is rect.y itself, not a separate `float y =
        // f` (MSVC keeps rect.y in f24 across the calls, as it does rect.x in
        // f27). That one change gives the image's f23/f24/f25 colouring (f,
        // cursor, totalHeight), its `fmr f24, f23` placement and its rect.x
        // before rect.y store order here.
```


## `?OnToggleHeap@Rnd@@IAA?AVDataNode@@PBVDataArray@@@Z`

`Rnd::OnToggleHeap` -- moved verbatim from `src/system/rndobj/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1326-1337):

```text
// BEHAVIOUR FIX (w14-a): the timer is restarted on BOTH arms. The image's
// off-arm (target Rnd.obj +0x5ff0: li r9,-1; stb r31,0x8(r11); stw r9,gCurHeap; b +0x6008)
// branches to the shared `addi r3,r11,0x40; bl Timer::Restart` at +0x6008; the
// old SetShowingOnly(false) arm skipped Restart. Same as RB3's SetShowing on
// both arms; spelled with the store split from one shared Restart, 100 modulo
// register permutation.
// w18-a: 100 normalized / 96.77 fuzzy -- 14 volatile-register rows only (the
// image keeps the overlay in r11, we in r9).  Tried: the RB3 shape
// (`if (!Showing()) SetShowing(true); else { ++; if (>=) ... }`, with and
// without an overlay local) -- moves this/sret onto r31/r29 (worse); an
// uninitialised overlay local assigned after MemNumHeaps (inert);
// mHeapOverlay read directly at every use (reloads it, worse).
```


## `?Modal@Rnd@@QAAXAAW4ModalType@Debug@@AAVFixedString@@_N@Z`

`Rnd::Modal` -- moved verbatim from `src/system/rndobj/Rnd.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1496-1508):

```text
                // w26-f11 (2026-11-02): the last 6 fuzzy rows are ONE
                // two-register swap between this loop's `type` load and its
                // `gNotifyKeepGoing` load -- image `lwz r11, 0x0(r28)` +
                // `cmpwi cr6, r11, 2` for the type and `lbz r10, -0x2(r30)` +
                // `cmplwi r10, 0` for the bool, ours the mirror (r10 for the
                // type, r11 for the bool). Both are volatile scratch
                // registers, no named leaf on either side, and every branch
                // target and the
                // redundant cmpwi already agree. Binding the bool
                // (`bool &`, `const bool &`, a named bool) is byte-inert --
                // measured -- so the swap sits in regasg's schedule of the two
                // scratch values. Reordering the two tests is not a spelling:
                // it is a different branch structure (and a different program).
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.845, 6 non-equal row(s): One
BRANCHDEST row (idx 149): the image jumps past the `cmpwi cr6, type, 0x2` re-test it
already knows is true on that edge; ours lands on it (r10 still holds the type) and
falls through to the same block. The 5 register rows are that r10/r11 choice. Jump
threading, NOT A BUG.
