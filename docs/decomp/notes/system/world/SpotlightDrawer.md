# Lane notes: `src/system/world/SpotlightDrawer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Init@SpotlightDrawer@@SAXXZ`

`SpotlightDrawer::Init` -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 111-141):

```text
    // MECHANISM (w8-h; re-measured w33-c): 93.929 canonical.  The image stores New()'s result into
    // sDefault FIRST (stw at 828275E4), writes 0.0f to 0x64 through the
    // still-live result, then RELOADS sDefault (lwz at 828275F0) for Select();
    // we store sDefault last and never reload, which swaps the two `lis` and
    // moves the stw.  MEASURED NEGATIVE (w8-h): naming the global on all three
    // lines -- `sDefault = New(); sDefault->mParams... = 0.0f; sDefault->
    // Select();`, the RB3 spelling -- reads 90.357, WORSE: MSVC then emits the
    // reload but ALSO reloads for the 0.0f store, adding rows.  Reverted.
    //
    // TWO MORE MEASURED NEGATIVES (w8-q).  The 4-byte shortfall is WHERE MSVC
    // puts the store to the global, and source order does not decide it:
    //   `ptr = New(); sDefault = ptr; ptr->...= 0.0f; sDefault->Select();`
    //       -- the target's statement order exactly -- is BYTE-INERT, 94.3% and
    //       the same 3 rows.  MSVC sinks the global store past the 0x64 field
    //       store on its own, so writing it earlier buys nothing.
    //   `sDefault = New(); ptr = sDefault; ptr->...= 0.0f; sDefault->Select();`
    //       reads 90.7, WORSE: it adds a `clrrwi r3, r3, 0` for the read-back.
    // w18-b: `(sDefault = New<SpotlightDrawer>())->mParams... = 0.0f;
    // sDefault->Select();` (store the global, use the assignment's value for
    // the field store) still sinks the global store past the field store and
    // adds a `clrrwi r3, r3, 0` for the reload: 90.7, worse.
    // Reading the global for the field store is the only thing that moves the
    // store, and that is the 90.357 negative above.  Open question: a construct
    // that pins the global store ahead of the field store without also forcing a
    // read-back for the field store.
    // w33-c behaviour verdict: EQUIVALENT.  The image reloads sDefault with
    // `lwz` where we keep the New() result in r3 -- same value, same Select()
    // receiver (relocation-set check: our only symbol delta is that one extra
    // sDefault reference).  Re-measured on this tree: target statement order is
    // byte-inert at 93.929; naming the global on all three lines is 90.357 and
    // emits `clrrwi r3, r3, 0`, not the image's `lwz`.
```


## `?DrawMeshVec@SpotlightDrawer@@MAAXAAV?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`SpotlightDrawer::DrawMeshVec` -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 163-178):

```text
// BEHAVIOURAL FIX (w17-b, 97.705 -> 100).  The two charged rows were the WRONG
// VIRTUAL, and w8-q's analysis below them was right up to the last step:
//   0x1D48  li   r4, 0x0
//           lwz  r30, 0x4(r31)        ; entry +4
//           mr   r3, r30              ; this, unadjusted
//           lwz  r11, 0x0(r30)        ; primary vptr
//           lwz  r11, 0x4(r11)        ; SLOT 1
//           bctrl                     (and again at 0x1DB8)
// Entry +4 is not a mesh: DrawLight stores RndEnviron::sCurrent there.  Slot 1
// of RndEnviron's primary (RndTransformable) vtable is
// ?Select@RndEnviron@@UAAXPBVVector3@@@Z (Env.s, ??_7RndEnviron@@6BRndTransformable@@@),
// so the call is `env->Select(nullptr)`: the cans are drawn grouped by
// environment and each group selects its environment first.  We had a
// reinterpret_cast to RndHighlightable calling slot 0, which on the image's
// layout is OnlyReturns -- the environment was never selected.  Field retyped
// to RndEnviron * in SpotlightDrawer.h.
```


## `?DrawLenses@SpotlightDrawer@@MAAXPAVSpotlightEntry@1@ABQAV21@@Z`

`SpotlightDrawer::DrawLenses` -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 259-288):

