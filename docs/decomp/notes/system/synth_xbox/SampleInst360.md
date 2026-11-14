# Lane notes: `src/system/synth_xbox/SampleInst360.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetProgress@SampleInst360@@UAAMXZ`

`SampleInst360::GetProgress` -- moved verbatim from `src/system/synth_xbox/SampleInst360.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 35-88):

```text
    // RESIDUAL (w7-ak, 89.55 canonical): the residual is one extra instruction plus
    // the regalloc cascade it drives. The target re-materialises `voice` inside the
    // `if` with a no-op truncating move (`clrrwi r10, r10, 0` at 0x82...+20) that we
    // do not emit, and keeps `pos` in r11 where we keep it in r9; the `%` operator's
    // two traps (`twllei` / `twi 5`) are scheduled either side of the remainder
    // subtraction instead of before and after it. NEGATIVE RESULT: moving this
    // declaration inside the `if` (testing `mVoice->mLoopStart` in the condition),
    // which is what would produce that re-materialisation, drops it to 88.13.
    // NEGATIVE (w7-bq, 2026-09-15): writing the sum the other way round,
    // `pos = voice->mStartSamp + offset % len;`, is inert -- 89.55224 before and
    // after, same 70 instructions and the same 15 diff_arg / 3 insert / 4 delete
    // profile -- so the trap scheduling is not commutative-operand order.
    // Measured shape of the whole residual: the instruction SET is identical on
    // both sides and only its order differs.  Target runs mullw, andc, subf,
    // twllei, twi, add (0x324..0x338); we run mullw, twllei, subf, andc, add,
    // twi.  That plus the target-only `clrrwi r10, r10, 0` at 0x2f8 is the
    // entire gap: regions 0-15 and 42-69 are 100%, and every one of the 15
    // diff_arg rows is the r11/r9/r8 renaming those two drive.  Note that
    // `lwz` already zero-extends on Xenon, so 0x2f8 is a true no-op the backend
    // chose to emit -- there is no source-level cast that removes an
    // instruction we do not have.
    // BUG FIX (w7-br, 89.6 -> 98.5 canonical, 22 -> 1 rows): the "pure
    // scheduling" residual above was a real behavioural divergence.  The image
    // subtracts mLoopStart on BOTH arms of the loop-end select: the `bne cr6`
    // at 0x82E44744 lands ON `subf r8, r7, r8` at 0x82E4474C (r7 = mLoopStart,
    // r8 = mLoopEnd or, at 0x82E44748, mNumSamples), so len is
    // `(mLoopEnd == -1 ? mNumSamples : mLoopEnd) - mLoopStart`.  Our old
    // `mLoopEnd == -1 ? mNumSamples - mLoopStart : mLoopEnd` jumped PAST the
    // subtraction and took the absolute loop-end position as the loop length
    // whenever an explicit loop end was set, so the modulo wrapped at the
    // wrong period.  Every trap/regalloc row was a consequence of that one
    // missing subf.  The sole remaining row is the no-op `clrrwi r10, r10, 0`
    // at 0x82E44730 on the voice pointer.  NEGATIVE (w7-br), post-fix:
    // declaring `voice` inside the if with `mVoice->mLoopStart` in the
    // condition, 92.2 (the field is reloaded); keeping the outer local but
    // testing `mVoice->mLoopStart`, the same 92.2; `unsigned int pos =
    // state.SamplesPlayed` (implicit narrowing), 97.6 -- pos must be a signed
    // int, the image's `extsw r11, r11` at 0x82E44778 becomes `rldicl`.
    // RESIDUAL CONFIRMED (w9-c 2026-09-30): 98.507 canonical / 98.507 fuzzy, ONE
    // row of 67 -- the target-only `clrrwi r10, r10, 0` at the top of the if-body,
    // a no-op 32-bit truncate of the voice pointer that MSVC re-materialises there
    // and we do not.  Six more pointer-alias spellings refuted, one full ninja
    // each: an inner `Voice *v = voice;` alias (98.507, inert), `Voice *const v =
    // voice;` (98.507, inert), an inner `Voice &v = *voice;` (98.507, inert), an
    // OUTER `Voice &voice = *mVoice;` for the whole body (98.507, inert), an inner
    // `Voice *v = mVoice;` re-read (92.239 -- WORSE, reloads 0xa8(r31)), and no
    // local at all with `mVoice->` at all five sites (82.687 -- much worse, five
    // reloads).  The truncate is not reachable by re-spelling the pointer.
    //
    // NOT A WRONG CALLEE: objdiff's "Function Call Diff" reports the image calling
    // `?GetMaxProcess@CFEWaveStreamDecoder@NUISPEECH@@UAAIXZ` where we call
    // `?GetSampleRate@SynthSample@@QBAHXZ`.  Both resolve to 82AF68F0 in
    // icf_aliases.map -- one proven ICF fold, so the row is charged by neither the
    // canonical nor the name_check ruler.  Do not "fix" the callee.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: one dead
`clrrwi r10,r10,0` on the target side.
