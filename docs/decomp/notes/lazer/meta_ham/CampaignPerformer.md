# Lane notes: `src/lazer/meta_ham/CampaignPerformer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnMovePassed@CampaignPerformer@@UAAXHPAVHamMove@@HM@Z`

`CampaignPerformer::OnMovePassed` -- moved verbatim from `src/lazer/meta_ham/CampaignPerformer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 223-230):

```text
                // `songname` is a TEMPORARY bound to a const reference, not a
                // named Symbol local (w7-bn, 99.98 -> 100.0).  The image builds
                // GetSongName's result straight into the generic temp slot at
                // 0x54 (828F1118), the same slot the MakeString/assert temps
                // and moveVariantName use; a named `Symbol songname` is pinned
                // to its own slot (0x60) and pushes the found-block GetSong()
                // temp to 0x64 (bg's 6 rows).  Refuted by bg: unnamed temp in
                // the `if` -> 99.69, `Symbol songname;` hoisted -> 99.08.
```
