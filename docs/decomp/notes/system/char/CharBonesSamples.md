# Lane notes: `src/system/char/CharBonesSamples.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Relativize@CharBonesSamples@@QAAXPAVCharClip@@@Z`

`CharBonesSamples::Relativize` -- moved verbatim from `src/system/char/CharBonesSamples.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 401-434):

```text
// w21-aq: stopped with 16 rows left (99.7 normalized), none of them a value:
// (1) the image forms every `mStart + mOffsets[TYPE_ROTX]` at a loop ENTRY as
// off+start (`add rX, rRotx, rStart`) where ours forms start+off, plus the
// register naming that follows (rows 70/163/202/236-247/273-275/320); the
// QUAT/END entries agree.  (2) the kCompressNone quat loop: the image computes
// the ROTX end bound before `b` into the loop test, ours jumps one instruction
// earlier onto the shared loop-bottom `add` (1 delete + 1 branch row) --
// identical behaviour, a tail-merge choice.  Measured inert: spelling the ROTX
// expressions offset-first (`mOffsets[TYPE_ROTX] + mStart`), and through the
// CharBones::RotXOffset() accessor.  MSVC canonicalises the operand order, so
// the source text of the sum does not reach it.
//
// w26-f VERDICT: all 16 rows are equivalent.  Re-read against the target listing
// (name_check, full): the six `add` rows pair with the two `lwz` rows beside them
// (r9/r10/r11 permuted), so each bound is `mStart + mOffsets[T]` on both sides --
// e.g. image `lwz r10,0x40/ lwz r11,0x14/ add r28,r10,r11` vs ours the same two
// loads into the other registers and `add r28,r11,r10`.  The one structural row
// (image-only `add r11, r10, r11` at the kCompressNone ROTX preheader, +4 bytes,
// dragging every downstream `b`/`bge` displacement with it) is the tail-merge
// w21-aq already adjudicated.
// w26-f MECHANISM (c2rs §8.7, measured): the source text cannot fix these
// because the order is the post-lowering commutative sort, descending on the
// printed `cqlo3` key.  The bounds are memory leaves
// `M6{key}[d00000000 w029f b V<sid>]` with key `0x10000 | (base sid & 3) << 14 |
// 8`.  The leaves that carry the three bounds print 0x10008 on V788, 0x14008 on
// V849 and 0x1c008 on V859; the function's other leaves print 0x16008 / 0x18008
// / 0x1a008 on V3 / V790 / V5.  The QUAT and END bounds agree with the image and
// only the ROTX one does not, so the required change is to ONE bound's base sid
// mod 4 -- and §8.7's only lever ("add or drop an upstream temp so a base sid
// moves by one") moves every base at once, which would break the two bounds
// that already agree.  No upstream-temp spelling was spent on it for that
// reason.  Open question: whether the ROTX bound's base is minted by a path the
// other two do not take (the extra image `add` above is the only structural
// difference between the three loop entries).
```

Inside the body (was lines 469-475):

```text
                // Inert, measured: declaring v before/after evalPos; spelling the
                // subtraction per component in the image's x,z,y store order;
                // `Subtract(v, evalPos, v)` in place.
                // w21-aq: subtracting into a FRESH Vector3 (`Subtract(v, evalPos,
                // d); pos->Set(d)`) closes all 16 rows of this block (the image's
                // x,z,y subtract order and evalPos load order) -- same values,
                // same 0x90 slot for the Set argument.
```


## `?EvaluateChannel@CharBonesSamples@@QAAXPAXHHM@Z`

`CharBonesSamples::EvaluateChannel` -- moved verbatim from `src/system/char/CharBonesSamples.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 620-664):

