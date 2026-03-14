// DC3 Native Port — Material Setup
// Fills MaterialUniforms, resolves texture views, and builds sampler descriptors
// for both primary materials and multi-pass materials.

#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "gfx/FrameCapture.h"
#include "rndobj/Mat.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Env.h"
#include "rndobj/CubeTex.h"
#include "rndobj/Lit.h"
#include "math/Mtx.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

// Simple render mode (MILO_SIMPLE_RENDER=1): skip multiply override, force prelit,
// minimal material processing. For isolating shader/blend regressions.
static bool sSimpleRender = false;
static bool sSimpleRenderChecked = false;
static bool IsSimpleRender() {
    if (!sSimpleRenderChecked) {
        sSimpleRender = (getenv("MILO_SIMPLE_RENDER") != nullptr);
        sSimpleRenderChecked = true;
        if (sSimpleRender) printf("DC3 Native: SIMPLE RENDER MODE enabled\n");
    }
    return sSimpleRender;
}

// Helper: resolve a material texture map, triggering upload if needed
static wgpu::TextureView ResolveMap(RndTex* tex, wgpu::TextureView& fallback) {
    if (!tex) return fallback;
    tex->PresyncBitmap();
    wgpu::TextureView v = GetGpuTexView(tex);
    return v ? v : fallback;
}

