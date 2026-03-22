#pragma once

class RndCam;
class RndEnviron;
struct ViewerScene;

// ImGui-based debug UI for the milo-viewer.
// Provides interactive controls for lights, environment, and rendering.
class ViewerDebugUI {
public:
    void Init(ViewerScene* scene);

    // Build ImGui windows. Call between NewFrame() and Render().
    void Draw();

    // Draw screen-space light position markers. Call during the 3D render pass.
    void DrawLightGizmos(RndCam* cam);

    bool showLightGizmos = true;

private:
    // Get or create an environment with default lights for scenes that lack one.
    RndEnviron* EnsureEnvironment();

    ViewerScene* mScene = nullptr;
    bool mShowWindow = true;
    bool mCreatedDefaultEnv = false;
};