```text
        // The guard is the LENS MATERIAL and the assert is on sDiskMesh -- that
        // is the image's nesting, not the other way round, and it is a
        // BEHAVIOURAL correction (w8-q, 90.381 -> 100.0).  Decoded at
        // 0x82823EEC..0x82823F54: `lwz r11,0x1ec(r25)` / `cmpwi cr6,r11,0` /
        // `beq cr6,.L_82823F54` jumps straight to the loop LATCH, so a spotlight
        // with no lens material is skipped entirely and never asserts; only then
        // is `sDiskMesh` loaded (0x82823EFC) and `cmplwi`-tested, with
        // `bne cr6,.L_82823F38` hopping over the line-0x2B9 Fail block.  So the
        // image fails iff mLensMaterial != 0 && sDiskMesh == 0.  We had it
        // inverted (guard on sDiskMesh, assert on LensMesh), which asserted on
        // the wrong condition AND drew the disk mesh with a null material for
        // every lens-less spotlight.  Spelling the image's nesting removed all 3
        // inserts, all 3 deletes and the beq/bne inversion at once; RB3's
        // SpotlightDrawer.cpp:DrawLenses has our old inverted shape, so it is not
        // a reference here.
        //
        // HAND-EXPANDED ASSERT, and it has to be: the image's line-0x2B9 message
        // string is "sl->LensMesh()" -- 14 chars, `??_C@_0P@ICJAFDBB@...` at
        // 0x820e6400, the ONLY assert literal in world:SpotlightDrawer.obj -- and
        // `_0P@` pins its length at 15 bytes including the NUL, so it cannot be
        // the 21-byte "Spotlight::sDiskMesh" that MILO_ASSERT's `#cond` would
        // produce for the condition the image actually tests.  There is no
        // "Spotlight::sDiskMesh" literal anywhere in ham_xbox_r.map.  Harmonix
        // moved the guard and left the old assert text behind; MILO_ASSERT
        // stringifies its own condition and so cannot reproduce that, and
        // `MILO_ASSERT(Spotlight::sDiskMesh, 0x2b9)` holds this row at 99.84127
        // on exactly one charged relocation-name row.  The `if` form (rather than
        // MILO_ASSERT's do/while) is free here: DrawLenses holds no
        // function-local static, so the scope-ordinal difference between the two
        // spellings has nothing to number.
```


## `?ApplyLightingApprox@SpotlightDrawer@@QBAXAAVBoxMapLighting@@M@Z`

`SpotlightDrawer::ApplyLightingApprox` -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 457-491):

```text
        // The residual is a REGISTER BUDGET, not an expression (w8-q, measured):
        // the image spends one more callee-saved GPR and one fewer callee-saved
        // FPR than we do.  Target prologue: `bl __savegprlr_26` + a single
        // `stfd f31, -0x40(r1)`, with `lis r26, __real@40000000@ha` hoisted
        // pre-loop and `lfs f0, __real@40000000@l(r26)` reloaded every iteration.
        // Ours: `bl __savegprlr_27` + `stfd f30` AND `stfd f31`, with the value
        // itself parked in f30 for the whole function.  That accounts for all six
        // prologue/epilogue rows (idx 1,2,3,133,134,135) and the three around the
        // materialisation (35,37,92) -- nine of the twenty-nine.  Any fix has to
        // make MSVC prefer the GPR-plus-reload trade, so it is about pressure, not
        // about how `* 2.0f` is spelled.
        // w26-l (2026-11-02): 13 more spellings of the two `* 2.0f` statements
        // (one named `two` inside the loop, two named `bottom`/`length` temps,
        // the two statements swapped, `2.0f * x` operand form, `* 2`,
        // `* (float)2`, and a `two` declared next to `params`) -- every one is
        // byte-inert or worse (the two-temps form adds 2 instructions, 90.26).
        // Mechanism: the image keeps the ADDRESS (`lis r26, __real@40000000@ha`)
        // and reloads `lfs f0, @l(r26)` inside the loop, so c2 declined to hoist
        // the load; ours takes the literal into f30, which costs the second
        // `stfd` and pushes the save range to __savegprlr_27. The choice is
        // c2's loop-invariant-motion cost model, not the expression: no spelling
        // of the use changes whether the constant is CSE'd across the loop.
        // RESIDUAL (w7-am, 94.2 canonical): MSVC hoists this 2.0f out of the
        // loop into a second callee-saved FPR (f30, plus the extra stfd), where
        // the image keeps only `lis r26, __real@40000000@ha` live and reloads
        // the literal every iteration at 0x82823CAC.  That is a register-
        // allocation heuristic, not a spelling: hoisting `params` out of the
        // loop (which the image's pre-loop `lwz r31,0x50(r1)` shows it does)
        // is byte-for-byte inert.
        // w43-b (2026-11-06): 94.19847 -> 95.72519 canonical, 93.20611 -> 95.11450 fuzzy,
        // EQUAL rows 105 -> 116, diff_arg 23 -> 13.  Binding the beam base ONCE, HERE, is the
        // lever: the same binding declared at the top of the loop body, after c50, or before
        // ParamsAt reads exactly the baseline (94.19847), i.e. byte-identical to no binding at
        // all.  At the use site it pins the base so the three loads are scheduled where the image
        // has them, which also fixes the mPosition/mDirection/mColor store order that follows.
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 597-601; nearest following definition: `SpotlightDrawer::DrawWorld`):

