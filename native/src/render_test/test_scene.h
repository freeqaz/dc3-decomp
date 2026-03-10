// DC3 Native Port — Render Test Scene Builder
// Programmatic construction of test scenes for renderer validation.

#pragma once

class ObjectDir;
class RndDir;
class RndMesh;
class RndMat;
class RndCam;
class RndEnviron;
class RndText;
class RndFont;

// Build a quad mesh with 4 verts and 2 faces in the XZ plane.
// x,z are bottom-left corner; w is width along X, h is height along Z.
// yPos controls depth (camera looks from -Y toward +Y).
// Normal faces -Y (toward the camera).
RndMesh* MakeQuad(RndDir* dir, const char* name, RndMat* mat,
                  float x, float z, float w, float h, float yPos = 0.0f);

// Create a solid-color prelit material with no backface culling.
RndMat* MakeSolidMat(RndDir* dir, const char* name,
                     float r, float g, float b, float a = 1.0f);

// Set all vertex colors on a mesh to the given RGBA.
void SetVertexColors(RndMesh* mesh, float r, float g, float b, float a = 1.0f);

// Set per-vertex colors (4 corners: top-left, top-right, bottom-right, bottom-left).
void SetCornerColors(RndMesh* mesh,
                     float r0, float g0, float b0,
                     float r1, float g1, float b1,
                     float r2, float g2, float b2,
                     float r3, float g3, float b3);

// ---- Test case builders ----
// Each populates the given RndDir with test objects.

void BuildSolidQuads(RndDir* dir);
void BuildVertexColors(RndDir* dir);
void BuildAlphaBlend(RndDir* dir);
void BuildAdditiveBlend(RndDir* dir);
void BuildMultiplyBlend(RndDir* dir);
void BuildZOrdering(RndDir* dir);

// ---- Text test case builders ----
// These require a font directory loaded from a .milo_xbox file.
// Call LoadFontDir() first; if it returns null, text tests are skipped.

// Load the default font .milo_xbox and return the dir (caller keeps alive).
// Returns null if the font file is not found.
ObjectDir* LoadFontDir(const char* fontMiloPath);

// Find the first RndFont in a loaded font dir.
RndFont* FindFirstFont(ObjectDir* fontDir);

void BuildTextBasic(RndDir* dir, RndFont* font);
void BuildTextClipping(RndDir* dir, RndFont* font);
void BuildTextWrap(RndDir* dir, RndFont* font);
void BuildTextMenu(RndDir* dir, RndFont* font);

// Build all test cases laid out in a grid.
void BuildAllTests(RndDir* dir, RndFont* font);

// ---- Venue + UI composite test ----
// Loads a .milo_xbox venue scene and draws UI text overlays on top.

// Load a .milo_xbox scene file and return the loaded ObjectDir.
// The returned ObjDirPtr is stored in a static to prevent GC.
ObjectDir* LoadVenueDir(const char* venueMiloPath);

// Build venue+UI composite: draws venue meshes, then UI overlays on top.
// venueDir is the loaded venue scene; font is for text overlays.
// cam is the venue camera (used to orient UI text to face the camera).
void BuildVenueWithUI(RndDir* dir, ObjectDir* venueDir, RndFont* font, RndCam* cam);

// Set up a camera that can see the venue (backs out far enough to frame it).
RndCam* SetupVenueCamera(RndDir* dir, ObjectDir* venueDir);

// Set up camera and environment for the test scene.
// Camera at -Y looking toward +Y, with viewProj matrix set.
RndCam* SetupTestCamera(RndDir* dir);
RndEnviron* SetupTestEnvironment(RndDir* dir);
