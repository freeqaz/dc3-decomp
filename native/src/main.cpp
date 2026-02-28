// DC3 Native Port — First Pixels
// Headless offscreen WebGPU triangle renderer via Dawn
// Renders a green triangle to an offscreen texture and saves as PPM

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static const char* kShaderSource = R"(
@vertex fn vs(@location(0) pos : vec4f) -> @builtin(position) vec4f {
    return pos;
}

@fragment fn fs() -> @location(0) vec4f {
    return vec4f(0.0, 0.8, 0.4, 1.0);
}
)";

static const float kVertexData[] = {
     0.0f,  0.5f, 0.0f, 1.0f,  // top
    -0.5f, -0.5f, 0.0f, 1.0f,  // bottom-left
     0.5f, -0.5f, 0.0f, 1.0f,  // bottom-right
};

static constexpr uint32_t kWidth = 800;
static constexpr uint32_t kHeight = 600;
static constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA8Unorm;

// Write raw RGBA pixels as a PPM file (P6 format, no extra deps needed)
bool WritePPM(const char* path, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t rowPitch) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = pixels + y * rowPitch;
        for (uint32_t x = 0; x < width; x++) {
            // RGBA -> RGB (drop alpha)
            f.write(reinterpret_cast<const char*>(row + x * 4), 3);
        }
    }
    return f.good();
}

int main(int argc, char** argv) {
    const char* outputPath = "output.ppm";
    if (argc > 1) outputPath = argv[1];

    // --- Instance ---
    static constexpr auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &kTimedWaitAny;

    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Failed to create WebGPU instance\n";
        return EXIT_FAILURE;
    }

    // --- Adapter (synchronous) ---
    wgpu::Adapter adapter;
    wgpu::RequestAdapterOptions adapterOpts{};
    instance.WaitAny(
        instance.RequestAdapter(
            &adapterOpts,
            wgpu::CallbackMode::WaitAnyOnly,
            [&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView msg) {
                if (status != wgpu::RequestAdapterStatus::Success) {
                    std::cerr << "Adapter request failed: " << msg << "\n";
                    return;
                }
                adapter = std::move(result);
            }),
        UINT64_MAX);
    if (!adapter) {
        std::cerr << "No WebGPU adapter found\n";
        return EXIT_FAILURE;
    }

    wgpu::AdapterInfo info{};
    adapter.GetInfo(&info);
    std::cout << "GPU: " << info.device << " (" << info.description << ")\n";

    // --- Device (synchronous) ---
    wgpu::Device device;
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView msg) {
            std::cerr << "Device lost: " << msg << "\n";
        });
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView msg) {
            std::cerr << "WebGPU error: " << msg << "\n";
        });

    instance.WaitAny(
        adapter.RequestDevice(
            &deviceDesc,
            wgpu::CallbackMode::WaitAnyOnly,
            [&device](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView msg) {
                if (status != wgpu::RequestDeviceStatus::Success) {
                    std::cerr << "Device request failed: " << msg << "\n";
                    return;
                }
                device = std::move(result);
            }),
        UINT64_MAX);
    if (!device) {
        std::cerr << "Failed to create WebGPU device\n";
        return EXIT_FAILURE;
    }

    wgpu::Queue queue = device.GetQueue();

    // --- Offscreen Render Target ---
    wgpu::TextureDescriptor texDesc{};
    texDesc.size = {kWidth, kHeight, 1};
    texDesc.format = kFormat;
    texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture renderTarget = device.CreateTexture(&texDesc);
    wgpu::TextureView renderView = renderTarget.CreateView();

    // --- Shader ---
    wgpu::ShaderSourceWGSL wgslSource;
    wgslSource.code = kShaderSource;

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = &wgslSource;
    wgpu::ShaderModule shaderModule = device.CreateShaderModule(&shaderDesc);

    // --- Vertex Buffer ---
    wgpu::BufferDescriptor vbDesc{};
    vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vbDesc.size = sizeof(kVertexData);
    wgpu::Buffer vertexBuffer = device.CreateBuffer(&vbDesc);
    queue.WriteBuffer(vertexBuffer, 0, kVertexData, sizeof(kVertexData));

    // --- Render Pipeline ---
    wgpu::VertexAttribute vertexAttr{};
    vertexAttr.format = wgpu::VertexFormat::Float32x4;
    vertexAttr.offset = 0;
    vertexAttr.shaderLocation = 0;

    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = 4 * sizeof(float);
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &vertexAttr;

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kFormat;

    wgpu::FragmentState fragment{};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.fragment = &fragment;

    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);

    // --- Render ---
    wgpu::RenderPassColorAttachment colorAttachment{};
    colorAttachment.view = renderView;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = {0.05, 0.05, 0.05, 1.0};

    wgpu::RenderPassDescriptor renderPassDesc{};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    {
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPassDesc);
        pass.SetPipeline(pipeline);
        pass.SetVertexBuffer(0, vertexBuffer);
        pass.Draw(3);
        pass.End();
    }

    // --- Copy texture to readback buffer ---
    // Row pitch must be aligned to 256 bytes
    uint32_t bytesPerRow = kWidth * 4;
    uint32_t alignedBytesPerRow = (bytesPerRow + 255) & ~255u;

    wgpu::BufferDescriptor readbackDesc{};
    readbackDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    readbackDesc.size = alignedBytesPerRow * kHeight;
    wgpu::Buffer readbackBuffer = device.CreateBuffer(&readbackDesc);

    wgpu::TexelCopyTextureInfo src{};
    src.texture = renderTarget;

    wgpu::TexelCopyBufferInfo dst{};
    dst.buffer = readbackBuffer;
    dst.layout.bytesPerRow = alignedBytesPerRow;
    dst.layout.rowsPerImage = kHeight;

    wgpu::Extent3D copySize = {kWidth, kHeight, 1};
    encoder.CopyTextureToBuffer(&src, &dst, &copySize);

    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);

    // --- Map and read back ---
    bool mapDone = false;
    bool mapSuccess = false;
    instance.WaitAny(
        readbackBuffer.MapAsync(
            wgpu::MapMode::Read, 0, readbackDesc.size,
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                mapDone = true;
                mapSuccess = (status == wgpu::MapAsyncStatus::Success);
            }),
        UINT64_MAX);

    if (!mapSuccess) {
        std::cerr << "Failed to map readback buffer\n";
        return EXIT_FAILURE;
    }

    const uint8_t* pixels = static_cast<const uint8_t*>(
        readbackBuffer.GetConstMappedRange(0, readbackDesc.size));

    if (WritePPM(outputPath, pixels, kWidth, kHeight, alignedBytesPerRow)) {
        std::cout << "Saved " << kWidth << "x" << kHeight << " image to " << outputPath << "\n";
    } else {
        std::cerr << "Failed to write " << outputPath << "\n";
        readbackBuffer.Unmap();
        return EXIT_FAILURE;
    }

    readbackBuffer.Unmap();
    std::cout << "Done!\n";
    return EXIT_SUCCESS;
}
