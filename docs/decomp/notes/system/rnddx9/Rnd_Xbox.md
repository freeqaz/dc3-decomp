# Lane notes: `src/system/rnddx9/Rnd_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0DxRnd@@QAA@XZ`

`DxRnd::DxRnd` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 49-65):

```text
// RESIDUAL (w7-an, 70.3 canonical): the member LAYOUT is confirmed correct --
// every store the image makes lands on a member we declare at that offset
// (0x304/0x310 vectors, 0x360/0x361 bools, 0x364/0x368/0x36c pointers,
// 0x370/0x374 floats, 0x378/0x3a4 bools), and the whole body after the
// initialiser list (0x3f4 .. 0x34d, indices 62-76) is instruction-for-
// instruction equal.  What differs is a scheduling permutation of ten stores
// inside indices 28-61: (a) the image finishes each inlined vector ctor
// before starting the next -- `addi r11, r30, BASE` / `addi r11, r11, 0x8` /
// three null stores / `stw r11, 0x50(r31)` -- where MSVC interleaves ours and
// defers both `stw ..., 0x50(r31)` homings; (b) the image emits the
// 0x360..0x36c group BEFORE the two `stfs` of mGPUBusyMs/mGPUCountMs and
// splits them around `stb r29, 0x378`, where MSVC hoists our `lis
// __real@00000000@h` to index 28 and both floats ahead of the group; (c) the
// image emits one extra `addi r11, r30, 0x350` (index 44) that nothing
// consumes.  The initialiser list is already in declaration order, which is
// the order MSVC emits regardless of how the list is written, so there is no
// source-order lever here.
```

Inside the body (was lines 86-106):

```text
    // w17-c: a loop, not two assignments -- the image keeps the unrolled
    // loop's dead base `addi r11, r30, 0x350` (70.28 -> 70.8).  The rest of
    // the residual is the scheduling of the two vectors' EH-frame stores
    // (`stw rX, 0x50(r31)`): the image emits each right after its vector's
    // three zero stores, we defer them; not resolved.
    // w18-c (70.8, unchanged): measured cause.  The image has unwind funclets
    // destroying BOTH vectors (mPendingReleases, mPendingDeletes) and ours has
    // none -- MSVC elides those EH states here because nothing after the
    // vectors can throw.  Probe: appending one possibly-throwing call (an
    // external `Symbol s("x")`) to this body gives 93.5 with EVERY remaining row
    // the probe's own call/frame (+0x10); the store schedule then matches the
    // image exactly.  So in the original something after the vectors was not
    // provably nothrow yet emitted no call.  Same shape as Skeleton::Skeleton
    // (gesture/Skeleton.cpp, w18-c note); cause not found.  Stronger probe: a
    // statement whose throwing arm CONSTANT-FOLDS away -- e.g. appending
    // `MILO_ASSERT(sizeof(mFrontBuffers) == 8, ...)` -- gives 100.0, 71/71 rows,
    // with no code of its own (the EH states survive the folding; this is what
    // closed DxShader::Compile, whose `new DxShaderBuffer(0)` has a dead
    // D3DXCreateBuffer/MILO_FAIL arm).  So the original body very likely had such
    // a statement after the vectors; we do not know which, and an invented
    // assert is not landed.
