#pragma once

#include "obj/Dir.h"
#include <vector>
#include <string>

class RndDir;
class RndMesh;
class RndLight;
class RndEnviron;
class RndCam;
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

    // Set when FileMerger is used for outfit/viseme loading
    bool fileMergerActive = false;

    // --- Lifecycle ---
    bool Load(const char* miloAbsPath, const ViewerConfig& cfg);
    bool LoadFileMerger(const ViewerConfig& cfg);
    void ReleaseResources();

    // --- Queries ---
    RndEnviron* FindEnvironment() const;
    void PrintSummary(bool verbose) const;

    // --- Setup ---
    void ResolveMeshVisibility(const ViewerConfig& cfg);
    void SetupSyntheticLights(const ViewerConfig& cfg);
    void AutoFrameCamera(OrbitCamera& cam, RndCam* rndCam, const ViewerConfig& cfg) const;

    // --- Rendering ---
    void DrawAllMeshes(const ViewerConfig& cfg) const;

    // --- Helpers ---
    static bool ShouldHideMesh(const RndMesh* mesh, const ViewerConfig& cfg);
    static bool HasUnresolvedTexture(const RndMesh* mesh);
};
