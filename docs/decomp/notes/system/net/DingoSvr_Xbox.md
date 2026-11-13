# Lane notes: `src/system/net/DingoSvr_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@DingoSvrXbox@@UAAXXZ`

`DingoSvrXbox::Poll` -- moved verbatim from `src/system/net/DingoSvr_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 151-170):

```text
        // NEGATIVE RESULT (w7-ar, 2026-09-14): this arm is BEHAVIOURALLY correct
        // and scores LOWER; keep it. Both messages below reach ?Notify@Debug@@ in
        // the image -- there is no ?Warn@Debug@@ anywhere in the target's Poll --
        // and the image cross-jumps them into ONE MakeString/Notify tail that
        // lives at the FIRST message's site (.L_825569B0 = 0x825569B0), with the
        // second message's block doing `addi r3, <str2>` / `b .L_825569B0`
        // BACKWARD into it (0x825569DC).
        //
        // MSVC merges the same two tails for us but puts the tail at the SECOND
        // site and branches forward from the first, which costs 5.5pp: 94.57
        // canonical for the unfaithful spelling (MILO_WARN on the second message,
        // no cross-jump at all) vs 89.11 for this one. Measured twice, by two
        // lanes. The residual is exactly those 14 rows -- the shared tail's
        // position, nothing else; every test, every branch condition and both
        // string operands already match.
        //
        // Refuted: swapping the two tests (changes which string goes with which
        // condition -- not a spelling of the same program); hoisting both
        // messages to one `const char *msg` site after the chain (produces one
        // tail plus a null test, which is a different shape from a cross-jump).
```
