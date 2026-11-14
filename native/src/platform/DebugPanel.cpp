#include "platform/DebugPanel.h"
#include "platform/NativeSettings.h"
#include "platform/NativeNotice.h"
#include "rndobj/Overlay.h"
#include <imgui.h>
#include <map>
#include <string>
#include <vector>

static bool sVisible = false;

void DebugPanel::Init() {}

void DebugPanel::Draw() {
    if (!sVisible) return;

    NativeSettings &s = NativeSettings::Get();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Camera Debug (~)", &sVisible)) {
        ImGui::End();
        return;
    }

    // Camera Blend
    if (ImGui::CollapsingHeader("Camera Blend", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Blend", &s.cameraBlend);
        ImGui::SliderFloat("Same-cat frames", &s.blendFramesSame, 0.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("Cross-cat frames", &s.blendFramesCross, 0.0f, 60.0f, "%.1f");
    }

    // FOV
    if (ImGui::CollapsingHeader("FOV")) {
        ImGui::SliderFloat("FOV Scale", &s.fovScale, 0.5f, 2.0f, "%.3f");
        if (ImGui::Button("Reset FOV")) s.fovScale = 1.0f;
    }

    // Near/Far Plane
    if (ImGui::CollapsingHeader("Clip Planes")) {
        ImGui::DragFloat("Near Override", &s.nearPlaneOverride, 0.01f, -1.0f, 100.0f, "%.2f");
        ImGui::DragFloat("Far Override", &s.farPlaneOverride, 1.0f, -1.0f, 10000.0f, "%.0f");
        ImGui::TextDisabled("-1 = use per-camera defaults");
    }

    // Aspect Ratio
    if (ImGui::CollapsingHeader("Aspect Ratio")) {
        ImGui::DragFloat("Aspect Override", &s.aspectOverride, 0.01f, -1.0f, 3.0f, "%.3f");
        ImGui::TextDisabled("-1 = auto from window size");
    }

    // Camera Offsets
    if (ImGui::CollapsingHeader("Camera Offset")) {
        ImGui::SliderFloat("Forward", &s.camForwardOffset, -5.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Height", &s.camHeightOffset, -5.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Lateral", &s.camLateralOffset, -5.0f, 5.0f, "%.2f");
        if (ImGui::Button("Reset Offsets")) {
            s.camForwardOffset = 0.0f;
            s.camHeightOffset = 0.0f;
            s.camLateralOffset = 0.0f;
        }
    }

    // Debug
    if (ImGui::CollapsingHeader("Debug")) {
        ImGui::Checkbox("Camera Debug Log", &s.cameraDebug);
    }

    ImGui::End();
}

bool DebugPanel::IsVisible() { return sVisible; }
void DebugPanel::Toggle() { sVisible = !sVisible; }
void DebugPanel::SetVisible(bool v) { sVisible = v; }

// Player-facing notices (NativeNotice): a banner across the top of the screen,
// then a one-line reminder in the bottom-right corner while the notice stays
// posted.  An input-transparent ImGui window, so it never takes the mouse or
// keyboard from the game, and drawn whether or not the debug panel is open.
void DebugPanel::DrawNotices() {
    NativeNotice::View v;
    if (!NativeNotice::Current(v))
        return;
    const ImGuiIO &io = ImGui::GetIO();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav;
    if (v.banner) {
        float width = io.DisplaySize.x * 0.7f;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.06f),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, io.DisplaySize.y));
        ImGui::SetNextWindowBgAlpha(0.85f * v.bannerAlpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, v.bannerAlpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
        if (ImGui::Begin("##native_notice_banner", nullptr, flags)) {
            ImGui::SetWindowFontScale(1.5f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", v.text.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    } else if (v.corner) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, io.DisplaySize.y - 12.0f),
            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.55f);
        if (ImGui::Begin("##native_notice_corner", nullptr, flags)) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", v.shortText.c_str());
        }
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// The original's RndOverlays (DC3_RND_OVERLAYS).  The image draws them in the
// top-left corner from Rnd::EndDrawing; WgpuRnd's EndDrawing never did, so
// cheat_display (every MILO_NOTIFY at notify_level 1, ui/cheats_funcs.dta) and
// the joypad quick cheats' overlays (LB+LT / RB+RT + a button, config/
// cheats.dta) showed nothing natively.
// ---------------------------------------------------------------------------
bool Dc3EnvFlag(const char *name, bool defaultOn); // System_Native.cpp

namespace {
struct OverlayLine {
    std::string overlay, text;
};
std::vector<OverlayLine> sOverlayLines;
std::map<std::string, std::string> sOverlayLast; // overlay -> its text last frame

bool RndOverlaysEnabled() {
    static const bool on = Dc3EnvFlag("DC3_RND_OVERLAYS", false);
    return on;
}

void CollectLine(void *, const char *overlay, const char *line) {
    sOverlayLines.push_back({overlay, line});
}
} // namespace

void DebugPanel::PollRndOverlays() {
    if (!RndOverlaysEnabled()) return;
    sOverlayLines.clear();
    RndOverlay::NativeCollect(CollectLine, nullptr);
    // Print an overlay's text on stderr when it changes, so a terminal (and a
    // test, and a NORENDER run with no ImGui) sees what the corner shows.
    std::map<std::string, std::string> now;
    for (const OverlayLine &l : sOverlayLines) {
        std::string &t = now[l.overlay];
        if (!t.empty()) t += "\n";
        t += l.text;
    }
    for (const auto &kv : now) {
        auto it = sOverlayLast.find(kv.first);
        if (it != sOverlayLast.end() && it->second == kv.second) continue;
        std::string esc;
        for (char c : kv.second) esc += (c == '\n') ? std::string(" | ") : std::string(1, c);
        fprintf(stderr, "DC3 OVERLAY [%s] %s\n", kv.first.c_str(), esc.c_str());
    }
    sOverlayLast.swap(now);
}

void DebugPanel::DrawRndOverlays() {
    if (!RndOverlaysEnabled() || sOverlayLines.empty()) return;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav;
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.02f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##rnd_overlays", nullptr, flags)) {
        for (const OverlayLine &l : sOverlayLines)
            ImGui::TextUnformatted(l.text.c_str());
    }
    ImGui::End();
}
