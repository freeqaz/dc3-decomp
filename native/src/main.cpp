// DC3 Native Port — WebGPU Window/Headless Test
// Windowed: opens a GLFW window, presents frames clearing to cornflower blue
// Headless: renders one frame to offscreen texture, saves as PPM

#include "gfx/GpuDevice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

static bool WritePPM(const char* path, const uint8_t* pixels, int width, int height) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t* px = pixels + (y * width + x) * 4;
            f.write(reinterpret_cast<const char*>(px), 3); // RGB, drop A
        }
    }
    return f.good();
}

int main(int argc, char** argv) {
    bool headless = false;
    const char* outputPath = "output.ppm";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) headless = true;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outputPath = argv[++i];
    }

    GpuDevice gpu;
    GpuDeviceDesc desc{};
    desc.headless = headless;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "DC3 Native — WebGPU";

    if (!gpu.Init(desc)) {
        fprintf(stderr, "Failed to initialize GPU device\n");
        return EXIT_FAILURE;
    }

    if (headless) {
        printf("Headless mode: rendering one frame...\n");

        wgpu::TextureView frameView = gpu.AcquireHeadlessFrame();

        wgpu::RenderPassColorAttachment colorAtt{};
        colorAtt.view = frameView;
        colorAtt.loadOp = wgpu::LoadOp::Clear;
        colorAtt.storeOp = wgpu::StoreOp::Store;
        colorAtt.clearValue = {0.392, 0.584, 0.929, 1.0};

        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAtt;

        wgpu::CommandEncoder encoder = gpu.Device().CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rpDesc);
        pass.End();

        wgpu::CommandBuffer cmd = encoder.Finish();
        gpu.Queue().Submit(1, &cmd);

        size_t pixelSize = desc.width * desc.height * 4;
        std::vector<uint8_t> pixels(pixelSize);
        if (gpu.ReadbackHeadlessFrame(pixels.data(), pixelSize)) {
            if (WritePPM(outputPath, pixels.data(), desc.width, desc.height)) {
                printf("Saved %dx%d to %s\n", desc.width, desc.height, outputPath);
            } else {
                fprintf(stderr, "Failed to write %s\n", outputPath);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Failed to readback headless frame\n");
            return EXIT_FAILURE;
        }
    } else {
        printf("Windowed mode: close the window to exit.\n");

        while (!gpu.ShouldClose()) {
            gpu.PollEvents();

            wgpu::TextureView frameView = gpu.AcquireNextFrame();
            if (!frameView) continue;

            wgpu::RenderPassColorAttachment colorAtt{};
            colorAtt.view = frameView;
            colorAtt.loadOp = wgpu::LoadOp::Clear;
            colorAtt.storeOp = wgpu::StoreOp::Store;
            colorAtt.clearValue = {0.392, 0.584, 0.929, 1.0};

            wgpu::RenderPassDescriptor rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments = &colorAtt;

            wgpu::CommandEncoder encoder = gpu.Device().CreateCommandEncoder();
            wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rpDesc);
            pass.End();

            wgpu::CommandBuffer cmd = encoder.Finish();
            gpu.Queue().Submit(1, &cmd);

            gpu.PresentFrame();
        }
    }

    gpu.Shutdown();
    printf("Clean exit.\n");
    return EXIT_SUCCESS;
}
