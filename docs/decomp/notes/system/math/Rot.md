# Lane notes: `src/system/math/Rot.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Set@Quat@Hmx@@QAAXABVVector3@@@Z`

`Hmx::Quat::Set` -- moved verbatim from `src/system/math/Rot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: ambiguous: 2 overloads of `Hmx::Quat::Set` fit equally; filed under the first, also possible: `?Set@Quat@Hmx@@QAAXABVMatrix3@2@@Z`

Above the definition (was lines 48-60):

```text
// w21-ac (96.87692 -> 100): the z-rotation half reads TWO of the old
// components -- x in the new y, w in the new w -- through a reference to
// *this, the rest directly.  MSVC's load forwarding keys on the object
// expression (lever: bind the object to a local reference), and only that
// mix gives the image's schedule: z*s, y*s, y*c, z*c standalone with x*s,
// w*s, x*c, w*c fused, stores w, x, y, z.  The selection is measured, not
// read off anything: of the 256 this/q mixes, 30 reach 100 (every one has q
// on s*x in y and c*w in w), all q is 94.97, no q is 94.88.  Behaviour is the
// same for every mix (Set evaluates all four arguments before storing); the
// fused-product choice now matches the image too (we used to fuse y*c and
// z*c instead).  w16-c had recorded as inert: member writes from saved
// ox/oy/oz/ow in w,x,y,z order; the y/z args written s-first; not Multiply()
// (the image has no zero terms).
```


## `?Normalize@@YAXABVQuat@Hmx@@AAV12@@Z`

`Normalize` -- moved verbatim from `src/system/math/Rot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 157-165):

```text
// w25-ge: the image multiplies w as (res, w) and z, y, x as (q, res).  Those
// commutative operand lists are fixed in the IL before either allocator runs
// (res is a demoted one-block local; the components are #2af reloads of the dot
// product's CSE loads), and they follow which name each component is read
// through.  A combinatorial search over the c2rs tap (194 spellings: dot via qin
// or a const ref, each component via qin, the ref or a named local, res reused
// or a separate inv) found exactly one shape (with either res spelling): the dot
// and x, w through the ref, y, z through qin.  Every single-name spelling leaves
// w or z on the wrong side.
```


## `?MakeRotQuat@@YAXABVVector3@@0AAVQuat@Hmx@@@Z`

