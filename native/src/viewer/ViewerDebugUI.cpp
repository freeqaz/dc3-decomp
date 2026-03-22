#include "viewer/ViewerDebugUI.h"
#include "viewer/ViewerScene.h"

#include "obj/Dir.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "math/Vec.h"
#include "platform/Rnd_Wgpu.h"

#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>

extern Rnd& TheRnd;

void ViewerDebugUI::Init(ViewerScene* scene) {
    mScene = scene;
}

// Project worldspace position to screen coordinates using current camera
static bool ProjectToScreen(const Vector3& worldPos, RndCam* cam, float& sx, float& sy) {
    if (!cam) return false;

    const Hmx::Matrix4& vp = cam->GetViewProjMatrix();

    // Multiply worldPos by viewProj (row-major)
    float x = worldPos.x * vp.x.x + worldPos.y * vp.y.x + worldPos.z * vp.z.x + vp.w.x;
    float y = worldPos.x * vp.x.y + worldPos.y * vp.y.y + worldPos.z * vp.z.y + vp.w.y;
    float z = worldPos.x * vp.x.z + worldPos.y * vp.y.z + worldPos.z * vp.z.z + vp.w.z;
    float w = worldPos.x * vp.x.w + worldPos.y * vp.y.w + worldPos.z * vp.z.w + vp.w.w;

    if (w <= 0.001f) return false; // behind camera

    float ndcX = x / w;
    float ndcY = y / w;

    // NDC [-1,1] to screen pixels
    float screenW = (float)TheRnd.Width();
    float screenH = (float)TheRnd.Height();
    sx = (ndcX * 0.5f + 0.5f) * screenW;
    sy = (1.0f - (ndcY * 0.5f + 0.5f)) * screenH; // flip Y

    return true;
}

// Helper: create a directional light and add to env + scene synthetic list
static RndLight* MakeDirLight(const char* name, ObjectDir* owner, RndEnviron* env,
                              float dx, float dy, float dz,
                              float r, float g, float b) {
    RndLight* light = Hmx::Object::New<RndLight>();
    light->SetName(name, owner);
    light->SetLightType(RndLight::kDirectional);
    Hmx::Color col;
    col.Set(r, g, b);
    light->SetColor(col);
    light->SetShowing(true);

    Transform xfm;
    xfm.Reset();
    Vector3 dir(dx, dy, dz);
    Normalize(dir, dir);
    xfm.m.z = dir;
    Vector3 up(0, 1, 0);
    if (fabsf(dir.y) > 0.9f) up.Set(1, 0, 0);
    Cross(dir, up, xfm.m.x);
    Normalize(xfm.m.x, xfm.m.x);
    Cross(xfm.m.x, dir, xfm.m.y);
    light->SetLocalXfm(xfm);

    env->AddLight(light);
    return light;
}

RndEnviron* ViewerDebugUI::EnsureEnvironment() {
    // First try the scene's own environment
    RndEnviron* env = mScene ? mScene->FindEnvironment() : nullptr;
    if (env) return env;

    // Try the global current environment
    env = RndEnviron::Current();
    if (env) return env;

    // No environment at all — create one with default 3-point lights.
    // This matches the fallback lighting in WriteSceneUniforms().
    if (mCreatedDefaultEnv) return nullptr; // already tried and failed
    if (!mScene || !mScene->baseScene) return nullptr;

    RndDir* rndScene = mScene->rndScene;
    ObjectDir* owner = rndScene ? (ObjectDir*)rndScene : mScene->baseScene;

    env = Hmx::Object::New<RndEnviron>();
    env->SetName("debug_env", owner);
    Hmx::Color amb;
    amb.Set(0.4f, 0.4f, 0.4f);
    env->SetAmbientColor(amb);

    if (rndScene) {
        rndScene->SetEnv(env);
    }

    // Key light — three-quarter from front-left
    RndLight* key = MakeDirLight("key_light", owner, env,
                                  -0.4f, -0.7f, 0.5f,  0.9f, 0.9f, 0.9f);
    mScene->syntheticLights.push_back(key);

    // Fill light — softer from front-right
    RndLight* fill = MakeDirLight("fill_light", owner, env,
                                   0.5f, -0.5f, 0.3f,  0.4f, 0.4f, 0.4f);
    mScene->syntheticLights.push_back(fill);

    // Rim light — from behind for edge definition
    RndLight* rim = MakeDirLight("rim_light", owner, env,
                                  0.0f, 0.8f, 0.4f,  0.3f, 0.3f, 0.3f);
    mScene->syntheticLights.push_back(rim);

    // Activate the environment
    Vector3 origin(0, 0, 0);
    env->Select(&origin);

    mCreatedDefaultEnv = true;
    printf("ViewerDebugUI: created default environment with 3-point lighting\n");
    return env;
}

