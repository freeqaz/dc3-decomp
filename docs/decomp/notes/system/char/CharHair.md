# Lane notes: `src/system/char/CharHair.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SimulateInternal@CharHair@@IAAXM@Z`

`CharHair::SimulateInternal` -- moved verbatim from `src/system/char/CharHair.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 285-308):

```text
                    // REFUTED (lane w7-a, 2026-09-14): spelling this as an
                    // accumulator chain seeded from vRes.z -- the image's term
                    // order, and the lever that closed CharBones::ScaleAdd and
                    // both CharIK/LookAt Polls in this same lane -- REGRESSES
                    // this site, 99.4775 -> 98.3, and makes the function three
                    // instructions LONGER.  vRes is a real Vector3 that is
                    // mutated and stored afterwards, so forcing the sum's
                    // association also pins vRes into its stack slot instead of
                    // letting the three differences stay in f0/f12/f13.  The
                    // accumulator lever only pays where the vector is dead
                    // after the reduction.
                    // w22-hc: written y, z, x on purpose.  The image sums
                    // (z*z + x*x) + y*y (rows 183-186: fmuls f11,f0,f0 with
                    // f0 = z diff; fmadds x; fmadds y).  /fp:fast MSVC rotates a
                    // flat 3-term sum -- measured: written x,y,z (LengthSquared)
                    // emits y,z,x; written z,x,y emits x,y,z; written y,z,x
                    // emits z,x,y -- so only this order gives the image's
                    // rounding, and it takes the whole function from 99.57 to
                    // 100 (the 531-540 "floor" below goes with it).  Explicit
                    // parentheses in the image's association (inline or as a
                    // helper) re-schedule pt.pos.z's gravity add and the vRes
                    // stores: 94.2.  Native (clang, left-assoc, no FMA) gets
                    // (y*y + z*z) + x*x -- still not the image's association,
                    // and no single spelling gives both (BUG-OPEN, sub-ulp).
```

Inside the body (was lines 311-328):

```text
                    // w25-ha (fuzzy 99.782, normalized 100): the 14 rows left in
                    // this function are all commutative operand order (c2 §8.7
                    // cqlo3 sort keys), no register or value differs.  Here: the
                    // scaling fmuls put operator*='s inlined float param first
                    // (variable leaf sid 737, key 0x15c20) over vRes's scalar
                    // pieces (sids 288-291, ~0x124xx); the image has vRes first.
                    // The pos += vRes adds compare pt.pos memory/temp leaves
                    // (0x18008, temp 0x16d00) against the same vRes keys; the
                    // image wants vRes.y/z first but pos.x first, which no shift
                    // of a base temp by mod 4 gives while vRes stays a variable
                    // leaf.  Hypothesis: the image's scaled vRes components are
                    // CSE temps (different key class), not this spelling.  Inert
                    // or worse (8 spellings): named scale (before or after vRes),
                    // Scale()/Add() helpers, explicit components, a delta vector,
                    // ScaleAddEq pairs.  The rsalen fmsubs (pt.length memory leaf,
                    // base temp sid 1574 = 2 mod 4, key 0x18008, over rsa sid 297)
                    // needs that base at 0 mod 4; ScaleAddEq/collide and the
                    // points[j - 1] add are the same key class.
```

Inside the body (was lines 362-375):

```text
                // REFUTED (lane w7-a, 2026-09-14): spelling this ScaleAdd as
                // three per-component statements in the image's emission order
                // (y, x, z -- the image's three fmadds land in f24, f25, f23
                // in that order, ours in f24, f23, f25) does NOT close the
                // cluster.  Canonical does not move (99.6 either way) and raw
                // drops 99.0 -> 98.9: the three [291]-[296] offset rows survive
                // with merely different offsets, and twelve NEW rows appear
                // hundreds of instructions away ([468], [488], [500]-[502],
                // [520], [525], [552]).  idealPos is live across the Interp
                // call and the entire collide loop, so all three components sit
                // in callee-saved FPRs with no stores at all; there is no store
                // order to shape, and forcing the emission order only perturbs
                // the allocator's choices downstream.  This cluster is
                // scheduler-owned, not source-owned.
```

Inside the body (was lines 462-467):

```text
                // CLOSED (w22-hc): this was recorded as a scheduling floor
                // (w7-bb, 99.57076; w21-ae, 20 spellings) on rows 531/533/540,
                // the pt.force -> pt.lastFriction copy's load interleave.  It
                // was not owned here: it followed the slack-check sum upstream
                // (see the lensq comment), and spelling that sum in the
                // image's order removes these rows with it.
```


## `??0Strand@CharHair@@QAA@PAVObject@Hmx@@@Z`

`CharHair::Strand::Strand` -- moved verbatim from `src/system/char/CharHair.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 752-759):

```text
// w14-b: `inline` -- the map lists this ctor `f i` (char:CharHair.obj), i.e. an
// inline COMDAT. Defined plain out-of-line, MSVC propagated its register footprint
// into same-TU callers (w8-i measured it touches none of r6/r29/r30/r31), so
// ObjVector<Strand>::resize parked `this` in volatile r6 across the call and
// PropSync<Strand> kept i/node in volatile regs; the image saves them
// (`bl __savegprlr_29` / `_25`). With `inline` both match: resize 57.65 -> 100,
// PropSync(ObjVector<Strand>&...) 98.18 -> 100; the ctor itself stays 100.
// (See docs/decomp/patterns/map-comdat-flag-gates-clobber-propagation.md.)
```


## `?SetRoot@Strand@CharHair@@QAAXPAVRndTransformable@@@Z`

`CharHair::Strand::SetRoot` -- moved verbatim from `src/system/char/CharHair.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 819-824):

```text
        // w17-c: 99.987 -> 100 (the 4-row "no source spelling" residual that was
        // noted here).  The ScaleAdd is written out at the call site, with the
        // two WorldXfm() reads named in the order the call evaluated them (the
        // m.y argument first, right to left).  Measured on a non-PCH probe:
        // ScaleAdd(named refs) 10, ScaleAdd(inline calls) 32, this spelling 0;
        // every component/operand order of the explicit form also scores 0.
```
