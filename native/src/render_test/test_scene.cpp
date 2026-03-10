// DC3 Native Port — Render Test Scene Builder
// Constructs programmatic test scenes for renderer validation.

#include "render_test/test_scene.h"

#include "obj/Object.h"
#include "obj/Dir.h"
#include "rndobj/Dir.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Text.h"
#include "rndobj/Font.h"
#include "utl/Loader.h"
#include "math/Vec.h"
#include "math/Mtx.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <unistd.h>

// ============================================================================
// Helpers
// ============================================================================

RndMat* MakeSolidMat(RndDir* dir, const char* name,
                     float r, float g, float b, float a) {
    RndMat* mat = Hmx::Object::New<RndMat>();
    mat->SetName(name, dir);
    mat->SetColor(r, g, b);
    mat->SetAlpha(a);
    mat->SetPreLit(true);
    mat->SetZMode(kZModeNormal);
    mat->SetBlend(BaseMaterial::kBlendSrc);
    mat->SetCull(kCullNone);
    return mat;
}

// Make a quad in the XZ plane (Milo: X=right, Z=up, Y=depth).
// x,z are bottom-left corner; w is width along X, h is height along Z.
// yPos controls depth (camera looks from -Y toward +Y).
// Normal faces -Y (toward the camera).
RndMesh* MakeQuad(RndDir* dir, const char* name, RndMat* mat,
                  float x, float z, float w, float h, float yPos) {
    RndMesh* mesh = Hmx::Object::New<RndMesh>();
    mesh->SetName(name, dir);
    mesh->SetMat(mat);

    mesh->Verts().resize(4);
    mesh->Faces().resize(2);

    // Quad corners in XZ plane, normal = -Y:
    //   v0 (top-left)     v1 (top-right)
    //   v3 (bottom-left)  v2 (bottom-right)
    RndMesh::Vert& v0 = mesh->Verts(0);
    v0.pos.Set(x, yPos, z + h);
    v0.norm.Set(0, -1, 0);
    v0.color.Set(1, 1, 1, 1);
    v0.tex.Set(0, 0);

    RndMesh::Vert& v1 = mesh->Verts(1);
    v1.pos.Set(x + w, yPos, z + h);
    v1.norm.Set(0, -1, 0);
    v1.color.Set(1, 1, 1, 1);
    v1.tex.Set(1, 0);

    RndMesh::Vert& v2 = mesh->Verts(2);
    v2.pos.Set(x + w, yPos, z);
    v2.norm.Set(0, -1, 0);
    v2.color.Set(1, 1, 1, 1);
    v2.tex.Set(1, 1);

    RndMesh::Vert& v3 = mesh->Verts(3);
    v3.pos.Set(x, yPos, z);
    v3.norm.Set(0, -1, 0);
    v3.color.Set(1, 1, 1, 1);
    v3.tex.Set(0, 1);

    // Two triangles: (0,1,2) and (0,2,3)
    mesh->Faces(0).Set(0, 1, 2);
    mesh->Faces(1).Set(0, 2, 3);

    mesh->SetShowing(true);

    return mesh;
}

void SetVertexColors(RndMesh* mesh, float r, float g, float b, float a) {
    int n = mesh->NumVerts();
    for (int i = 0; i < n; i++) {
        mesh->Verts(i).color.Set(r, g, b, a);
    }
}

void SetCornerColors(RndMesh* mesh,
                     float r0, float g0, float b0,
                     float r1, float g1, float b1,
                     float r2, float g2, float b2,
                     float r3, float g3, float b3) {
    if (mesh->NumVerts() < 4) return;
    mesh->Verts(0).color.Set(r0, g0, b0, 1);
    mesh->Verts(1).color.Set(r1, g1, b1, 1);
    mesh->Verts(2).color.Set(r2, g2, b2, 1);
    mesh->Verts(3).color.Set(r3, g3, b3, 1);
}

