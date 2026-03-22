#include "ViewerCamera.h"

#include "rndobj/Cam.h"
#include "math/Vec.h"
#include "gfx/ImGuiBackend.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>

OrbitCamera gOrbitCam;

void Mat4Multiply(const float* a, const float* b, float* out) {
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

void OrbitCamera::Reset() {
    azimuth = 0.0f;
    elevation = 0.3f;
    distance = 10.0f;
    targetX = 0.0f;
    targetY = 1.0f;
    targetZ = 0.0f;
}

void OrbitCamera::Update(RndCam* cam) {
    // Clamp elevation to avoid gimbal lock
    if (elevation > 1.5f) elevation = 1.5f;
    if (elevation < -1.5f) elevation = -1.5f;
    if (distance < 0.1f) distance = 0.1f;

    // Camera position from spherical coordinates (Milo convention: Z-up)
    float cosElev = cosf(elevation);
    float eyeX = targetX + distance * cosElev * sinf(azimuth);
    float eyeY = targetY + distance * cosElev * cosf(azimuth);
    float eyeZ = targetZ + distance * sinf(elevation);

    // Build look-at vectors
    Vector3 eye, tgt, fwd, right, up;
    eye.Set(eyeX, eyeY, eyeZ);
    tgt.Set(targetX, targetY, targetZ);
    Subtract(tgt, eye, fwd);
    Normalize(fwd, fwd);

    // Milo world: Z is up
    Vector3 worldUp;
    worldUp.Set(0, 0, 1);
    Cross(fwd, worldUp, right);
    float rightLen = Length(right);
    if (rightLen < 0.001f) {
        worldUp.Set(0, 1, 0);
        Cross(fwd, worldUp, right);
    }
    Normalize(right, right);
    Cross(right, fwd, up);
    Normalize(up, up);

    // Set camera's local transform
    // Milo convention: m.x = right, m.y = forward, m.z = up
    Transform xfm;
    xfm.m.x.Set(right.x, right.y, right.z);
    xfm.m.y.Set(fwd.x, fwd.y, fwd.z);
    xfm.m.z.Set(up.x, up.y, up.z);
    xfm.v.Set(eyeX, eyeY, eyeZ);
    cam->SetLocalXfm(xfm);

    // Build viewProj matrix manually (RndCam::UpdateLocal is stubbed)
    float dr = -Dot(right, eye);
    float df = -Dot(fwd, eye);
    float du = -Dot(up, eye);

    float view[16] = {
        right.x, fwd.x, up.x, 0,
        right.y, fwd.y, up.y, 0,
        right.z, fwd.z, up.z, 0,
        dr,      df,    du,   1
    };

    // Perspective projection (row-major, Y-forward depth convention)
    float near = cam->NearPlane();
    float far = cam->FarPlane();
    float yfov = cam->YFov();
    float aspect = 16.0f / 9.0f;
    float cot = 1.0f / tanf(yfov / 2.0f);
    float zRange = far - near;

    float proj[16] = {
        cot / aspect, 0,   0,              0,
        0,            0,   far / zRange,   1,
        0,            cot, 0,              0,
        0,            0,   -near * far / zRange, 0
    };

    // ViewProj = View * Proj
    float viewProj[16];
    Mat4Multiply(view, proj, viewProj);

    // Set on camera (bypass stubbed UpdateLocal)
    Hmx::Matrix4 vp;
    memcpy(&vp, viewProj, 64);
    cam->SetViewProj(vp);
}

// ============================================================================
// GLFW Callbacks (mouse/scroll only — keyboard stays in milo_viewer.cpp)
// ============================================================================
static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    double dx = xpos - gOrbitCam.lastX;
    double dy = ypos - gOrbitCam.lastY;
    gOrbitCam.lastX = xpos;
    gOrbitCam.lastY = ypos;

    if (ImGuiBackend::WantCaptureMouse()) return;

    if (gOrbitCam.leftDrag) {
        gOrbitCam.azimuth -= (float)dx * 0.005f;
        gOrbitCam.elevation += (float)dy * 0.005f;
    }

    if (gOrbitCam.middleDrag) {
        float cosElev = cosf(gOrbitCam.elevation);
        float sinAz = sinf(gOrbitCam.azimuth);
        float cosAz = cosf(gOrbitCam.azimuth);
        float rx = cosAz, rz = -sinAz;
        float panScale = gOrbitCam.distance * 0.002f;
        gOrbitCam.targetX -= rx * (float)dx * panScale;
        gOrbitCam.targetZ -= rz * (float)dx * panScale;
        gOrbitCam.targetY += (float)dy * panScale;
    }
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (ImGuiBackend::WantCaptureMouse()) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gOrbitCam.leftDrag = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &gOrbitCam.lastX, &gOrbitCam.lastY);
        }
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        gOrbitCam.middleDrag = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &gOrbitCam.lastX, &gOrbitCam.lastY);
        }
    }
}

static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    if (ImGuiBackend::WantCaptureMouse()) return;
    gOrbitCam.distance *= (1.0f - (float)yoffset * 0.1f);
    if (gOrbitCam.distance < 0.1f) gOrbitCam.distance = 0.1f;
}

void InstallCameraCallbacks(GLFWwindow* window) {
    if (!window) return;
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);
}