void ViewerDebugUI::Draw() {
    if (!mShowWindow) {
        // Still allow toggling back on with a small button
        if (ImGui::Begin("##toggle", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (ImGui::Button("Debug UI")) mShowWindow = true;
        }
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Lighting Debug", &mShowWindow)) {
        ImGui::End();
        return;
    }

    RndEnviron* env = EnsureEnvironment();
    bool lightEdited = false;

    if (!env) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No environment available");
        ImGui::End();
        return;
    }

    // ---- Environment ----
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Ambient
        Hmx::Color amb = env->AmbientColor();
        float ambCol[3] = { amb.red, amb.green, amb.blue };
        if (ImGui::ColorEdit3("Ambient", ambCol)) {
            Hmx::Color newAmb;
            newAmb.Set(ambCol[0], ambCol[1], ambCol[2]);
            env->SetAmbientColor(newAmb);
            lightEdited = true;
        }

        // Fog
        bool fogOn = env->FogEnable();
        if (ImGui::Checkbox("Fog", &fogOn)) {
            env->SetFogEnable(fogOn);
            lightEdited = true;
        }
        if (fogOn) {
            float fogStart = env->FogStart();
            float fogEnd = env->FogEnd();
            if (ImGui::DragFloat("Fog Start", &fogStart, 1.0f, 0.0f, 10000.0f)) {
                env->SetFogRange(fogStart, fogEnd);
                lightEdited = true;
            }
            if (ImGui::DragFloat("Fog End", &fogEnd, 1.0f, 0.0f, 10000.0f)) {
                env->SetFogRange(fogStart, fogEnd);
                lightEdited = true;
            }
            Hmx::Color fc = env->FogColor();
            float fogCol[3] = { fc.red, fc.green, fc.blue };
            if (ImGui::ColorEdit3("Fog Color", fogCol)) {
                Hmx::Color newFog;
                newFog.Set(fogCol[0], fogCol[1], fogCol[2]);
                env->SetFogColor(newFog);
                lightEdited = true;
            }
        }
    }

    // ---- Lights ----
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Light Gizmos", &showLightGizmos);
        ImGui::Separator();

        int lightIdx = 0;
        auto drawLightUI = [&](RndLight* light, const char* listName) {
            if (!light) return;
            ImGui::PushID(lightIdx);

            const char* typeName = RndLight::TypeToStr(light->GetType());
            bool showing = light->Showing();

            // Header: checkbox + name + type
            char label[256];
            snprintf(label, sizeof(label), "[%s] %s (%s)", listName, light->Name(), typeName);

            if (ImGui::Checkbox("##show", &showing)) {
                light->SetShowing(showing);
                lightEdited = true;
            }
            ImGui::SameLine();

            bool open = ImGui::TreeNode(label);
            if (open) {
                // Color
                Hmx::Color lc = light->GetColor();
                float col[3] = { lc.red, lc.green, lc.blue };
                if (ImGui::ColorEdit3("Color", col)) {
                    Hmx::Color newCol;
                    newCol.Set(col[0], col[1], col[2]);
                    light->SetColor(newCol);
                    lightEdited = true;
                }

                if (light->GetType() == RndLight::kDirectional) {
                    // Direction (from Y axis of world transform)
                    const Transform& lxfm = light->WorldXfm();
                    float dir[3] = { lxfm.m.y.x, lxfm.m.y.y, lxfm.m.y.z };
                    if (ImGui::DragFloat3("Direction", dir, 0.01f, -1.0f, 1.0f)) {
                        // Normalize
                        float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
                        if (len > 0.001f) {
                            dir[0] /= len; dir[1] /= len; dir[2] /= len;
                        }
                        Transform xfm = light->WorldXfm();
                        xfm.m.y.Set(dir[0], dir[1], dir[2]);
                        // Rebuild orthonormal basis
                        Vector3 fwd(dir[0], dir[1], dir[2]);
                        Vector3 up(0, 0, 1);
                        if (fabsf(dir[2]) > 0.9f) up.Set(1, 0, 0);
                        Cross(fwd, up, xfm.m.x);
                        Normalize(xfm.m.x, xfm.m.x);
                        Cross(xfm.m.x, fwd, xfm.m.z);
                        light->SetLocalXfm(xfm);
                        lightEdited = true;
                    }
                } else {
                    // Position
                    const Transform& lxfm = light->WorldXfm();
                    float pos[3] = { lxfm.v.x, lxfm.v.y, lxfm.v.z };
                    if (ImGui::DragFloat3("Position", pos, 0.5f)) {
                        Transform xfm = light->WorldXfm();
                        xfm.v.Set(pos[0], pos[1], pos[2]);
                        light->SetLocalXfm(xfm);
                        lightEdited = true;
                    }

                    // Range
                    float range = light->Range();
                    if (ImGui::DragFloat("Range", &range, 1.0f, 0.0f, 5000.0f)) {
                        light->SetRange(range);
                        lightEdited = true;
                    }

                    // Falloff
                    float falloff = light->FalloffStart();
                    if (ImGui::DragFloat("Falloff Start", &falloff, 0.1f, 0.0f, range)) {
                        light->SetFalloffStart(falloff);
                        lightEdited = true;
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
            lightIdx++;
        };

        // Real lights
        {
            ObjPtrList<RndLight>& realLights = env->LightsReal();
            int realCount = 0;
            for (auto it = realLights.begin(); it != realLights.end(); ++it) realCount++;
            char realLabel[64];
            snprintf(realLabel, sizeof(realLabel), "Real Lights (%d)", realCount);
            if (ImGui::TreeNode(realLabel)) {
                for (auto it = realLights.begin(); it != realLights.end(); ++it) {
                    drawLightUI(*it, "R");
                }
                ImGui::TreePop();
            }
        }

        // Approx lights
        {
            ObjPtrList<RndLight>& approxLights = env->LightsApprox();
            int approxCount = 0;
            for (auto it = approxLights.begin(); it != approxLights.end(); ++it) approxCount++;
            char approxLabel[64];
            snprintf(approxLabel, sizeof(approxLabel), "Approx Lights (%d)", approxCount);
            if (ImGui::TreeNode(approxLabel)) {
                for (auto it = approxLights.begin(); it != approxLights.end(); ++it) {
                    drawLightUI(*it, "A");
                }
                ImGui::TreePop();
            }
        }

        // Synthetic lights
        if (mScene && !mScene->syntheticLights.empty()) {
            char synthLabel[64];
            snprintf(synthLabel, sizeof(synthLabel), "Synthetic Lights (%d)",
                     (int)mScene->syntheticLights.size());
            if (ImGui::TreeNodeEx(synthLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
                for (RndLight* light : mScene->syntheticLights) {
                    drawLightUI(light, "S");
                }
                ImGui::TreePop();
            }
        }
    }

    // ---- Renderer Info ----
    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Window: %dx%d", TheRnd.Width(), TheRnd.Height());
        if (mCreatedDefaultEnv) {
            ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "Using auto-created debug environment");
        }
        if (ImGui::Button("Reload Shaders")) {
            if (gWgpuRnd->Pipelines().ReloadShaders()) {
                ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "OK");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(edits standard_wgsl.inc)");
        if (ImGui::Button("Force Scene Uniform Re-upload")) {
            lightEdited = true;
        }
    }

    ImGui::End();

    // Invalidate scene uniform cache if any light was edited
    if (lightEdited && gWgpuRnd) {
        gWgpuRnd->InvalidateSceneUniforms();
    }
}

void ViewerDebugUI::DrawLightGizmos(RndCam* cam) {
    if (!showLightGizmos || !cam) return;

    RndEnviron* env = mScene ? mScene->FindEnvironment() : RndEnviron::Current();
    if (!env) env = RndEnviron::Current();
    if (!env) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    auto drawGizmo = [&](RndLight* light) {
        if (!light || !light->Showing()) return;
        if (light->GetType() == RndLight::kDirectional) return; // no position

        const Transform& lxfm = light->WorldXfm();
        Vector3 worldPos(lxfm.v.x, lxfm.v.y, lxfm.v.z);

        float sx, sy;
        if (!ProjectToScreen(worldPos, cam, sx, sy)) return;

        const Hmx::Color& lc = light->GetColor();
        float maxC = lc.red;
        if (lc.green > maxC) maxC = lc.green;
        if (lc.blue > maxC) maxC = lc.blue;
        float scale = maxC > 1.0f ? 1.0f / maxC : 1.0f;

        ImU32 col = IM_COL32(
            (int)(lc.red * scale * 255),
            (int)(lc.green * scale * 255),
            (int)(lc.blue * scale * 255),
            200
        );
        ImU32 outlineCol = IM_COL32(255, 255, 255, 180);

        float radius = 8.0f;
        drawList->AddCircleFilled(ImVec2(sx, sy), radius + 1.5f, outlineCol);
        drawList->AddCircleFilled(ImVec2(sx, sy), radius, col);

        // Label
        const char* name = light->Name();
        if (name && name[0]) {
            drawList->AddText(ImVec2(sx + radius + 4, sy - 6), IM_COL32(255, 255, 255, 200), name);
        }
    };

    ObjPtrList<RndLight>& realLights = env->LightsReal();
    for (auto it = realLights.begin(); it != realLights.end(); ++it) {
        drawGizmo(*it);
    }

    ObjPtrList<RndLight>& approxLights = env->LightsApprox();
    for (auto it = approxLights.begin(); it != approxLights.end(); ++it) {
        drawGizmo(*it);
    }

    if (mScene) {
        for (RndLight* light : mScene->syntheticLights) {
            drawGizmo(light);
        }
    }
}