// ============================================================================
// Layout grid
//
// Camera: at -Y looking +Y, target=(0,0,0), dist=10
// With yFov=0.6024, aspect=4:3, at dist=10:
//   visible half-height = 10 * tan(0.6024/2) = ~3.10
//   visible half-width  = 3.10 * 4/3 = ~4.13
// So visible range: X ∈ [-4.1, 4.1], Z ∈ [-3.1, 3.1]
//
// Grid layout (3 columns x 3 rows), centered at origin:
//   Cell size: 2.5 x 2.0  (with content ~2.0 x 1.5 + padding)
//   Row 1 (z=+1.0): solid_quads | vertex_colors | z_ordering
//   Row 2 (z=-1.0): alpha_blend | additive_blend | multiply_blend
//   Row 3 (z=-3.0): text_basic | text_clipping | text_wrap
// ============================================================================

static const float kCellW = 2.7f;   // horizontal spacing between cell centers
static const float kCellH = 1.9f;   // vertical spacing between cell centers
static const float kQS = 1.4f;      // quad size within a cell

// Cell center X for column 0,1,2 (centered at X=0)
static float ColX(int col) { return (col - 1) * kCellW; }

// Cell top-Z for row 0,1,2 (row 0 at top)
static float RowZ(int row) { return 1.5f - row * kCellH; }

// ============================================================================
// Test case: Solid colored quads (row 0, col 0)
// ============================================================================

void BuildSolidQuads(RndDir* dir) {
    float cx = ColX(0);
    float z = RowZ(0);
    float sz = kQS * 0.45f;
    float gap = 0.1f;
    float startX = cx - (1.5f * sz + gap);

    RndMat* redMat = MakeSolidMat(dir, "test_red_mat", 1, 0, 0);
    RndMat* greenMat = MakeSolidMat(dir, "test_green_mat", 0, 1, 0);
    RndMat* blueMat = MakeSolidMat(dir, "test_blue_mat", 0, 0, 1);

    MakeQuad(dir, "test_red_quad", redMat, startX, z, sz, sz);
    MakeQuad(dir, "test_green_quad", greenMat, startX + sz + gap, z, sz, sz);
    MakeQuad(dir, "test_blue_quad", blueMat, startX + 2 * (sz + gap), z, sz, sz);
}

// ============================================================================
// Test case: Vertex color interpolation (row 0, col 1)
// ============================================================================

void BuildVertexColors(RndDir* dir) {
    float cx = ColX(1);
    float z = RowZ(0);
    float w = kQS * 1.0f;
    float h = kQS * 0.75f;
    RndMat* mat = MakeSolidMat(dir, "test_vcolor_mat", 1, 1, 1);
    RndMesh* quad = MakeQuad(dir, "test_vcolor_quad", mat,
                             cx - w * 0.5f, z, w, h);

    // Top-left=red, top-right=green, bottom-right=blue, bottom-left=yellow
    SetCornerColors(quad,
                    1, 0, 0,    // v0 top-left
                    0, 1, 0,    // v1 top-right
                    0, 0, 1,    // v2 bottom-right
                    1, 1, 0);   // v3 bottom-left
}

// ============================================================================
// Test case: Z-ordering (row 0, col 2)
// ============================================================================

void BuildZOrdering(RndDir* dir) {
    float cx = ColX(2);
    float z = RowZ(0);
    float sz = kQS * 0.55f;

    RndMat* mat1 = MakeSolidMat(dir, "test_z_mat1", 1, 0, 0);
    RndMat* mat2 = MakeSolidMat(dir, "test_z_mat2", 0, 1, 0);
    RndMat* mat3 = MakeSolidMat(dir, "test_z_mat3", 0, 0, 1);

    // Red background, green middle, blue front — each offset
    float startX = cx - sz * 0.8f;
    MakeQuad(dir, "test_z_red", mat1, startX, z, sz, sz, 1.0f);
    MakeQuad(dir, "test_z_green", mat2, startX + sz * 0.3f, z - 0.1f, sz, sz, 0.0f);
    MakeQuad(dir, "test_z_blue", mat3, startX + sz * 0.6f, z - 0.2f, sz, sz, -1.0f);
}

// ============================================================================
// Test case: Alpha blending (row 1, col 0)
// ============================================================================

