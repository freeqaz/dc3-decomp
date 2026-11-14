# Lane notes: `src/system/rnddx9/ShaderMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Compile@DxShader@@MAA_NW4ShaderType@@ABUShaderOptions@@AAPAVRndShaderBuffer@@2@Z`

`DxShader::Compile` -- moved verbatim from `src/system/rnddx9/ShaderMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 112-123):

```text
// w18-c: 94.37 -> 100.  The two buffers are built with the SIZED ctor,
// `new DxShaderBuffer(0)`, not the default one.  With 0 the D3DXCreateBuffer /
// MILO_FAIL arm folds away and the code is the same two stores, but the EH
// states that arm needed survive: the image has an operator-delete and a
// ~RndShaderBuffer unwind funclet per `new` (r31+0x1104 / +0x1108), and the
// default ctor gave us neither.  Those states are what scheduled the two
// D3DXCompileShaderExA argument blocks (the old 27-row "scheduling floor",
// w7-bh/w7-bn).  Same object built either way: mBuffer == nullptr.
//
// The MakeString name rows are ICF noise: the image's three MILO_ASSERT
// MakeStrings resolve to whichever `MakeString<char[N], int, char[M]>`
// instantiation won the fold (all resolve to 0x824D1870 in icf_aliases.map).
```


## `DxShaderMgr::SetVConstant()`

`DxShaderMgr::SetVConstant` -- moved verbatim from `src/system/rnddx9/ShaderMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Attribution: ambiguous: 5 overloads of `DxShaderMgr::SetVConstant` fit equally (e.g. `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVVector4@@@Z`, `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z`, `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@PAVRndTex@@@Z`, `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@_N@Z`); filed under the source name

Above the definition (was lines 331-340):

```text
// 85.2 canonical (w12-c; was 70.0 at w8-h's floor), SetPConstant(PShaderConstant,
// const Vector4 &) below likewise.  `start` is a SIGNED int holding the logical
// shift: that is what keeps MSVC from fusing the 32-bit `srwi r8,r4,2` with the
// zero-extension its 64-bit consumer (`srd`) needs (an `unsigned int` start
// gives `rldicl r8,r4,62,34`; measured in a scratch TU, every unsigned spelling
// fuses).  What remains is scheduling: the image computes the VertexShaderF
// index (`addi r10,r4,0x78`) at slot 2 and `li r11,1` after the TheDxRnd addi;
// we emit `li` first.  A `__restrict` dst moves the addi up but also hoists
// the `slwi` and scores lower (72.7); statement order, a reference dev, a
// mask temp and a no-dst spelling are all inert (scratch TU).
```

## `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@ABVMatrix4@Hmx@@@Z`

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.

## `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z`

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.
