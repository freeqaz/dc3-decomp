#include "rndobj\BoxMap.h"
#include "math\Utl.h"
#include "os\Timer.h"
#include "rndobj\Lit.h"

// The original uses the raw PowerPC reciprocal-square-root estimate (frsqrte,
// ~5 bits of mantissa) with no Newton refinement -- see CharHair.cpp for the
// refined variant. Native has no such instruction, so use the exact form there.
static inline float RecipSqrtEst(float x) {
#ifdef HX_NATIVE
    return 1.0f / sqrtf(x);
#else
    return __frsqrte(x);
#endif
}

static unsigned int gLightIndex = 0;
static Hmx::Color gLightBuffer1[150];
static Hmx::Color gLightBuffer2[150];

BoxMapLighting::BoxMapLighting() { Clear(); }

void BoxMapLighting::Clear() {
    mQueued_Directional.Clear();
    mQueued_Point.Clear();
    mQueued_Spot.Clear();
}

bool BoxMapLighting::QueueLight(RndLight *light, float colorScale) {
    if (light->Showing()) {
        Hmx::Color lightColor(light->GetColor());
        lightColor.red *= colorScale;
        lightColor.green *= colorScale;
        lightColor.blue *= colorScale;
        switch (light->GetType()) {
        case RndLight::kDirectional:
        case RndLight::kFakeSpot:
            LightParams_Directional *paramsDirectional;
            if (ParamsAt(paramsDirectional)) {
                paramsDirectional->mColor = lightColor;
                Negate(light->WorldXfm().m.y, paramsDirectional->mDirection);
                return true;
            }
            break;
        case RndLight::kPoint:
            LightParams_Point *paramsPoint;
            if (ParamsAt(paramsPoint)) {
                paramsPoint->mPosition = light->WorldXfm().v;
                paramsPoint->mColor = lightColor;
                paramsPoint->mRange = light->Range();
                paramsPoint->mFalloffStart = light->FalloffStart();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

/** 97.5% canonical (w7-ay, from 62.8%), 648 B.  The w7-aj survey that used
 *  to sit here concluded "we are CHEAPER than the image, so no rewriting of
 *  these statements can reach it" -- that was WRONG.  Two spellings close
 *  the 35-point gap, neither of which the survey tried:
 *
 *   (1) The loop is a count-UP `for (i = 0; i < gLightIndex; i++)`, not the
 *       countdown `for (c = gLightIndex; c != 0; c--)`.  The count-up form
 *       keeps its own `cmplwi cr6, r11, 0x0` / `beq` guard (0x826F11A4 /
 *       0x826F11F4) even though the enclosing `if (gLightIndex != 0)` already
 *       tested it; the countdown form got value-numbered into the outer test,
 *       which moved the eighteen colour loads and the walker setup below
 *       the guard and cost every register assignment downstream.
 *   (2) The light direction is read THROUGH `const Hmx::Color &dir = *light1`
 *       at each use (`Max(0.0f, dir.blue)` and `Max(0.0f, -dir.blue)`), not
 *       into named locals x1/y1/z1.  Repeating the member read is what makes
 *       MSVC emit the three dead home-slot stores `stfs f12/f26/f25, 0x54(r31)`
 *       at 0x826F124C-54, the un-CSEd fneg pairs, and the 18th callee-saved
 *       FPR (__savefpr_14, frame 0x140).  Named locals are cheaper and can
 *       never reach that shape.
 *   The walkers are `const Hmx::Color *` pointers (`light1++`), which give the
 *   `subi r11, r11, 0x8` bias + `lfs 0x8/0xc` / `lfsu 0x10` walk; indexing
 *   gLightBuffer1[i] gives lfsx (81.0%).
 *
 *  Residual 2.5% (66 diff_arg + 2 insert/2 delete, all FPR/slot allocation):
 *  the image loads color[5].green/blue first and spills them to 0x50/0x54(r31)
 *  where we spill the swapped pair; our light1 bias is -0xc with the red read
 *  at 0x14(r11) vs the image's -0x8 / 0x8(r11); and the stfd f27/f29 spill
 *  order.  Tried and neutral or worse: hoisting `-dir.red` etc. into locals
 *  before the Max() calls (97.5, no change); declaring/reading c20g/c20b
 *  before c0r (97.4, more offset diffs). */
void BoxMapLighting::ApplyQueuedLights(Hmx::Color * __restrict color, const Vector3 *v3) const {
    START_AUTO_TIMER("draw_light_approx");
    gLightIndex = 0;
    if (v3) {
        ApplyLight(mQueued_Spot, *v3);
        ApplyLight(mQueued_Point, *v3);
    }
    ApplyLight(mQueued_Directional);

    if (gLightIndex != 0) {
        float c0r = color[0].red;
        float c0g = color[0].green;
        float c0b = color[0].blue;
        float c4r = color[1].red;
        float c4g = color[1].green;
        float c4b = color[1].blue;
        float c8r = color[2].red;
        float c8g = color[2].green;
        float c8b = color[2].blue;
        float c12r = color[3].red;
        float c12g = color[3].green;
        float c12b = color[3].blue;
        float c16r = color[4].red;
        float c16g = color[4].green;
        float c16b = color[4].blue;
        float c20r = color[5].red;
        float c20g = color[5].green;
        float c20b = color[5].blue;

        const Hmx::Color *light1 = gLightBuffer1;
        const Hmx::Color *light2 = gLightBuffer2;
        for (unsigned int i = 0; i < gLightIndex; i++, light1++, light2++) {
            const Hmx::Color &dir = *light1;
            float x2 = light2->red;
            float y2 = light2->green;
            float z2 = light2->blue;

            // Six box-map axes: +Z, +X, +Y, -X, -Y, -Z. Each face accumulates the
            // light colour weighted by the squared clamped projection of the light
            // direction onto that face's axis.
            float posZ = Max(0.0f, dir.blue);
            float posX = Max(0.0f, dir.red);
            float posY = Max(0.0f, dir.green);
            float negX = Max(0.0f, -dir.red);
            float negY = Max(0.0f, -dir.green);
            float negZ = Max(0.0f, -dir.blue);

            float wPosZ = posZ * posZ;
            float wPosX = posX * posX;
            float wPosY = posY * posY;
            float wNegX = negX * negX;
            float wNegY = negY * negY;
            float wNegZ = negZ * negZ;

            c16b += wPosZ * z2;
            c0b += wPosX * z2;
            c8b += wPosY * z2;
            c0r += wPosX * x2;
            c0g += wPosX * y2;
            c8r += wPosY * x2;
            c8g += wPosY * y2;
            c4b += wNegX * z2;
            c12b += wNegY * z2;
            c20g = wNegZ * y2 + c20g;
            c20b = wNegZ * z2 + c20b;
            c20r = wNegZ * x2 + c20r;
            c16r += wPosZ * x2;
            c4r += wNegX * x2;
            c4g += wNegX * y2;
            c12r += wNegY * x2;
            c12g += wNegY * y2;
            c16g = wPosZ * y2 + c16g;
        }

        color[0].red = c0r;
        color[0].green = c0g;
        color[0].blue = c0b;
        color[1].red = c4r;
        color[1].green = c4g;
        color[1].blue = c4b;
        color[2].red = c8r;
        color[2].green = c8g;
        color[2].blue = c8b;
        color[3].red = c12r;
        color[3].green = c12g;
        color[3].blue = c12b;
        color[4].red = c16r;
        color[4].green = c16g;
        color[4].blue = c16b;
        color[5].red = c20r;
        color[5].green = c20g;
        color[5].blue = c20b;
    }
}

bool BoxMapLighting::CacheData(LightParams_Spot &spot) {
    if (spot.mBeamLength > 0) {
        if (spot.mBottomRadius >= spot.mTopRadius
            && (spot.mColor.red > 0.003921569f || spot.mColor.green > 0.003921569f
                || spot.mColor.blue > 0.003921569f)) {
            float f3 = (spot.mTopRadius * spot.mBeamLength)
                / (spot.mBottomRadius - spot.mTopRadius);
            Vector3 v58;
            Scale(spot.mDirection, f3, v58);
            Vector3 v4c;
            Subtract(spot.mPosition, v58, v4c);
            float f1 = spot.mBottomRadius / (spot.mBeamLength + f3);
            f1 *= f1;
            float f2 = 1.0f / (spot.mBeamLength * 2.0f);
            f1 = (1.0f - f1) / (f1 + 1.0f);
            spot.mApex = v4c;
            spot.mConeAngleFactor = f1;
            spot.mConeAngleInverse = 1.0f / (1.0f - f1);
            spot.mHalfLengthRecip = f2;
            spot.mOffsetFactor = f3 * f2;
            return true;
        }
    }
    mQueued_Spot.RemoveEntry();
    return false;
}

void BoxMapLighting::ApplyLight(
    const BoxLightArray<LightParams_Directional, 50> &arr
) const {
    for (unsigned int i = 0; i < arr.NumElements(); i++) {
        const Hmx::Color *src = (const Hmx::Color *)&arr[i];
        gLightBuffer1[gLightIndex] = src[0];
        gLightBuffer2[gLightIndex] = src[1];
        gLightIndex++;
    }
}

void BoxMapLighting::ApplyLight(
    const BoxLightArray<LightParams_Point, 50> &arr, const Vector3 &viewPos
) const {
    for (unsigned int i = 0; i < arr.NumElements(); i++) {
        const LightParams_Point &light = arr[i];
        if (light.mRange > light.mFalloffStart) {
            float dz = light.mPosition.z - viewPos.z;
            float dy = light.mPosition.y - viewPos.y;
            float dx = light.mPosition.x - viewPos.x;
            Hmx::Color &dir = gLightBuffer1[gLightIndex];
            dir.red = dx;
            dir.green = dy;
            dir.blue = dz;
            float distSq = dy * dy + dx * dx + dz * dz;
            if (0.0f < distSq) {
                float invDist = RecipSqrtEst(distSq);
                float dist = Max(0.0f, invDist * distSq - light.mFalloffStart);
                float atten = Max(0.0f, 1.0f - dist / (light.mRange - light.mFalloffStart));
                Hmx::Color &col = gLightBuffer2[gLightIndex];
                col.red = light.mColor.red * atten;
                col.green = light.mColor.green * atten;
                col.blue = light.mColor.blue * atten;
                dir.red = dx * invDist;
                dir.green = dy * invDist;
                dir.blue = dz * invDist;
                gLightIndex++;
            }
        }
    }
}

/** RESIDUAL w7-at + w7-bl, 99.21 canonical / 97.1 raw, 360 B (was 87.37).
 *  21 mismatch rows remain and NONE of them is an insert, a delete or an
 *  offset: 18 are register permutation (forgiven by the canonical ruler), two
 *  are one adjacent `addi`/`subi` scheduling swap, and one is the col.red
 *  store.  Four source-level fixes from w7-bl, each read off the target listing
 *  at 0x826F0E78-0x826F0F7C rather than off a diff summary:
 *
 *    (1) `Min(cone, 1.0f)`, NOT `Min(1.0f, cone)`.  Min(a,b) lowers to
 *        `fsubs a-b` + `fsel(a-b, b, a)`, so the operand order is legible in
 *        the listing: the image emits `fsubs f10, f12, f0` (cone - 1.0) then
 *        `fsel f12, f10, f0, f12`.  The `dist` clamp one line above already had
 *        the image's order and was never wrong.
 *    (2) `float blue = atten * light.mColor.blue;` hoisted ABOVE the three
 *        gLightBuffer1 stores, with only `col.blue = blue;` left below them.
 *        The image loads mColor.blue (`lfsu f12, 0x5c(r11)`) and multiplies
 *        BEFORE storing the direction triple.  MSVC may not sink a load of
 *        `arr` below a store to gLightBuffer1 on its own -- it cannot prove
 *        they do not alias -- so the load position has to come from the source.
 *        87.37 -> 91.9.
 *    (3) `coneX` names the mDirection.x product and mDirection.z * ndz is left
 *        INLINE, the reverse of the old `coneZ` spelling.  MSVC makes the
 *        inline product the standalone `fmuls` and folds the NAMED one into the
 *        `fmadds`; the image's standalone product is mDirection.z * ndz
 *        (`fmuls f9, f9, f12`, f9 from 0x4c(r11)).  This closed the (0x44,0x4c)
 *        offset swap and brought back the second callee-saved FPR that the old
 *        note called unreachable.  91.9 -> 94.09.
 *    (4) The two light buffers are spelled DIFFERENTLY on purpose: `col` is a
 *        reference into gLightBuffer2, gLightBuffer1 is written through a
 *        repeated subscript.  That asymmetry is in the image: it stores the
 *        direction triple with three `stfsx` off three bases (r9, r9+4, r9+8)
 *        and the colour through a per-iteration pointer
 *        `r8 = r10 + &gLightBuffer2[0] + 8`, at -0x4(r8) and 0x0(r8).  Making
 *        BOTH of them references gives six precomputed bases and six `stfsx`,
 *        which costs a fourth callee-saved GPR (__savegprlr_28 vs the image's
 *        _29) and drops the two FPR save slots 8 bytes.  94.09 -> 99.21.  The
 *        MIRROR of this (subscript the colour, reference the direction) puts
 *        the pointer on the wrong buffer and reaches only 94.7 -- the asymmetry
 *        has to point the same way the image's does.
 *
 *  What is left: rows 14/16 are one adjacent scheduling swap (the image emits
 *  gLightBuffer2's `addi` low half before `subi r11, r4, 0x44`, we emit them
 *  the other way round), row 72 stores col.red as `stfs f12, -0x8(r9)` through
 *  the same pointer where the image uses `stfsx f12, r10, r3`, and the rest is
 *  r8<->r9 and fmuls operand permutation.
 *
 *  Measured, all reverted:
 *    - caching gLightIndex in a local across the loop and storing it back once:
 *      87.37 -> 78.6.  The image ALREADY hoists the global load itself
 *      (`lwz r7, lbl_830E0278@l(r31)` once, `stw r7` once after the loop) and
 *      our unmodified source already reproduces that; a source-level local only
 *      moves the load ABOVE the loop guard, where the image does not have it.
 *    - the same with an explicit `if (arr.NumElements() > 0)` guard around it:
 *      87.37 -> 76.3, and the guard is emitted twice.
 *    - `cone` as a flat left-associated sum
 *      (`dir.z*ndz + dir.x*ndx + dir.y*ndy`): 87.37 -> 85.2 (w7-at).  w7-bl
 *      re-tested the same idea as a statement accumulation
 *      (`cone = .x*ndx + coneZ; cone = .y*ndy + cone;`) on top of fixes (1)
 *      and (2): EXACTLY inert at 91.9, same raw, same offset swap.  MSVC
 *      canonicalises the chain; only which product is NAMED decides the
 *      lowering, which is what (3) exploits.
 *    - hoisting `-ndx/-ndy/-ndz` into named temps ahead of the clamp chain to
 *      reproduce the image's early fnegs: byte-identical, MSVC already
 *      schedules them there.
 *    - declaring `dir` before `col` while both were still references: 94.09
 *      canonical but 91.7 raw, one extra row -- worse.
 *    - writing col.red through the subscript and green/blue through the
 *      reference, to reach the image's `stfsx` for red: exactly inert at 99.21;
 *      MSVC CSEs the address back into the single pointer.
 *    - hoisting the `col` reference to the top of the loop body to flip the
 *      rows-14/16 scheduling swap: exactly inert at 99.21. */
void BoxMapLighting::ApplyLight(
    const BoxLightArray<LightParams_Spot, 50> &arr, const Vector3 &viewPos
) const {
    for (unsigned int i = 0; i < arr.NumElements(); i++) {
        const LightParams_Spot &light = arr[i];
        float dy = viewPos.y - light.mApex.y;
        float dz = viewPos.z - light.mApex.z;
        float dx = viewPos.x - light.mApex.x;
        float distSq = dz * dz + dx * dx + dy * dy;
        float invDist = RecipSqrtEst(distSq);
        float ndz = dz * invDist;
        float ndx = dx * invDist;
        float ndy = dy * invDist;
        // The image computes (invDist * distSq) as its own product and only then
        // scales by mHalfLengthRecip -- `fmuls f3, f31, f3` then
        // `fmsubs f6, f3, f5, f6` at the target's rows 49/53.  Written as the
        // single expression `invDist * distSq * light.mHalfLengthRecip`, /fp:fast
        // reassociates it to (mHalfLengthRecip * invDist) * distSq and the whole
        // FPR assignment downstream shifts; the named temp pins the association.
        float trueDist = invDist * distSq;
        float dist = trueDist * light.mHalfLengthRecip - light.mOffsetFactor;
        float coneX = light.mDirection.x * ndx;
        float cone = light.mDirection.y * ndy + (light.mDirection.z * ndz + coneX);
        dist = Min(1.0f, dist);
        float coneClamped = Min(cone, 1.0f) - light.mConeAngleFactor;
        float distAtten = Max(0.0f, 1.0f - dist);
        float coneAtten = Max(0.0f, coneClamped);
        float atten = distAtten * (coneAtten * light.mConeAngleInverse);
        Hmx::Color &col = gLightBuffer2[gLightIndex];
        col.red = atten * light.mColor.red;
        col.green = atten * light.mColor.green;
        float blue = atten * light.mColor.blue;
        gLightBuffer1[gLightIndex].red = -ndx;
        gLightBuffer1[gLightIndex].green = -ndy;
        gLightBuffer1[gLightIndex].blue = -ndz;
        col.blue = blue;
        gLightIndex++;
    }
}
