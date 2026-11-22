// A portrait frame during a song: see NativePortrait.h.
#include "platform/NativePortrait.h"

#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "world/Dir.h"

#include <cstdio>
#include <cstdlib>

extern WgpuRnd *gWgpuRnd;

namespace {
constexpr float kLandscapeAspect = 16.0f / 9.0f;

bool FrameSize(int &w, int &h) {
    if (!gWgpuRnd) return false;
    w = gWgpuRnd->Gpu().WindowWidth();
    h = gWgpuRnd->Gpu().WindowHeight();
    return w > 0 && h > 0;
}

bool IsWorldCam(const RndCam *cam) {
    return cam && TheWorld && TheWorld->Cam() == cam;
}
}

float NativeFrameAspect() {
    int w = 0, h = 0;
    if (!FrameSize(w, h)) return kLandscapeAspect;
    return (float)w / (float)h;
}

bool NativePortraitFrame() {
    int w = 0, h = 0;
    return FrameSize(w, h) && h > w;
}

bool NativePortraitWorldRatio(const RndCam *cam, float &ratio) {
    if (!NativePortraitFrame() || !IsWorldCam(cam)) return false;
    ratio = 1.0f / NativeFrameAspect();
    return true;
}

float NativePortraitHudScale() {
    static float sScale = -1.0f;
    if (sScale < 0.0f) {
        sScale = 1.8f;
        if (const char *v = getenv("MILO_PORTRAIT_HUD_SCALE")) {
            const float f = (float)atof(v);
            if (f >= 1.0f && f <= 3.0f) sScale = f;
        }
        fprintf(stderr, "[portrait] HUD drawn %.2f frame-widths wide in a portrait frame\n", sScale);
    }
    return sScale;
}

void NativePortraitHudRemapFor(float frameAspect, float hudScale, float centreY, float &kx,
                               float &tx, float &ky, float &ty) {
    // x: the 16:9 picture is hudScale frame-widths wide, right edges together.
    kx = hudScale;
    tx = 1.0f - hudScale;
    // y: its height, as a fraction of the frame's height, is its width
    // (hudScale * W) times 9/16, over H = W / frameAspect.
    ky = hudScale * (9.0f / 16.0f) * frameAspect;
    ty = 1.0f - 2.0f * centreY;  // NDC y points up
}

bool NativePortraitHudRemap(const RndCam *cam, float &kx, float &tx, float &ky, float &ty) {
    if (!cam || !NativePortraitFrame() || IsWorldCam(cam) || cam->TargetTex()) return false;
    NativePortraitHudRemapFor(NativeFrameAspect(), NativePortraitHudScale(), 0.5f, kx, tx, ky, ty);
    return true;
}
