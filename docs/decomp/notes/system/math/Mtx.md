# Lane notes: `src/system/math/Mtx.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 127-132; nearest following definition: `Hmx::Matrix4::Matrix4`):

```text
        // w8-e: defined here, not out-of-line in mtx.cpp.  ham_xbox_r.map lists
        // ??0Matrix4@Hmx@@QAA@ABVTransform@@@Z once, flagged `i` (COMDAT) and
        // contributed by rnddx9:Rnd.obj at 0x82610B38 -- an out-of-line
        // definition in mtx.cpp would have been flagged `f` and contributed by
        // math:mtx.obj, and build/373307D9/asm/system/math/mtx.s does not
        // mention the symbol at all.  Ten target units name it.
```


## `Plane::Plane()`

`Plane::Plane` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `Plane::Plane` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 403-411):

```text
    // w13-o: Character::DrawShadow's residual (the image keeps x*0 + y*0 as
    // two products; we factor them into (x+y)*0) does not move with this body.
    // Measured on DrawShadow + CharCollide::Highlight (the only two callers):
    // `-::Dot(normal, point)` and `-::Dot(point, normal)` 97.32 -> 94.34;
    // terms z,y,x flat: inert; `float dot = ...; d = -dot;` (named or via
    // ::Dot): inert; accumulator statements and explicit parens cost Highlight
    // its 100; delegating to Set(): inert.  Call-site respellings in DrawShadow
    // (worldPos.z += planeD first, named point/normal temps, 0.0f literals) are
    // inert too.
```


## `?Normalize@@YAXABVMatrix3@Hmx@@AAV12@@Z`

`Normalize` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 495-501):

```text
// w13-o: written with Cross(), the spelling og-dc3 and rb3-xenon carry. The
// previous body spelled the two cross products out as `out.x.Set(...)` with
// the identical expressions, and that alone was 95.78 canonical (the w8-i
// residual: the image stores out.x z, x, y and we stored z, y, x, with the
// fmuls operand orders and an FPR permutation following from it). Going
// through the inline Cross() is 100.0 for this COMDAT (char/Character.obj);
// whole-binary A/B, full ninja both sides: 1 up, 0 down.
```


## `?Multiply@@YAXABVVector3@@ABVQuat@Hmx@@AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 535-598):

