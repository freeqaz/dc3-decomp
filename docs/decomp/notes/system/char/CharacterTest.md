# Lane notes: `src/system/char/CharacterTest.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CharacterTest@@QAAXXZ`

`CharacterTest::Poll` -- moved verbatim from `src/system/char/CharacterTest.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 378-382):

```text
            // w13-c: 99.386 -> 100.  FileSystemRoot() is passed uncast (the
            // image calls MakeString<const char *>, not <char *>), and the
            // FilePath is a NAMED local in its own block: the image re-derives
            // `addi r3, r31, 0x58` for LoadObjects instead of forwarding the
            // ctor's return, and destroys the path straight after the call.
```
