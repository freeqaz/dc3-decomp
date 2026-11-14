# Lane notes: `src/system/rndobj/Part.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 624-630; nearest following definition: `RndParticleSys::Load`):

```text
// w15-r: the image's TU .rdata is [4.0f @0x820A13B8, gRev, gAltRev].  Nothing
// loads that 4.0 by name -- every use folds to __real@40800000 -- which is the
// shape of a named file-scope const used only in folded expressions (cf.
// SkeletonViz's FOV).  Which use it was is not recoverable; UpdateParticles'
// pause clamp is the only 4.0f in the TU that compiles identically through it
// (still __real@40800000, measured).  Its presence takes gRev off offset 0 and
// MSVC anchors Load's ASSERT_REVS pair on gAltRev like the image.
```


## `?Load@RndParticleSys@@UAAXAAVBinStream@@@Z`

`RndParticleSys::Load` -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 684-694):

```text
            // Term order is deliberate. The target seeds this dot product with
            // the C term (`lfs 0xc8` / `lfs 0x88` / `fmuls` = c*z), folds B in
            // with an fmadds, and folds A and the negation together into a
            // single fnmadds. MSVC seeds the chain with the left-most product
            // of the innermost sum, so naming C first is what puts c*z in the
            // fmuls; the parentheses on their own do nothing, /fp:fast
            // reassociates them away.
            // w21-bj: the C term's two operands are spelled z-first: the image's
            // `fmuls f0, f0, f13` is c(0x88) * z(0xc8), and MSVC emits the
            // product of `v1.z * p150.c` in that order (row [221] closed;
            // commutative, exact in IEEE, so no behaviour change).
```

Inside the body (was lines 703-728):

```text
            // w21-bj: v is assigned BEFORE m.z: with m.z first the two 16-byte
            // copies (p150 -> m.z at r31+0x100, the On() temp at 0xa0 -> v at
            // 0x110) took each other's pointer registers (r8/r9, 11 rows).
            // Stopped at 10 rows (canonical 99.997, all register-only or
            // commutative): the image loads p150.b (0x84) before p150.c (0x88)
            // and squares b first in On()'s sum (fmuls b*b; fmadds c*c + bb;
            // fmadds a*a + ..), and keeps source operand order in On()'s
            // c*scalar / b*scalar and both Cross() products; ours loads c first
            // and flips each of those.  Only On()'s sum differs in value (an
            // FMA rounds the other square: image fma(c,c,b*b), ours
            // fma(b,b,c*c)).  Inert (measured): no v128 local (m.z used
            // directly), v128 as a const Vector3 & alias of p150, v128 declared
            // after On(), and swapping the operand order of the legacy d terms.
            // On() and Cross() are math/ inlines (Mtx.h / Vec.h), not touched;
            // expanding On() at the call site was not tried -- the image keeps
            // On()'s 0xa0 return temp and its 16-byte copy, which an expansion
            // would not reproduce.
            // w25-ma (c2 tap, section 8.7 operand-order key): p150's forwarded
            // fields are named-variable leaves a=V26 b=V22 c=V27 (keys 0x10340,
            // 0x102c0, 0x10360); On()'s scalar (V971, 0x17960) and m.x
            // (V980..983, ~0x17a80) outrank all three, so ours puts those
            // first in every product. The image needs b and c ABOVE scalar and
            // m.x and a below them: hypothesis, b and c reach the sort as later
            // minted symbols (sid > 983) or temp/memory leaves. Inert (about 25
            // spellings): v128 placement or type, a Plane copy for On(), On()
            // expanded inline (association and operand order canonicalize).
```

Inside the body (was lines 774-785):

```text
        // w19-a (99.99613 -> 99.99742): the image takes the stream for mRPM from
        // the BinStreamRev& the bool read RETURNS (`lwz r3, 0x8(r3)` right after
        // `bl ??5BinStreamRev@@QAAAAV0@AA_N@Z`), and mRPMDrag's ReadEndian then
        // reuses the BinStream& the mRPM read returns.  `d >> mRotate >> mRPM >>
        // mRPMDrag` reloads stream from the saved BinStreamRev for mRPMDrag
        // (99.48); `(d >> mRotate).stream >> mRPM >> mRPMDrag` in one expression
        // computes &mRPM before the bool call (99.79).  Naming the returned
        // stream first gives both.  A whole-binary A/B of the BinStreamRev
        // template returning BinStream& instead (`return stream >> t;`) was 29
        // DOWN / 0 UP -- the header is right, this site is just spelled this way.
        // Remaining: the 0x84/0x88 load order in p150.On() below (2 rows).
        // (w21-bj: see the stop note at the bounce transform.)
