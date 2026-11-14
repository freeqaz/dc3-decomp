#pragma once

// ImGui-based debug overlay for dc3-native.
// Provides real-time sliders for NativeSettings camera parameters.
// Toggle with backtick (~) key or via DTA {native_settings toggle_debug_panel}.

namespace DebugPanel {

void Init();
void Draw();        // Call between ImGuiBackend::NewFrame() and ImGui::Render()
bool IsVisible();
void Toggle();
void SetVisible(bool v);
void DrawNotices(); // NativeNotice banner/corner line; same call window as Draw()
// DC3_RND_OVERLAYS (dev profile default): the original's RndOverlays
// (cheat_display, the quick-cheat framerate/stats/timer overlays ...).
// PollRndOverlays runs every frame inside BeginDrawing/EndDrawing, with or
// without ImGui (it also prints `DC3 OVERLAY [<name>] <line>` on stderr when an
// overlay's text changes); DrawRndOverlays draws what it collected, in the
// ImGui frame.
void PollRndOverlays();
void DrawRndOverlays();

} // namespace DebugPanel
