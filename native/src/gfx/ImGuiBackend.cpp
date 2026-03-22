#include "gfx/ImGuiBackend.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <GLFW/glfw3.h>

static bool sInitialized = false;

void ImGuiBackend::Init(GLFWwindow* window, wgpu::Device device, wgpu::TextureFormat surfaceFmt) {
    if (sInitialized) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't save imgui.ini

    ImGui::StyleColorsDark();

    // Scale UI for readability
    ImGui::GetStyle().ScaleAllSizes(1.2f);
    io.FontGlobalScale = 1.2f;

    // GLFW backend — install_callbacks=true chains to existing callbacks
    ImGui_ImplGlfw_InitForOther(window, true);

    // WebGPU backend
    ImGui_ImplWGPU_InitInfo wgpuInfo{};
    wgpuInfo.Device = device.Get();
    wgpuInfo.RenderTargetFormat = static_cast<WGPUTextureFormat>(surfaceFmt);
    wgpuInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    wgpuInfo.NumFramesInFlight = 3;
    ImGui_ImplWGPU_Init(&wgpuInfo);

    sInitialized = true;
}

void ImGuiBackend::NewFrame() {
    if (!sInitialized) return;
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiBackend::Render(wgpu::RenderPassEncoder& pass) {
    if (!sInitialized) return;
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass.Get());
}

void ImGuiBackend::Shutdown() {
    if (!sInitialized) return;
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    sInitialized = false;
}

bool ImGuiBackend::WantCaptureMouse() {
    if (!sInitialized) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiBackend::WantCaptureKeyboard() {
    if (!sInitialized) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}