```text
// w7-by (88.04237 -> 92.47034 canonical): the w7-as "r30/r31 role swap" (dest in
// r31 at 0x823E11F4, srcNext in r30 at 0x823E1310) and the placement of the
// shared ROTX tail (fmuls at 0x823E122C kept in the frac == 0 arm, stfs at
// 0x823E136C kept in the frac != 0 arm) were both the CFG: the image is one
// if/else tree with a single exit, not a ladder of early returns.  Written as
// og-dc3 nests it, both fall out, and the frac == 0 kCompressVects arm
// (hoisted lha at 0x823E1284, three slots, dead `mr r11, r8`) is reproduced by
// ShortVector3::ToVector3 -- which MSVC folds identically to Vector3(short *)
// and to Set(); all three were byte-identical here.
// NEGATIVE RESULTS (w7-by, canonical): `int comp` in the frac == 0 half drops
// the two extsh at 0x823E1248 but shrinks the frame 0xd0 -> 0xb0 -- the slot
// packer then overlaps q0/q1 with sv0/sv1 (q1 on sv0 at 0x60), 82.2;
// `unsigned short comp` keeps 0xd0 but costs clrlwi+cmplwi, 91.7; hoisting
// sv0/sv1/q0/q1 to the frac != 0 scope (or leaving them per-arm) is inert --
// packing is by liveness; ShortVector3 by-value copies materialise sth/lha,
// 70.3; Interp(v0, v1, frac, *out) + `*out *= scale` for ROTX, scalar Interp
// for the quat lerps, v1-before-v0 in ROTX: all byte-identical; converting sv1
// before sv0: 90.5.  Residual at 92.5: the two extsh, the ROTX short-arm
// load/convert order (0x823E1328..0x823E1354), element 1 of the uncompressed
// quat lerp, and the six-conversion schedule of the frac != 0 vects arm
// (0x823E1474..0x823E153C).
// w16-c: reading mCompression directly in the frac == 0 half (no local) is
// the w7-by `int comp` result again, 82.2 / frame 0xb0; one function-scope
// `int comp` for both halves is 79.8.
// w21-am (92.47, unchanged; ~44 rows): measured inert, each with the built
// object -- ROTX compressed arm as scalar Interp(...) * k, via short locals,
// via short* locals, as `(v1 - v0) * frac + v0`, and as `*out = ...; *out *=
// k` (the image loads src before srcNext at 0x823E1328; every spelling keeps
// srcNext first); vects arm with `Vector3 sv1, sv0`, ShortVector3* locals,
// Vector3 3-float ctors, and an interleaved x0/x1/y0/y1/z0/z1 conversion
// (92.49 canonical / fuzzy down).  `CompressionType comp` / `long comp` =
// 82.19 (the int result), `int comp = (short)mCompression` 87.87.  With an
// int comp the frame is 0xb0 because the frac != 0 vects arm REUSES its
// double-conversion temps; the image keeps all six live at once
// (0x50/0x58/0x60/0x70/0x80/0x90) and that is what puts q0/q1 at 0x90/0x80
// -- so the frame and the vects-arm schedule are one residual, not two.
// w31-d (2026-11-03): re-checked against the listing -- no new lever, no
// defect.  The whole-function opcode multiset delta is exactly the documented
// one: our two `extsh` (the `short comp` truncate) plus one `mr` in the
// vects-arm conversion schedule; every other row pairs 1:1.  The ROTX
// compressed arm is the same interpolation written from the other end (image
// `(srcNext - src)*frac + src`, ours `(src - srcNext)*frac + srcNext`) -- a
// /fp:fast reassociation, not a different value.  Do not re-run the `short`
// ladder: int / unsigned short / CompressionType all land 82.2-91.7 and the
// two extsh are its entire cost (w7-by, w16-c, w26-r2 above).
```

Inside the body (was lines 675-690):

```text
            // `short` on purpose: see the w7-by note above (int shrinks the
            // frame to 0xb0 and repacks the four 16-byte locals).
            // w21-i re-check (stopped at 92.47): the image does NOT truncate
            // -- 0x823E1248 `lwz r11, 0x4(r11)` feeds `cmpwi r11, 0x3` at
            // 0x823E1254 directly; our two `extsh` there are the only cost
            // of this spelling.  Behaviour is identical for every
            // CompressionType value (0..4); it differs only for an int outside
            // short range, which Load would have to read from a corrupt file.
            // So the short is a matching artifact, not a behaviour bug.  Re-measured: `int comp` with q0/q1/sv0/sv1 hoisted
            // to function scope is 82.2 (frame 0xb0), the same as w7-by's.
            // w26-r2 measured the NARROW spelling too, since this arm has no
            // q0/q1/sv0/sv1 in scope: `int comp` here alone is 82.19492 --
            // the frame repacks to 0xb0 anyway, so no narrower form recovers
            // it.  `unsigned short comp` is 91.72034.  The two `extsh` are
            // the entire cost of the short.  Oracle verdict: EQUIVALENT
            // (unicorn harness 4, high confidence).
```

Inside the body (was lines 745-757):

```text
                // w26-r2: the residual here is the scheduler's ordering of the
                // two fully inlined ShortVector3::ToVector3 bodies, not a
                // source-order property.  The image interleaves the six
                // component stores x,x,y,y,z,z (sv0.x at 0x60, sv1.x at 0xa0,
                // sv0.y at 0x64, ...); ours emits sv0's three then sv1's.
                // Ten spellings -- sv0/sv1 declared in either order, as one or
                // two declarations, named pointer or reference bases, an
                // explicit six-component Set, a named `out` for the Interp
                // destination -- all compile BYTE-IDENTICALLY (92.47034
                // unchanged); reversing the two calls drops to 90.53.  Both
                // sides read the same six components and multiply each by
                // 0.039674062f, store to the same six slots.  Oracle verdict:
                // EQUIVALENT (unicorn harness 4, high confidence).
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharBonesSamples.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 885-898; no definition follows it):

```text
// w8-c: orphan COMDAT, but NOT a template -- `NormalizeTo` is an `inline` free
// function in math/Mtx.h, so there is no explicit instantiation to write.  The
// image's CharBonesSamples.obj carries it out-of-line at 0x823E1150
// (CharBonesSamples.s) and it is the ONLY definition in the whole binary; the
// three `bl` sites are all in math/Key.s (0x82E07480/8C/98, Key.cpp's
// Catmull-Rom quat interpolation), which means CharBonesSamples.obj won the
// COMDAT fold and retail's CharBonesSamples.cpp odr-used NormalizeTo somewhere
// we have not recovered -- there is no inlined copy in the image's unit either
// (the only fnegs in CharBonesSamples.s outside this body are FracToSample's,
// at 0x823E2DE4).  Taking the address is the only thing that forces MSVC to
// emit the body.  It MUST have external linkage: a namespace-scope `const`
// pointer has internal linkage, MSVC then discards the unused definition, and
// the odr-use never happens (measured: the symbol was absent from the object
// and the row stayed at 0.0).
```