void BuildAlphaBlend(RndDir* dir) {
    float cx = ColX(0);
    float z = RowZ(1);
    float sz = kQS * 0.7f;

    // Background: solid red
    RndMat* bgMat = MakeSolidMat(dir, "test_alpha_bg_mat", 1, 0, 0);
    MakeQuad(dir, "test_alpha_bg", bgMat, cx - sz * 0.7f, z, sz, sz, 0.0f);

    // Foreground: 50% transparent blue, overlapping and offset
    RndMat* fgMat = MakeSolidMat(dir, "test_alpha_fg_mat", 0, 0, 1, 0.5f);
    fgMat->SetBlend(BaseMaterial::kBlendSrcAlpha);
    RndMesh* fg = MakeQuad(dir, "test_alpha_fg", fgMat,
                           cx - sz * 0.2f, z - 0.15f, sz, sz, -0.1f);
    fg->SetOrder(1.0f);
}

// ============================================================================
// Test case: Additive blending (row 1, col 1)
// ============================================================================

void BuildAdditiveBlend(RndDir* dir) {
    float cx = ColX(1);
    float z = RowZ(1);
    float sz = kQS * 0.7f;

    RndMat* bgMat = MakeSolidMat(dir, "test_add_bg_mat", 0.2f, 0.1f, 0.1f);
    MakeQuad(dir, "test_add_bg", bgMat, cx - sz * 0.7f, z, sz, sz, 0.0f);

    RndMat* addMat = MakeSolidMat(dir, "test_add_fg_mat", 0, 0.6f, 0);
    addMat->SetBlend(BaseMaterial::kBlendAdd);
    RndMesh* fg = MakeQuad(dir, "test_add_fg", addMat,
                           cx - sz * 0.2f, z - 0.15f, sz, sz, -0.1f);
    fg->SetOrder(1.0f);
}

// ============================================================================
// Test case: Multiply blending (row 1, col 2)
// ============================================================================

void BuildMultiplyBlend(RndDir* dir) {
    float cx = ColX(2);
    float z = RowZ(1);
    float sz = kQS * 0.7f;

    // Larger white background so the multiply overlay stays within it
    RndMat* bgMat = MakeSolidMat(dir, "test_mul_bg_mat", 1, 1, 1);
    MakeQuad(dir, "test_mul_bg", bgMat, cx - sz * 0.9f, z - 0.1f, sz * 1.5f, sz * 1.1f, 0.0f);

    RndMat* mulMat = MakeSolidMat(dir, "test_mul_fg_mat", 1, 0.5f, 0);
    mulMat->SetBlend(BaseMaterial::kBlendMultiply);
    RndMesh* fg = MakeQuad(dir, "test_mul_fg", mulMat,
                           cx - sz * 0.3f, z - 0.05f, sz, sz, -0.1f);
    fg->SetOrder(1.0f);
}

// ============================================================================
// Font loading
// ============================================================================

// Keep a static ObjDirPtr alive so the font dir doesn't get GC'd
static ObjDirPtr<ObjectDir> sFontDirPtr;

ObjectDir* LoadFontDir(const char* fontMiloPath) {
    if (access(fontMiloPath, R_OK) != 0) {
        printf("Render Test: font file not found: %s\n", fontMiloPath);
        return nullptr;
    }

    // Resolve to absolute path (engine file system requires it)
    char absPath[PATH_MAX];
    if (!realpath(fontMiloPath, absPath)) {
        printf("Render Test: cannot resolve path: %s\n", fontMiloPath);
        return nullptr;
    }

    printf("Render Test: loading font from %s...\n", absPath);
    FilePath fp(absPath);
    sFontDirPtr.LoadFile(fp, false, false, kLoadFront, false);
    ObjectDir* dir = sFontDirPtr;
    if (!dir) {
        printf("Render Test: failed to load font dir\n");
        return nullptr;
    }
    printf("Render Test: font dir loaded successfully\n");
    return dir;
}

RndFont* FindFirstFont(ObjectDir* fontDir) {
    ObjDirItr<RndFont> it(fontDir, true);
    if (it) {
        printf("Render Test: found font '%s'\n", it->Name());
        return it;
    }
    printf("Render Test: no RndFont found in font dir\n");
    return nullptr;
}