MaterialParams BuildMaterialParams(RndMat* mat, bool isTextMesh) {
    MaterialParams result{};
    MaterialUniforms& matUni = result.uniforms;
    uint32_t heuristics = 0;

    // --- Color + alpha ---
    const Hmx::Color& matColor = mat->GetColor();
    matUni.color[0] = matColor.red;
    matUni.color[1] = matColor.green;
    matUni.color[2] = matColor.blue;
    matUni.color[3] = matColor.alpha;

    BaseMaterial::Blend matBlend = mat->GetBlend();

    if (mat->GetAlphaCut()) {
        matUni.alphaThreshold = mat->GetAlphaThreshold() / 255.0f;
    } else {
        matUni.alphaThreshold = 0.0f;
    }

    // --- Specular ---
    const Hmx::Color& spec = mat->GetSpecularRGB();
    float specPower = spec.alpha > 0.0f ? spec.alpha : 0.0f;
    float specScale = 1.0f;
    // Per-pixel-lit materials without normal map: the Xbox shader uses normal map
    // alpha as specular mask. Without it, attenuate specular and raise min power
    // to avoid unrealistically broad sheen across entire surfaces.
    if (specPower > 0.0f && specPower < 32.0f) {
        specPower = 32.0f;  // tighten the specular lobe
        specScale = 0.4f;   // reduce intensity
        heuristics |= kHeuristicSpecularClamp;
    }
    matUni.specularColor[0] = spec.red * specScale;
    matUni.specularColor[1] = spec.green * specScale;
    matUni.specularColor[2] = spec.blue * specScale;
    matUni.specularColor[3] = 1.0f;
    matUni.specularPower = specPower;

    // --- Emissive ---
    // Only applies when an emissive map texture exists.
    // Without a map, emissiveMultiplier defaults to 1.0 which would add
    // the full diffuse color as self-illumination (completely wrong)
    matUni.emissiveMultiplier = mat->GetEmissiveMap() ? mat->GetEmissiveMultiplier() : 0.0f;
    if (!mat->GetEmissiveMap()) heuristics |= kHeuristicEmissiveGuard;

    // --- Rim lighting ---
    const Hmx::Color& rim = mat->GetRimRGB();
    matUni.rimColor[0] = rim.red;
    matUni.rimColor[1] = rim.green;
    matUni.rimColor[2] = rim.blue;
    matUni.rimColor[3] = rim.alpha > 0.0f ? rim.alpha : 0.0f;
    matUni.rimLightUnder = mat->GetRimLightUnder() ? 1.0f : 0.0f;

    // --- Intensify ---
    matUni.intensify = mat->GetIntensify() ? 2.0f : 1.0f;

    // --- Shader variation (skin, hair, etc.) ---
    // DC3 skin materials often have shader_variation=0 but use "_skin" in the name.
    // Detect skin by either the explicit flag or name convention.
    ShaderVariation variation = mat->GetShaderVariation();
    if (variation == kShaderVariationNone) {
        const char* matName = mat->Name();
        if (strstr(matName, "_skin") || strstr(matName, "_head")) {
            variation = kShaderVariationSkin;
            heuristics |= kHeuristicSkinNameDetect;
        }
    }
    matUni.shaderVariation = (float)variation;

    // --- Second specular lobe (used by skin shader) ---
    const Hmx::Color& spec2 = mat->GetSpecular2RGB();
    matUni.specular2Color[0] = spec2.red;
    matUni.specular2Color[1] = spec2.green;
    matUni.specular2Color[2] = spec2.blue;
    matUni.specular2Color[3] = spec2.alpha > 0.0f ? spec2.alpha : 0.0f;

    // --- Diffuse texture ---
    RndTex* diffTex = mat->GetDiffuseTex();
    wgpu::TextureView diffuseTexView;
    if (diffTex) {
        diffTex->PresyncBitmap();
        diffuseTexView = GetGpuTexView(diffTex);
    }

    if (diffuseTexView) {
        matUni.useTexture = 1.0f;
    } else {
        matUni.useTexture = 0.0f;
        diffuseTexView = gWgpuRnd->WhiteTexView();
    }

    // --- Normal map and additional material properties ---
    matUni.deNormal = mat->GetDeNormal();
    matUni.hasNormalMap = mat->NormalMap() ? 1.0f : 0.0f;
    matUni.anisotropy = mat->GetAnisotropy();

    // --- Per-material fog ---
    BaseMaterial::Blend blend = mat->GetBlend();
    bool allowFog = mat->GetFog() &&
        blend != BaseMaterial::kBlendDest && blend != BaseMaterial::kBlendAdd &&
        blend != BaseMaterial::kBlendSubtract && blend != BaseMaterial::kBlendSrcAlphaAdd;
    matUni.materialFogEnabled = allowFog ? 1.0f : 0.0f;
    if (!allowFog && mat->GetFog()) heuristics |= kHeuristicFogBlendCheck;

    // HACK DISABLED: Auto-detect fullbright UI
    // Was: scan environment lights, force fullbright for UI panels with zero ambient
    // and 0-1 directional lights. Testing showed UI renders correctly without this
    // heuristic — materials are already marked prelit where needed.
    // Once menus are finished and working, we can remove this code.
    bool forcePrelit = IsSimpleRender();
#if 0
    if (!forcePrelit && !mat->Prelit() && !isTextMesh) {
        RndEnviron* env = RndEnviron::Current();
        if (env) {
            const Hmx::Color& amb = env->AmbientColor();
            if (amb.red < 0.01f && amb.green < 0.01f && amb.blue < 0.01f) {
                int numDirLights = 0;
                ObjPtrList<RndLight>& approx = env->LightsApprox();
                for (auto it = approx.begin(); it != approx.end(); ++it) {
                    if (*it && (*it)->Showing() && (*it)->GetType() == RndLight::kDirectional)
                        numDirLights++;
                }
                if (numDirLights <= 1) {
                    forcePrelit = true;
                    heuristics |= kHeuristicAutoPrelit;
                }
            }
        }
    }
#endif
    if (isTextMesh) heuristics |= kHeuristicTextMeshDetect;
    matUni.prelit = (mat->Prelit() || isTextMesh || forcePrelit) ? 1.0f : 0.0f;
    matUni.useAlphaAsRGB = isTextMesh ? 1.0f : 0.0f;
    if (isTextMesh) {
        heuristics |= kHeuristicTextAlphaAsRGB;
    }

    // --- Detail normal map ---
    matUni.normDetailTiling = mat->GetNormDetailTiling();
    matUni.normDetailStrength = mat->GetNormDetailStrength();
    matUni.hasNormDetailMap = mat->GetNormDetailMap() ? 1.0f : 0.0f;

    // --- TexGen mode and transform ---
    matUni.texGenMode = (float)mat->GetTexGen();
    if (mat->GetTexGen() == kTexGenXfm || mat->GetTexGen() == kTexGenXfmOrigin ||
        mat->GetTexGen() == kTexGenProjected) {
        const Transform& xfm = mat->TexXfm();
        matUni.texXfmRow0[0] = xfm.m.x.x; matUni.texXfmRow0[1] = xfm.m.x.y;
        matUni.texXfmRow0[2] = xfm.v.x;   matUni.texXfmRow0[3] = xfm.v.z;
        matUni.texXfmRow1[0] = xfm.m.y.x; matUni.texXfmRow1[1] = xfm.m.y.y;
        matUni.texXfmRow1[2] = xfm.v.y;   matUni.texXfmRow1[3] = 0.0f;
    }

    // --- Resolve all material texture views ---
    WgpuRnd::MaterialTexViews& texViews = result.texViews;
    texViews.diffuse = diffuseTexView;

    texViews.normal   = ResolveMap(mat->NormalMap(),      gWgpuRnd->FlatNormalTexView());
    texViews.specular = ResolveMap(mat->GetSpecularMap(), gWgpuRnd->WhiteTexView());

    texViews.emissive = ResolveMap(mat->GetEmissiveMap(), gWgpuRnd->BlackTexView());
    texViews.rim      = ResolveMap(mat->GetRimMap(),      gWgpuRnd->WhiteTexView());

    // Detail normal map
    texViews.normDetail = ResolveMap(mat->GetNormDetailMap(), gWgpuRnd->FlatNormalTexView());

    // --- Environment cube map ---
    RndCubeTex* environMap = mat->GetEnvironMap();
    if (environMap) {
        wgpu::TextureView cubeView = GetGpuCubeTexView(environMap);
        texViews.environCube = cubeView ? cubeView : gWgpuRnd->BlackCubeTexView();
        matUni.environMapStrength = 1.0f;
        matUni.environMapFalloff = mat->GetEnvironMapFalloff() ? 1.0f : 0.0f;
        matUni.environMapSpecMask = mat->GetEnvironMapSpecMask() ? 1.0f : 0.0f;
    } else {
        texViews.environCube = gWgpuRnd->BlackCubeTexView();
        matUni.environMapStrength = 0.0f;
    }

    // --- Sampler descriptors ---
    SamplerDesc& sampDesc = result.samplerDesc;
    switch (mat->GetTexWrap()) {
    case kTexWrapClamp:
        sampDesc.addressU = wgpu::AddressMode::ClampToEdge;
        sampDesc.addressV = wgpu::AddressMode::ClampToEdge;
        break;
    case kTexWrapRepeat:
        sampDesc.addressU = wgpu::AddressMode::Repeat;
        sampDesc.addressV = wgpu::AddressMode::Repeat;
        break;
    case kTexWrapMirror:
        sampDesc.addressU = wgpu::AddressMode::MirrorRepeat;
        sampDesc.addressV = wgpu::AddressMode::MirrorRepeat;
        break;
    default:
        sampDesc.addressU = wgpu::AddressMode::ClampToEdge;
        sampDesc.addressV = wgpu::AddressMode::ClampToEdge;
        break;
    }

    // Map sampler -- always repeat for tiled texture maps
    result.mapSamplerDesc.addressU = wgpu::AddressMode::Repeat;
    result.mapSamplerDesc.addressV = wgpu::AddressMode::Repeat;

    result.heuristics = heuristics;
    return result;
}