`MakeRotQuat` -- moved verbatim from `src/system/math/Rot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 292-368):

```text
// w16-c (90.30645, 13 rows, all FP colouring inside the inlined Cross /
// LengthSquared / Dot block): Dot(v2, v1) for Dot(v1, v2) is byte-identical.
// w21-ac (90.30645, unchanged): the image (0x82533978) sums |v1|^2 as
// (xx + zz) + yy and the dot as (x + z) + y, |v2|^2 as (xx + yy) + zz.  Both
// are now written out in that order so the native build evaluates in the
// image's association (fidelity fix); on PPC the spelling is byte-identical
// (/fp:fast re-sorts).  Remaining rows: the four `fmr` copies MSVC makes of
// v1.z/v2.y/v2.z/v1.y while inlining (the image copies the other operand of
// each pair) and the load order v1.z, v2.y, v2.z (ours v2.z, v1.z, v2.y).
// Inert (probe, all byte-identical): all 64 operand orders of the Cross
// products written as a Vector3 ctor; Dot(v1,v1)*Dot(v2,v2) for the lengths;
// LengthSquared(v2) * LengthSquared(v1); separate l1/l2 locals; the dot as a
// local after sq; q.x/y/z/w member stores; early return.  Worse: dot local
// before sq 71.7; sq before Cross 65.3; Scale/`*=` of vec 88.8; rb3-xenon's
// six component locals 50.8.  Binding v1/v2 to local references (the lever
// that closed Quat::Set(Vector3)): 192 placements measured; the only ones
// that keep the image's association move fuzzy 89.42 -> 89.68 (the dot read
// as a.x * b.x + ... through refs; the v2.y copy then matches) and leave
// canonical at 90.31 -- not shipped.
//
// w29-a (2026-11-02): PERMUTATION CLASS -- the RESULT is equal, and the row is
// not a spelling. Ours and the image are 62 instructions / 248 B on BOTH sides,
// with an identical mnemonic census (b 1, ble 1, blr 1, fadds 1, fcmpu 1,
// fdivs 2, fmadds 6, fmr 4, fmsubs 3, fmuls 12, fsqrts 2, lfd 3, lfs 10,
// lis 4, stfd 3, stfs 8) and no diff_op row -- every remaining row is
// diff_arg. Six further commutative operand-order spellings of the dot and the
// length products compiled BYTE-IDENTICALLY (c2 canonicalises them --
// C2RS-BRIDGE section 8.7), so that axis has no handle. Statement order does move
// it and all of it is destructive: Cross after lenSq1 61.18, Cross after dot
// 69.95, `const Vector3 &a = v1, &b = v2` ref-binding 49.02 (272 B).
// Left as the scheduling/emission-order class.
//
// w48-a (2026-11-06): the c2rs tap settles the colour half of that class, and it
// is NOT an FPR permutation.  `c2rs stage cands --regasg` on this TU (neutral:
// stage-tap-obj-differs 0, replay-vs-capture-differs 0, cands-errors 0) gives
// every candidate a DISTINCT priority (28, 22, 22, 18, 16, 14, 14, 14, 8) and
// the selector picks the registers the image uses for the same values
// (f0 = v1.z, f13 = v2.y, f11 = 1.0f, f12 = 0.5f, f10 = 1e-7f on both sides).
// What differs is DOWNSTREAM of both allocators: the sched3 order of the three
// preheader `lfs` that the inlined `Cross` emits.  Image: v1.z(0xc), v2.y(0x14),
// v2.z(0x20); ours: v2.z(0xc), v1.z(0x14), v2.y(0x1c).  The `RT` list shows our
// mixed products uniformly v1-first (`u{ s54 s56 }`, `u{ s48 s47 }`) while the
// image has v1 first for x but v2 first for y and z.  That is c2's post-lowering
// commutative sort (C2RS-BRIDGE 8.7), whose key follows MINT ORDER for these
// temp leaves -- so it is fixed by the load schedule, and `Cross` lives in
// math/Vec.h, a PCH-reached header that cannot be touched from this TU.
// Eight more spellings this wave, five of them BYTE-IDENTICAL: `Dot(v1,v2)`,
// `const float &v1z/v2z`, `LengthSquared(v1)`, named products pxx/pzz/pyy, and
// `float z1 = v1.z`.  Destructive: `const Vector3 &a = v1;` before lenSq1
// 64.29 (256 B) and dot-before-lenSq1 76.26 (224 B).
// One real handle, canonical-neutral so NOT banked: `const Vector3 &b = v2;`
// bound AT THE USE SITE just before the dot line, with `v1.x*b.x + v1.z*b.z +
// v1.y*b.y`, keeps canonical at 90.30645 and moves fuzzy 89.41936 -> 89.66129
// (equal 52 -> 54, diff_arg 7 -> 5, body 248 B unchanged).  Combine it with any
// future latch on this row.
//
// NEGATIVE CONTROL, and the reason canonical alone must not accept a change
// here: `float f1 = 0.25f / (sq * sq2);` -- a WRONG CONSTANT -- scores
// 93.50000 canonical (up from 90.30645) while equal rows collapse 52 -> 24 and
// the body grows to 256 B.  That is the XTEABlockEncrypter trap measured on
// this row; it is exactly why the row classes are read alongside the headline.
//
// SECOND INSTANCE OF THE SAME TRAP, found by the permuter (2026-11-09) and NOT
// banked.  Its `commutative_swap` move parenthesised the length sum as
//     float lenSq1 = (v1.x * v1.x + (v1.z * v1.z + v1.y * v1.y));
// which reports canonical 90.30645 -> 96.741936 and fuzzy 89.41936 -> 93.758064
// (+6.44 / +4.34, the largest delta that search has produced on this tree).
// It is a FALSE GAIN: equal rows fall 52 -> 39 (equal_percent 80.0 -> 61.9),
// diff_arg rises 7 -> 22, and the body is unchanged at 248 B.  Alignment moved
// -- inserts+deletes 6 -> 2 -- and paired-orphan rows are scored far more
// generously than register-mismatched ones, so the headline rises while the
// function reproduces strictly less.  Measured in one worktree, both states,
// via `objdiff-cli diff --include-instructions --summary`.
// So: a permuter delta on this row is not evidence of progress unless the
// equal-row count also rises.  The same caveat applies in reverse to the
// `const Vector3 &b = v2;` handle above, which is canonical-neutral and was
// left unbanked for the same reason.
```


## `?Multiply@@YAXABVVector3@@ABVQuat@Hmx@@AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/Rot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 393-430):