// ============================================================================
// Helper: create a RndText with font, position, and text content
// ============================================================================

static RndText* MakeText(RndDir* dir, const char* name, RndFont* font,
                         float x, float z, float yPos, const char* text,
                         float fontSize = 0.4f) {
    RndText* t = Hmx::Object::New<RndText>();
    t->SetName(name, dir);

    // Set font on default style
    t->Styles()[0].mFont = font;
    t->Styles()[0].mSize = fontSize;
    t->Styles()[0].mTextColor.Set(1, 1, 1);

    // Position the text
    Transform xfm;
    xfm.Reset();
    xfm.v.Set(x, yPos, z);
    t->SetLocalXfm(xfm);

    t->SetText(text);
    t->SetShowing(true);
    t->UpdateText();

    return t;
}

// ============================================================================
// Test case: Basic text rendering (row 2, col 0)
// ============================================================================

void BuildTextBasic(RndDir* dir, RndFont* font) {
    float cx = ColX(0);
    float z = RowZ(2);

    // Simple text — baseline glyph mesh generation
    MakeText(dir, "test_text_hello", font, cx - 0.6f, z + 0.8f, -0.1f,
             "Hello World!", 0.35f);

    // Colored text via style (orange — DC3 accent color)
    RndText* colored = MakeText(dir, "test_text_colored", font,
                                cx - 0.6f, z + 0.15f, -0.1f, "DANCE CENTRAL", 0.25f);
    colored->Styles()[0].mTextColor.Set(1.0f, 0.4f, 0.15f);
    colored->UpdateText();
}

// ============================================================================
// Test case: Text clipping — reproduces "OKAY" -> "KAY" bug (row 2, col 1)
// ============================================================================

void BuildTextClipping(RndDir* dir, RndFont* font) {
    float cx = ColX(1);
    float z = RowZ(2);

    // Narrow width to force clipping — the "OKAY" bug clips the first glyph
    RndText* t = MakeText(dir, "test_text_clip", font,
                          cx - 0.8f, z + 0.8f, -0.1f, "OKAY", 0.4f);
    t->SetWidth(1.2f);  // constrained width
    t->SetAlignment(RndText::kMiddleLeft);
    t->UpdateText();

    // Reference: same text with generous width (should show all chars)
    RndText* ref = MakeText(dir, "test_text_clip_ref", font,
                            cx - 0.8f, z + 0.1f, -0.1f, "OKAY", 0.4f);
    ref->SetWidth(10.0f);
    ref->SetAlignment(RndText::kMiddleLeft);
    ref->UpdateText();
}

// ============================================================================
// Test case: Text wrapping — reproduces "Learn m/ore" bug (row 2, col 2)
// ============================================================================

void BuildTextWrap(RndDir* dir, RndFont* font) {
    float cx = ColX(2);
    float z = RowZ(2);

    // Wrap text at a width that triggers the word-wrap bug
    RndText* t = MakeText(dir, "test_text_wrap", font,
                          cx - 1.0f, z + 1.0f, -0.1f,
                          "Learn more about this feature", 0.3f);
    t->SetWidth(2.0f);  // narrow width forces wrapping
    t->SetAlignment(RndText::kTopLeft);
    t->SetFitType(RndText::kFitWrap);
    t->UpdateText();
}

// ============================================================================
// Test case: Menu text layout — matches DC3 main menu (standalone, no venue)
// Uses a full-screen dark background to simulate the game's dark venue backdrop.
// Reference: archive/screenshots/session34_mainmenu_text_f300.png
// ============================================================================

