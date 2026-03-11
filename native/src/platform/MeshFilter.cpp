#include "platform/MeshFilter.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/BaseMaterial.h"
#include <cstring>

bool ShouldSkipMesh(const char* name, RndMat* mat) {
    // Skip Kinect-specific UI elements that render incorrectly without
    // DTA PropAnim driving their material properties. On Xbox, controller_mode.flow
    // and DTA scripts animate these to correct alpha/visibility.

    // Player indicator elements (Kinect skeleton tracking display)
    if (!strcmp(name, "ui_blank.mesh") ||
        !strncmp(name, "silhouette_guy", 14) ||
        !strncmp(name, "buffer_container", 16) ||
        !strncmp(name, "buffer_left", 11) ||
        !strncmp(name, "buffer_right", 12) ||
        strstr(name, "buffer_glass") ||
        strstr(name, "_crown.mesh")) {
        return true;
    }
    // Microphone/voice control UI
    if (!strncmp(name, "mic_", 4) ||
        !strncmp(name, "geo_mic", 7) ||
        !strncmp(name, "geo_mictab", 10)) {
        return true;
    }
    // Hand gesture icons
    if (!strncmp(name, "shield_hand", 11)) {
        return true;
    }
    // Tutorial/gesture overlay content
    if (strstr(name, "tutorial") || strstr(name, "gesture") ||
        strstr(name, "spotlight") || strstr(name, "nav_tut")) {
        return true;
    }
    // Voice-tip / speech warning overlays (Kinect speech UI).
    // On Xbox, controller_mode.flow hides these in controller mode.
    // On native, speech is unavailable and these full-screen overlays
    // paint over the already-rendered menu text and ribbon content.
    if (!strcmp(name, "grey_alpha.mesh") ||
        !strncmp(name, "warning_", 8)) {
        return true;
    }

    // Skip PropAnim-driven shading overlays that haven't been animated.
    // These use a small solid-white texture (e.g. white.tex 8x8) with srcAlpha blend.
    // On Xbox, PropAnim sets their material color/alpha at runtime to create
    // tinted ribbon/gradient overlays. Without flow animations running, they
    // default to opaque white rectangles that obscure the UI.
    if (mat) {
        RndTex* diffTex = mat->GetDiffuseTex();
        if (diffTex && diffTex->Width() <= 8 && diffTex->Height() <= 8 &&
            mat->GetBlend() == BaseMaterial::kBlendSrcAlpha &&
            mat->Alpha() > 0.99f) {
            return true;
        }
    }

    return false;
}
