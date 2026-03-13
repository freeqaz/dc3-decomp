#pragma once

#include <cstdio>
#include <string>
#include <vector>

// Forward declare to avoid pulling in Lit.h
namespace RndLight_ns { enum Type : int; }

struct ViewerConfig {
    const char* miloPath = nullptr;
    const char* screenshotPath = nullptr;
    const char* clipsPath = nullptr;
    const char* visemesPath = nullptr;
    const char* clipName = nullptr;
    const char* videoPath = nullptr;
    const char* cameraMode = "orbit";
    const char* exportTexturesDir = nullptr;
    const char* exportMaterialsDir = nullptr;
    const char* exportGltfPath = nullptr;
    const char* poseDumpPath = nullptr;
    const char* poseDumpBonesCsv = nullptr;
    const char* poseDumpBeatArg = nullptr;
    const char* testBoneName = nullptr;
    const char* testBoneAxis = "x";
    const char* charSetupPath = nullptr;
    const char* movieFilePath = nullptr;  // --movie <video.mp4> for TexMovie test

    struct SubdirEntry {
        std::string path;
        float offsetX = 0, offsetY = 0, offsetZ = 0;
        float rotateDeg = 0; // rotation around Z axis (up)
    };
    std::vector<SubdirEntry> subdirs;

    struct LightDef {
        int type; // RndLight::Type (0=point, 1=directional)
        float x, y, z;        // position (point) or direction (dir)
        float r, g, b;        // color 0-1
        float intensity;       // multiplier (default 1.0)
    };
    std::vector<LightDef> lights;
    std::vector<std::string> hidePatterns;

    float camAzimuthDeg = -999.0f;  // sentinel: use default
    float camElevationDeg = -999.0f;
    float camDistanceOverride = -1.0f;  // sentinel: use auto
    float eyeX = 0, eyeY = 0, eyeZ = 0;
    float lookX = 0, lookY = 0, lookZ = 0;
    bool hasEye = false, hasLookat = false;
    float startFrame = -1.0f;       // sentinel: use default
    float animSpeed = 1.0f;
    float testBoneAngle = 45.0f;
    float bpm = 120.0f;
    float videoDuration = 10.0f;
    float ambientR = -1, ambientG = -1, ambientB = -1; // sentinel: -1 = don't override
    int videoFps = 30;
    bool startPaused = false;
    bool verbose = false;
    bool dumpBones = false;
    bool directPose = false;
    int maxFrames = -1;

    bool IsExportOnly() const {
        return exportTexturesDir || exportMaterialsDir || exportGltfPath;
    }

    bool IsHeadless() const {
        return screenshotPath || videoPath;
    }

    static ViewerConfig Parse(int argc, char** argv);
    static void PrintHelp(FILE* f);
};