void BuildTextMenu(RndDir* dir, RndFont* font) {
    // Layout matching DC3 main menu (reference: session34_mainmenu_text_f300.png)
    // Game has text centered horizontally in the lower half of the screen.
    // Camera visible range: X ~[-4.1, 4.1], Z ~[-3.1, 3.1] (at dist=10, yFov=0.6024)

    // "MAIN MENU" — centered, in lower-center area
    RndText* title = MakeText(dir, "menu_title", font,
                              0.0f, 0.0f, -0.1f, "MAIN MENU", 0.5f);
    title->SetAlignment(RndText::kMiddleCenter);
    title->UpdateText();

    // "START THE PARTY" — centered, below title
    RndText* sub = MakeText(dir, "menu_subtitle", font,
                            0.0f, -0.7f, -0.1f, "START THE PARTY", 0.42f);
    sub->SetAlignment(RndText::kMiddleCenter);
    sub->UpdateText();

    // "PLAYERS: 1 - 2" — centered, smaller, lighter gray
    RndText* players = MakeText(dir, "menu_players", font,
                                0.0f, -1.7f, -0.1f, "PLAYERS: 1 - 2", 0.28f);
    players->Styles()[0].mTextColor.Set(0.8f, 0.8f, 0.8f);
    players->SetAlignment(RndText::kMiddleCenter);
    players->UpdateText();

    // Copyright — bottom center, very small, light gray
    RndText* copyright = MakeText(dir, "menu_copyright", font,
                                  0.0f, -2.7f, -0.1f,
                                  "\xC2\xA9 2012 HARMONIX MUSIC SYSTEMS, INC.", 0.14f);
    copyright->Styles()[0].mTextColor.Set(0.6f, 0.6f, 0.6f);
    copyright->SetAlignment(RndText::kMiddleCenter);
    copyright->UpdateText();
}

// ============================================================================
// Build all tests
// ============================================================================

void BuildAllTests(RndDir* dir, RndFont* font) {
    printf("Render Test: building solid quads...\n");
    BuildSolidQuads(dir);

    printf("Render Test: building vertex colors...\n");
    BuildVertexColors(dir);

    printf("Render Test: building Z-ordering...\n");
    BuildZOrdering(dir);

    printf("Render Test: building alpha blend...\n");
    BuildAlphaBlend(dir);

    printf("Render Test: building additive blend...\n");
    BuildAdditiveBlend(dir);

    printf("Render Test: building multiply blend...\n");
    BuildMultiplyBlend(dir);

    if (font) {
        printf("Render Test: building text basic...\n");
        BuildTextBasic(dir, font);

        printf("Render Test: building text clipping...\n");
        BuildTextClipping(dir, font);

        printf("Render Test: building text wrap...\n");
        BuildTextWrap(dir, font);
    } else {
        printf("Render Test: skipping text tests (no font loaded)\n");
    }
}

// ============================================================================
// Venue loading
// ============================================================================

// Keep a static ObjDirPtr alive so the venue dir doesn't get GC'd
static ObjDirPtr<ObjectDir> sVenueDirPtr;

ObjectDir* LoadVenueDir(const char* venueMiloPath) {
    if (access(venueMiloPath, R_OK) != 0) {
        printf("Render Test: venue file not found: %s\n", venueMiloPath);
        return nullptr;
    }

    char absPath[PATH_MAX];
    if (!realpath(venueMiloPath, absPath)) {
        printf("Render Test: cannot resolve venue path: %s\n", venueMiloPath);
        return nullptr;
    }

    printf("Render Test: loading venue from %s...\n", absPath);
    FilePath fp(absPath);
    sVenueDirPtr.LoadFile(fp, false, false, kLoadFront, false);
    ObjectDir* dir = sVenueDirPtr;
    if (!dir) {
        printf("Render Test: failed to load venue dir\n");
        return nullptr;
    }

    // SyncObjects so mDraws is populated
    RndDir* rndDir = dynamic_cast<RndDir*>(dir);
    if (rndDir) {
        rndDir->SyncObjects();
        printf("Render Test: venue SyncObjects complete\n");
    }

    printf("Render Test: venue loaded: '%s' (class '%s')\n",
           dir->Name(), dir->ClassName().Str());
    return dir;
}

// ============================================================================
// Venue + UI composite test
// ============================================================================

