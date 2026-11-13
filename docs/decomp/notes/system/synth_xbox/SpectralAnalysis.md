# Lane notes: `src/system/synth_xbox/SpectralAnalysis.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Analyze@SpectralAnalysis@DSP@@QAAXPBMPAM@Z`

`DSP::SpectralAnalysis::Analyze` -- moved verbatim from `src/system/synth_xbox/SpectralAnalysis.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 28-42):

```text
    // Magnitude spectrum back into mData0.
    // w7-bx (2026-09-15, 85.90323 -> 95.48387): the image's magnitude loop is
    // ONE induction pointer plus two byte biases -- 0x82E4D4A4 `beq cr6` /
    // 0x82E4D4A8 `mtctr r10` first, THEN 0x82E4D4AC `subf r10, r11, r8` /
    // 0x82E4D4B0 `subf r9, r11, r9` off the mData5 (im, 0xd0) walker r11,
    // and `lfs f0, 0x0(r11)` / `lfsx f13, r10, r11` / `stfsx f0, r9, r11`.
    // Index-based `re[k]` / `im[k]` / `mag[k]` (NOT named char* biases, NOT
    // walking pointers -- w7-ap's 81.0 / 85.9 spellings) reproduce all of it:
    // MSVC strength-reduces the three same-stride arrays to one walker plus
    // biases, and emits the zero-trip guard and mtctr before the biases when
    // the biases are its own, not named locals.  Under /fp:fast the image
    // squares im (the walker) FIRST (0x82E4D4B4 `lfs f0, 0x0(r11)` /
    // `fmuls f0, f0, f0`, then `fmadds f0, f13, f13, f0` on re); the source
    // spelling that lands there is the OPPOSITE order, `re*re` then
    // `im*im + acc` -- `im*im` first walks re instead.
```

Inside the body (was lines 53-75):

```text
    // Spectral window recombination over the first half, using the sin/cos
    // table, accumulating the cosine term into mAccum.
    // w7-bx: the table pointers are loaded at 0x82E4D4E8/0x82E4D4EC, ABOVE
    // the `data[0]` store and the `ble` guard at 0x82E4D524 -- they must be
    // named locals declared BEFORE the `data[0] = ...` store, or MSVC keeps
    // the member loads below the (possibly aliasing) stfs through `data`.
    // RESIDUAL (w7-bx, 95.48387): the image biases BOTH tables off the data
    // walker (0x82E4D52C `subf r8, r4, r8`, 0x82E4D530 `subf r7, r4, r7`,
    // then `lfsx f10, r8, r11` / `lfsx f12, r7, r11`); ours chains the
    // second table off the first (`subf r6, r8, r7` = sin - cos, then
    // `add r7, r8, r11` / `lfsx f9, r7, r6`), which costs r5 for `quarter`
    // (image keeps it in r6), moves `addi r9, r9, 0x1`, and renames f9-f12.
    // Tried: cosT declared first (only swaps the two lwz targets), int vs
    // unsigned i (identical), pc before ps (fixes the lwz pair, kept),
    // walking lo/hi pointers for data (93.3), mSinTable[i]/mCosTable[i]
    // read directly in the loop (92.2, loads land inside the loop).
    // w20-e (still 95.48387, same 15 rows 54-88): also tried and refuted --
    // `for (int i = 1; i < quarter; ++i)` (93.2: trip count becomes bdnz),
    // s/c table loads hoisted to the top of the body or swapped (identical),
    // `const float *` tables (identical), table loads folded into the pc/ps
    // products (identical), a separate decrementing hi index for
    // data[half - i] (94.2).  The cos-off-sin bias chain survives every
    // spelling of the loop body.
```


## `?SetMode@SpectralAnalysis@DSP@@QAAXII@Z`

`DSP::SpectralAnalysis::SetMode` -- moved verbatim from `src/system/synth_xbox/SpectralAnalysis.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 135-139):

```text
    // Grow the FFT size (power of two) until it spans the window plus hop.
    // w15-b (98.36 -> 100): a plain `while`, which MSVC rotates into the
    // image's `windowSize + hop > 8` guard plus a do-while that re-reads
    // mWindowSize every trip and keeps the doubled value in a `clrrwi` copy.
    // w7-ak's hand-rotated if/do-while folded both away (see its notes in git).
```
