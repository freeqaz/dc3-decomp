#pragma once

#include <webgpu/webgpu_cpp.h>

class RndTex;
class RndCubeTex;

// GPU texture view accessors (defined in Tex_Wgpu.cpp)
wgpu::TextureView GetGpuTexView(RndTex* tex);
wgpu::TextureView GetGpuTexDepthView(RndTex* tex);
wgpu::TextureView GetGpuCubeTexView(RndCubeTex* cubeTex);

// Upload raw RGBA pixel data to a render-target RndTex's GPU texture
void UploadRGBAToRndTex(RndTex* tex, const uint8_t* rgba, int w, int h);