```


## `?FinishPostProcess@DxRnd@@AAAXXZ`

`DxRnd::FinishPostProcess` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 451-481):

```text
    // ADJUDICATED AND REFUSED -- do not "fix" the MakeColor row here.  Retail
    // CALLS MakeColor out of line at this one site (`bl ?MakeColor@@YAKABV
    // Color@Hmx@@@Z` at 0x82617DF4, after storing 0,0,0.3,1.0 to a Color temp);
    // we inline it and constant-fold the whole thing to 0xff00004c.  That
    // single `bl` is the ONLY one in the image -- 1 across all 2,223 target
    // objects -- while the ARGB pack sequence appears inlined in eight target
    // functions, so it is a per-site inliner decision, not a linkage one.
    // Measured, whole report, name_check:
    //   * three call-site spellings (temporary / named Color local / Rect via
    //     Set() / Rect via member stores) are INERT -- byte-identical 29
    //     mismatch rows at identical offsets, 83.608696 every time;
    //   * `__declspec(noinline)` on MakeColor buys this function +10.90pp
    //     (83.91304 -> 94.81739) and costs EIGHT neighbours: BeginTiling
    //     99.96 -> 29.72, Clear 100.0 -> 42.89, DrawRect 100.0 -> 57.67,
    //     DrawLine 98.64 -> 54.98, DrawParticles 99.98 -> 71.14, DrawString
    //     96.75 -> 84.81, ModalDraw 89.78 -> 66.19; matched_functions -1,
    //     matched_code -220 B;
    //   * `#pragma inline_depth(0)` around this function takes it to 39.65 --
    //     it also un-inlines DxRnd::Device() and the D3D setters.
    // All 29 residual rows have this one cause; the two `lwa 0x40/0x44` order
    // rows and the 0x68/0x6c store swap are downstream scheduling, not a
    // wrong field (both sides put mWidth in .w and mHeight in .h).
    // w7-bx (2026-09-15, still 83.91304): two more probes, both inert or
    // worse.  (a) Caller-size inline budget: inflating the inlined preamble
    // with 1 or 4 extra MipFilter/AddressU pairs before the Clear does NOT
    // turn our MakeColor into a call -- MSVC folds the constant-argument
    // inline regardless of how much it has already inlined here, so a
    // budget reachable from this TU is not the mechanism.  (b) Building
    // `rect` before the Clear (so its lwa/fcfid work precedes the call)
    // is 70.1: the image's `lwa 0x40/0x44` sit after `bl D3DDevice_Clear`
    // (0x82617E1C/0x82617E28), as written below.
```


## `?SavePreBuffer@DxRnd@@AAAXXZ`

`DxRnd::SavePreBuffer` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 628-633):

```text
// w18-c: 66.78 -> 100, rb3-xenon's shape.  The zero is a function-local static
// const: the image keeps its page base (`lis r29, __real@00000000`) across the
// first Resolve and reloads it for both the w lane and the second call's
// ClearDepth, where a plain literal makes MSVC hold 0.0 in f31 instead.  The w
// lane is written before the first call (the old note's "dead store" bug fix is
// kept: the second Resolve reads all four lanes through &vector).
```


## `?DrawString@DxRnd@@UAAAAVVector2@@PBDABV2@ABVColor@Hmx@@_N@Z`

`DxRnd::DrawString` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 694-722):

```text
// w24-ds: no `RndShaderMgr &shaderMgr = TheShaderMgr;` alias for the
// SetVConstant call.  fe85675f8 added it as a lever while
// Matrix4(const Transform &) was out of line in mtx.cpp.  952429f11 made that
// ctor an inline in math/Mtx.h, so this TU now sees its body; the call is
// still a `bl` on both sides.  After that change the alias was no longer
// neutral: it fixed the r22<->r23 permutation described below but reordered
// the MakeColor field loads in the glyph loop (blue, alpha, green, red rather
// than the image's blue, green, red, then alpha after the y store), and the
// function fell from 99.99 to 96.09.  Without the alias it is back to 99.99099,
// with the same 15 rows as before 952429f11.
//
// RESIDUAL (w7-bl, 99.99 canonical, 15 rows, all register/operand ORDER and
// no value): (a) a flat r22<->r23 permutation -- the image gives r22 to the
// `?TheShaderMgr@@...@h` page base and r23 to `s`, we give them the other way
// round, 10 rows; (b) inside the inlined MakeColor at 0x38C4/0x38D8 the image
// issues the red (0x0) and alpha (0xc) `lfs` in that order and we issue alpha
// then red, which cascades into the r9/r10 naming of the three `rlwimi`s --
// the packed ARGB word is bit-identical either way (verified by hand from the
// rlwimi masks).  Failed spellings: reordering MakeColor's four |-terms in
// Rnd.h so red precedes alpha is BYTE-INERT here (15 rows before and after),
// so it is not an argument-order lever, matching the negative already recorded
// for the same packing in rnddx9/Part.cpp.  w24-ds: writing the pack out by
// hand in DrawString (as separate statements in b,g,r,a or a,r,g,b order,
// right-associated, or with 255.0f on the left) is also inert.
//
// Debug text: each glyph is a list of polylines held in the `font` DataArray,
// indexed by character code, each point a pair of floats scaled to a 9x12 cell
// on a 13.5 x 18 pixel grid.  Returns a reference to a shared cursor holding
// the end of the string, which DrawStringScreen scales back to 0..1.
```

Inside the body (was lines 738-762):

```text
    // w24-ds: a named alias of `color`, 99.99099 -> 100.0 canonical.  It
    // closes residual (b) above (the red/alpha lfs order and the rlwimi
    // naming).  Residual (a), the 9-row r22<->r23 permutation, is still there;
    // the canonical ruler forgives it, and fuzzy reads 99.77477 (those rows
    // plus the ICF'd MakeString callee name).  The alias works at function
    // scope, before MILO_ASSERT, inside the inner loop, or aliasing `pos`.  With
    // the shaderMgr alias back on SetVConstant as well, it reads 96.09 again.
    // An `unsigned long` temp for the pack, or a cast, is inert.
    //
    // STILL OPEN (w26-f8, fuzzy 99.77477, canonical 100.0).  Residual (a) is
    // one two-way exchange: image r23 = `s` and r22 = TheShaderMgr's page
    // base, we hold them the other way round.  Tap (`c2rs stage cands
    // --regasg --fn DrawString`): the page base is id76 pri -21 tie 0
    // (code#104) and `s` is id6 pri -24 tie 237 (arena#7); the page base pops
    // first and takes r23.  The tie is already won by `s`, so only PRIORITY can
    // flip it, and the gap is 3.  The C2RS-BRIDGE 8.8 global-OBJECT reference
    // binding -- the lever that closed DrawLargeQuad in this same file -- was
    // measured at five placements here and every one is inert or worse:
    // bind + call through `RndShaderMgr &mgr` for the early pair 99.02252 /
    // 95.86487 (SetVConstant only), 99.33784 (SetTransform only), binding
    // alone (no call rewritten) INERT at 99.77477, binding at function scope
    // before MILO_ASSERT INERT, and a binding at the tail SetVConstant
    // 99.63063.  Naming the Hmx::Matrix4 temporary costs to 94.60.  So this
    // row is not a leaf-key question; it is a priority gap between two
    // long-lived candidates.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.775, 9 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?ModalDraw@DxRnd@@MAAXW4ModalType@Debug@@PBD@Z`

