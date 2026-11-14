# Lane notes: `src/system/world/SpotlightDrawer_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?RenderSphere@NgSpotlightDrawer@@IAAXPAVSpotlight@@@Z`

`NgSpotlightDrawer::RenderSphere` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 190-197):

```text
    // w25-gh: ownerColor, brighten and mat are named for c2's commutative
    // operand sort (C2RS-BRIDGE 8.7).  A red channel read at displacement 0
    // from a named Color reference stays a memory leaf keyed by the
    // reference's sid; with ownerColor at sid 8 and matColor at sid 16 (both
    // 0 mod 8) the key is 0x10008, below intensity's and r's variable keys,
    // so both red multiplies come out `fmuls f0,f8,f12` / `fmuls f0,f0,f10`
    // as in the image.  green/blue/alpha are displaced loads (subexpressions)
    // and keep the load first.
```


## `?RenderBeams@NgSpotlightDrawer@@IAAXABVMatrix4@Hmx@@@Z`

`NgSpotlightDrawer::RenderBeams` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 283-317):

```text
            // RESIDUAL (w7-am, 99.8 canonical): the only rows left are the
            // three loop-invariant MILO_ASSERT address constants.  The image
            // parks "false" in r27, TheDebug in r26 and the __FILE__ string in
            // r25 (82821C44-82821C50); we get the same three registers in a
            // 3-cycle rotation.  Both sides hoist the same four `lis` and the
            // same three `addi` out of the loop, so this is allocation order
            // inside the shared MILO_ASSERT expansion, not anything this
            // function spells.
            // STOP (w21-bi, still 3 rows, 99.76 normalized): the image emits
            // the three `lis` as "false", TheDebug, __FILE__; we emit TheDebug,
            // __FILE__, "false". A standalone cl.exe probe of this loop
            // reproduces ours and shows the order follows the assert
            // EXPRESSION, not the code around it. Inert there or in the build:
            // MILO_ASSERT_IF / MILO_ASSERT_EXPR / bare `TheDebugFailer <<`, a
            // named msg local, the switch in a static inline helper, if/else
            // chain, default arm first, assert after `shaderShape = 0`, a plain
            // for loop instead of the rotated do/while, sl->HasBeam() for the
            // bool (identical code), an earlier "false" literal in the TU.
            // `TheDebug.Fail(MakeString(...), 0)` reorders to __FILE__, "false",
            // TheDebug -- a third order, not the image's. `int shaderShape = 0`
            // with no-op arms costs 93.2.
            // (w26-h) MEASURED with the c2rs tap: the assert's three address
            // constants are code-band mints, C=6 with F=5 demotions, so they get
            // ids 37 (fresh, `false`), 27 (TheDebug) and 5 (__FILE__) and pop in
            // that descending-id order -- exactly our register order.  The image
            // pops __FILE__, `false`, TheDebug, i.e. __FILE__ must be the SIXTH
            // code mint and `false` the third, so its address-computation IL
            // order differs from ours (ours is the MakeString argument order).
            // Changing F only rotates the pop onto the other two rotations
            // (F=4 -> `false`,__FILE__,TheDebug; F>=6 -> TheDebug,__FILE__,`false`),
            // so no free-list size reaches the image's order.  (An attempt to
            // move F with a `float len = sl->mBeam.mLength;` local did NOT
            // demote -- CDEMOTE stayed 5 -- so that test only shows c1 folds a
            // named float copy of a load; F itself was not moved.)  The mint
            // order is the unclosed question.
```


## `?RenderConeDefs@NgSpotlightDrawer@@IAAXPAVSpotlight@@ABVColor@Hmx@@@Z`

`NgSpotlightDrawer::RenderConeDefs` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 384-396):

```text
    // w27-h stop note (96.48340 canonical / 95.27490 fuzzy).  All four
    // insert/delete clusters read against the image: 161 is an `fcmpu` the
    // image schedules one slot earlier than we do (both sides then branch on
    // the same cr6 into the same arm); 224/227 and 256/259 are the same load
    // moved across a store; 286-294 is the minRad/offset block, where the image
    // hoists `lfs m.z.y`/`lfs m.z.x` above the `fmuls` and we re-load after.
    // No value, constant or callee differs in any of the four clusters.  The
    // bulk of the row is 43 register-swap rows (8 FPR pairs, f0/f13 dominated)
    // plus a -32 and a -64 shift of two Vector4 stack objects -- c2 ordering
    // and colouring.  w27-h ran no variant sweep here past the association and
    // if/else spellings already recorded above; the open hypothesis is the 8.7
    // key on the f0/f13 leaves plus the first-field-read bias of C2RS-BRIDGE
    // 11 on `def`.
```

Inside the body (was lines 443-448):

