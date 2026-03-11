#pragma once

#include <webgpu/webgpu_cpp.h>

class RndTex;
class RndCubeTex;

// GPU texture view accessors (defined in Tex_Wgpu.cpp)
wgpu::TextureView GetGpuTexView(RndTex* tex);
wgpu::TextureView GetGpuCubeTexView(RndCubeTex* cubeTex);
