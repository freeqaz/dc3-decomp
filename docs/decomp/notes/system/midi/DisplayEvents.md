# Lane notes: `src/system/midi/DisplayEvents.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DisplayEvents@@YAMPAVDataEventList@@MM@Z`

`DisplayEvents` -- moved verbatim from `src/system/midi/DisplayEvents.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 31-39):

```text
        // ONE Rect for the whole function (w7-bn, 98.96 -> 100.0): the image's
        // 0x80..0x8c is stored both inside the loop and after it because it
        // is the same function-scope local, not two sibling-scope temps being
        // overlaid.  `min++` lives in the for-increment so it lands after the
        // String/DataEvent dtors (82552C6C), and the tail assigns the four
        // fields in x, w, y, h order -- that order is what schedules the
        // image's `stfs f31, 0x88` ahead of `stfs f13, 0x84` at 82552C8C/94.
        // (Named `auto eventRect`/`cursorRect` in sibling scopes: 98.96;
        //  block-scoping the tail: inert; `rect = Hmx::Rect(...)`: 94.2.)
```