```text
// ⚠ Do NOT right-associate these three sums as `a + (b + c)`.  Measured
// whole-binary A/B in worktree w5-f (2026-09-14, full ninja both sides):
// 5 functions up, 14 down, matched_functions 30962 -> 30958, matched_code
// -800 B, headline 47.6415 -> 47.6345.  The five small functions whose whole
// body IS one inlined copy -- ?Multiply@@YAXABVTransform@@ABVMatrix3@Hmx@@AAV1@@Z
// (100 B), MultiplyInverse (180 B), CharServoBone::MoveToDeltaFacing (216 B),
// GetLightPosition (256 B), DrawBounds (264 B) -- are all at 100% with the
// left-associated form and all fall to 91-96% with parentheses, so the
// left-associated spelling is the original's.  The two functions that GAIN
// from the parentheses (Spotlight::UpdateTransforms 91.956 -> 100.000 exactly,
// RndShadowMap::PrepShadow 93.388 -> 99.985) both call this with a vector whose
// x and z are literal 0.0f; under /fp:fast MSVC factors `(m.x.c + m.z.c) * 0`
// out of the unparenthesised form and the target does not.  Whatever those two
// sites called in the original, it was not this overload spelled this way --
// chase it there, not here.
//
// RESOLVED 2026-09-14 (lane w6-i, same worktree).  Both of those sites are now
// at 100.0 canonical, fixed AT THE CALL SITE, with this overload untouched:
// Spotlight::UpdateTransforms 91.956 -> 100.0 (1456 B) and
// RndShadowMap::PrepShadow 93.388 -> 100.0 (1072 B).  Whole-binary check after
// both: 0 regressions, 2 improvements, 48371 unchanged.
//
//   * The control that settles WHERE the lever lives: spelling this overload's
//     own left-associated tree out at the Spotlight call site scores 91.95605,
//     the baseline to five decimals.  Call-site context is irrelevant; only the
//     expression TREE shape matters.  So a site that needs a different tree can
//     have one locally, and the four callers that are already at 100% are safe.
//
//   * The defect is not the association, it is MSVC's `a*K + b*K -> (a+b)*K`
//     factoring.  Read the target of either function: it emits the two zero
//     terms as plain `fmuls fN,fN,f31` and folds them in with `fmadds`, with no
//     leading `fadds` of two matrix elements.  PrepShadow's target is the proof
//     that association is a red herring -- its y and z components are LEFT
//     associated (they seed from m.x.c at 0x94/0x98) and are still not factored.
//     Right-association merely happens to break the factoring pattern.
//
//   * What actually works is the ACCUMULATOR form -- `float a = ...; a += ...;
//     a += ...;` -- because statement order pins the association and MSVC does
//     not reassociate across the `+=`.  That lets each component seed from the
//     element the target seeds from.  Same shape as Hmx::Dot4 above, which a
//     previous lane also had to write as a reversed accumulator.  On PrepShadow:
//     uniform right-association 99.98508 (4 charged seed-offset rows left),
//     left-associated y/z as one expression 94.66418 (factoring comes back),
//     per-component accumulators seeded like the target 100.0.
//
//   * REFUTED here, one full ninja, 7 probe functions: swapping the multiply
//     operand order in this body to v-first (`v.x * m.x.x + ...`).  All seven
//     scored byte-identical -- MSVC canonicalises the commutative multiply, so
//     operand order in the SOURCE is not a lever on this overload at all.
//
//   * ⚠ The call-site accumulator lever does NOT generalise to every site, and
//     the failure mode is not local.  Lane w7-e (2026-09-14) applied it to
//     RndParticleSys::InitParticle's aliasing `Multiply(particle->Vel3(),
//     xfm->m, particle->Vel3())`, seeded from the Y term exactly as that
//     target does, and the function fell 99.2943 -> 98.5 canonical.  The
//     rows it was aiming at did move, but the five extra named floats and the
//     `Vector3 &`/`Matrix3 &` aliases changed the whole function's register
//     assignment: a fresh r25<->r26 swap appears at index 49, ~600
//     instructions BEFORE the call site, and the charged row count goes 73 ->
//     106.  Spotlight::UpdateTransforms and RndShadowMap::PrepShadow are short
//     functions whose body is essentially the one call; in a 2,700-byte
//     function with 30 live floats the accumulators cost more than the tree
//     shape buys.  Check the function's size and register pressure before
//     reaching for this, and measure the WHOLE function, not the cluster.
```


## `?Multiply@@YAXABVVector3@@ABVTransform@@AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 607-616):

```text
// w13-o: the aliasing arm rotates into a temporary and adds the translation on
// the way out, the shape rb3-xenon recovered for RB3 retail. The fused 4-term
// `out.Set(... + t.v.x, ...)` it replaces scheduled the components z, y, x and
// tail-merged with the fast arm; the image (char/Character.obj COMDAT) evaluates
// x, y, z, reads t.v through the `&t.v` it already computed for the compare,
// and returns from each arm separately. 94.53 -> 100.0; whole-binary A/B (full
// ninja both sides): 1 up, 0 down -- every inlined copy at a call site where
// the compiler can prove &t.v != &out is unaffected.  Both arms then written
// through Multiply(v, t.m, .) (og-dc3's shape; it needs that overload's body
// above it): whole-binary byte-inert, 0 up / 0 down.
```


## `?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 631-682):