`DxRnd::ModalDraw` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 831-852):

```text
    // BUG FIX (w7-bl).  0x82618DAC-0x82618DB4 packs the clear colour as
    // A=0xff (a CONSTANT -- `lis r8, 0xffff`, so alpha is a compile-time
    // 1.0f), R=f13, G=f12, B=f11, and 0x82618D34/0x82618D3C load f12=0.5f
    // and f11=0.1f with f13=0.0f; the kModalFail arm at 0x82618D48-0x82618D50
    // sets f13=0.25f and zeroes f12/f11.  So the image clears to an OPAQUE
    // (0, 0.5, 0.1) green for a normal modal and an OPAQUE (0.25, 0, 0) red
    // for a failure.  We had green/blue swapped, alpha 0 instead of 1, and
    // the failure arm writing 0.25 into ALPHA instead of RED -- our modal
    // cleared to 0x0000197f (fully transparent blue) and our failure screen
    // to 0x3f000000 instead of 0xff3f0000.
    // 100% (w7-br; w7-bl left it at 89.7 / 135 rows).  The five zeros --
    // colour red, the fail arm's green/blue, Clear's Z and Resolve's ClearZ --
    // are ONE function-local `static const float`.  MSVC folds its value into
    // the `__real@00000000` pool but still treats it as memory: the page base
    // is kept in callee-saved r30 (`lis r30` at 0x82618D20) and the value is
    // RE-LOADED after the calls (`lfs f1` at 0x82618D30 and again at
    // 0x82618E0C, past Clear / DrawStringScreen / DrawAll).  Any literal
    // spelling (`0`, `0.0f`, `0.0`, mixed) is one value CSE'd into a
    // callee-saved f31 with `fmr f1, f31` at both call sites, an extra
    // stfd/lfd pair and `__savegprlr_26` for the image's `_25`: 93.6, and
    // byte-inert across all four literal spellings.  Same idiom as
    // RatioToDb's `static const float zero` (Decibels.cpp, 100%).
```


## `?InitBuffers@DxRnd@@AAAXXZ`

`DxRnd::InitBuffers` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 878-883):

