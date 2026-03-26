#include "platform/MeshFilter.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/BaseMaterial.h"
#include <cstring>

bool ShouldSkipMesh(const char* name, RndMat* mat) {
    // Skip Kinect-specific UI elements that render incorrectly without
    // the Xbox gesture/speech systems. On Xbox, controller_mode.flow and
    // DTA scripts animate these to correct alpha/visibility. On native,
    // these systems don't run and the elements render as opaque overlays.

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
    // Player silhouette projections (Kinect depth buffer → render target texture)
    // Without skeleton tracking, projection.tex/projectionp2.tex stay white
    if (!strncmp(name, "pose_flash", 10)) {
        return true;
    }
    // Kinect camera preview (render-target texture never filled on native)
    if (!strcmp(name, "preview.mesh")) {
        return true;
    }
    // Tutorial/gesture overlay content
    if (strstr(name, "tutorial") || strstr(name, "gesture") ||
        strstr(name, "spotlight") || strstr(name, "nav_tut")) {
        return true;
    }
    // Voice-tip / speech warning overlays (Kinect speech UI)
    if (!strcmp(name, "grey_alpha.mesh") ||
        !strncmp(name, "warning_", 8)) {
        return true;
    }
    // Light-catcher overlay meshes: multiply-blend surfaces (no diffuse texture,
    // white material color) that capture scene lighting and project it onto the
    // venue floor/stage as a multiply overlay. On Xbox, DTA scripts and the
    // full lighting pipeline produce realistic light projections. On native,
    // without the complete lighting setup, these render as bright white blocks
    // (lighting exceeds 1.0, multiply blend amplifies instead of modulating).
    if (mat && strstr(mat->Name(), "lightCatch")) return true;
    // Venue TV screen surfaces (food court monitors, upper floor walls).
    // Screen.tex is a placeholder white texture; on Xbox, video_recorder.srec
    // or TexRenderer fills these with Kinect camera/video content. Without
    // the video feed, they render as solid bright white rectangles.
    if (mat && !strcmp(mat->Name(), "Screen.mat")) return true;
    return false;
}
