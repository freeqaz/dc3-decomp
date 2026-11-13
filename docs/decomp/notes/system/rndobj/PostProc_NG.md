# Lane notes: `src/system/rndobj/PostProc_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Bloom_Blur@@YAXPAVRndTex@@0W4BloomBlurStyle@@W4BloomBlurDirection@@IMM@Z`

`Bloom_Blur` -- moved verbatim from `src/system/rndobj/PostProc_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 57-62):

```text
// BUG FIX (w18-a): the SOURCE texture is the first parameter and the render
// target the second.  The image keeps param 1 (r3) in r26 and param 2 (r4) in
// r30, and every texDst use reads r30: the `texDst->Width() > 0` assert loads
// 0x58(r4) first (0x826AA6A8), MakeDrawTarget and FinishDrawTarget are called on
// r30, and SetDiffuseTex gets r26.  With the parameters named the other way
// round every call site drew into the texture it meant to sample.
```


## `?DoBloom@NgPostProc@@IAAXXZ`

`NgPostProc::DoBloom` -- moved verbatim from `src/system/rndobj/PostProc_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 213-235):

```text
        // NOTE (w7-b, 2026-09-14): the only residual on this function is the
        // SCHEDULE of the 16-byte copy out of this temporary. The image loads
        // w1,w0,w3,w2 (clobbering the source pointer r10 with the last load) and
        // then stores w1,w2,w3; we load w1,w0,w2,w3 and recycle r11 for w3, which
        // forces `stw r11,0x4(r7)` two slots earlier. Refuted: naming the temporary
        // as a local, swapping this statement with the s_prevBloomIntensity
        // store, and writing the four Colour components as `-1.0f` instead of
        // `-1` (w26-h) -- every one left all 6 rows byte-identical.
        // The remaining difference is a sched0 tie inside the 16-byte copy: the
        // image loads w1,w0,w3,w2 and reuses the source pointer r10 for the last
        // load (so it dies there), then stores w1,w2,w3; we load w1,w0,w2,w3 and
        // recycle r11 for the third, which forces `stw r11,4(r7)` two slots
        // earlier.  No source spelling found moves the death point of the temp.
        // w27-l (2026-11-02, 99.584366 canonical / 99.5535 fuzzy, 486 rows):
        // still exactly that schedule -- the charged rows are idx 458-463, the
        // 16-byte copy out of the temporary, and nothing else: `lwz r6,0xc` /
        // `lwz r10,0x8` (image) against `lwz r6,0x8` / `lwz r11,0xc` (ours), with
        // the image's `stw r11,0x4(r7)` one slot earlier.  Two further rows in
        // objdiff's "Function Call Diff" block are not source differences and
        // cost nothing: Hmx::Color::operator== and Hmx::Rect::operator== are one
        // ICF body (0x82712120 in icf_aliases.map), and our `sBloomDebugCounter`
        // relocates to 0x830A8A30, which symbols.txt itself calls
        // `lbl_830A8A30` -- the image has no name there to match.
```