```text
        // BUG FIX (w7-bw): the image lays the 0x5b constant out as
        // (0, mHalfDistance, 0, invFarPlane) -- 82821640 `stfs f13, 0xe4(r1)`
        // (half distance -> .y), 82821648 `stfs f0, 0xec(r1)` (1/farPlane
        // -> .w), 82821650/54 `stfs f19` to 0xe0/0xe8 (.x/.z = 0). The old
        // spelling (mHalfDistance, invFarPlane, 0, 0) fed the pixel shader
        // both values in the wrong lanes.
```

Inside the body (was lines 489-495):

```text
        // NEGATIVE RESULT (w7-bw): retail fills this Vector4 through one FPR,
        // load/store interleaved (828217B4..828217D8). Member-wise assignment
        // (`Vector4 direction; direction.x = ...`) reproduces that shape but
        // re-colours every Vector4 slot in the frame (camPosVec 0xb0 -> 0x90,
        // fogParams 0xe0 -> 0xa0, apex 0x90 -> 0xe0, ...): 96.47 -> 93.5, and
        // 94.8 with the block wrapped in its own scope. The ctor form keeps
        // the frame layout; the three-load-then-store shape is the residual.
```

Inside the body (was lines 517-538):

```text
        // After the 0x1d upload retail re-reads both radii from the Vector4
        // it just passed (828218C0 `lfs f12, 0x74(r1)`, 828218C8
        // `lfs f13, 0x70(r1)`) rather than from the registers still holding
        // minRad/botRad: the source reads radiiVec back.
        // RESIDUAL (w7-bw, 96.5 canonical): after the 0x1d upload retail
        // re-reads both radii from the Vector4 it just passed (828218C0
        // `lfs f12, 0x74(r1)`, 828218C8 `lfs f13, 0x70(r1)`) instead of the
        // callee-saved copies, so the source read radiiVec.x/.y back. Spelling
        // it that way (radiusDiff = radiiVec.y - radiiVec.x, shift from
        // radiiVec.x, cos from radiiVec.y) matches those three rows but
        // re-colours totalLength/invTotalLength (f24/f23 -> f23/f22) and
        // reschedules the whole apex block 82821700..82821780 (copy words
        // 0,4,c,8 -> c,0,4,8; y/z sums swapped): 96.5 -> 93.2 twice. Kept the
        // register-sourced form.
        // w26-z (2026-11-02): re-measured, and the two extra `lfs` the image
        // carries (`lfs f12, 0x74(r1)` / `lfs f13, 0x70(r1)`) are exactly this
        // read-back -- it is the ONLY non-register divergence in the function;
        // every other row is a register or stack-slot permutation. Taking the
        // read-back whole is 93.22 again (the 0x1d upload arm recolours
        // totalLength/invTotalLength and reschedules the apex block), and the
        // pieces do not grade better alone: radiusDiff from radiiVec 92.01,
        // cosAngle from radiiVec.y 95.64. Kept the register-sourced form.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: NGRadii
min/fsel and divide verified; vector copies are reordered but equal; stack layout
differs.


## `?RenderFogProxy@NgSpotlightDrawer@@IAAXXZ`

`NgSpotlightDrawer::RenderFogProxy` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 604-608):

```text
    // w16-b: test the ObjPtr itself, not the raw `RndDrawable *` copied out of
    // it: `if (objPtr)` compiles to the image's SIGNED `cmpwi cr6, r29, 0x0`
    // (0x828306AC), a raw pointer test to `cmplwi` (same lever as
    // CharHair::Strand::RootRef). MSVC CSEs the two loads, so the raw copy
    // still rides in r29 for the Draw() call.
```


## `?RenderScene@NgSpotlightDrawer@@QAAXXZ`

`NgSpotlightDrawer::RenderScene` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 734-740):

```text
        // No lights this frame: reset post-process state and bail. The target
        // checks the light count separately from the showing/resource checks
        // (numLights==0 short-circuits to its own inline ClearPostProc + return).
        // w13-c: spelled as an early `return` rather than `} else if`, which is
        // what keeps this ClearPostProc call separate from the one below
        // (the image has two bctrl sites) and gives the "world_draw" timer the
        // image's frame slot; 99.493 -> 100.
