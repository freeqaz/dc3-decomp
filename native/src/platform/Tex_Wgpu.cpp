// DC3 Native Port — WebGPU Texture Upload
// Provides real RndTex::PresyncBitmap() / SyncBitmap() implementations
// that upload bitmap data to GPU textures via TextureConvert.
// Overrides weak stubs in engine_stubs_generated.cpp.

#include "platform/Rnd_Wgpu.h"
#include "gfx/TextureConvert.h"
#include "rndobj/Tex.h"
#include "rndobj/CubeTex.h"
#include "rndobj/Bitmap.h"

#include <unordered_map>
#include <cstdio>

// ============================================================================
// GPU texture side table — maps RndTex* to GPU resources
// ============================================================================

struct GpuTexData {
    wgpu::Texture texture;
    wgpu::TextureView view;
    bool uploaded = false;
    const uint8_t* lastPixelPtr = nullptr;  // detect bitmap data changes
    uint32_t pixelFingerprint = 0;          // quick content check
};

static std::unordered_map<RndTex*, GpuTexData> sTexGpuData;

// Public accessor — used by Mesh_Wgpu.cpp to get texture view for binding
wgpu::TextureView GetGpuTexView(RndTex* tex) {
    if (!tex) return wgpu::TextureView();
    auto it = sTexGpuData.find(tex);
    if (it != sTexGpuData.end() && it->second.uploaded) {
        return it->second.view;
    }
    return wgpu::TextureView();
}

// ============================================================================
// RndTex::PresyncBitmap — create GPU texture from bitmap data
// ============================================================================

// Quick fingerprint of pixel data to detect content changes
static uint32_t PixelFingerprint(const uint8_t* pixels, int size) {
    if (!pixels || size < 16) return 0;
    // Sample a few positions across the data
    uint32_t h = 0;
    int step = size / 8;
    if (step < 1) step = 1;
    for (int i = 0; i < size; i += step) {
        h = h * 31 + pixels[i];
    }
    return h;
}

void RndTex::PresyncBitmap() {
    if (!gWgpuRnd) return;

    // Only process regular textures with bitmap data
    if (mBitmap.Width() <= 0 || mBitmap.Height() <= 0 || mBitmap.Bpp() <= 0) {
        return;
    }
    const uint8_t* curPixels = mBitmap.Pixels();
    if (!curPixels) return;

    // Check if already uploaded AND bitmap data hasn't changed.
    // Font textures may be uploaded before their data is loaded from
    // the .milo file — the bitmap has valid dimensions but placeholder
    // pixel data. When the real data loads, we need to re-upload.
    auto it = sTexGpuData.find(this);
    if (it != sTexGpuData.end() && it->second.uploaded) {
        uint32_t fp = PixelFingerprint(curPixels, mBitmap.PixelBytes());
        if (it->second.lastPixelPtr == curPixels && it->second.pixelFingerprint == fp)
            return; // Same data, skip
        // Data changed — re-upload
    }

    // Create GPU texture from Milo bitmap
    int numMips = 0;
    RndBitmap* mip = mBitmap.nextMip();
    while (mip) {
        numMips++;
        mip = mip->nextMip();
    }

    wgpu::Texture gpuTex = TextureConvert::CreateFromBitmap(
        gWgpuRnd->Gpu(), mBitmap, numMips);

    if (!gpuTex) {
        fprintf(stderr, "Tex_Wgpu: failed to create GPU texture for '%s' (%dx%d, %d bpp, %d mips)\n",
                Name(), mBitmap.Width(), mBitmap.Height(), mBitmap.Bpp(), numMips);
        return;
    }

    GpuTexData data;
    data.texture = gpuTex;
    data.view = gpuTex.CreateView();
    data.uploaded = true;
    data.lastPixelPtr = curPixels;
    data.pixelFingerprint = PixelFingerprint(curPixels, mBitmap.PixelBytes());

    sTexGpuData[this] = data;
}

// ============================================================================
// RndTex::SyncBitmap — finalize texture upload (Tier 1: no-op, done in Presync)
// ============================================================================

void RndTex::SyncBitmap() {
    // In Tier 1, all upload work is done in PresyncBitmap
}

// ============================================================================
// Cleanup — called when a RndTex is destroyed
// ============================================================================
// Note: RndTex destructor doesn't call us directly yet.
// For Tier 1, leaked GPU textures are acceptable (cleaned up at shutdown).
// TODO: Hook into RndTex destructor or add ref-counting.

void CleanupGpuTex(RndTex* tex) {
    sTexGpuData.erase(tex);
}

// ============================================================================
// Cube texture GPU side table — maps RndCubeTex* to GPU resources
// ============================================================================

struct GpuCubeTexData {
    wgpu::Texture texture;
    wgpu::TextureView view;
    bool uploaded = false;
};

static std::unordered_map<RndCubeTex*, GpuCubeTexData> sCubeTexGpuData;

wgpu::TextureView GetGpuCubeTexView(RndCubeTex* cubeTex) {
    if (!cubeTex || !gWgpuRnd) return wgpu::TextureView();

    auto it = sCubeTexGpuData.find(cubeTex);
    if (it != sCubeTexGpuData.end() && it->second.uploaded) {
        return it->second.view;
    }

    // Lazy upload: gather 6 face bitmaps and create cube texture
    RndBitmap* faces[6];
    bool allValid = true;
    for (int i = 0; i < 6; i++) {
        faces[i] = &cubeTex->GetBitmap((RndCubeTex::CubeFace)i);
        if (!faces[i] || faces[i]->Width() <= 0 || faces[i]->Height() <= 0 || !faces[i]->Pixels()) {
            allValid = false;
        }
    }
    if (!allValid) return wgpu::TextureView();

    wgpu::Texture gpuTex = TextureConvert::CreateCubeFromBitmaps(
        gWgpuRnd->Gpu(), faces, 6);
    if (!gpuTex) return wgpu::TextureView();

    // Create cube texture view
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.dimension = wgpu::TextureViewDimension::Cube;
    viewDesc.arrayLayerCount = 6;
    viewDesc.baseArrayLayer = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseMipLevel = 0;

    GpuCubeTexData data;
    data.texture = gpuTex;
    data.view = gpuTex.CreateView(&viewDesc);
    data.uploaded = true;
    sCubeTexGpuData[cubeTex] = data;

    return data.view;
}
