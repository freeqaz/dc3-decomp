# Lane notes: `src/lazer/meta_ham/ShellInput.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@ShellInput@@QAAXXZ`

`ShellInput::Poll` -- moved verbatim from `src/lazer/meta_ham/ShellInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 134-142):

```text
    // Both builds run the image body (0x82902CC0). Since w23-sk ShellInput::Init
    // creates every member read here natively too (both gesture filters, the
    // DepthBuffer, the SkeletonIdentifier, the SkeletonChooser, the
    // SkeletonExtentTracker; mWrongHandPosAnim is a fail=true Find), so the old
    // native arm's null tests were dead. It also polled the SkeletonChooser only
    // while NumTrackedSkeletons() > 0; the image polls it unconditionally
    // (`lwz r3,0xd0(r22); bl SkeletonChooser::Poll` at 0x82903454, no test), and
    // the chooser body is written for an empty sensor -- Kinect with nobody in
    // view is its ordinary state.
```