void BuildVenueWithUI(RndDir* dir, ObjectDir* venueDir, RndFont* font, RndCam* cam) {
    if (!venueDir) {
        printf("Render Test: no venue loaded, skipping venue_with_ui\n");
        return;
    }

    int meshCount = 0;
    ObjDirItr<RndMesh> countIt(venueDir, true);
    while (countIt) { meshCount++; ++countIt; }
    printf("Render Test: venue has %d meshes\n", meshCount);

    if (font && cam) {
        printf("Render Test: venue loaded with %d meshes — adding UI overlay\n", meshCount);

        // Camera basis vectors for billboard placement
        Transform camXfm = cam->LocalXfm();
        Vector3 camPos, camFwd, camRight, camUp;
        camPos.Set(camXfm.v.x, camXfm.v.y, camXfm.v.z);
        camFwd.Set(camXfm.m.y.x, camXfm.m.y.y, camXfm.m.y.z);
        camRight.Set(camXfm.m.x.x, camXfm.m.x.y, camXfm.m.x.z);
        camUp.Set(camXfm.m.z.x, camXfm.m.z.y, camXfm.m.z.z);

        // Place UI plane in front of camera
        float uiDist = 50.0f;
        float uiScale = uiDist * 0.05f;

        Vector3 uiCenter;
        uiCenter.x = camPos.x + camFwd.x * uiDist;
        uiCenter.y = camPos.y + camFwd.y * uiDist;
        uiCenter.z = camPos.z + camFwd.z * uiDist;

        // Helper: camera-facing text at (offRight, offUp) from center
        auto makeUIText = [&](const char* name, float offRight, float offUp,
                              const char* text, float fontSize,
                              float r, float g, float b) -> RndText* {
            RndText* t = Hmx::Object::New<RndText>();
            t->SetName(name, dir);
            t->Styles()[0].mFont = font;
            t->Styles()[0].mSize = fontSize * uiScale;
            t->Styles()[0].mTextColor.Set(r, g, b, 1.0f);

            Transform xfm;
            xfm.m.x.Set(camRight.x, camRight.y, camRight.z);
            xfm.m.y.Set(-camFwd.x, -camFwd.y, -camFwd.z);
            xfm.m.z.Set(camUp.x, camUp.y, camUp.z);
            xfm.v.x = uiCenter.x + camRight.x * offRight * uiScale + camUp.x * offUp * uiScale;
            xfm.v.y = uiCenter.y + camRight.y * offRight * uiScale + camUp.y * offUp * uiScale;
            xfm.v.z = uiCenter.z + camRight.z * offRight * uiScale + camUp.z * offUp * uiScale;
            t->SetLocalXfm(xfm);

            t->SetText(text);
            t->SetShowing(true);
            t->UpdateText();
            return t;
        };

        // Layout matching DC3 main menu (session34_mainmenu_text_f300.png reference):
        // Game has text centered in bottom 40% of screen against dark venue backdrop.
        // (0,0) = screen center, positive right = right, positive up = up
        makeUIText("venue_ui_title", 0.0f, -2.5f, "MAIN MENU", 0.55f, 1, 1, 1);
        makeUIText("venue_ui_subtitle", -0.5f, -3.5f, "START THE PARTY", 0.48f, 1, 1, 1);
        makeUIText("venue_ui_players", 0.0f, -5.0f, "PLAYERS: 1 - 2", 0.3f, 0.85f, 0.85f, 0.85f);

        // Copyright at very bottom — matching game's wide centered copyright text
        makeUIText("venue_ui_copyright", -4.0f, -6.5f,
                   "\xC2\xA9 2012 HARMONIX MUSIC SYSTEMS, INC.",
                   0.18f, 0.65f, 0.65f, 0.65f);
    } else {
        printf("Render Test: no font/camera for venue UI overlays\n");
    }
}

// ============================================================================
// Venue camera setup
// Computes bounding box of venue meshes and positions camera to frame them.
// ============================================================================

