# Lane notes: `src/system/rndobj/TexBlender.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawBlendList@RndTexBlender@@IAAXABV?$vector@U?$pair@PAVRndTexBlendController@@M@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@PAVRndTexBlendController@@M@stlpmtx_std@@@2@@stlpmtx_std@@W4TexState@1@@Z`

`RndTexBlender::DrawBlendList` -- moved verbatim from `src/system/rndobj/TexBlender.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 126-132):

```text
        // The shader manager is bound to a reference for the SetVConstant call
        // only.  Through the global, c2 made the hoisted vptr load (live across
        // the Matrix4 ctor) a colour candidate at pri 22, which took r28 ahead of
        // texmap (pri 13) and lowered hi/it and state/addr into the wrong pairs;
        // through the reference the vptr is a regasg temp, and texmap r28 /
        // vptr r27, it r26 / &TheShaderMgr hi r25, state r20 come out as in the
        // image.  (w7-ag measured this spelling as worse; the tree has moved.)
```


## `?DrawShowing@RndTexBlender@@UAAXXZ`

`RndTexBlender::DrawShowing` -- moved verbatim from `src/system/rndobj/TexBlender.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 171-212):

```text
// w18-a (99.13 canonical, 128 rows, frame 0x250 vs the image's 0x240): the
// whole residual is one compiler spill slot.  The image keeps a CSE'd
// &TheDebug / Name() temp at 0x7c -- inside customList's 4-byte tail pad
// (vectors at 0x70/0x80/0x90) -- so the DrawRect Hmx::Color temp fits at 0xa0
// under the Rect at 0xb0; we put that temp at 0x70, which pushes the vectors to
// 0x78/0x88/0x98 and the Color above the Rect (0xc0), +0x10 for every slot
// after.  Naming the Color as a local before DrawRect is byte-identical.
// w20-s: branch rows 476/610 land one row apart only because the image loads
// customList's begin (`lwz r25, 0x70(r31)`) into r25 before DrawBlendList and
// reuses it for the deallocate; we reload it after (0x78(r31) in our frame).
// DrawBlendList takes the vector by const&, same value.  ARTIFACT.
// w26-i (99.68 canonical, 60 rows): every row re-read -- no immediate, call or
// field differs, so this is behaviour-equivalent.  The r28/r29 rows are NOT a
// two-value exchange: the image holds TheShaderMgr's pointer in r26 and
// `work` (= TheShaderMgr.GetWork()) in r29, while we hold them in r29 and r28,
// and we additionally emit two `stw r29, 0x50(r31)` home stores around the
// Matrix4 ctor / SetTransform calls (idx 376, 511) that the image does not.
// Treat that as a live-range difference on a rematerialisable global load
// rather than a priority tie (hypothesis: c2 gave the pointer a home because
// it spans two calls, and the image did not).  The rest of the rows are the
// Rect/Color temporary slots covered by w18-a above.
// w26-n3 confirmed the mechanism and found the lever, which CANNOT be landed as
// written.  DrawShowing's 2 home stores come from the inlined DrawBlendList body
// (`TheShaderMgr.GetWork()` -> SetVConstant -> SetTransform), and the manager is
// already bound to `shaderMgr` for SetVConstant only.  Binding SetTransform too
// (`shaderMgr.SetTransform(xfm)` below) moves DrawShowing 99.63126 -> 99.76479
// canonical / 99.14568 -> 99.19575 fuzzy (per-object path; report.json reads
// 99.68437 / 99.24431 for the same source) -- but it drops DrawBlendList's own
// out-of-line symbol from 99.972374 to 98.53591 on the SAME ruler (it is 100.0
// in report.json), because that spelling was chosen
// for DrawBlendList's registers in the first place (see the note above).  One
// source, two contexts: the inline copy wants the reference, the standalone
// symbol wants the global.  Binding only the trailing SetVConstant is far worse
// (97.80425), and binding the manager at the top for GetWork is worse (99.47344).
// A landable fix would have to reach the inlined copy alone, which the source
// cannot express here.
// w32-a: the POINTER form of the same binding (`RndShaderMgr *shaderMgr =
// &TheShaderMgr;`) is byte-identical here -- 99.63126 canonical on the
// per-object path, the same digits as the reference, so the pointer/reference
// asymmetry measured on NgEnviron::Select (w31-c) does not transfer to this
// global.  The 2 surplus instructions remain the `stw r29, 0x50(r31)` home
// stores the image does not emit.
```
