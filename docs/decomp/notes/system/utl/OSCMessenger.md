# Lane notes: `src/system/utl/OSCMessenger.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetFloat@OSCMessenger@@QAAMVString@@M@Z`

`OSCMessenger::GetFloat` -- moved verbatim from `src/system/utl/OSCMessenger.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 162-174):

```text
// Same shape as GetInt above, and the same lever: the flag store is written
// first so c2 colours the loaded value onto fValue's own pseudo-register and
// can tail-merge the two returns, which reproduces the image's shared exit
// (0x827E7E08..0x827E7E18, branch back at 0x827E7E68). Behaviourally
// identical -- mHasNewValue (0x89) and buffer (0x8) are disjoint.
// Spelling matters only as a pair, so record what was refuted here at 36.5
// when the lever was still missing: the single tail return (w7-ar) and the
// placeholder arm written first, `if (!val) {...; return fValue;}` (flips the
// branch to `bne` and inlines the placeholder arm); `if (val == nullptr) {...}`
// first; an explicit `if (!val) goto add; ...; add: {...}` (MSVC re-lays the
// goto target as the fall-through); two separate ifs (byte-identical); and
// adding a `return` to the found arm WITHOUT the store reorder, which
// introduces an r28<->r29 swap and stays at 36.5.
```
