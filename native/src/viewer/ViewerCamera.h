#pragma once

class RndCam;
struct GLFWwindow;

struct OrbitCamera {
    float azimuth = 0.4f;       // radians around Y axis
    float elevation = 0.3f;     // radians above horizon
    float distance = 3.0f;      // distance from target
    float targetX = 0.0f;
    float targetY = 1.0f;       // Y=1 centers on typical character height
    float targetZ = 0.0f;

    // Mouse state
    bool leftDrag = false;
    bool middleDrag = false;
    double lastX = 0, lastY = 0;

    void Reset();
    void Update(RndCam* cam);
};

// Global orbit camera instance
extern OrbitCamera gOrbitCam;

// Install mouse/scroll callbacks only (keyboard callback stays in milo_viewer.cpp
// because it references AnimState which is not yet extracted)
void InstallCameraCallbacks(GLFWwindow* window);

// 4x4 matrix multiply (row-major, right-multiply convention)
void Mat4Multiply(const float* a, const float* b, float* out);
