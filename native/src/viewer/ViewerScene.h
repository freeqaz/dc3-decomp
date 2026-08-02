#pragma once

#include "obj/Dir.h"
#include <vector>
#include <string>
#include <set>

class RndDir;
class RndMesh;
class RndLight;
class RndEnviron;
class RndCam;
class RndTex;
class TexMovie;
class Character;
struct OrbitCamera;
struct ViewerConfig;

struct ViewerScene {
    // Owned resources (ObjDirPtr prevents GC)
    ObjDirPtr<ObjectDir> baseDir;
    std::vector<ObjDirPtr<ObjectDir>> subdirs;
    ObjDirPtr<ObjectDir> clipsDir;
    ObjDirPtr<ObjectDir> visemeDir;

    // Derived (non-owning pointers into the dirs above)
    ObjectDir* baseScene = nullptr;
    RndDir*    rndScene = nullptr;
    Character* character = nullptr;

    // Synthetic resources created at runtime
    std::vector<RndLight*> syntheticLights;

    // TexMovie objects found in the scene (non-owning)
    std::vector<TexMovie*> movies;

    // Synthetic TexMovie created for --movie flag (owned)
    TexMovie* syntheticMovie = nullptr;
    RndTex*   syntheticMovieTex = nullptr;

    // Set when FileMerger is used for outfit/viseme loading
    bool fileMergerActive = false;

    // --- Lifecycle ---
    bool Load(const char* miloAbsPath, const ViewerConfig& cfg);
    bool LoadFileMerger(const ViewerConfig& cfg);
    void ReleaseResources();

    // --- Queries ---
    RndEnviron* FindEnvironment() const;
    void PrintSummary(bool verbose) const;

    // Neutral grey handed to meshes that ship no RndMat (geometry-library milos)
    class RndMat* fallbackMat = nullptr;

    // --- Setup ---
    void ResolveMeshVisibility(const ViewerConfig& cfg);
    int  ChooseLod() const;
    void CollectRedundantLodMeshes(std::set<const class RndDrawable*>& out, bool verbose) const;
    void ApplyFallbackMaterial(const ViewerConfig& cfg);
    void SetupSyntheticLights(const ViewerConfig& cfg);
    void AutoFrameCamera(OrbitCamera& cam, RndCam* rndCam, const ViewerConfig& cfg) const;

    // --- Movie support ---
    void EnterMovies(const ViewerConfig& cfg);
    void PollMovies(float seconds = -1.0f); // seconds >= 0 forces virtual time
    void DrawMovieOverlay();

    // --- Rendering ---
    void DrawAllMeshes(const ViewerConfig& cfg) const;

    // --- Helpers ---
    static bool ShouldHideMesh(const RndMesh* mesh, const ViewerConfig& cfg);
    static bool HasUnresolvedTexture(const RndMesh* mesh);
};