```


## `?SetupXSection@NgSpotlightDrawer@@IAAXPAVSpotlight@@ABUBeamDef@2@@Z`

`NgSpotlightDrawer::SetupXSection` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 849-870):

```text
// w7-bj (2026-09-14): still 79.24 canonical. The residual is 30 insert/delete
// clusters spread over the corner block (rows ~118-330): the image keeps one
// extra by-value copy of perp at 0x120 for the botR slides, reuses perp's slot
// 0x60 for topLeft once perp is dead, and saves r17-r31 (15 GPRs, three more
// than we do) because it keeps more 16-byte copies in flight at once. Refuted
// here: hoisting all five corner copies above the slides (copies first, slides
// after) drops the function to 67.9 with 76 inserts / 72 deletes. Interleaved
// copy-then-slide, as written, is the closer shape.
// w21-d (2026-10-03): still 79.249 canonical (526 rows: 236 diff_arg, 46
// insert, 48 delete, 23 replace).  Re-read the image's value flow end to end
// and found nothing behavioural: viewDir lives in f21/f20/f22 from the first
// Normalize to the closing Dot(beamDir, viewDir) (82821xxx rows 73-76 /
// 508-516), so the image reusing viewDir's 0x60 slot for a perp copy is slot
// sharing, not a different operand; bisector = rightPlane + leftPlane is built
// in place on a copy at 0x60, copied to 0x80 and normalised into 0xa0, exactly
// our NormalizeCopy; both plane divides and the fsel min-radius clamp agree.
// The gap is still the 16-byte-copy layout (frame 0x220 vs ours 0x200, r17 vs
// r20 save point: the image keeps two more Vector3 copies in flight).
// Measured this pass: `Vector3 eye = camXfm.v;` (a whole 16-byte copy, to add
// a copy in flight) 68.83; the five corners as plain Vec.h ScaleAdd calls
// instead of the by-value slide helpers 58.84.  Both reverted; see w7-bj
// below for the copies-first refutation.
```


w-low-world (lane B, 2026-11-13): value-audited end to end against the listing
again (corners, both eye-edge crosses, plane constants, bisector, both inverse
cosines, apex/half-angle/fade, the three SetPConstant ids): NOT A BUG.  Codegen:
decomp-synth hill_climb found `toCam.y` subtracted before `toCam.x` plus
`rightD = eye.z*rz + eye.y*ry + eye.x*rx` (the image's fmuls is the z product,
then two fmadds).  79.25 -> 81.4 canonical, fuzzy 76.45 -> 78.58; equal 219 ->
186 (register renumbering, diff_arg 236 -> 274) BUT insert/delete 46/48 -> 40/41,
our length 524 -> 525 vs the image's 526, frame delta -0x20 -> -0x10, save point
r20 -> r19.  Same spelling applied to leftD as well is WORSE (79.8): keep leftD as is.

## `?_M_fill_insert_aux@?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@AAAXPAVSpotMeshEntry@SpotlightDrawer@@IABV34@ABU__false_type@2@@Z`

`stlpmtx_std::vector<SpotMeshEntry,StlNodeAlloc<SpotMeshEntry>>::_M_fill_insert_aux` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1121-1129):

```text
        // Manual backward copy loop to match target codegen
        // w39-a: `dst` FIRST, then `src` derived from it.  Written the other way
        // round (`src = __old_finish - __n; dst = __old_finish;`) c1 copy-propagates
        // `dst` into `src + __n` and emits a FOURTH instruction rematerialising it
        // (`add r31, r27, r28`) where the image keeps `__old_finish` in r31 across
        // the __uninitialized_copy call.  Same values either way: the image's own
        // `subf r28, r27, r31` (src = __old_finish - n*0x50, derived from the
        // register holding __old_finish) is this declaration order.  396 B / 99
        // instructions, 99/99 identical.
```

Inside the body (was lines 1132-1136):

```text
        // w22-hx BEHAVIOURAL FIX (port of rb3-xenon 561a44549): `src - __pos`
        // is already an element count.  DC3 image 0x82822968 has ONE divide
        // (`divw. r11, r11, r25` with r25 = 0x50, feeding the `ble`); the extra
        // `/ sizeof` added a second `divwu.`, so the backward shift moved only
        // 1/80 of the tail (none at all for fewer than 80 elements).
```


## `?_M_erase@?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVSpotMeshEntry@SpotlightDrawer@@PAV34@0ABU__false_type@2@@Z`

`stlpmtx_std::vector<SpotMeshEntry,StlNodeAlloc<SpotMeshEntry>>::_M_erase` -- moved verbatim from `src/system/world/SpotlightDrawer_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1169-1178):

```text
    // w8-h BEHAVIOURAL FIX: `_M_finish - __src` is a POINTER difference between
    // two SpotMeshEntry*, so it is already an element count -- the compiler
    // emits the /0x50 itself.  The explicit `/ 0x50` divided a second time.
    // Proof in the listings: the target's _M_erase (SpotlightDrawer_NG.s:4852)
    // has exactly ONE `divw.` (the recording one feeding its `ble`); we emitted
    // an extra `divw r10, r10, r11` ahead of it.  With the double divide,
    // __count is (finish-last)/80 instead of (finish-last), so any erase with
    // fewer than 80 elements following __last took __count <= 0, skipped the
    // shift loop entirely, and still ran `_M_finish = __pos` -- silently
    // discarding the tail instead of moving it down.
```
