#pragma once

#include <variant>
#include <vector>
#include <string>

struct ViewerScene;
struct AnimState;
struct CharAnimState;
struct ViewerConfig;
class RndCam;

// ============================================================================
// Mode structs — derived state only
// ============================================================================

struct ScreenshotMode {
    std::vector<std::string> poseDumpBones;
    int warmupFrames = 3;
};

struct VideoMode {
    int   totalFrames;
    float dt;
};

struct InteractiveMode {};

using ViewerMode = std::variant<ScreenshotMode, VideoMode, InteractiveMode>;

// ============================================================================
// Mode selection + runner functions
// ============================================================================

ViewerMode SelectMode(const ViewerConfig& cfg);

int RunScreenshot(ScreenshotMode& m, ViewerScene& scene,
                  AnimState& anim, CharAnimState& charAnim,
                  RndCam* cam, const ViewerConfig& cfg,
                  const char* absPath);

int RunVideo(VideoMode& m, ViewerScene& scene,
             AnimState& anim, CharAnimState& charAnim,
             RndCam* cam, const ViewerConfig& cfg);

int RunInteractive(InteractiveMode& m, ViewerScene& scene,
                   AnimState& anim, CharAnimState& charAnim,
                   RndCam* cam, const ViewerConfig& cfg);
