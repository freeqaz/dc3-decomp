#include "math\Mtx.h"

Hmx::Matrix2 Hmx::Matrix2::sID(Vector2(1, 0), Vector2(0, 1));
Hmx::Matrix3 Hmx::Matrix3::sID(Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1));
Hmx::Matrix4 Hmx::Matrix4::sID(
    Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, 0, 0, 1)
);

Transform Transform::sID(Hmx::Matrix3::GetIdentity(), Vector3(0, 0, 0));

// w8-e: Matrix4(const Transform &) moved into Mtx.h as an inline member --
// the image emits it as a per-TU COMDAT, not as a math:mtx.obj definition.

// Computes determinant of 3x3 matrix and returns its reciprocal (1/det)
// Used for matrix inversion - returns 0 if matrix is singular
float Det(const Hmx::Matrix3 &m) {
    float det = (m.y.y * m.z.z - m.y.z * m.z.y) * m.x.x
                - (m.y.x * m.z.z - m.y.z * m.z.x) * m.x.y
                + (m.y.x * m.z.y - m.y.y * m.z.x) * m.x.z;
    if (det == 0) {
        return det;
    }
    return 1.0f / det;
}

void Invert(const Hmx::Matrix3 &min, Hmx::Matrix3 &mout) {
    float det = (min.y.y * min.z.z - min.y.z * min.z.y) * min.x.x
                - (min.y.x * min.z.z - min.z.x * min.y.z) * min.x.y
                + (min.y.x * min.z.y - min.z.x * min.y.y) * min.x.z;
    float mult = 0.0f;
    if (det != 0.0f) {
        mult = 1.0f / det;
    }
    mout.Set(
        (min.z.z * min.y.y - min.y.z * min.z.y) * mult,
        -((min.z.z * min.x.y - min.x.z * min.z.y) * mult),
        (min.y.z * min.x.y - min.x.z * min.y.y) * mult,
        -((min.z.z * min.y.x - min.y.z * min.z.x) * mult),
        (min.z.z * min.x.x - min.x.z * min.z.x) * mult,
        -((min.y.z * min.x.x - min.x.z * min.y.x) * mult),
        (min.z.y * min.y.x - min.z.x * min.y.y) * mult,
        -((min.z.y * min.x.x - min.z.x * min.x.y) * mult),
        (min.y.y * min.x.x - min.x.y * min.y.x) * mult
    );
}

// Multiply(Matrix3, Matrix3, Matrix3) is inline in Mtx.h -- see the note there.

void Multiply(const Transform &a, const Transform &b, Transform &out) {
#ifdef HX_NATIVE
    // Native path: out.v = a.v * b.m + b.v (same math as the PPC path below).
    //
    // ALIASING (2026-07-02, feet ankle-solve round 2): callers routinely pass
    // `out` aliased to `b` — e.g. HamIKEffector::Poll's back-transform does
    // `Multiply(effW, inv, inv)` then `Multiply(inv, finalXfm, finalXfm)`.
    // The original PPC code below is carefully alias-safe: it computes the
    // TRANSLATION first (with an explicit `&b != &out` branch) and only then
    // the matrix product. A previous native rewrite computed `out.m` FIRST,
    // so aliased calls formed the translation from the already-clobbered
    // product matrix — a venue-offset-scale corruption of every aliased
    // Transform compose (ankle IK targets flung to +/-200 for venue-placed
    // dancers; near-origin characters only mildly off). Compute the
    // translation into temporaries BEFORE any write; Multiply(Matrix3) is
    // itself alias-safe (evaluates all products before writing via Set).
    float vx = a.v.x * b.m.x.x + a.v.y * b.m.y.x + a.v.z * b.m.z.x + b.v.x;
    float vy = a.v.x * b.m.x.y + a.v.y * b.m.y.y + a.v.z * b.m.z.y + b.v.y;
    float vz = a.v.x * b.m.x.z + a.v.y * b.m.y.z + a.v.z * b.m.z.z + b.v.z;
    Multiply(a.m, b.m, out.m);
    out.v.x = vx;
    out.v.y = vy;
    out.v.z = vz;
#else
    // w19-c (67.77 -> 100, all 62 rows equal): the translation is the
    // Multiply(const Vector3 &, const Transform &, Vector3 &) shape rb3-xenon
    // carries -- rotate straight into out.v and add t.v when out does not
    // alias b, rotate into a temporary otherwise -- with the add written
    // t.v + out.v (the image's fadds operand order).  Measured spellings:
    //   Multiply(a.v, b.m, out.v) + Add in the fast arm           60.1
    //   a call to the Transform overload (not inlined)             12.5
    //   spelled-out Set() written on a.v / b directly, no refs     2 lfs rows swapped
    //   the else arm ALSO written through v / t                     62.7
    //   this (refs in the fast arm only)                           100
    // So the shared y-component partial the image hoists above the alias
    // test, and the b.v.x / out.v.x load order, both hang on exactly this
    // spelling; do not "tidy" the two arms into one style.
    const Vector3 &v = a.v;
    const Transform &t = b;
    if (&t != &out) {
        out.v.Set(
            t.m.x.x * v.x + t.m.y.x * v.y + t.m.z.x * v.z,
            t.m.x.y * v.x + t.m.y.y * v.y + t.m.z.y * v.z,
            t.m.x.z * v.x + t.m.y.z * v.y + t.m.z.z * v.z
        );
        Add(t.v, out.v, out.v);
    } else {
        // out aliases b, so rotate into a temporary and add on the way out.
        Vector3 tmp;
        Multiply(a.v, b.m, tmp);
        Add(tmp, b.v, out.v);
    }
    Multiply(a.m, b.m, out.m);
#endif
}


