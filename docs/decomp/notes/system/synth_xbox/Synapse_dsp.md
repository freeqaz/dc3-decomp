# Lane notes: `src/system/synth_xbox/Synapse_dsp.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetAttackSmoothing@Synapse@1DSP@@QAAXM@Z`

`DSP::Synapse::Synapse::SetAttackSmoothing` -- moved verbatim from `src/system/synth_xbox/Synapse_dsp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 139-172):

```text
// w26-f9 (fuzzy 99.75 both, canonical 100.0, 160 B, 40 instructions).  The one
// charged row in SetAttackSmoothing AND the one in SetReleaseSmoothing are the
// SAME mechanism, mirrored -- instruction 26, `add r3, r29, r11` in the image
// but `add r3, r11, r29` in ours.  Identical operands, identical registers,
// identical size; only the operand position differs.  One note covers both.
//
// Tap (c2rs cqlo3, identical key in both functions): the add's use list is
//   M6{0001a008}[d00000000 w029f b V5/k02s04{00000000}]  V5/k02s04{00000000}
// Operand 1 is the MEMORY leaf `*(void **)vp`.  Its base is the named local
// `vp`, so the full `hash(base) << 8` term applies (0xa0 here) and the key is
// 0x1a008.  Operand 2 is `offset` (sid 7), a variable leaf, key 0x100e0.
// 0x1a008 > 0x100e0, so the load sorts first; the image sorts the offset first.
//
// This is NOT the C2RS-BRIDGE 8.8 lever.  8.8 re-binds an operand whose base is
// a TEMP, so the memory leaf stops collapsing to `(base sid & 3) << 14` and
// picks up the full variable-base key.  Here the base is already a named
// variable; there is no temp to re-key, which is why every reference binding
// measured inert.
//
// Measured inert (16 variants, row byte-identical): `void *begin =
// *(void **)vp;` inside the loop and hoisted; `void *&` and `const void *&`
// bindings of the same load; a named element pointer; `void **` for vp;
// `char *` / `PitchCorrectedVoice *` / `unsigned char *` as the load's type;
// `unsigned int offset`; moving the vp/count declarations; the source operand
// swap and parenthesisation.  Measured WORSE: dropping the `vp` local (85.70 --
// the `addi r31, r31, 0x5c` is then re-emitted at each use) and inlining the
// base at the loop site only (90.95, a second addi inside the loop).
//
// Why no spelling reaches it: the variable leaf would have to beat 0x1a008,
// which needs sid > 0x140 (320); a temp leaf would need sid > 640.  Both are
// out of reach in this TU.  The remaining routes are a base whose hash low
// byte is 0 (~1/256) or a temp base with sid % 4 == 0 -- and a temp base costs
// the addi, measured above.  Open question: what sets `hash(base)`, which is
// the whole difference here.
```


## `DSP::Synapse::AsRvalue()`

`DSP::Synapse::AsRvalue` -- moved verbatim from `src/system/synth_xbox/Synapse_dsp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Attribution: no function named `DSP::Synapse::AsRvalue` in report.json (unit); filed under the source name

Above the definition (was lines 203-209):

```text
// Rounds a duration to a sample count.  The rate comes in by const reference
// and the ctor hands it an RVALUE copy of mTargetPitch: binding the prvalue
// materialises a stack temp, which is the three dead `stfs f12, 0x58(r31)`
// in the image (0x82E47120/0x82E47150/0x82E47158).  w7-bj: this took the ctor
// 97.83 -> 99.21.  `float(mTargetPitch)` alone is NOT enough -- MSVC binds the
// reference straight to the member and emits no store (measured 97.83, inert);
// the prvalue has to come out of an inlined by-value helper.
```


## `??0Synapse@0DSP@@QAA@M@Z`

`DSP::Synapse::Synapse::Synapse` -- moved verbatim from `src/system/synth_xbox/Synapse_dsp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 217-226):

```text
    // The 0.4 s ring-buffer length is a LOCAL, rounded down to a multiple of
    // four (the downsampled buffer is a quarter of it).  Only the two period
    // limits -- 1/650 s and 1/60 s -- reach members 0x1c and 0x20; 0x24 keeps
    // the 64 from the initialiser list.
    // w7-bj residual (99.21): the image coalesces the vector-ctor pointer
    // scratch and these rate temps onto the 8-byte fctidz slot at 0x58(r31)
    // and leaves 0x50 to the resize fill temp; ours packs them onto 0x50 with
    // the fill temp (14 `stw ..., 0x58` rows + these 3 stfs, 0x82E4700C..).
    // The rest is commutative add/lwzx/stwx/stfsx operand order and the
    // mInputBuffer begin/end load order in the byte-offset loops.