```text
// 100% (w7-br; w7-bl left it at 95.2 / 96 rows).  Four levers, each noted
// at its site: the tile rects are `i * tile` (a strength-reduced IV, two
// adds per iteration), mWidth is stored inside each branch of the aspect
// test, the CreateTexture calls go through the XDK's inline
// IDirect3DDevice9_CreateTexture wrapper (d3d9.h), and the front-buffer
// clear size multiplies mWidth's term by mHeight's.
```

Inside the body (was lines 913-917):

```text
    // mWidth is assigned in BOTH arms (w7-br): MSVC tail-merges the two
    // stores into the one `stw r7, 0x40(r30)` at 0x826190DC, and that is
    // what puts the quotient in r7 with the shift in place on r11
    // (0x826190BC-0x826190C4); a `width` local stored after the join
    // colours them the other way round and hoists the zero `li r24`.
```

Inside the body (was lines 927-939):

```text
        // 0x826190EC-0x8261916C: two tile rects covering the frame.  Bit 1 of
        // mFlags picks a horizontal split line (stacked tiles, full width,
        // half height) over the default vertical one (side-by-side tiles,
        // half width, full height).  The loop re-reads mNumTiles from the
        // member every iteration (`lwz r8, 0x3b0(r30)`).
        // w7-br: the rect edges are `i * tile` and `(i + 1) * tile`, NOT a
        // running `offset`.  MSVC strength-reduces `i * tile` into an IV
        // (r10, stepped by `add r10, r10, r23` at 0x82619124) and computes
        // `(i + 1) * tile` as a SEPARATE `add r8, r10, r23` at 0x82619110 --
        // the two adds w7-bl saw.  An explicit `offset` local with
        // `y2 = offset + tile; offset += tile` is one value-numbered add
        // however it is spelled (reading y2 back from the just-stored y1 is
        // forwarded and CSE'd the same way): 95.2 -> 96.9 from this alone.
```

Inside the body (was lines 1007-1018):

```text
    // w7-br: the XDK inline wrapper (d3d9.h) with `&member` as its out
    // pointer.  The `tracker` local gives this function an EH state, and
    // an inlined callee that uses a computed `&member` twice then gets a
    // dead home-slot store of that address before the call (`addi r11,
    // r30, 0x390` + `stw r11, 0x58(r31)` at 0x826193C8/0x826193DC, and
    // again for 0x394 and 0x358); the loop's `&mFrontBuffers[i]` is an IV
    // (r28) and gets none.  The wrapper's null test is the `subic/subfe/
    // and.` against E_OUTOFMEMORY held in r29 (0x826193F4-0x82619408).
    // Assigning the member from D3DDevice_CreateTexture directly and
    // DX_ASSERT-ing it also CSEs D3DFMT_A8R8G8B8 into a callee-saved r19
    // (`__savegprlr_18` for the image's `_19`) where the image
    // re-materialises `lis/ori` per call.  96.9 -> 99.6.
```

Inside the body (was lines 1055-1062):

```text
    // 0x8261963C-0x82619658: srawi+addze on BOTH terms -- a signed divide by
    // 32, not an arithmetic shift.  With `>> 5` MSVC fuses one of them into a
    // single `extlwi` and the addze pair disappears.
    // w7-br: mWidth's term FIRST.  MSVC evaluates the right operand first,
    // so this is what loads mHeight into r11 at 0x8261962C ahead of mWidth
    // into r10 at 0x82619630.  w7-bl measured the swap as inert at 95.2;
    // with the register set settled by the levers above it is the last two
    // rows (99.6 -> 100).
```


## `?BeginDrawing@DxRnd@@UAAXXZ`

`DxRnd::BeginDrawing` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1253-1268):

```text
    // 99.98771, 3 rows, and they are one rotation of the three argument loads
    // for the D3DDevice_SetShaderGPRAllocation that this call inlines. Target
    // [182]/[184]/[186] = mD3DDevice(0x224)->r3, mDefaultPSRegAlloc(0x400)->r6,
    // mDefaultVSRegAlloc(0x3fc)->r5; ours is r6, r5, r3 -- MSVC's plain
    // right-to-left argument order, with the target hoisting the device load
    // above the other two. Same three registers, same call, same values: only
    // the schedule differs, and the rest of the inlined block (the cmpwi, the
    // beq, the li 1 and the stw to mRegAlloc at 0x3f8) is equal.
    // Measured INERT (w7-m): spelling the block out here the way DoPostProcess
    // and CopyPostProcess already do --
    //   if (mRegAlloc != 1) { mRegAlloc = (RegisterAlloc)1;
    //     D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, mDefaultVSRegAlloc,
    //                                      mDefaultPSRegAlloc); }
    // -- gives a byte-identical object, same three rows. The inlined call and
    // the hand-written block are indistinguishable, so the call stays (it also
    // keeps SetShaderRegisterAlloc, which is already 100%, untouched).
