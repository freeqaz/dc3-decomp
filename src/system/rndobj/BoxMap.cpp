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

        float *lightBuf1 = (float *)gLightBuffer1 - 2;
        float *lightBuf2 = (float *)gLightBuffer2 - 2;
        for (unsigned int counter = gLightIndex; counter != 0; counter--) {
            float x1 = lightBuf1[2];
            float y1 = lightBuf1[3];
            lightBuf1 += 4;
            float z1 = *lightBuf1;

            float x2 = lightBuf2[2];
            float y2 = lightBuf2[3];
            lightBuf2 += 4;
            float z2 = *lightBuf2;

            // Six box-map axes: +Z, +X, +Y, -X, -Y, -Z. Each face accumulates the
            // light colour weighted by the squared clamped projection of the light
            // direction onto that face's axis.
            float posZ = Max(0.0f, z1);
            float posX = Max(0.0f, x1);
            float posY = Max(0.0f, y1);
            float negX = Max(0.0f, -x1);
            float negY = Max(0.0f, -y1);
            float negZ = Max(0.0f, -z1);

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
        float dist = invDist * distSq * light.mHalfLengthRecip - light.mOffsetFactor;
        float ndz = dz * invDist;
        float ndx = dx * invDist;
        float ndy = dy * invDist;
        float cone = light.mDirection.y * ndy
            + (light.mDirection.x * ndx + light.mDirection.z * ndz);
        dist = Min(1.0f, dist);
        float coneClamped = Min(1.0f, cone) - light.mConeAngleFactor;
        float distAtten = Max(0.0f, 1.0f - dist);
        float coneAtten = Max(0.0f, coneClamped);
        float atten = distAtten * (coneAtten * light.mConeAngleInverse);
        Hmx::Color &col = gLightBuffer2[gLightIndex];
        Hmx::Color &dir = gLightBuffer1[gLightIndex];
        col.red = atten * light.mColor.red;
        col.green = atten * light.mColor.green;
        dir.red = -ndx;
        dir.green = -ndy;
        dir.blue = -ndz;
        col.blue = atten * light.mColor.blue;
        gLightIndex++;
    }
}