RndCam* SetupVenueCamera(RndDir* dir, ObjectDir* venueDir) {
    // Compute venue bounding box
    float minX = 1e9f, minY = 1e9f, minZ = 1e9f;
    float maxX = -1e9f, maxY = -1e9f, maxZ = -1e9f;
    int meshCount = 0;

    ObjDirItr<RndMesh> bboxIt(venueDir, true);
    while (bboxIt) {
        RndMesh* m = bboxIt;
        if (m->Showing() && m->NumVerts() > 0) {
            for (int i = 0; i < m->NumVerts(); i++) {
                RndMesh::Vert& v = m->Verts(i);
                if (v.pos.x < minX) minX = v.pos.x;
                if (v.pos.y < minY) minY = v.pos.y;
                if (v.pos.z < minZ) minZ = v.pos.z;
                if (v.pos.x > maxX) maxX = v.pos.x;
                if (v.pos.y > maxY) maxY = v.pos.y;
                if (v.pos.z > maxZ) maxZ = v.pos.z;
            }
            meshCount++;
        }
        ++bboxIt;
    }

    // Compute camera parameters (same logic as milo-viewer AutoFrameCamera)
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float cz = (minZ + maxZ) * 0.5f;
    float sx = maxX - minX;
    float sy = maxY - minY;
    float sz = maxZ - minZ;
    float extent = sqrtf(sx * sx + sy * sy + sz * sz) * 0.5f;
    if (extent < 0.01f) extent = 1.0f;

    // Place camera inside the venue for an interior view.
    // Position at 30% from one edge, looking across the room.
    float eyeX = minX + sx * 0.3f;
    float eyeY = minY + sy * 0.3f;
    float eyeZ = minZ + sz * 0.4f;  // ~40% up from floor
    // Look toward far corner
    float lookX = minX + sx * 0.7f;
    float lookY = minY + sy * 0.7f;
    float lookZ = minZ + sz * 0.35f;
    float dist = sqrtf((eyeX-lookX)*(eyeX-lookX) + (eyeY-lookY)*(eyeY-lookY) + (eyeZ-lookZ)*(eyeZ-lookZ));

    printf("Render Test: venue camera — eye=(%.1f,%.1f,%.1f) look=(%.1f,%.1f,%.1f) dist=%.1f\n",
           eyeX, eyeY, eyeZ, lookX, lookY, lookZ, dist);

    // Build camera same as SetupTestCamera but with venue-appropriate params
    RndCam* cam = Hmx::Object::New<RndCam>();
    cam->SetName("venue_cam", dir);

    Vector3 eye, tgt, fwd, right, up;
    eye.Set(eyeX, eyeY, eyeZ);
    tgt.Set(lookX, lookY, lookZ);
    Subtract(tgt, eye, fwd);
    Normalize(fwd, fwd);

    Vector3 worldUp;
    worldUp.Set(0, 0, 1);
    Cross(fwd, worldUp, right);
    Normalize(right, right);
    Cross(right, fwd, up);
    Normalize(up, up);

    Transform xfm;
    xfm.m.x.Set(right.x, right.y, right.z);
    xfm.m.y.Set(fwd.x, fwd.y, fwd.z);
    xfm.m.z.Set(up.x, up.y, up.z);
    xfm.v.Set(eyeX, eyeY, eyeZ);
    cam->SetLocalXfm(xfm);

    float farPlane = dist * 5.0f;
    if (farPlane < 1000.0f) farPlane = 1000.0f;
    float nearPlane = farPlane * 0.001f;
    if (nearPlane < 0.1f) nearPlane = 0.1f;
    float yFov = 0.6024f;
    cam->SetFrustum(nearPlane, farPlane, yFov, 1.0f);

    // Compute viewProj (same as SetupTestCamera)
    float dr = -Dot(right, eye);
    float df = -Dot(fwd, eye);
    float du = -Dot(up, eye);

    float view[16] = {
        right.x, fwd.x, up.x, 0,
        right.y, fwd.y, up.y, 0,
        right.z, fwd.z, up.z, 0,
        dr,      df,    du,   1
    };

    float aspect = 16.0f / 9.0f;  // venue test uses widescreen
    float cot = 1.0f / tanf(yFov / 2.0f);
    float zRange = farPlane - nearPlane;

    float proj[16] = {
        cot / aspect, 0,   0,                           0,
        0,            0,   farPlane / zRange,            1,
        0,            cot, 0,                           0,
        0,            0,   -nearPlane * farPlane / zRange, 0
    };

    float viewProj[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) sum += view[i*4+k] * proj[k*4+j];
            viewProj[i*4+j] = sum;
        }
    }

    Hmx::Matrix4 vp;
    memcpy(&vp, viewProj, 64);
    cam->SetViewProj(vp);

    return cam;
}

