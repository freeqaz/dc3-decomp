# Lane notes: `src/system/rndobj/SoftParticleBuffer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?BlurSurface@RndSoftParticleBuffer@@AAAXXZ`

`RndSoftParticleBuffer::BlurSurface` -- moved verbatim from `src/system/rndobj/SoftParticleBuffer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 65-94):

```text
        // Open residual (wave 7, lane w7-y), 95.7 canonical, 82 rows, all of
        // them one allocation difference plus two structural rows:
        //
        //  * The image does NOT hold 0.5f in a callee-saved FPR.  It keeps the
        //    literal's page in a callee-saved GPR (`lis r20, __real@3f000000@h`)
        //    and RE-LOADS `lfs f0, ...@l(r20)` twice -- once before the
        //    static-guard test and once at the top of the tap loop, entered via
        //    a `b` that skips the second copy on the first trip.  That is why
        //    the image opens `__savegprlr_17` / `__savefpr_20` where we open
        //    `_18` / `_19`: one more GPR, one fewer FPR, and a frame 8 bytes
        //    shallower.  Every f19..f31 and r17..r30 row follows from it.
        //    `invW / 2.0f` instead of `invW * 0.5f` is INERT (still 95.7) --
        //    /fp:fast folds it to the same multiply by the same literal.
        //  * The image emits only NINE stores into kBlurTaps, 0x4 through 0x24:
        //    element [0].x is never stored because the image keeps it
        //    statically initialised in .data.  We emit ten.
        //
        // The SetObjConcrete row (target ObjRefConcrete<AnimTask,ObjectDir>, we
        // ObjRefConcrete<RndTex,ObjectDir>) is an ICF fold, and kBlurTaps
        // (0x82F16D28) and its guard (0x830E1CE8) are unnamed lbl_* on the
        // target side, i.e. missing from config/373307D9/symbols.txt.
        //
        // (weight, offset) pairs for the five taps.
        // w22-a15: tap 0's weight is 0.1, not 0.0.  The image's table
        // (lbl_82F16D28, .data, size 0x28) reads `.float 0.1` at +0x0 and the
        // nine dynamic stores (BlurSurface rows `stfs f26, 0x4(r25)` ..
        // `stfs f21, 0x24(r25)`) fill the rest; weights 0.1/0.25/0.3/0.25/0.1
        // sum to 1.  The old 0.0 (from a misread of that elided store as a
        // .bss zero) dropped the first tap from the blur.  rb3-xenon's
        // W16-PU (2f6d26cda) reads the same 0.1 out of its retail .data.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: kBlurTaps
values identical; the target statically initialises [0]=0.1 in .data where we store it.