```


## `?UpdateRelativeXfm@RndParticleSys@@IAAXXZ`

`RndParticleSys::UpdateRelativeXfm` -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1130-1141):

```text
        // The three named references below are behaviour-neutral (each names
        // the member the call already took by reference). They exist for the
        // Add() at the end: c2 orders the operands of each fadds by a key
        // (c2rs W-STAGETAP-11, C2RS-BRIDGE 8.7), and for two memory operands
        // with temp bases that key is the base temp's sid mod 4. Each
        // reference mints one more lowering temp ahead of the Add's address
        // temps, so the three of them move the Y and Z base sids by +3
        // (mod 4: A.y 558->561, B.y 560->563; A.z 541->544, B.z 543->546).
        // That gives mLastWorldXfm.v.y the higher key, so the image's
        // b+a order (lfs 0x290 before 0x250) now holds on Y as on X and Z,
        // while Z keeps it. One or two references flip Z the wrong way
        // (+1/+2 measured), which is how the +3 was predicted (w25-mc).
```


## `?MoveParticles@RndParticleSys@@IAAXMM@Z`

`RndParticleSys::MoveParticles` -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1154-1186):

```text
// 97.9 canonical (w7-bw). The breakdown below dates from the 69.3% state and
// is kept for its inventory of what the image does; most of it is closed.
//
// Remaining diff breakdown at 69.3% (614 instructions total):
//   - r29<->r30 register swap: 117 instructions. Target uses r30 for 'this',
//     our compiler picks r29. Unfixable compiler register allocation choice.
//   - 111 deletes: target has dead stores to stack slots 0x60/0x64 where it
//     caches intermediate pointers (addi rX, rBase, offset; stw rX, 0x60, r31).
//     Our compiler optimizes these away. Also target caches &p->pos in r25 and
//     &p->vel in r26 as dedicated pointer registers throughout the inner loop.
//   - 2 diff_ops remaining:
//     (1) idx 126: bounce WorldXfm call uses bl (call) in target vs b (branch)
//         in ours. Target reuses a shared branch point for the two WorldXfm calls.
//     (2) idx 340: attractor strength==0.015625 check uses beq (branch-if-equal
//         to special case) in target vs bne (skip special case) in ours. Target
//         also has dead code after (li 0; clrlwi. 0; beq - always-taken branch),
//         suggesting original code had a boolean variable for the condition.
//   - fmadds vs fmuls+fadds: our compiler fuses multiply-add in position update,
//     bounce reflection (fnmsubs vs fmuls+fsubs), and basic particle color/size.
//     Target uses separate instructions. Hard to prevent without volatile temps.
//   - Stack frame: target 0x1c0, ours larger. Target saves from r14 (savegprlr_14),
//     ours from r17 (3 fewer callee-saved GPRs).
//
// Potential improvements to investigate:
//   - Restructure bounce WorldXfm calls to match target's shared-branch pattern
//   - Try a bool variable for the attractor strength check to match dead code
//   - Volatile or separate-statement tricks to prevent fmadds fusion
//   - Declaration order changes to shift r14-r16 register assignment
//
// RndFancyParticle offset note: header comments are wrong by -8 bytes.
// RndParticle is 0x68 bytes (not 0x60), so RndFancyParticle fields start at 0x68.
// E.g. growFrame comment says 0x60 but actual compiled offset is 0x68,
// midcolFrame comment says 0x80 but actual is 0x88, etc.
```

Inside the body (was lines 1189-1223):

```text
    // w27-c (2026-11-02): the auto-timer's two statics are the one part of
    // this function that cannot be closed from the source.  We emit
    // `?_t@?1??MoveParticles@RndParticleSys@@IAAXMM@Z@4PAVTimer@@A@` and its
    // `?$S9@...@4IA@` init guard; the image's two globals are at
    // 0x830DFAA8 / 0x830DFAAC and symbols.txt carries them only as
    // `lbl_830DFAA8` / `lbl_830DFAAC`, i.e. the image has NO recovered name
    // for them to match.  Six rows are that relocation-name charge and no
    // respelling of the timer macro can pay it.
    //
    // The rest of the 143 rows is a permutation.  Register-and-branch-
    // insensitive multiset comparison: the target has 585 instructions and
    // we have 581, and the four that differ are already documented below --
    // the dead `stfs f1,0x64(r31)` home of tileTime before `bl fmod`, the
    // `fmr f0,f12` + `lfs f12,0x8(r26)` re-read of vel.z in the drag block,
    // and one extra `fsubs` in the three-phase size fork.  No member, call
    // or constant differs, so the body is behaviour-equivalent as written.
    // w31-a (2026-11-03) re-derived both halves on the current tree:
    // function-bounded multiset is ours 581 / target 585 with exactly
    // +1 fmr, +1 fsubs, +1 lfs, +1 stfs on the target side, and the
    // RELOCATION-SYMBOL multiset is 79 on both sides and differs ONLY in the
    // two auto-timer statics above (`?_t@?1??MoveParticles...` / `?$S9@...`
    // here against the image's unnamed `lbl_830DFAA8` / `lbl_830DFAAC`).
    // Nothing else in the body resolves to a different symbol.
    //
    // w31-c (2026-11-03) re-adjudicated all four and they are NOT arithmetic
    // differences.  The fork: the image spends one `fsubs f0,f24,f0` per arm
    // (826BFBFC / 826BFC18 / 826BFC24); we cross-jump arms 1 and 3 onto ONE
    // `fsubs 0,24,0` at 0x844 whose f0 is loaded differently per arm (0x44
    // p->birthFrame in arm 1, the already-loaded 0x70 value in arm 3) -- same
    // subtraction, one fewer instruction, which is why our body is SHORTER
    // (581 vs 585) and why the score is not a shortened-body artifact.  The
    // tileTime home store is dead on both sides (nothing reads 0x64(r31)), and
    // the `fmr`/`lfs` pair is a store-then-reload round trip of the same float
    // with no write in between.  The remaining 131 rows are register
    // permutation (f0<->f13 alone is 23 of 146 swapped instructions).
