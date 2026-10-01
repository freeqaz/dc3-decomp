#include "platform/MeshFilter.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/BaseMaterial.h"
#include <cstring>

bool ShouldSkipMesh(const char* name, RndMat* mat) {
    // Migrated out of milo-native-engine's RndMesh::DrawShowing, which used to
    // hardcode these two DC3-specific name tests for every consumer (RB3-Wii and
    // rb3-xenon link the same engine and the LOD one is actively wrong for RB3
    // content — its crowd characters are authored *as* their LOD-2 asset).

    // Kinect depth-sensor visualisation (80x60 sensor grid) — no data on native.
    if (strstr(name, "grid_80by60")) return true;

    // No `_lod` name test. dc3-native used to drop every mesh whose name holds
    // "_lod" (a leftover of the milo-viewer era, when meshes were drawn by
    // walking the ObjectDir rather than the draw lists). The image never looks
    // at the name: DxMesh::DrawShowing (826229B0) refuses only !CanDraw(), and
    // Character decides which LOD group to draw (Character::DrawShowing picks
    // the LOD, DrawLodOrShadow draws that mLods group). Measured over the full
    // perform route with the name test lifted and every draw checked against
    // the drawing Character's mLods (branch native-meshdraw, 2026-09-30): the
    // only `_lod` meshes that reach DrawShowing are the 14 in the four dancers'
    // mShadow lists, drawn by DrawLodOrShadow's shadow branch (drawMode 4, the
    // extrude/occlusion passes RndShadowMap::PrepShadow and the spotlight
    // shadows render into their own targets) -- exactly what the image draws
    // there. No mesh of a non-chosen LOD group was drawn by any other route.
    // The name was also wrong on its own terms: emilia01's
    // emilia_head_lod1.1.mesh is in LOD group 0, the full-detail group.

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
    // Light-catcher overlay meshes (e.g., Rink_lightCatcher.mat) now render correctly:
    // MaterialSetup forces multiply-blend materials to prelit mode, so their base
    // color passes through as the multiply factor. White = identity = invisible.
    //
    // Venue TV/arcade screens: previously skipped because Screen.tex wasn't
    // uploading and screen materials without a diffuse texture rendered as white.
    // Fixed in MaterialSetup.cpp: failed texture uploads fall back to opaque black,
    // and screen materials without a diffuse texture (IsScreenMaterial) also render
    // black ("TV is off") instead of the material's white base color.
    return false;
}
