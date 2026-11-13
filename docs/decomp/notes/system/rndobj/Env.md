# Lane notes: `src/system/rndobj/Env.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Copy@RndEnviron@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

`RndEnviron::Copy` -- moved verbatim from `src/system/rndobj/Env.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_COPYS); symbol matched by the name the macro expands to

Inside the body (was lines 235-239):

```text
            // w8-l: naming the source of this 0x94-byte struct copy is what
            // orders memcpy's two address setups the way the image does
            // (addi r4 source before subi r3 dest).  Plain
            // COPY_MEMBER(mColorXfm) emits them the other way round and is
            // the only thing that kept this unit off 100%.
```
