# Lane notes: `src/lazer/meta_ham/HamSongMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?InitializePlaylists@HamSongMgr@@QAAXXZ`

`HamSongMgr::InitializePlaylists` -- moved verbatim from `src/lazer/meta_ham/HamSongMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 425-433):

```text
// w8-l: 99.856820 normalized, the only function short of 100% in this unit
// (94/95) and the largest row in this lane's worklist at 1760 bytes.  63
// charged rows out of 440: essentially every frame reference in the body is
// +16 from the image's, i.e. one extra 16-byte local near the bottom of our
// frame pushes everything above it.  Worth knowing before picking it up: the
// target-only / base-only callees objdiff lists here (MakeString template
// lengths, and _M_find<MoveRatingHistory::Key> vs _M_find<Symbol>) are ICF
// folds of byte-identical instantiations, not wrong callees -- do not chase
// them.  Measured only in this wave.
```

Inside the body (was lines 471-497):

```text
    // w7-bj: 99.86, all 63 rows are r31 frame-slot offsets (+0x4/+0x10/+0x20).
    // The image shares 0x54 between the assert line temps and crewSym and
    // parks the loop-2 `new Playlist()` home at 0x88 among the statement
    // temps (0x828C9E48); ours puts that `new` home in 0x54, which pushes
    // crewSym to 0x5c, the map from 0x60 to 0x70 and the buffers up 0x20.
    // Refuted (all 99.857, slot map unchanged): unnamed `playlistMap[k] =
    // new Playlist()`; the two char arrays moved into the loop body; one
    // `Playlist *p` at loop-body scope; early `continue` instead of the
    // nested if.  The MakeString<char[19],int,char[5]> vs <char[15],...>
    // call-name rows are the per-TU MakeString ICF fold (the image loads the
    // 15-byte "HamSongMgr.cpp" literal at 0x828C9AC8), charged 0 under
    // name_check -- not a string bug.
    // The shipped image sprintf()s to TWO distinct stack addresses here
    // (r31+0x90 for "%d0s", r31+0xa0 for "%s_dynamic_playlist"), so the
    // original had two separate char arrays, not one shared scratch buffer.
    // The 0x10/0x60 split is what keeps the frame at the image's 0x1c0.
    // w21-az (99.857 -> 100): three changes, measured in this order.  (1)
    // crewSym is a `const Symbol &` bound to GetCrewForCharacter's returned
    // temporary: as a named Symbol MSVC colours the loop-2 `new` home into
    // 0x54 ahead of it; as a lifetime-extended temporary it takes 0x54 and
    // the `new` homes go to 0x88, exactly the image's slot map (63 -> 9
    // rows, only the buffers left +0x10).  (2) Both char arrays scoped in
    // the if-block: decadeBuffer then shares 0x90 with the map-ctor temp, as
    // in the image (3 rows, frame 0x1a0 vs 0x1c0).  (3) buffer is 0x80, not
    // 0x60: with the arrays at the image's 0x90/0xa0, the image's 0x1c0
    // frame leaves exactly 0x80 for buffer.  (Natively only a larger
    // scratch buffer.)
```