void FastInvert(const Hmx::Matrix3 &min, Hmx::Matrix3 &mout) {
    float xdot = Dot(min.x, min.x);
    if (xdot != 0)
        xdot = 1.0f / xdot;
    float ydot = Dot(min.y, min.y);
    if (ydot != 0)
        ydot = 1.0f / ydot;
    float zdot = Dot(min.z, min.z);
    if (zdot != 0)
        zdot = 1.0f / zdot;
    mout.Set(
        min.x.x * xdot,
        min.y.x * ydot,
        min.z.x * zdot,
        min.x.y * xdot,
        min.y.y * ydot,
        min.z.y * zdot,
        min.x.z * xdot,
        min.y.z * ydot,
        min.z.z * zdot
    );
}

QuatXfm::QuatXfm(const Transform &tf) : v(tf.v), q(tf.m) {}

// Transform::LookAt is defined in-class in Mtx.h -- see the note there.

// Matrix4 operator*(Matrix4, Matrix4) is inline in Mtx.h

float Det(const Hmx::Matrix4 &m) {
    float a11 = m.y.y, a12 = m.y.z, a13 = m.y.w;
    float a21 = m.z.y, a22 = m.z.z, a23 = m.z.w;
    float a31 = m.w.y, a32 = m.w.z, a33 = m.w.w;

    // Cofactor expansion along row 0, reusing a single Matrix3 for each minor
    Hmx::Matrix3 minor(a11, a12, a13, a21, a22, a23, a31, a32, a33);
    float det = Det(minor) * m.x.x;

    float a10 = m.y.x, a20 = m.z.x, a30 = m.w.x;
    minor.Set(a10, a12, a13, a20, a22, a23, a30, a32, a33);
    det = -(Det(minor) * m.x.y - det);

    minor.Set(a10, a11, a13, a20, a21, a23, a30, a31, a33);
    det = Det(minor) * m.x.z + det;

    minor.Set(a10, a11, a12, a20, a21, a22, a30, a31, a32);
    det = -(Det(minor) * m.x.w - det);

    return det;
}

