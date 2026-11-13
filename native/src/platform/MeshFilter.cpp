#include "platform/MeshFilter.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/BaseMaterial.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

static const char *SkipRule(const char* name) {
    // Migrated out of milo-native-engine's RndMesh::DrawShowing, which used to
    // hardcode these two DC3-specific name tests for every consumer (RB3-Wii and
    // rb3-xenon link the same engine and the LOD one is actively wrong for RB3
    // content — its crowd characters are authored *as* their LOD-2 asset).

    // Kinect depth-sensor visualisation (80x60 sensor grid) — no data on native.
    if (strstr(name, "grid_80by60")) return "kinect-depth-grid";

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

    // Kinect UI whose texture or driver does not exist natively.  Each rule
    // below names the measurement that keeps it (branch np-gesture-menus,
    // PLAN P-3: the whole gesture perform route with DC3_MESHFILTER_LOG=1, and
    // again with DC3_MESHFILTER_ALLOW=all, screenshots compared with the
    // retail capture archive/screenshots/references/dc3_main_menu.jpg).
    //
    // Unhidden by P-3, each root-caused:
    //  - ui/gen/cursor.milo is the player-box UI the hand menus sit beside: per
    //    player side a framed
    //    box (buffer_container_*, buffer_glass_*) showing the player's depth
    //    image, the hand ring over it, silhouette_guy* in an empty slot (the
    //    retail capture shows it in both), the edge warnings warning_top/
    //    bottom/left/right (player too close to a frame edge -- skeleton
    //    driven, never shown on a centred player) and ui_blank.mesh.  They
    //    were filed as "player indicator" / "speech warning" overlays that
    //    render opaque without Kinect; with controller_mode.flow and the
    //    native sensor running, the image's own anims drive them and the
    //    allow-all route showed no stray overlay from any of them.
    //  - shield_handDown.mesh (ui/letterbox): the letterbox hand-help icon,
    //    animated in and out by LetterboxPanel::SyncToPanel
    //    (handDown_icon_show.anim, show_letterbox_hand_help); at rest it
    //    is not visible -- the allow-all route drew nothing new there.
    //  - the generic "_crown.mesh" substring: the only crowns it ever caught
    //    were buffer_*_crown, which the depth-image rule below covers.

    // The player's depth image in the cursor box (buffer_left/right and their
    // leader-crown variants, mats depth_buffer_*): a render target the Kinect
    // depth stream fills.  LiveCameraInput is null natively, so the quad draws
    // as a flat magenta/blue square -- what the original draws under Xenia
    // with no Kinect (archive/screenshots/xenia-gameplay/04_main_menu.png).
    // P-4 (player on screen) owns filling it.  With it hidden the box shows
    // its container/glass backdrop, a flat purple (P1) or blue square -- the
    // same thing the original draws under Xenia, with no depth stream at all.
    if (!strncmp(name, "buffer_left", 11) ||
        !strncmp(name, "buffer_right", 12)) {
        return "depth-image";
    }
    // Voice control (mic_*, geo_mic*, the letterbox mic shield's
    // grey_alpha.mesh): there is no speech recognizer natively; allowed, the
    // mic tab draws as a stray icon in the bottom-right corner of every
    // screen, gameplay included.  Not gesture UI.
    if (!strncmp(name, "mic_", 4) ||
        !strncmp(name, "geo_mic", 7) ||
        !strcmp(name, "grey_alpha.mesh")) {
        return "microphone";
    }
    // Photo-moment flash and camera preview: render targets fed by the camera
    // (projection.tex, preview) -- never filled natively.  P-4.
    if (!strncmp(name, "pose_flash", 10)) {
        return "pose_flash";
    }
    if (!strcmp(name, "preview.mesh")) {
        return "preview";
    }
    // Name substrings from the milo-viewer era.  Matched NOTHING on the
    // measured routes (the whole perform route, and a fresh console up to
    // tutorial_perform_it_screen_0), so there is no evidence either way; kept
    // until a tutorial / party survey logs what it catches.
    if (strstr(name, "tutorial") || strstr(name, "gesture") ||
        strstr(name, "spotlight") || strstr(name, "nav_tut")) {
        return "tutorial/gesture/spotlight";
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
    return nullptr;
}

// DC3_MESHFILTER_LOG=1: print each distinct refused mesh once, with the rule
// that refused it and its material -- the evidence for (or against) a rule.
// DC3_MESHFILTER_ALLOW=<rule>[,<rule>...] | all: draw what the named rules
// would refuse -- to look at a mesh before deciding its rule is wrong.
static bool RuleAllowed(const char *rule) {
    static const char *sAllow = getenv("DC3_MESHFILTER_ALLOW");
    if (!sAllow || !*sAllow)
        return false;
    if (!strcmp(sAllow, "all"))
        return true;
    size_t n = strlen(rule);
    for (const char *p = sAllow; (p = strstr(p, rule)) != nullptr; p += n) {
        bool startOk = p == sAllow || p[-1] == ',';
        bool endOk = p[n] == '\0' || p[n] == ',';
        if (startOk && endOk)
            return true;
    }
    return false;
}

bool ShouldSkipMesh(const char* name, RndMat* mat) {
    const char *rule = SkipRule(name);
    if (!rule || RuleAllowed(rule))
        return false;
    static const bool sLog = getenv("DC3_MESHFILTER_LOG") != nullptr;
    if (sLog) {
        static std::set<std::string> sSeen;
        if (sSeen.insert(name).second)
            printf("DC3 MeshFilter: skip '%s' (rule %s, mat '%s')\n", name, rule,
                   mat ? mat->Name() : "");
    }
    return true;
}