```text
        // w18-b: no `Spotlight *sl` local -- the image re-reads it->mSpotlight
        // at each use (`lwz r11, 0x4(r25)` before LensXfm and again before the
        // second LensMesh read); a local pinned sl in r31 and recoloured the
        // loop (94.28 -> 100).  The two != tests are written disk/lensMat
        // first to match the image's subf operand order.
```

File-level (was lines 612-619; nearest following definition: `SpotlightDrawer::DrawWorld`):

```text
            // MEASURED NEGATIVE (w8-q, 94.277 -> 92.858): flipping this to
            // `if (disk->Showing()) { sphere path } else { visible = false; }`.
            // The image tests mShowing and branches AWAY to the sphere path
            // (`lbz r10,0x8(r28)` / `cmplwi r10,0` / `bne 0x2448` at 0x2430) with
            // the visible=false arm in the fall-through, which is this spelling's
            // source order and NOT what MSVC gives us -- it inverts ours to
            // `beq` into the false arm.  Writing the arms the other way round does
            // not make MSVC invert a second time; it just loses rows elsewhere.
```


## `?DrawWorld@SpotlightDrawer@@MAAXXZ`

`SpotlightDrawer::DrawWorld` -- moved verbatim from `src/system/world/SpotlightDrawer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 687-719):

```text
                    // RESIDUAL (w7-am, 98.5 canonical): the image multiplies
                    // blue, then green, then red -- Set()'s arguments evaluated
                    // right-to-left -- and we emit red, green, blue.  Refuted:
                    // inlining spot->Intensity() into all three arguments (no
                    // change), and writing the three products as separate
                    // member assignments in blue/green/red order (94.1, it
                    // splits the Color() base load in two).
                    // w27-f (2026-11-02): the w7-am/w13-c note above is stale
                    // on the store ORDER -- both sides now store 0x60, 0x68,
                    // 0x64.  What is left is (a) the four colour loads'
                    // order and register names and (b) a REAL structural
                    // difference: we emit one `stw` more than the image (8 vs
                    // 7).  The image jumps `b 0x3294` straight onto the loop
                    // head's `stw r11,0x50(r1)`, so `e2` is homed once; our
                    // rotation jumps `b 0xa83c` past it to the loop test and
                    // therefore needs a second, earlier `stw r11,0x50(r1)` at
                    // the definition.  Same values, same instruction stream
                    // otherwise.  Measured byte-inert: `e2 = e1 + 1`, deferring
                    // e2's declaration to the for-loop, declaring e2 before e1,
                    // while/for/`e2 = e2 + 1` rotations, `const float &r/g/b`
                    // products (98.488, worse), binding `spot->Color()` to a
                    // named reference, and naming the intensity.  Mechanism
                    // hypothesis: c2's loop rotation for a `for(;; ++i)` with
                    // a mid-body `break`, not a colour or operand-order
                    // decision.
                    // w13-c: the 3-arg Hmx::Color ctor (alpha from the init
                    // list) in place of Set(r, g, b, 1.0f) is also inert, 98.47.
                    // w-field-call (2026-11-13, behaviour re-read): the sweep's
                    // "fmuls f13,f13,f0 vs f13,f13,f12" is NOT a different
                    // multiplicand -- intensity (0x1c0) lives in f0 in the
                    // image and f12 here; both store red*I->0x60, blue*I->0x68,
                    // green*I->0x64, 1.0->0x6c.  The ours-only `stw r31,0x54`
                    // homes the same `it` (e1).  Codegen-only.
```

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/world/SpotlightDrawer.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 42-47; nearest following definition: `SpotlightDrawer::SpotlightEntry::SpotlightEntry`):

```text
        // w14-b: no user operator= (RB3's had `memcpy(this, &o, sizeof(*this))`).
        // The compiler-generated copy is still one memcpy of 0x50 bytes, but
        // it materialises the SOURCE address first, as the image's
        // __adjust_heap<SpotMeshEntry*, ByEnvMesh> does at both hole moves
        // (mulli r10, r31 / add r4 before the dest); the hand-written memcpy
        // computed the dest first. __adjust_heap 94.67 -> 100.
```
