#pragma once
#include "types.h"
#include "utl\Str.h"
#include <vector>

enum ShaderType {
    kBloomShader = 0,
    kBlurShader = 1,
    kDepthVolumeShader = 2,
    kDownsampleShader = 3,
    kDownsample4xShader = 4,
    kDownsampleDepthShader = 5,
    kDrawRectShader = 6,
    kErrorShader = 7,
    kFurShader = 8,
    kLineNozShader = 9,
    kLineShader = 10,
    kMovieShader = 11,
    kMultimeshShader = 12,
    kMultimeshBBShader = 13,
    kParticlesShader = 14,
    kPostprocessErrorShader = 15,
    kPostprocessShader = 16,
    kShadowmapShader = 17,
    kStandardShader = 18,
    kStandardBBShader = 19,
    kSyncTrackShader = 20,
    kSyncTrackChargeEffectShader = 21,
    kUnwrapUVShader = 22,
    kVelocityCameraShader = 23,
    kVelocityObjectShader = 24,
    kPlayerDepthVisShader = 25,
    kPlayerDepthShellShader = 26,
    kBloomGlareShader = 27,
    kPlayerDepthShell2Shader = 28,
    kDepthBuffer3DShader = 29,
    kYUVtoRGBShader = 30,
    kYUVtoBlackAndWhiteShader = 31,
    kPlayerGreenScreenShader = 32,
    kPlayerDepthGreenScreenShader = 33,
    kCrewPhotoShader = 34,
    kTwirlShader = 35,
    kKillAlphaShader = 36,
    kAllWhiteShader = 37,
    kMaxShaderTypes = 38
};

struct ShaderMacro {
    ShaderMacro(const char *n = nullptr, const char *v = nullptr) : Name(n), Value(v) {}

    // REVERTED EXPERIMENT (2026-08-19).  Retail's
    // __uninitialized_fill_n<ShaderMacro*> is ICF-folded into the
    // <pair<int,int>*> instantiation at 0x82461B08, whose loop is mtctr/bdnz;
    // ours is a countdown loop, and dropping this hand-written operator= is
    // what makes MSVC emit the mtctr form.  But that instantiation is
    // LINKER_MERGED (invisible to the metric) while GenerateMacros is not:
    // removing the operator= took ShaderOptions::GenerateMacros from 100% to
    // 97.3% normalized, -3612 B whole-build.  Right diagnosis, wrong code --
    // do not retry without a spelling that keeps GenerateMacros at 100%.
    ShaderMacro &operator=(const ShaderMacro &other) {
        this->Name = other.Name;
        this->Value = other.Value;
        return *this;
    }

    const char *Name; // 0x0
    const char *Value; // 0x4
};

/** The 64-bit shader option word.
 *
 *  The named bits below are the material-shader view of the word (standard,
 *  multimesh, particles, fur, sync_track); the post-process shaders reuse most
 *  of the low bits for unrelated effects and build `flags` by hand.
 *
 *  ⚠ MSVC on Xenon allocates bitfields from the MOST significant bit down, so
 *  the FIRST field declared here is bit 63 and the LAST is bit 0.  The numbers
 *  in the comments are LSB-relative and match the shifts `GenerateMacros` uses.
 *  Assigning one of these fields is what produces the target's
 *  `and rX, rX, ~mask` + `rldimi rX, rY, bit, 63-bit` pair; writing the same
 *  thing as a shift-and-or expression does not. */
struct ShaderOptions {
    ShaderOptions(u64 u) : flags(u) {}

    void GenerateMacros(ShaderType, std::vector<ShaderMacro> &) const;

    union {
        u64 flags; // 0x0
        struct {
            u64 mUnused63 : 1; // 63
            u64 mHueConverge : 1; // 62
            u64 mFastCheapLighting : 1; // 61
            u64 mShockwave : 1; // 60
            u64 mSyncTrackChargeEffect : 1; // 59
            u64 mUnused58 : 1; // 58
            u64 mUnused57 : 1; // 57
            u64 mSplinePulse : 1; // 56
            u64 mFitToSpline : 1; // 55
            u64 mFlipNormal : 1; // 54
            u64 mIntensify : 1; // 53
            u64 mHiResScreen : 1; // 52
            u64 mSpotlight : 1; // 51
            u64 mShowShaderCost : 1; // 50
            u64 mEnvironMapSpecMask : 1; // 49
            u64 mPointCubeTex : 1; // 48
            u64 mNoiseMidtone : 1; // 47
            u64 mRefractWorld : 1; // 46
            u64 mSoftDepthBlend : 1; // 45
            u64 mProjLightMultiply : 1; // 44
            u64 mEnvironMapFalloff : 1; // 43
            u64 mVelocity : 1; // 42
            u64 mNumPoint : 2; // 40-41
            u64 mToneMapping : 1; // 39
            u64 mEnableAO : 1; // 38
            u64 mRimLight : 1; // 37
            u64 mVignette : 1; // 36
            u64 mDisplayError : 1; // 35
            u64 mFurDetail : 1; // 34
            u64 mColorMod : 2; // 32-33
            u64 mCustomVariation : 2; // 30-31
            u64 mNumProj : 2; // 28-29
            u64 mFadeOut : 2; // 26-27
            u64 mBillboard : 1; // 25
            u64 mNormDetail : 1; // 24
            u64 mExtrude : 1; // 23
            u64 mPseudoHDR : 1; // 22
            u64 mColorXfm : 1; // 21
            u64 mAnisotropic : 1; // 20
            u64 mShadowBuffer : 1; // 19
            u64 mFog : 1; // 18
            u64 mApproxLights : 1; // 17
            u64 mRealLights : 1; // 16
            u64 mRimLightMap : 1; // 15
            u64 mRimLightUnder : 1; // 14
            u64 mScreenAligned : 1; // 13
            u64 mSkinned : 1; // 12
            u64 mTexGen : 2; // 10-11
            u64 mUnused9 : 1; // 9
            u64 mPrelit : 1; // 8
            u64 mGlowMap : 1; // 7
            u64 mCopyPrevious : 1; // 6
            u64 mNormalMap : 1; // 5
            u64 mDiffuseMap : 1; // 4
            u64 mEnvironMap : 1; // 3
            u64 mSpecular : 1; // 2
            u64 mSpecularMap : 1; // 1
            u64 mPerPixelLighting : 1; // 0
        };
    };
};

void InitShaderOptions();
const char *ShaderTypeName(ShaderType);
ShaderType ShaderTypeFromName(const char *);
const char *ShaderSourcePath(const char *);
const char *ShaderCachedPath(const char *, u64, bool);
bool IsPostProcShaderType(ShaderType);
void ShaderMakeOptionsString(ShaderType, const ShaderOptions &, String &);