```

Inside the body (was lines 269-275):

```text
    // Resize each channel buffer to 0x2000 floats and set output buffer pointers.
    // w16-e: plain indexed loops here and below (was hand-stepped byte offsets
    // inside a hand-rotated `if (n) do {} while`): MSVC rotates and
    // strength-reduces them itself, and that also frees 0x50 so the scratch
    // temps above coalesce onto 0x58 as in the image -- 99.21 -> 99.995.
    // Residual (4 rows, voice loop below): `stfsx f31, r10, r9` operand order
    // and the image loading mVoices._M_start before _M_finish for size().
```

Inside the body (was lines 285-295):

```text
    // Zero out voice gains in GranularSynth
    // w17-e: still 99.995 (rows 332-335).  `j < mVoices.size()` (no casts) is
    // byte-identical; `(mVoices.end() - mVoices.begin())` is 99.7 (it changes
    // the hoisted pre-loop copy of the bound too).
    // w19-e: making the stand-in's `GranularVoice *mVoices` a vector (as the
    // real GranularSynth.h VoiceVec is) closes row 332 here (image `stfsx f31,
    // r10, r9`, index first; still 99.995 on rows 333-335, the in-loop bound
    // reload reads _M_start before _M_finish at 82E4755C/60) but drops
    // ProcessInPlace 100 -> 98.79: there the image evaluates GetCorrection()
    // BEFORE loading gs->mVoices, which the raw pointer gives and inline
    // operator[] does not; hoisting the call into a local was 98.1.  Reverted.
```

Inside the body (was lines 315-321):

```text
    // BUG FIX (w16-e): the time constant is 8.16 ms -- the image loads
    // __real@3c05b186 (0.0081600007f) at 82E4763C `lfs f1, ...` for the
    // Time2IirA call at 82E47640.  We passed 0.00811767578125f (0x3c050000),
    // a truncated mantissa, so mIirCoeff was computed from the wrong time
    // constant.  0x3c05b186 is exactly the float product 8.16f * 0.001f (the
    // ms-to-seconds idiom SetAttackSmoothing/SetReleaseSmoothing use); a bare
    // 0.00816f rounds to 0x3c05b185, one ulp low.
```


## `?ProcessInPlace@Synapse@1DSP@@QAAXIPAM@Z`

`DSP::Synapse::Synapse::ProcessInPlace` -- moved verbatim from `src/system/synth_xbox/Synapse_dsp.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 330-366):

```text
// w16-e: 96.75 -> 100 canonical (modulo register permutation: the image
// adds the mVoices base FIRST in `stfsx`/`add`/`lwzx`, 5 commutative-operand
// rows).  Levers: the two per-voice passes are plain indexed loops (were
// m2c's hand-stepped byte offsets in a hand-rotated do-while), and the
// threshold test reads the member mPitchConfidence after the two stores,
// which keeps the confidence in f0 across the mPitchClarity copy (image
// `lfs f12, 0x14(r11)` at 0x684).
//
// w26-f4 (fuzzy lane, 99.76852 fuzzy / 100.0 normalized): all 5 charged rows
// are PENALTY_REG_DIFF with no relocation-name mismatch, and none of them is a
// register CHOICE -- both sides use the same two registers and only the
// operand POSITIONS swap:
//   117/120/123/127  mVoices[v]: image `add r11, r11, r29` and
//                    `stfsx f0, r11, r29` put the vector's _M_start (the
//                    folded load `lwz r11, 0x0(r30)`) in the FIRST operand
//                    slot and the strength-reduced byte offset (r29) second;
//                    we emit the offset first.
//   186              mOutputBuffers[v]: `lwzx r4, r11, r28` vs
//                    `lwzx r4, r28, r11` -- same shape.
// This is C2RS-BRIDGE 8.7's commutative operand-order key, and it is NOT a
// per-site convention: row 21 (`inputStart[mBufferIndex]`) MATCHES on both
// sides with the index in the first slot, in the same function.
//
// Measured inert (normalized held at 100.0 on every one): mVoices[v] ->
// mVoices.begin()[v]; mVoices[v] -> *(mVoices.begin() + v); binding
// `float *outBuf = mOutputBuffers[v]` and passing outBuf; binding
// `PeakDetector *pdPeak`; rewriting the loop bound as `v < mVoices.size()`;
// hoisting `unsigned int temp_r11_2` out of its `if`. Measured and REJECTED
// for shape loss: `for (int v ...)` 99.47/99.70 canon, a named reference to
// the vector 96.98/97.68 canon, moving `temp_r10` below the `& 3` test
// 99.21/99.44 canon.
//
// Open: the deciding quantity is the key of the strength-reduced offset temp
// (0x10000 | sid<<6, C2RS-BRIDGE 8.7) against the folded load's key. The
// deployed c2rs has no CQT dump (strings -a | grep CQT -> 0), so which sid has
// to move is unread here; that is the next step, not a claim that the row is
// unreachable.
```
