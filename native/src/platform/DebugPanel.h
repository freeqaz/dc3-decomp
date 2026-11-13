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

} // namespace DebugPanel
