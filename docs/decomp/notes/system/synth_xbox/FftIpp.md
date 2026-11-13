# Lane notes: `src/system/synth_xbox/FftIpp.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?FftRealCcs@FftIpp@@QAAXPIBMPIAM@Z`

`FftIpp::FftRealCcs` -- moved verbatim from `src/system/synth_xbox/FftIpp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 22-44):

```text
    // NOTE (w7-av): 97.55 is a CR-field floor.  Every one of the 49
    // instructions matches except the condition-register field of this one
    // compare and its branch: the image writes
    //     82E4DC24  cmplwi r29, 0x0     (cr0)
    //     82E4DC28  beq    .L_82E4DC3C
    // and we write `cmplwi cr6, r29, 0x0` / `beq cr6`.  Everything before
    // (indices 0-32, including the assert's own cr0 `cmpwi r3, 0x0`) and
    // everything after (35-48) is byte-identical, so there is no liveness or
    // scheduling difference left to steer the allocator with.  Refuted, all
    // measured: hoisting `n` above the MILO_ASSERT (93.53 -- it splits the
    // single `lwz r29, 0x0(r31)` into an extra load before the assert),
    // `if (n)` instead of `if (n != 0)` (97.55, identical rows), and dropping
    // the local entirely so the guard and the tail both index `mSize`
    // directly (97.55, identical rows -- MSVC CSEs it back into one r29).
    // NEGATIVE RESULT (w9-a): four more spellings, each a full ninja, all exactly
    // 97.55102 with the same two rows -- an inverted `if (n == 0) {} else {...}`,
    // the `if` wrapped in a redundant inner block, the brace-less one-liner, and a
    // `while (n != 0) { ...; break; }`.  Note what the shape of the residue rules
    // out: the FIRST guard at 0x82E4DBB8 is `cmplwi cr6` in BOTH builds and the
    // assert's own `cmpwi r3, 0x0` is cr0 in both, so MSVC is not simply
    // preferring one field -- it reuses cr0 for the third compare where we take
    // cr6, after an identical compare/branch/call prefix.  Nothing in the source
    // names a condition register.  CR-allocation floor.
```
