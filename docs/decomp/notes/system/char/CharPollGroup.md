# Lane notes: `src/system/char/CharPollGroup.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SortPolls@CharPollGroup@@QAAXXZ`

`CharPollGroup::SortPolls` -- moved verbatim from `src/system/char/CharPollGroup.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 121-132):

```text
    // 100% (w7-br, from 89.03).  w7-ak and w7-bl correctly located the whole
    // residual at one MSVC decision -- the image re-derives polls._M_start /
    // _M_finish from 0x60/0x64(r31) after every `insert` (0x823A8B44-58) where
    // we hoisted them into r23/r25 -- and correctly found that no spelling of
    // this loop (reference element, materialised iterator, explicit
    // insert(end())) moves it.  It was not the loop: this TU also DEFINED
    // CharPollableSorter::Sort (a duplicate of Character.cpp's, where the
    // image has it), so MSVC could see that Sort never retains &polls, `polls`
    // never escaped, and hoisting across the opaque insert was legal.  With
    // Sort external, as in the image, polls escapes through `sorter.Sort(polls)`
    // and the reload -- and the homed operator[] reference at 0x54, and
    // __savegprlr_25 -- all follow.
```
