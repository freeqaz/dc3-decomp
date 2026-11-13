# Lane notes: `src/system/rndobj/DOFProc_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Set@NgDOFProc@@MAAXPBVRndCam@@MMMM@Z`

`NgDOFProc::Set` -- moved verbatim from `src/system/rndobj/DOFProc_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 60-83):

```text
    // RESIDUAL (w7-ar, 99.14 canonical): 3 of 116 rows, all register allocation.
    // The image's Clamp result for mMaxBlur lands in f13, is clobbered by the
    // mMinBlur computation and is RELOADED from 0x48(r31) for the test below
    // (0x826AC110); ours lands in f11, survives, and needs no reload -- so the
    // reload is a delete row and the two producing instructions are reg diffs.
    // The fourth row, `fnmsubs f12, f30, f0, f30` vs our f0/f30 operand order on
    // farFocal, follows the same allocation. Refuted: writing farFocal as
    // `focalPlane - mBlurDepth * focalPlane` (inert, byte-identical result).
    // w26-q (98.9655 fuzzy / 99.13793 canonical, same 4 rows): the regasg tap
    // names the Clamp row exactly. At the LAST fsel of the first Clamp
    // (regasg fn 78 t48, sym s93) the first-fit list reads
    // `f0*phys f13! f12! f11 ...` -- f0 holds the 1.0f literal, and both f13
    // and f12 are still marked interfering, so the selector takes f11 and the
    // value survives; the image takes f13 in place, i.e. its fsel writes the
    // same register it reads, and therefore has to reload it from 0x48 for the
    // test shortly after. The four rows are that one register choice plus
    // the fnmsubs operand order it forces. 40 spellings measured this round --
    // both source orders of the product, the product / the member / `*this`
    // named as a local, `const float &` and `const float *` bindings of
    // mBlurDepth (the reference form survives and costs 3.5 points instead),
    // and all six orders of the three assignment statements (each is 2-14
    // points worse) -- none moves t48 off f11. Open handle: make the t48 use of
    // s91 a last use in the sched3 order, which is what frees f13 for the
    // in-place write.
```
