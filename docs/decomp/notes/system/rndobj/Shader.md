# Lane notes: `src/system/rndobj/Shader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Init@RndShader@@SAXXZ`

`RndShader::Init` -- moved verbatim from `src/system/rndobj/Shader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 62-68):

```text
// w19-a (76.82 -> 100): the 38 stores are independent, but MSVC schedules them
// from the SOURCE order, so the statement order is what the image encodes.  The
// image's store order (sShaders[0], [2], [27], [1], [5], [4], [6], [3], ...) is
// reproduced by putting BloomGlare right after Bloom and DownsampleDepth ahead
// of Downsample/Downsample4x; everything else stays in enum order.  Found by a
// local search over statement orders (each slot is still assigned exactly once,
// with the same value).
```


## `?MatShaderFlagsOK@RndShader@@KA_NPAVRndMat@@W4ShaderType@@@Z`

`RndShader::MatShaderFlagsOK` -- moved verbatim from `src/system/rndobj/Shader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 179-188):

```text
    // BEHAVIOURAL FIX (w12-a): both fadeout warnings are gated on CheckError(0).
    // The image's `beq` after that call (idx 44) jumps past BOTH arms to the
    // CheckError(1) test; the old `CheckError(0) && !FadeOut() ... else if
    // (FadeOut())` spelling warned "fadeout unchecked" for shaders that do
    // not check fadeout at all.
    // BEHAVIOURAL FIX (w12-a): both fadeout warnings are gated on CheckError(0).
    // The image's `beq` straight after that call (idx 44) jumps past BOTH arms
    // to the CheckError(1) test; the old `CheckError(0) && !FadeOut() ... else
    // if (FadeOut())` spelling warned "fadeout unchecked" for shaders that do
    // not check fadeout at all.
```
