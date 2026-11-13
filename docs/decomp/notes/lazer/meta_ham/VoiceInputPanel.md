# Lane notes: `src/lazer/meta_ham/VoiceInputPanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CreateSongSelectGrammar@VoiceInputPanel@@QBAXVSymbol@@@Z`

`VoiceInputPanel::CreateSongSelectGrammar` -- moved verbatim from `src/lazer/meta_ham/VoiceInputPanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 251-268):

```text
        // RESIDUAL (w7-ab, then w7-ak 2026-09-14).  Class (a) below is CLOSED:
        //  a) FIXED (w7-ak).  w7-ab read this as "the image emits the begin()
        //     load before the end() load and swapping the two declarations is
        //     inert, so it is scheduling".  It is not scheduling -- it was the
        //     named `VoiceContext **end` local.  Deleting it and spelling
        //     `mVoiceContexts.end()` at all three test sites puts the two loads
        //     in the image's order (0x4c then 0x50) and removes the
        //     (0x4c,0x50) OFFSET_SWAP entirely: 39 -> 37 rows.
        //  b) STILL OPEN: the image re-loads TheSpeechMgr->Overlay() at each of
        //     the three uses in the deactivate block (`lwz 0x48(r11)` at the
        //     Showing() test, again to fetch the vtable before the MakeString
        //     call, and a third time for `this` after it -- and it re-reads
        //     TheSpeechMgr itself in between).  We CSE the overlay pointer into
        //     callee-saved r30 and hold it across the call.  Both sides already
        //     spell the two Overlay() calls separately in source, so this is
        //     MSVC choosing to spill where the image rematerialises; the knock-on
        //     is that TheSpeechMgr's anchor lands in r28 for us and r29 for the
        //     image, which is the whole 10-row r28<->r29 swap.
```

Inside the body (was lines 289-298):

```text
            // No named temp for the message: the image loads the overlay's
            // vtable pointer into a callee-saved register BEFORE the MakeString
            // call (`lwz r30, 0x0(r11)` at 0x82957BCC, then `lwz r10, 0x4(r30)`
            // after), which is what the one-expression form emits.  And the
            // object is the member LVALUE `mOverlay`, not the `Overlay()`
            // accessor: an lvalue object expression is re-evaluated for `this`
            // after the argument call (`lwz r11, TheSpeechMgr; lwz r3, 0x48(r11)`
            // at 0x82957BD4..0x82957BE0), whereas the accessor's rvalue is homed
            // in r30 and held across the call -- the whole 10-row r28/r29
            // rotation and both replace rows (93.4 -> 100.0, w7-bv).
```