MaterialParams BuildPassMaterialParams(BaseMaterial* nextPass) {
    MaterialParams result{};
    MaterialUniforms& npMatUni = result.uniforms;

    // --- Color ---
    const Hmx::Color& npc = nextPass->GetColor();
    npMatUni.color[0] = npc.red; npMatUni.color[1] = npc.green;
    npMatUni.color[2] = npc.blue; npMatUni.color[3] = npc.alpha;
    npMatUni.alphaThreshold = nextPass->GetAlphaCut() ? nextPass->GetAlphaThreshold() / 255.0f : 0.0f;

    // --- Specular ---
    const Hmx::Color& nps = nextPass->GetSpecularRGB();
    float npSpecPower = nps.alpha > 0.0f ? nps.alpha : 0.0f;
    npMatUni.specularPower = npSpecPower;
    npMatUni.specularColor[0] = nps.red; npMatUni.specularColor[1] = nps.green;
    npMatUni.specularColor[2] = nps.blue; npMatUni.specularColor[3] = 1.0f;

    // --- Other properties ---
    npMatUni.emissiveMultiplier = nextPass->GetEmissiveMap() ? nextPass->GetEmissiveMultiplier() : 0.0f;
    npMatUni.intensify = nextPass->GetIntensify() ? 2.0f : 1.0f;
    npMatUni.deNormal = nextPass->GetDeNormal();
    npMatUni.hasNormalMap = nextPass->NormalMap() ? 1.0f : 0.0f;
    npMatUni.prelit = nextPass->Prelit() ? 1.0f : 0.0f;
    npMatUni.texGenMode = (float)nextPass->GetTexGen();

    // --- Resolve textures ---
    WgpuRnd::MaterialTexViews& npTexViews = result.texViews;

    // Diffuse: no PresyncBitmap needed for multi-pass (already synced by primary pass)
    RndTex* npDiffTex = nextPass->GetDiffuseTex();
    wgpu::TextureView npDiffuse = npDiffTex ? GetGpuTexView(npDiffTex) : wgpu::TextureView{};
    if (npDiffuse) {
        npMatUni.useTexture = 1.0f;
        npTexViews.diffuse = npDiffuse;
    } else {
        npMatUni.useTexture = 0.0f;
        npTexViews.diffuse = gWgpuRnd->WhiteTexView();
    }

    npTexViews.normal     = ResolveMap(nextPass->NormalMap(),      gWgpuRnd->FlatNormalTexView());
    npTexViews.specular   = ResolveMap(nextPass->GetSpecularMap(), gWgpuRnd->WhiteTexView());
    npTexViews.emissive   = ResolveMap(nextPass->GetEmissiveMap(), gWgpuRnd->BlackTexView());
    npTexViews.rim        = ResolveMap(nextPass->GetRimMap(),      gWgpuRnd->WhiteTexView());
    npTexViews.environCube = gWgpuRnd->BlackCubeTexView();
    npTexViews.normDetail = ResolveMap(nextPass->GetNormDetailMap(), gWgpuRnd->FlatNormalTexView());

    // Multi-pass materials don't set their own sampler -- caller reuses primary material's sampler
    result.heuristics = 0;
    return result;
}