```text
// w21-ac: ONE body for PPC and native, in the image's float association.
// An `#ifdef HX_NATIVE` arm used to run a different formula natively
// (vx * (1 - 2(yy+zz)) + ...): algebraically the same rotation, a different
// evaluation order.  The image (0x82533AF0) computes every component as
//   ((vx-term + vy-term) + vz-term) * 2 + v
// e.g. out.x = fmadds(f13 = (xz+yw), vz, f11 = (-zz + -yy)*vx + (xy-zw)*vy),
// then fmadds(f13, 2.0f, vx), with the three neg-squares fneg'd separately.
// The PARENTHESES below are load-bearing: MSVC's /fp:fast re-sorts a flat
// sum (all 216 left-associated term orders compile to the same code, which
// landed as (vz + vx) + vy -- NOT the image's association) but keeps an
// explicitly parenthesised grouping.  So the PPC build now rounds like the
// image too (symbolic evaluation of both listings), and native evaluates
// the same order (fidelity fix, both builds).  Every vin component is read
// into a local before any vout store, so &vout == &vin (HamSkeletonConverter)
// stays safe.  Declaration order and operand order inside each product/sum
// are a measured search (hill-climb over commutative swaps and independent
// declaration swaps, keeping the association exact): fuzzy 75.6 for the
// first parenthesised spelling -> 85.7 here, vs 84.8 for the old flat body.
//
// w7-av (91.5 canonical, the old flat body): a pure FP-scheduling residual.
// The image's emission order is: q.x, q.z, qx*qx, qz*qz, q.y, q.w, qy*qx,
// qz*qw, vin.y, qz*qy, vin.x, qx*qw, vin.z, qy*qy, 2.0f, qz*qx, qy*qw.
// Rejected there: x,y,z term order (inert -- MSVC re-sorts), neg_* order,
// vout.x/vout.y swap 87.3, squares hoisted 75.0, image emission order as
// declaration order 83.2.  w21-ac also measured: a vout.Set() of the sums
// (og-dc3 shape) 67.8; plain vin locals 26.2; neg locals with the products
// inline 46.5; the reference-binding lever is inert (every component is
// loaded once into a local).
//
// w29-a (2026-11-02): PERMUTATION CLASS -- the RESULT is equal, and the row is
// not a spelling. 48 instructions / 192 B on BOTH sides, identical mnemonic
// multiset, no diff_op row. The only two differences are the load order of
// vin.x vs vin.y (the image reads viny at slot 11 and vinx at 13; we do vinx
// then viny) and the slot the q.y load lands in -- i.e. whether the three
// fmuls-by-vin run before or interleaved with the two fadds. Swapping the two
// declarations is the one spelling that reaches it and it makes the REGISTERS
// match the image (f3 = vinx, f5 = viny) without moving the loads: fuzzy fell
// 85.73 -> 85.52. Emission order with no source handle; left as-is.
```
