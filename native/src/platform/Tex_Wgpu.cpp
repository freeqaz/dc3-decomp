// DC3 Native Port — WebGPU Texture Upload
// Provides real RndTex::PresyncBitmap() / SyncBitmap() implementations
// that upload bitmap data to GPU textures via TextureConvert.
// Overrides weak stubs in engine_stubs_generated.cpp.

#include "platform/Rnd_Wgpu.h"
#include "gfx/TextureConvert.h"
#include "rndobj/Tex.h"
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

void RndTex::PresyncBitmap() {
    if (!gWgpuRnd) return;

    // Only process regular textures with bitmap data
    if (mBitmap.Width() <= 0 || mBitmap.Height() <= 0) return;
    if (mBitmap.Bpp() <= 0) return;

    // Check if already uploaded
    auto it = sTexGpuData.find(this);
    if (it != sTexGpuData.end() && it->second.uploaded) return;

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
        return;
    }

    GpuTexData data;
    data.texture = gpuTex;
    data.view = gpuTex.CreateView();
    data.uploaded = true;

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
