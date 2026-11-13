# Lane notes: `src/system/meta/StoreEnumeration.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@XboxEnumeration@@UAAXXZ`

`XboxEnumeration::Poll` -- moved verbatim from `src/system/meta/StoreEnumeration.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 148-155):

```text
    // FRAME SLOTS (w7-bn, 2026-09-15): the image homes the four 4-byte locals
    // as insert-pos copy 0x50 / insert result 0x54 / bytesReceived 0x58 /
    // overlappedResult 0x5c.  Every spelling that gives the ERROR_NO_MORE_FILES
    // arm below a conversion TEMPORARY for its MakeString argument adds a
    // fifth 4-byte object and the packer reverses all four (97.5 canonical,
    // 22 offset rows); see the note at that arm.  Declaration order and scope
    // of `it` / the DWORDs were inert (function scope, before bytesReceived,
    // push_back instead of insert(end()): all 97.5 with identical rows).
```

Inside the body (was lines 222-245):

```text
        // THE THREE ERROR ARMS WERE ROTATED.  0x82E1D448 sends overlappedResult
        // == 0x12 (ERROR_NO_MORE_FILES) to .L_82E1D518, the "error no more
        // files" block -- which in our source was `error_no_more`, and NOTHING
        // BRANCHED TO IT.  0x82E1D454 sends 0x65b to .L_82E1D488, the
        // extended-error / winsock block.  And the FALLTHROUGH at 0x82E1D458 is
        // the "overlapped failed with ... extended ..." message, where our
        // source called XGetOverlappedExtendedError and threw the result away.
        // RESOLVED (w7-bn, 2026-09-15, 88.7 -> 100.0 canonical): the image's
        // dispatch is two FORWARD beq's with the bodies laid out default /
        // 0x65b / check_more_offers / error_no_more (0x82E1D444-0x82E1D518).
        // It is a `switch` written in CASE ORDER 0x12 / 0x65b / default.  MSVC
        // lays the compare chain out with the `default:` body as the
        // fall-through no matter where `default:` is written, and the three
        // arms share the `TheDebug << str; mEnumerating = false; return` tail
        // at .L_82E1D534: the cross-jumped copy that is KEPT is the earliest
        // one in source order, so the 0x12 arm must be written first for its
        // copy to be the one at .L_82E1D534.  w7-bi measured the same switch
        // with `default:` written first at 45.0 (210 instructions) and read
        // it as "MSVC duplicates the continue_enum tail"; re-measured (S1),
        // the 45.0 is a pure block-order difference plus two constant-
        // propagation rows, not tail duplication.  Still refuted: nested
        // inverted ifs (bi, 45.0, byte-identical to default-first) and a
        // forward `if (r == 0x12) ... else if (r == 0x65b) ... else` chain in
        // image order (37.0: bne inversions and 216 instructions).
```
