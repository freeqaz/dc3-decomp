# Lane notes: `src/system/world/CameraShot.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetCam@CamShot@@QAAPAVRndCam@@XZ`

`CamShot::GetCam` -- moved verbatim from `src/system/world/CameraShot.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 1959-1985):

```text
// w17-b correction (still 98.91): except_data_828161A0 is SetFrame's EH prefix
// (it sits at 0x82816198, immediately BEFORE SetFrame at 0x828161A0), not
// GetCam's.  GetCam's own is except_data_82816028 (before 0x82816028): its
// funcinfo (0x820E4138) has 3 unwind states, ALL with null actions
// (lbl_820E4120 = {-1,0},{0,0},{1,0}), no try blocks, one ip-map entry.  So the
// original had objects whose destruction needed no code at all -- the frame
// pointer is the price of that EH, not of a real cleanup.  A one-NOTIFY_ONCE
// function (SpotlightDrawer::DrawShowing) has NO EH in the image, so the
// statics alone do not explain it.
// RESIDUAL (w12-b, 98.91 canonical): the image runs this with a frame pointer
// (`subi r31, r1, 0x90`) and EH data (except_data_828161A0 right after the
// function), i.e. one more callee-saved GPR, and every other row is that
// register shift. Nothing here has a destructor on our side, so the EH state
// must come from something we do not see (likely how the two
// MILO_NOTIFY_ONCE statics are constructed); not chased further.
// w21-ad (98.91): behaviour re-read vs 82816028..82816194 -- agrees (ret
// defaults to 0, Cam() at 0x210, the two notify arms tail-merged). The two
// NOTIFY_ONCE statics are NOT the EH source: FracToSample@CharBonesSamples
// builds two `_dw` with the identical std/ld/std/stw/stw/atexit sequence and
// has no frame pointer and no EH. Whatever owns the two null-action states is
// something with an empty destructor whose lifetime spans no call.
// w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG -- `subi r31, r1, 0x90`
// is the EH frame pointer, not a this-adjustment.  Every pointer argument
// matches: the inlined GetCrowdDir (0x1b0 test, vbase hop to Dir() at +0x28,
// __RTDynamicCast to WorldDir), Cam() at 0x210, and both PathName arguments
// via the same Hmx::Object vbase conversion.  Re-measured 98.9 canonical,
// equal 57 / diff_arg 34 / del 1; every diff_arg is the r31 frame shift.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED.  Listing: cam = 0;
// GetCrowdDir inlined (mParentDir 0x1b0 else Dir(), __RTDynamicCast to
// WorldDir); cam = 0x210 (PanelDir::mCam's pointer); null -> NOTIFY_ONCE
// "paneldir but no cam" with PathName(crowdDir); no dir -> "no worlddir"
// with PathName(this); returns cam.  Unicorn h4, fresh: DIVERGENT
// data_layout, stable_divergent (RTTI arguments of the cast).  Native:
// CopyPollSemanticsTest.CamShotGetCamReturnsCrowdDirCamera (WorldDir with and
// without a cam, plain ObjectDir, mParentDir override; ignoring mParentDir
// fails it).  ret -> cam, `== 0` -> `!cam`: byte-identical.
```
