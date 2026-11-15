// A DC3 material's tex_wrap clamps its other maps as it clamps its diffuse
// (np-gfx-sharp item 8, rndshape::kMapsFollowTexWrap).
//
// NgMat::SetBasicState (src/system/rndobj/Mat_NG.cpp) sets
// sTexWrapClampModes[mTexWrap] on samplers 0, 6, 1, 2 and 3 -- diffuse,
// diffuse2, normal, specular and emissive -- and only the detail normal map
// (sampler 0xe) is set to wrap. Native sampled every map but the diffuse
// through an always-repeat sampler (MaterialParams::mapSamplerDesc), so
// streetside's peak stage edge (streetside_peek_blue.mat: tex_wrap clamp,
// emissive stripe10.tex at 0.3) drew wrapped copies of its stripe above and
// below the strip; the alpha fringe fade had been hiding them, because the
// clamped diffuse's alpha is 0 there. The map sampler is shared with the
// detail normal and rim maps, so a material that has either keeps it
// repeating.
//
// Sabotage tried: the kMapsFollowTexWrap block removed from
// BuildMaterialParams -> AClampMaterialClampsItsMaps and
// AMirrorMaterialMirrorsItsMaps fail (Repeat); the detail / rim condition
// dropped -> ADetailOrRimMapKeepsTheMapsRepeating fails (ClampToEdge).

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "platform/LookProfile.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"

#include "obj/Data.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"

static_assert(rndshape::kMapsFollowTexWrap, "DC3's maps follow the material's tex_wrap");

namespace {

class MapSamplerWrapTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

MaterialParams ParamsFor(RndMat *mat) { return BuildMaterialParams(mat, false); }

const milo::LookProfile kLooks[] = {milo::LookProfile::Retail, milo::LookProfile::Enhanced};

}  // namespace

TEST_F(MapSamplerWrapTest, AClampMaterialClampsItsMaps) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    for (milo::LookProfile look : kLooks) {
        milo::ScopedLookOverride o(look);
        RndMat *mat = Hmx::Object::New<RndMat>();
        mat->SetProperty("tex_wrap", DataNode((int)kTexWrapClamp));
        ASSERT_EQ(mat->GetTexWrap(), kTexWrapClamp);
        const MaterialParams p = ParamsFor(mat);
        EXPECT_EQ(p.samplerDesc.addressU, wgpu::AddressMode::ClampToEdge);
        EXPECT_EQ(p.mapSamplerDesc.addressU, wgpu::AddressMode::ClampToEdge)
            << milo::LookProfileName(look) << ": the emissive / normal / specular maps wrap";
        EXPECT_EQ(p.mapSamplerDesc.addressV, wgpu::AddressMode::ClampToEdge)
            << milo::LookProfileName(look);
        delete mat;
    }
}

TEST_F(MapSamplerWrapTest, ARepeatMaterialRepeatsItsMaps) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetProperty("tex_wrap", DataNode((int)kTexWrapRepeat));
    const MaterialParams p = ParamsFor(mat);
    EXPECT_EQ(p.mapSamplerDesc.addressU, wgpu::AddressMode::Repeat);
    EXPECT_EQ(p.mapSamplerDesc.addressV, wgpu::AddressMode::Repeat);
    delete mat;
}

TEST_F(MapSamplerWrapTest, AMirrorMaterialMirrorsItsMaps) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetProperty("tex_wrap", DataNode((int)kTexWrapMirror));
    const MaterialParams p = ParamsFor(mat);
    EXPECT_EQ(p.mapSamplerDesc.addressU, wgpu::AddressMode::MirrorRepeat);
    EXPECT_EQ(p.mapSamplerDesc.addressV, wgpu::AddressMode::MirrorRepeat);
    delete mat;
}

// The detail normal map is set to wrap in the image (SetTextureClamp(0xe, 0))
// and native samples it, and the rim map, through the same map sampler.
TEST_F(MapSamplerWrapTest, ADetailOrRimMapKeepsTheMapsRepeating) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    for (const char *prop : {"norm_detail_map", "rim_map"}) {
        RndMat *mat = Hmx::Object::New<RndMat>();
        RndTex *tex = Hmx::Object::New<RndTex>();
        mat->SetProperty("tex_wrap", DataNode((int)kTexWrapClamp));
        mat->SetProperty(prop, DataNode(tex));
        const bool has = std::string(prop) == "rim_map" ? mat->GetRimMap() == tex
                                                         : mat->GetNormDetailMap() == tex;
        ASSERT_TRUE(has) << prop << " was not set (the comparison would prove nothing)";
        const MaterialParams p = ParamsFor(mat);
        EXPECT_EQ(p.samplerDesc.addressU, wgpu::AddressMode::ClampToEdge) << prop;
        EXPECT_EQ(p.mapSamplerDesc.addressU, wgpu::AddressMode::Repeat) << prop;
        EXPECT_EQ(p.mapSamplerDesc.addressV, wgpu::AddressMode::Repeat) << prop;
        delete mat;
        delete tex;
    }
}