```


## `?DoPointTests@DxRnd@@AAAXXZ`

`DxRnd::DoPointTests` -- moved verbatim from `src/system/rnddx9/Rnd_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 1299-1306):

```text
    // Process query results from previous frame.  w7-br: the manager is read
    // into a fresh local at the TOP of the body -- the image hoists that load
    // into the loop latch (`lwz r3, 0x204(r29)` at 0x8261B02C, between the
    // end() load and the compare) and then reuses the same r3 for the
    // ToggleFrameIndex block after the loop (0x8261B038).  With the member
    // read inline as the call's `this` it sits last in the argument setup
    // and never leaves the body (92.9 -> 95.9 together with the two locals
    // below).  The second call keeps the member read: it reloads (0x8261AFF4).
```

Inside the body (was lines 1329-1339):

```text
    // Update frame index - both direct manipulation and virtual call.
    // BUG FIX (w7-bl): the two virtual calls were the wrong way round.
    // 0x8261B008 calls vtable slot 0x20 (OnEndFrame) right after the 0x1804
    // toggle, and 0x8261B018 calls slot 0x1c (OnBeginFrame) after the 0x1808
    // increment -- we emitted 0x1c then 0x20.  Semantically the image retires
    // the previous frame's queries, bumps the counter, then opens the new
    // frame; we were opening the new frame before retiring the old one.
    // w7-br: one pointer serves Toggle + OnEndFrame (0x8261B038-0x8261B054,
    // the vtable is read off r3 BEFORE the 0x1804 store) and a second read
    // serves Increment + OnBeginFrame (0x8261B058); reading the member four
    // times reloads it after each store instead.
```

Inside the body (was lines 1347-1352):

```text
    // Resize mPointTestQueries to match mPointTests count.  w7-br: the
    // count loop IS list::size() -- and because it is the resize's argument,
    // the {0, -1, -1} default value (0x8261B084-0x8261B090) is built BEFORE
    // the loop walks the list (0x8261B098-0x8261B0A4), right-to-left; a
    // hand-written count loop
    // put it after (95.9 -> 98.1).
```

Inside the body (was lines 1382-1388):

```text
    // Setup view matrix.  0x8261B160-0x8261B16C passes the Matrix4
    // CONSTRUCTOR'S return value (`mr r5, r3`) straight to SetVConstant -- an
    // unnamed temporary, not a named local whose address is re-taken.
    // w7-br: via a reference local the manager pointer is loaded ONCE, before
    // the ctor call, and held in r31 (0x8261B158 / `mr r3, r31` at
    // 0x8261B170); written as `TheShaderMgr.SetVConstant(...)` it is reloaded
    // after the ctor.
```

Inside the body (was lines 1425-1430):

```text
        // w7-br: `idx++` in the subscript -- the byte-offset IV is bumped
        // right after the address is formed (0x8261B270), not at the loop
        // end -- and the point test reads `test.mFlare` back: the image's
        // `clrrwi r11, r10, 0` at 0x8261B264 is that store-forwarded load
        // (a 32-bit zero-extending copy of the value just stored), which a
        // `flare` local cannot produce.
```

Inside the body (was lines 1497-1504):

```text
            // 100% canonical (w7-br; w7-bl left it at 92.90).  The
            // callee-saved renumbering was the four levers above.  What is
            // left is register permutation only (r21/r22) plus the four
            // `fadds` whose operands MSVC canonicalises (writing
            // `verts[0].y + area.h` is byte-identical, per w7-bl).
            // 0x8261B430/0x8261B444: the manager is read ONCE into a
            // callee-saved register and reused for BeginQuery (`mr r3, r30`);
            // EndQuery at 0x8261B478 reloads the member.
```
