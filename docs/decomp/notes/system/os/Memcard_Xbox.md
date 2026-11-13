# Lane notes: `src/system/os/Memcard_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Mount@MCContainerXbox@@UAA?AW4MCResult@@W4CreateType@@@Z`

`MCContainerXbox::Mount` -- moved verbatim from `src/system/os/Memcard_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 227-261):

```text
    // NEGATIVE RESULT on the block PLACEMENT: our build sinks this whole chain
    // below the epilogue and threads `li r3, 0x570` straight into the Translate
    // call, because ERROR_FILE_CORRUPT is a compile-time constant on that edge.
    // The image does not const-propagate it. Four spellings (early returns,
    // `MCResult result` + if/else-if/else, `switch`, and a `goto` that puts the
    // success block last) all produce the identical sunk layout; only the
    // unfaithful "call Translate first, then overwrite" shape keeps the rows
    // aligned, and it does so with the wrong register. Every instruction here
    // is present and correct -- the residual is ordering only.
    // NEGATIVE RESULT (w7-bq, 2026-09-15), a fifth spelling: the inverted
    // nested form -- `if (res != 3) { if (res != 0xb7) return Translate(res);
    // return kMCCorrupt; } return kMCFileNotFound;` -- reads 83.9 (22 rows, 10
    // inserts / 9 deletes), WORSE than the 87.08 of the flat form below.  The
    // blocker is not arm polarity: it is that MSVC threads the compile-time
    // 0x570 straight past both compares (base falls from `li r3, 0x570` into
    // `bl Translate` with the compare chain sunk below the epilogue), and the
    // image at 0x825F639C..0x825F63B0 does not thread it.  No source spelling
    // tried so far hides the constant on that edge.
    // NEGATIVE RESULTS (w7-br, 87.1 held, same 21 rows unless stated), four
    // more spellings aimed at that constant rather than at the arms:
    //   - the chain as one nested ternary `return res == 3 ? ... : res == 0xb7
    //     ? ... : Translate(res);` -- identical rows;
    //   - the chain moved into an anonymous-namespace `inline` helper and
    //     called from here -- MSVC inlines it and threads the constant
    //     through it exactly as before, identical rows;
    //   - a second `if (res == ERROR_SUCCESS)` test between the corrupt edge
    //     and the success return (three compares for the constant to cross
    //     instead of two) -- threaded through all three, identical rows;
    //   - `res = ERROR_FILE_CORRUPT` written BEFORE the XContentClose call --
    //     86.4: `res` is then kept in r29 across the call (`mr. r29, r3`) and
    //     the whole tail recolours; the image assigns after the call
    //     (0x825F639C) and never leaves r3.
    // The constant is visible on that edge to every pass MSVC runs after
    // inlining; nothing at source level makes it opaque without adding
    // instructions the image does not have.
```

```text
lane E w-low-synth-os (2026-11-13), behaviour audit against the target listing, value by value: NOT A BUG
(87.08).  Both asserts (0x144 !IsMounted, 0x145 device != ANY),
GenerateDriveName(device +8, user +4), XCONTENT_DATA (DeviceID, type 1,
strncpy 42 into szFileName at +0x108, wcsncpy 128 into szDisplayName), the
CreateType -> flags map (0->3, 1->0x14, 2->0x12, else 0), XContentCreateEx's
nine arguments (size from Cid +0xc in the low half), XContentGetCreator, the
not-creator close + ERROR_FILE_CORRUPT (0x570), success = mounted byte +0x10,
and the 3->kMCFileNotFound / 0xb7->kMCCorrupt / else Translate mapping all
agree.  Residual is block placement only.
```