```

Inside the body (was lines 1228-1242):

```text
    // The 1/30 is spelled inline at all three sites, not hoisted into a
    // named local: a named `oneOverThirty` (declared first) flips the
    // commutative operand order of ~12 fmadds/fmuls/fadds rows across the
    // whole body (826BF58C, 826BF6F4, 826BFA98 ...), 97.57 -> 97.23 with
    // the levers below applied; declared after dragFactor it is inert.
    // The registers are still the image's mirror (f30 holds 1/30 and f29
    // the pow result at 826BF4A4/826BF4B8; ours f29/f30), which costs the
    // two reload rows at 826BF9C4/826BF9DC (w7-bw).
    // w26-v re-measured this mirror with c2rs-bridge 8.8: giving the
    // exponent its own object (`float exp30 = frameSpan * (1.0f / 30.0f);`
    // shared by both pow calls), the same as a reference, and a reference to
    // the `1.0f / 30.0f` literal itself are all one step WORSE (fuzzy 96.2991
    // vs 96.3419; canonical unchanged at 97.9145) -- the shared object moves
    // rows without flipping the f29/f30 pair. So the pair is not decided by
    // the exponent temp's identity.
```

Inside the body (was lines 1252-1256):

```text
        // Second pow evaluates the base before the exponent (826BF4D0 fsubs,
        // 826BF4D4 fmuls); the first does the reverse. A hoisted `exponent`
        // local is kept in f31 and passed by fmr on both calls (2 -> 3 rows);
        // naming the BASE instead sequences it first and closes both rows
        // (w7-bw).
```

Inside the body (was lines 1398-1407):

```text
                // Residual (5 rows): the image loads vel.z, vel.y, adds z, THEN
                // loads vel.x (826BF9F4-826BFA00), and the drag block below
                // re-reads vel.z after storing it (`fmr f0, f12` / `lfs f12,
                // 0x8(r26)`, 826BFA28-826BFA2C). Add(vel, relForce, vel),
                // Add(relForce, vel, vel) and three explicit `+=` in z, y, x
                // order all load x first and keep z in a register. Named
                // loads in z, y, x order with the sums spelled force-first
                // for x and z and vel-first for y reproduce the image's three
                // fadds operand orders (826BFA00/826BFA10/826BFA14, w7-bw);
                // the hoisted x load stays.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: field
swaps are consistent; the extra fsubs is block placement; dead home store of f1.


## `?UpdateParticles@RndParticleSys@@IAAXXZ`

