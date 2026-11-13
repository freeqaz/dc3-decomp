# Lane notes: `src/system/char/CharPollable.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0Dep@CharPollableSorter@@QAA@ABU01@@Z`

`CharPollableSorter::Dep::Dep` -- moved verbatim from `src/system/char/CharPollable.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 16-21):

```text
        // w19-c BEHAVIOUR FIX: the image's map<Object*, Dep>::operator[]
        // (char/Character.obj) stores 0 to the default value's obj (0x60(r31))
        // and poll (0x6c(r31)) and leaves searchID alone, i.e. this ctor.
        // Without it MSVC's value-initialisation of this non-POD left obj as
        // stack garbage, and AddDeps tests `if (!mapDep.obj)` to recognise a
        // freshly inserted entry.  rb3-xenon recovered the same ctor.
```
