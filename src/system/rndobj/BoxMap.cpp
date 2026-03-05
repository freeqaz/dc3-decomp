#include "rndobj/BoxMap.h"
#include "os/Timer.h"
#include "rndobj/Lit.h"

// Accumulation buffers for light calculations
static int gLightIndex = 0;
static Vector3 gLightBuffer1[50];
static Vector3 gLightBuffer2[50];

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
    ApplyLight(mQueued_Directional);
    if (v3) {
        ApplyLight(mQueued_Spot, *v3);
        ApplyLight(mQueued_Point, *v3);
    }
    gLightIndex = 0;
    // Accumulate light contributions into output colors
    for (int i = 0; i < 6; i++) {
        color[i].red = gLightBuffer1[i].x;
        color[i].green = gLightBuffer1[i].y;
        color[i].blue = gLightBuffer1[i].z;
        color[i].alpha = 1.0f;
    }
}

bool BoxMapLighting::CacheData(LightParams_Spot &spot) {
    if (spot.mBeamLength > 0 && spot.mTopRadius <= spot.mBottomRadius
        && (spot.mColor.red > 0.003921569f || spot.mColor.green > 0.003921569f
            || spot.mColor.blue > 0.003921569f)) {
        float f3 = (spot.mTopRadius * spot.mBeamLength) / (spot.mBottomRadius - spot.mTopRadius);
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
    } else {
        mQueued_Spot.RemoveEntry();
        return false;
    }
}