`RndParticleSys::UpdateParticles` -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1586-1595):

```text
                        // Both bounds of each range are named locals, high
                        // first, and the yaw pair is read AFTER the pitch
                        // LimitAng call: the image loads 0x18c then 0x188
                        // (826C4C34/38), calls, then 0x194/0x190 (826C4C48/4C),
                        // and keeps each low bound in f29/f27 across its call
                        // for the `+ lo` term.  Inlining `mPitch.x` into the
                        // expression reloads it after the call (w7-ao, 93.33);
                        // loading yawLo before the first call hoists the
                        // 0x190 load a call too early (94.43); naming only the
                        // low bounds loads x before y (99.98) -- w7-bt, 100.0.
```

Inside the body (was lines 1610-1621):

```text
                        // The frame scaling is a separate Scale() after the
                        // Set(), not folded into each component: with
                        // `Set(a * frameUpdate, ...)` MSVC hoists the x and y
                        // products and their stores above the final
                        // `bl FastSin` (w7-ao's 14-row residual at 94.43 --
                        // three spellings of the folded form were
                        // byte-identical, and inlining the last FastSin into
                        // the Set argument is too).  Set-then-Scale keeps
                        // negXVel/yVel in f29/f27 across the call and emits
                        // the three `fmuls fN, fN, f31; stfs` pairs after it,
                        // which is the image at 826C4CBC..826C4CD8 (w7-bt,
                        // 94.43 -> 98.87).
```


## `?InitParticle@RndParticleSys@@IAAXMPAVRndParticle@@PBVTransform@@AAVPartOverride@@@Z`

`RndParticleSys::InitParticle` -- moved verbatim from `src/system/rndobj/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1689-1698):

```text
    // w32-a: the residual is instruction SHAPEI not a value.  Our 683
    // instructions and the image's 683 are identical and the relocation-symbol
    // multisets are equal (74/74)I'll The only mnemonic difference is that the
    // image emits `fmr f12, f27` on the false arm and ONE shared
    // `stfs f12, 0x3c(r31)` after the joinI where we store once per armI'll
    // That shared-store shape is not reachable from this branch structure:
    // spelling it as a merge local assigned in both armsI and as an
    // initialise-to-zero armI both measure 99.29429 canonical -- byte-identical
    // to the ternary aboveI'll  Binding the speed scale below to
    // `const float &` is canonical-inert too (99.29429I fuzzy -0.015I)I'll
```

Inside the body (was lines 1912-1938):

```text
    // These two inlined Multiply()s carry most of this function's residual
    // (~30 charged rows). The target seeds each output component's FMA chain
    // from a different matrix row than we do -- Y for all three at this site,
    // and Y/X/X at the Bubble3 site below, so it is /fp:fast scheduling rather
    // than one rule. Open-coding either one with per-component accumulators
    // (the lever math/Mtx.h documents) is REFUTED here: 99.2943 -> 98.5. See
    // the note on Multiply(Vector3, Matrix3, Vector3&) in math/Mtx.h.
    //
    // w26-p: the §8.8 base-rebinding sweep was run over the whole function and
    // every variant is inert or worse (56 charged rows before and after;
    // baseline 99.2943 canonical / 98.7379 fuzzy, re-measured under the pinned
    // name_check ruler after the 2026-11-02 vsearch fix).  A named `float
    // invSpan` local and a no-op control tie the baseline exactly; `Vector3
    // &posOut = particle->Pos3();` ties it on the canonical ruler but not on
    // fuzzy (98.7233); `const Hmx::Matrix3 &xfmM = xfm->m;` is lower (99.2899);
    // `Vector3 &velOut` lower again (99.2855); and naming the color operands
    // (`Hmx::Color &colVel` on the fancy leg, `&midVel`, `&pColVel`) is
    // clearly worse (99.1962 / 98.9898 / 98.0176).  The reason is that every
    // one of those operands is ALREADY a named object or a member access off a
    // named pointer -- their §8.7 keys are in the same class, so the re-key
    // has nothing to move.  What is left is the /fp:fast FMA-chain seeding
    // described above (a scheduling choice inside c2's fp fusion, per
    // C2RS-BRIDGE §8.7 an inline call's SUBEXPRESSION key beating a load, not
    // a commutative re-key), and it is not reachable from the operand
    // spellings.  Behaviour is not in question: no store is added, removed or
    // moved across a call, and the remaining differences are register and
    // operand order over identical values.
```