// ============================================================================
// Camera setup
// Milo: X=right, Y=forward/depth, Z=up
// Camera at -Y looking toward +Y, centered at origin
// ============================================================================

// 4x4 matrix multiply (row-major)
static void Mat4Mul(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            out[i * 4 + j] = sum;
        }
    }
}

RndCam* SetupTestCamera(RndDir* dir) {
    RndCam* cam = Hmx::Object::New<RndCam>();
    cam->SetName("test_cam", dir);

    // Camera centered at origin, looking +Y
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    float dist = 10.0f;

    float eyeX = targetX;
    float eyeY = targetY - dist;
    float eyeZ = targetZ;

    // Build look-at vectors (same as OrbitCamera::Update in the viewer)
    Vector3 eye, tgt, fwd, right, up;
    eye.Set(eyeX, eyeY, eyeZ);
    tgt.Set(targetX, targetY, targetZ);
    Subtract(tgt, eye, fwd);
    Normalize(fwd, fwd);

    Vector3 worldUp;
    worldUp.Set(0, 0, 1);
    Cross(fwd, worldUp, right);
    Normalize(right, right);
    Cross(right, fwd, up);
    Normalize(up, up);

    // Set camera local transform (Milo: m.x=right, m.y=forward, m.z=up)
    Transform xfm;
    xfm.m.x.Set(right.x, right.y, right.z);
    xfm.m.y.Set(fwd.x, fwd.y, fwd.z);
    xfm.m.z.Set(up.x, up.y, up.z);
    xfm.v.Set(eyeX, eyeY, eyeZ);
    cam->SetLocalXfm(xfm);

    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float yFov = 0.6024f;
    cam->SetFrustum(nearPlane, farPlane, yFov, 1.0f);

    // Build viewProj manually (same as viewer — RndCam::UpdateLocal is stubbed on native)
    float dr = -Dot(right, eye);
    float df = -Dot(fwd, eye);
    float du = -Dot(up, eye);

    float view[16] = {
        right.x, fwd.x, up.x, 0,
        right.y, fwd.y, up.y, 0,
        right.z, fwd.z, up.z, 0,
        dr,      df,    du,   1
    };

    // Perspective projection (Y-forward depth convention)
    float aspect = 4.0f / 3.0f;  // 640x480
    float cot = 1.0f / tanf(yFov / 2.0f);
    float zRange = farPlane - nearPlane;

    float proj[16] = {
        cot / aspect, 0,   0,                          0,
        0,            0,   farPlane / zRange,           1,
        0,            cot, 0,                          0,
        0,            0,   -nearPlane * farPlane / zRange, 0
    };

    float viewProj[16];
    Mat4Mul(view, proj, viewProj);

    Hmx::Matrix4 vp;
    memcpy(&vp, viewProj, 64);
    cam->SetViewProj(vp);

    printf("Render Test: camera at (%.1f, %.1f, %.1f) looking at (%.1f, %.1f, %.1f)\n",
           eyeX, eyeY, eyeZ, targetX, targetY, targetZ);

    return cam;
}

// ============================================================================
// Environment setup — full white ambient for prelit materials
// ============================================================================

RndEnviron* SetupTestEnvironment(RndDir* dir) {
    RndEnviron* env = Hmx::Object::New<RndEnviron>();
    env->SetName("test_env", dir);

    Hmx::Color ambient;
    ambient.Set(1.0f, 1.0f, 1.0f);
    env->SetAmbientColor(ambient);

    // Add a directional light
    RndLight* light = Hmx::Object::New<RndLight>();
    light->SetName("test_light", dir);
    light->SetLightType(RndLight::kDirectional);
    Hmx::Color white;
    white.Set(1.0f, 1.0f, 1.0f);
    light->SetColor(white);
    light->SetShowing(true);

    Transform lightXfm;
    lightXfm.Reset();
    // Light pointing from front (+Y direction, same as camera)
    lightXfm.m.z.Set(0, 1, 0);
    lightXfm.m.x.Set(1, 0, 0);
    lightXfm.m.y.Set(0, 0, 1);
    light->SetLocalXfm(lightXfm);

    env->AddLight(light);

    return env;
}