// w21-ac (2026-10-03): 70.67 -> 71.2 canonical (fuzzy 67.27 -> 68.0).
// CORRECTNESS, checked term by term: a symbolic evaluator run over the
// target listing and over ours gives all 16 outputs as expression trees;
// expanded as polynomials, all 16 were already equal (no wrong cofactor
// term, no wrong sign).  Rounding was not: the c10 acc chain had terms 4
// and 5 swapped, and 12 of the y/z/w cofactors were associated differently
// on PPC (and 10 natively).  Both are fixed (see the two w21-ac notes below);
// PPC and native association now equal the image's for all 16.  Measured
// negatives: the twelve y/z/w cofactors as locals with all four Sets at the
// end (the image's store placement) 31.2 even with the parenthesised trees
// (MSVC still hoists the operator[] block); a local Matrix4 copied to out at
// the end 62.7 (not scalar-replaced, stores to the stack); writing every
// element through row refs / operator[] without the inline_depth pragma
// inlines all 88 calls (22.5) -- so the image's out-of-line operator[] calls
// are not an inline-budget effect.
//
// AT_LIMIT as of 2026-09-01, 70.67% normalized, with codegen evidence:
// 759 instruction rows, `diff_op: none`, and ALL 245 diff_arg rows are
// accounted for by register renaming (477 swaps over 207 pairs, e.g.
// f0<->f13 x40, f20<->f28 x23) plus stack-offset shift -- 0 unexplained.
// The 41 `replace` rows are spill placement, not different work: they pair
// `stfs f?, 0x??(r1)` against `lfs`/`fmuls` at the same index, i.e. the two
// sides spill the same values at different points in the same expression.
//
// The DECOMPOSITION STRUCTURE already matches the target and is not the
// residual: the target calls Det(Matrix4) first (that function is at 100%),
// tests `fabs` against __real@38d1b717 (= 0.0001f), forms the reciprocal as a
// plain `fdivs` (one division, so no /fp:fast reciprocal folding to undo), and
// materialises row base pointers `addi r29,r31,0x20 / r28,r31,0x10 /
// r30,r31,0x30` matching the row1/row2/row3 references below.
// The residual is register allocation and spill scheduling inside expressions
// that exceed the FPR file. Do not permute operand order here.
//
// MEASURED NEGATIVE (2026-09-13, lane w3-m), read off
// build/373307D9/asm/system/math/mtx.s directly. The target really does hold all
// twelve column-1/2/3 cofactors live across the ~250 `bl ??AVector4@@QBAABMH@Z`
// calls and write ALL SIXTEEN results to `out` only at the very end: eight
// survive in callee-saved FPRs (f22-f30), the four `w` values are spilled to
// 0x5c/0x60/0xb8/0xbc(r1), and the sixteen `stfs ..(r27)` sit at 82539B4C-82539B9C
// after the last call. That accounts for the -0x30 frame delta and the twelve
// TGT_ONLY stack slots. Hoisting the three `out.?.Set(...)` calls out of the
// middle into twelve named `float` locals and Set-ing at the end -- which is
// exactly that shape -- makes MSVC do the OPPOSITE: with no store between them,
// it schedules the whole operator[] call block FIRST (first `bl` at index 53 of
// 647) and the cofactor math after it, because that keeps 4 values live across
// the calls instead of 12. 70.67 -> 32.8. Reordering the four Sets so `out.x`
// comes last (use order y,z,w,x) does not change that: 33.0. The early
// `out.y/z/w.Set` stores are what pins the region order in our build; there is
// no construct found so far that keeps the target's order without them, so the
// twelve mid-function stores are the price of the correct region order.
// Do not retry either variant without a new idea for pinning the schedule.
void Invert(const Hmx::Matrix4 &m, Hmx::Matrix4 &out) {
    float det = Det(m);
    bool small = std::fabs(det) < 0.0001f;
    float invDet;
    if (!small) {
        invDet = 1.0f / det;
    } else {
        invDet = 0.0f;
    }

    float a00 = m.x.x, a01 = m.x.y, a02 = m.x.z, a03 = m.x.w;
    float a10 = m.y.x, a11 = m.y.y, a12 = m.y.z, a13 = m.y.w;
    float a20 = m.z.x, a21 = m.z.y, a22 = m.z.z, a23 = m.z.w;
    float a30 = m.w.x, a31 = m.w.y, a32 = m.w.z, a33 = m.w.w;

    // Cofactors for columns 1,2,3 stored directly to output rows y,z,w.
    // w21-ac: every term, product and sum here is the image's own expression
    // tree (0x825390C0), read off its fmuls/fmadds/fmsubs/fnmsubs chain by a
    // symbolic evaluator and checked as a polynomial against the inverse --
    // all 16 outputs were already mathematically right, but 12 rounded
    // differently from the image: MSVC's /fp:fast re-sorts a flat sum and a
    // flat product, and only an explicitly PARENTHESISED grouping survives.
    // The redundant-looking parentheses are therefore load-bearing: with
    // them the PPC build's association equals the image's for all 16 outputs
    // (it was 8 of 16), and the native build evaluates the same trees (it was
    // 6 of 16) -- a fidelity fix for both.  Commutative operand order and the
    // `x + -y` / `x - y` spellings were then hill-climbed for match with the
    // association held exact: 70.68 -> see the function comment.
    out.y.Set(
        (-(((a22 * a33) * a10) - ((((a23 * a32) * a10) - (((a20 * a32) * a13) - (((a22 * a30) * a13) + -((a23 * a30) * a12)))) + ((a20 * a33) * a12))) * invDet),
        ((((a22 * a33) * a00) + -(((a20 * a33) * a02) - -(((a23 * a32) * a00) - ((((a23 * a30) * a02) - ((a22 * a30) * a03)) + ((a20 * a32) * a03))))) * invDet),
        (-(((a00 * a33) * a12) - ((((a10 * a33) * a02) + -(((a10 * a32) * a03) - (((a03 * a12) * a30) - ((a13 * a02) * a30)))) + ((a00 * a32) * a13))) * invDet),
        ((((a00 * a23) * a12) + -(((a10 * a23) * a02) + (((a13 * a22) * a00) - ((((a13 * a20) * a02) - ((a12 * a20) * a03)) + ((a10 * a22) * a03))))) * invDet)
    );

    out.z.Set(
        ((((a21 * a33) * a10) - (((a20 * a33) * a11) + (((a23 * a31) * a10) - ((((a23 * a30) * a11) - ((a21 * a30) * a13)) - -((a20 * a31) * a13))))) * invDet),
        (-(((a21 * a33) * a00) + -((((a23 * a31) * a00) + -(((a20 * a31) * a03) - (((a21 * a30) * a03) - ((a23 * a30) * a01)))) + ((a20 * a33) * a01))) * invDet),
        ((((a00 * a33) * a11) + -(((a10 * a33) * a01) + (((a13 * a31) * a00) - ((((a13 * a30) * a01) - ((a11 * a30) * a03)) + ((a10 * a31) * a03))))) * invDet),
        (-(((a00 * a23) * a11) - ((((a00 * a13) * a21) + -(((a10 * a21) * a03) - (((a11 * a20) * a03) - ((a13 * a20) * a01)))) + ((a10 * a23) * a01))) * invDet)
    );

    out.w.Set(
        (-(((a21 * a32) * a10) - ((((a22 * a31) * a10) + -(((a20 * a31) * a12) - (((a21 * a30) * a12) - ((a22 * a30) * a11)))) + ((a20 * a32) * a11))) * invDet),
        ((((a21 * a32) * a00) + -(((a20 * a32) * a01) - -(((a22 * a31) * a00) - ((((a22 * a30) * a01) - ((a21 * a30) * a02)) + ((a20 * a31) * a02))))) * invDet),
        (-(((a00 * a32) * a11) - ((((a10 * a32) * a01) - (((a10 * a31) * a02) - (((a11 * a30) * a02) - ((a30 * a01) * a12)))) + ((a00 * a31) * a12))) * invDet),
        ((((a00 * a22) * a11) + -(((a10 * a22) * a01) - -(((a00 * a21) * a12) - ((((a12 * a20) * a01) - ((a11 * a20) * a02)) + ((a10 * a21) * a02))))) * invDet)
    );

    // Cofactors for column 0 via operator[] (cols 1,2,3) -> out.x (transposed)
    const Vector4 &row0 = m.x;
    const Vector4 &row1 = m.y;
    const Vector4 &row2 = m.z;
    const Vector4 &row3 = m.w;
#pragma inline_depth(0)

    // c30: minor removing row 3, col 0 -> rows 0,1,2 cols 1,2,3 (sign: -)
    float acc = a21 * a12 * a03;
    acc = -(row2[1] * row1[3] * row0[2] - acc);
    acc = -(row2[2] * row1[1] * row0[3] - acc);
    acc = row2[3] * row1[1] * row0[2] + acc;
    acc = row2[2] * row1[3] * row0[1] + acc;
    acc = -(row2[3] * row1[2] * row0[1] - acc);
    float c30 = acc * invDet;

    // c20: minor removing row 2, col 0 -> rows 0,1,3 cols 1,2,3 (sign: +)
    acc = row3[1] * row1[3] * row0[2];
    acc = -(row3[1] * row1[2] * row0[3] - acc);
    acc = row3[2] * row1[1] * row0[3] + acc;
    acc = -(row3[2] * row1[3] * row0[1] - acc);
    acc = -(row3[3] * row1[1] * row0[2] - acc);
    acc = row3[3] * row1[2] * row0[1] + acc;
    float c20 = acc * invDet;

    // c10: minor removing row 1, col 0 -> rows 0,2,3 cols 1,2,3 (sign: -)
    // w21-ac: terms 4 and 5 were in the opposite order -- the image
    // (0x825390C0) adds row3[3]*row2[1]*row0[2] before row3[2]*row2[3]*row0[1]
    // (read off its acc chain); fixed, both builds now round like the image.
    acc = row3[1] * row2[2] * row0[3];
    acc = -(row3[1] * row2[3] * row0[2] - acc);
    acc = -(row3[2] * row2[1] * row0[3] - acc);
    acc = row3[3] * row2[1] * row0[2] + acc;
    acc = row3[2] * row2[3] * row0[1] + acc;
    acc = -(row3[3] * row2[2] * row0[1] - acc);
    float c10 = acc * invDet;

    // c00: minor removing row 0, col 0 -> rows 1,2,3 cols 1,2,3 (sign: +)
    acc = row3[1] * row2[3] * row1[2];
    acc = -(row3[1] * row2[2] * row1[3] - acc);
    acc = row3[2] * row2[1] * row1[3] + acc;
    acc = -(row3[2] * row2[3] * row1[1] - acc);
    acc = -(row3[3] * row2[1] * row1[2] - acc);
    acc = row3[3] * row2[2] * row1[1] + acc;
    float c00 = acc * invDet;
#pragma inline_depth()

    out.x.Set(c00, c10, c20, c30);
}

// Transpose(Matrix4) moved to Mtx.h as inline