```text
// NOTE for whoever is chasing Spotlight::UpdateTransforms: the lane that moved
// this function into the header disclosed a -2.34% regression there and expected
// it back "with the real two-path body". It does not come back, and it cannot.
// Landing the two-path body left ?UpdateTransforms@Spotlight@@IAAXXZ
// byte-identical -- same 1456 bytes, same 91.96% normalized in report.json, not
// in either the regressed or the improved list of a whole-build diff. A body
// this different (one out.Set(9) became a branch plus three per-row Sets) could
// not leave an inlined copy unchanged, so MSVC is not inlining this into
// UpdateTransforms at all; its three Multiply calls are out-of-line. The -2.34%
// is a cost of the PLACEMENT change (mtx.cpp definition -> header COMDAT, so the
// call now binds to Spotlight.obj's own folded copy), not of the body, and it
// has to be attacked as such.
//
// Two paths behind an aliasing test.  The target opens with
//   cmplw cr6, r4, r5 ; beq cr6, <buffered>
// i.e. it compares &b against &out and falls through to the fast path, so the
// source shape is "if (&b != &out) { fast } else { buffered }".  The fast path
// stores each output row the moment it is computed (stfs to 0x0/0x4/0x8 of the
// row, three floats -- a Vector3::Set), which is only alias-safe when b and out
// are distinct.  The buffered path computes each row into a stack Vector3 and
// copies it out with Vector3's implicit operator=, which is a 16-byte copy
// because of the trailing PAD word -- that is why the target's tail moves four
// words per row (lwz/stw at +0x0/+0x4/+0x8/+0xc) instead of three, and why it
// writes garbage into out's pad slots.
//
// Floor 80.7 canonical (lane w7-ay, 2026-09-14), consistent with the notes
// above: the residual is FPR scheduling of the 27 products (the target keeps
// f24-f31 and r30-r31 live, we keep f25-f31 and r31) and the store order.
// Read from the target without building: vy and vz share one stack slot
// (target -0x70, ours -0x60) so a `Matrix3 tmp` is out, and the +0xc pad copy
// rules out `Set()` copies.  Built and measured, each a full ninja:
// `Vector3 vz, vy, vx;` is inert (80.7, same rows); copy-out order z,y,x is
// 77.8 and y,x,z is 80.0 against x,y,z's 80.7.  One real lever surfaced: the copy
// order decides which b element MSVC hoists above the alias branch -- z,y,x
// hoists b.y.x, x,y,z hoists nothing, and the target hoists b.y.y
// (`lfs f7, 0x14(r4)` at 0x8236E970 in CharLookAt.s).  w13-o built the
// remaining three: x,z,y 81.05 (kept -- whole-binary A/B, full ninja both
// sides: this row 80.706 -> 81.053, nothing else moved), z,x,y 78.3, y,z,x
// 79.9.  None of the six reaches the image's b.y.y hoist, so the copy order is
// not the whole story.
// w21-q (81.05, unchanged): read the image's 18 output trees symbolically
// (fast arm row a.x: x=((Y+Z)+X), y=((Z+Y)+X), z=((Y+X)+Z); the alias arm and
// the other rows use other associations) -- the image's association is NOT
// uniform per row, so MSVC chose it, and this flat-sum body already produces
// 10 of the 18 trees.  Standalone cl.exe probe (reproduces this build
// exactly), each scored on raw registers and on tree identity: all 216
// per-component term orders of the Vector3 x Matrix3 row helper as
// accumulators (best 105/170 raw rows but only 5/18 trees), parenthesised
// (89 raw, fewer trees), flat (canonicalised: every order identical),
// per-component float temps in all 6 statement orders; inverted branch,
// early return, separate Vector3 decls, a Matrix3 temp (direct or recursive).
// None beats this body on trees; same 80.7 floor in rb3-xenon's mtx.cpp copy.
```

Inside the body (was lines 689-695):

```text
        // The three products are independent and each row is copied into a
        // distinct row of a distinct object, so this order is a free choice.
        // It is measured: all 6x6 product/copy orderings were ranked with the
        // c2rs replay, and this one is 84.42 canonical against 81.05 for the
        // previous x,y,z / x,z,y spelling; the winner was confirmed by a
        // whole-report A/B (UP 1, DOWN 0). The three Vector3 declarations'
        // order is inert. w26-w.
```
