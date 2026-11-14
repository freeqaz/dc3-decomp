// milo-pp-dump — print every RndPostProc in .milo_xbox files as JSON lines.
//
// Usage: milo-pp-dump <input.milo_xbox>...
//
// One line per post-process object: {"file":..,"path":..,"name":..,"class":..,
// "fields":{...},"active":{...}}. "fields" are the loaded members as authored;
// "active" are the image's own enable predicates (RndPostProc::DoVignette,
// DoMotionBlur, BlendPrevious, ...) evaluated on them, i.e. what NgPostProc::
// DoPost would turn on. Written for the np-gfx-post usage inventory
// (archive/np-gfx-2026-11-14/lanes/post/); loads through the decomp's own
// RndPostProc::Load, so a field means what the game reads.
//
// Also one line per RndEnviron, {"file":..,"name":..,"class":"Environ",
// "fade_out":..,"fade_start":..,"fade_end":..,"fade_cancels_mask":..}:
// the last is Shader.cpp's fadeOut (FadeOut() && FadeEnd() != FadeStart()),
// which turns the pseudo-HDR bloom mask off (np-gfx-post round 2).
//
// And one line per RndMotionBlur, {"file":..,"name":..,"class":"MotionBlur",
// "showing":..,"draws":[{"name":..,"class":..,"bones":..}]}: the objects whose
// DrawShowing queues their draw list for NgPostProc's object velocity
// (RndVelocityBuffer::DrawMesh; meshes over 40 bones are skipped there) --
// np-gfx-post round 3.

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Rnd.h"
#include "rndobj/PostProc.h"
#include "rndobj/Env.h"
#include "rndobj/MotionBlur.h"
#include "rndobj/Mesh.h"
#include "rndobj/Tex.h"
#include "char/Char.h"
#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "utl/FilePath.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <unistd.h>

extern Rnd &TheRnd;
void SetFileChecksumData();

namespace {

// Reads RndPostProc's protected members through a derived class's
// member-pointer names (no layout assumptions; debug tool only).
struct PPPeek : RndPostProc {
    static const PPPeek &Of(const RndPostProc *p) { return *static_cast<const PPPeek *>(p); }
    using RndPostProc::mBlendAmount;
    using RndPostProc::mBloomColor;
    using RndPostProc::mBloomGlare;
    using RndPostProc::mBloomIntensity;
    using RndPostProc::mBloomStreak;
    using RndPostProc::mBloomStreakAngle;
    using RndPostProc::mBloomStreakAttenuation;
    using RndPostProc::mBloomThreshold;
    using RndPostProc::mBrightnessPower;
    using RndPostProc::mChromaticAberrationOffset;
    using RndPostProc::mChromaticSharpen;
    using RndPostProc::mColorXfm;
    using RndPostProc::mEmulateFPS;
    using RndPostProc::mFlickerModBounds;
    using RndPostProc::mFlickerTimeBounds;
    using RndPostProc::mForceCurrentInterp;
    using RndPostProc::mGradientMap;
    using RndPostProc::mGradientMapEnd;
    using RndPostProc::mGradientMapIndex;
    using RndPostProc::mGradientMapOpacity;
    using RndPostProc::mGradientMapStart;
    using RndPostProc::mHallOfTimeColor;
    using RndPostProc::mHallOfTimeMix;
    using RndPostProc::mHallOfTimeRate;
    using RndPostProc::mHallOfTimeType;
    using RndPostProc::mHueFocus;
    using RndPostProc::mHueTarget;
    using RndPostProc::mKaleidoscopeAngle;
    using RndPostProc::mKaleidoscopeComplexity;
    using RndPostProc::mKaleidoscopeFlipUVs;
    using RndPostProc::mKaleidoscopeRadius;
    using RndPostProc::mKaleidoscopeSize;
    using RndPostProc::mMotionBlurBlend;
    using RndPostProc::mMotionBlurVelocity;
    using RndPostProc::mMotionBlurWeight;
    using RndPostProc::mNoiseBaseScale;
    using RndPostProc::mNoiseIntensity;
    using RndPostProc::mNoiseMap;
    using RndPostProc::mNoiseMidtone;
    using RndPostProc::mNoiseStationary;
    using RndPostProc::mNoiseTopScale;
    using RndPostProc::mPosterLevels;
    using RndPostProc::mPosterMin;
    using RndPostProc::mPriority;
    using RndPostProc::mRefractAngle;
    using RndPostProc::mRefractDist;
    using RndPostProc::mRefractMap;
    using RndPostProc::mRefractPanning;
    using RndPostProc::mRefractScale;
    using RndPostProc::mRefractVelocity;
    using RndPostProc::mTrailDuration;
    using RndPostProc::mTrailThreshold;
    using RndPostProc::mVignetteColor;
    using RndPostProc::mVignetteIntensity;
};

// RndMotionBlur's draw list is protected, read the same way.
struct MBPeek : RndMotionBlur {
    static const MBPeek &Of(const RndMotionBlur *m) { return *static_cast<const MBPeek *>(m); }
    using RndMotionBlur::mDrawList;
};

std::string Esc(const char *s) {
    std::string o;
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') o += '\\';
        o += *s;
    }
    return o;
}

void F(std::string &o, const char *k, double v) {
    char b[96];
    snprintf(b, sizeof(b), "\"%s\":%.6g,", k, v);
    o += b;
}
void C(std::string &o, const char *k, const Hmx::Color &c) {
    char b[160];
    snprintf(b, sizeof(b), "\"%s\":[%.6g,%.6g,%.6g,%.6g],", k, c.red, c.green, c.blue, c.alpha);
    o += b;
}
void V2(std::string &o, const char *k, const Vector2 &v) {
    char b[128];
    snprintf(b, sizeof(b), "\"%s\":[%.6g,%.6g],", k, v.x, v.y);
    o += b;
}
void T(std::string &o, const char *k, const RndTex *t) {
    o += "\"";
    o += k;
    o += "\":\"";
    o += t ? Esc(t->Name()) : "";
    o += "\",";
}
void Close(std::string &o) {
    if (!o.empty() && o.back() == ',') o.pop_back();
}

void Dump(const char *file, const RndPostProc *pp) {
    const PPPeek &p = PPPeek::Of(pp);
    std::string o = "{\"file\":\"" + Esc(file) + "\",\"path\":\"" + Esc(PathName(pp)) + "\",\"name\":\""
        + Esc(pp->Name()) + "\",\"class\":\"" + Esc(pp->ClassName().Str()) + "\",\"fields\":{";
    F(o, "priority", p.mPriority);
    C(o, "bloom_color", p.mBloomColor);
    F(o, "bloom_threshold", p.mBloomThreshold);
    F(o, "bloom_intensity", p.mBloomIntensity);
    F(o, "bloom_glare", p.mBloomGlare);
    F(o, "bloom_streak", p.mBloomStreak);
    F(o, "bloom_streak_attenuation", p.mBloomStreakAttenuation);
    F(o, "bloom_streak_angle", p.mBloomStreakAngle);
    const RndColorXfm &x = p.mColorXfm;
    F(o, "hue", x.mHue);
    F(o, "saturation", x.mSaturation);
    F(o, "lightness", x.mLightness);
    F(o, "contrast", x.mContrast);
    F(o, "brightness", x.mBrightness);
    C(o, "in_lo", x.mLevelInLo);
    C(o, "in_hi", x.mLevelInHi);
    C(o, "out_lo", x.mLevelOutLo);
    C(o, "out_hi", x.mLevelOutHi);
    {
        const Transform &t = x.mColorXfm;
        char b[400];
        snprintf(b, sizeof(b), "\"xfm\":[[%.6g,%.6g,%.6g],[%.6g,%.6g,%.6g],[%.6g,%.6g,%.6g],[%.6g,%.6g,%.6g]],",
                 t.m.x.x, t.m.x.y, t.m.x.z, t.m.y.x, t.m.y.y, t.m.y.z, t.m.z.x, t.m.z.y, t.m.z.z, t.v.x,
                 t.v.y, t.v.z);
        o += b;
    }
    F(o, "num_levels", p.mPosterLevels);
    F(o, "min_intensity", p.mPosterMin);
    F(o, "kaleidoscope_complexity", p.mKaleidoscopeComplexity);
    F(o, "kaleidoscope_size", p.mKaleidoscopeSize);
    F(o, "kaleidoscope_angle", p.mKaleidoscopeAngle);
    F(o, "kaleidoscope_radius", p.mKaleidoscopeRadius);
    F(o, "kaleidoscope_flipUVs", p.mKaleidoscopeFlipUVs);
    V2(o, "flicker_intensity", p.mFlickerModBounds);
    V2(o, "flicker_secs_range", p.mFlickerTimeBounds);
    V2(o, "noise_base_scale", p.mNoiseBaseScale);
    F(o, "noise_top_scale", p.mNoiseTopScale);
    F(o, "noise_intensity", p.mNoiseIntensity);
    F(o, "noise_stationary", p.mNoiseStationary);
    F(o, "noise_midtone", p.mNoiseMidtone);
    T(o, "noise_map", p.mNoiseMap.Ptr());
    F(o, "trail_threshold", p.mTrailThreshold);
    F(o, "trail_duration", p.mTrailDuration);
    F(o, "emulate_fps", p.mEmulateFPS);
    F(o, "hall_of_time_type", p.mHallOfTimeType);
    F(o, "hall_of_time_rate", p.mHallOfTimeRate);
    C(o, "hall_of_time_color", p.mHallOfTimeColor);
    F(o, "hall_of_time_mix", p.mHallOfTimeMix);
    F(o, "motion_blur_blend", p.mMotionBlurBlend);
    C(o, "motion_blur_weight", p.mMotionBlurWeight);
    F(o, "motion_blur_velocity", p.mMotionBlurVelocity);
    T(o, "gradient_map", p.mGradientMap.Ptr());
    F(o, "gradient_map_opacity", p.mGradientMapOpacity);
    F(o, "gradient_map_index", p.mGradientMapIndex);
    F(o, "gradient_map_start", p.mGradientMapStart);
    F(o, "gradient_map_end", p.mGradientMapEnd);
    T(o, "refract_map", p.mRefractMap.Ptr());
    F(o, "refract_dist", p.mRefractDist);
    V2(o, "refract_scale", p.mRefractScale);
    V2(o, "refract_panning", p.mRefractPanning);
    V2(o, "refract_velocity", p.mRefractVelocity);
    F(o, "refract_angle", p.mRefractAngle);
    F(o, "chromatic_aberration_offset", p.mChromaticAberrationOffset);
    F(o, "chromatic_sharpen", p.mChromaticSharpen);
    C(o, "vignette_color", p.mVignetteColor);
    F(o, "vignette_intensity", p.mVignetteIntensity);
    F(o, "hue_target", p.mHueTarget);
    F(o, "hue_focus", p.mHueFocus);
    F(o, "blend_amount", p.mBlendAmount);
    F(o, "brightness_power", p.mBrightnessPower);
    F(o, "force_current_interp", p.mForceCurrentInterp);
    Close(o);
    o += "},\"active\":{";
    // The image's own predicates (rndobj/PostProc.cpp, PostProc_NG.cpp).
    F(o, "bloom", pp->BloomIntensity() > 0.0f || p.mBloomColor.alpha > 0.0f);
    F(o, "colorxfm", pp->ColorXfmEnabled());
    F(o, "noise", p.mNoiseIntensity != 0.0f && p.mNoiseMap.Ptr());
    F(o, "blend_previous", pp->BlendPrevious());
    F(o, "hall_of_time", pp->HallOfTime());
    F(o, "motion_blur", pp->DoMotionBlur());
    F(o, "gradient_map", pp->DoGradientMap());
    F(o, "refract", pp->DoRefraction());
    F(o, "chromatic", pp->DoChromaticAberration());
    F(o, "posterize", p.mPosterLevels * p.mPosterMin > 0.0f);
    F(o, "kaleidoscope", p.mKaleidoscopeComplexity > 0.0f);
    F(o, "vignette", pp->DoVignette());
    F(o, "hue_converge", p.mBlendAmount > 0.0f);
    F(o, "flicker", p.mFlickerTimeBounds.x > 0 && p.mFlickerTimeBounds.y > 0 && p.mFlickerModBounds.y > 0);
    Close(o);
    o += "}}";
    printf("%s\n", o.c_str());
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: milo-pp-dump <input.milo_xbox>...\n");
        return 1;
    }
    unsetenv("MILO_RENDER");
    setenv("MILO_NORENDER", "1", 1);

    InitMakeString();
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");
    TheRnd.PreInit();
    SystemInit("config/ham_keep.dta");
    TheRnd.Init();
    FlowInit();
    CharInit();
    WorldInit();
    HamInit();

    int total = 0;
    for (int i = 1; i < argc; i++) {
        char absPath[PATH_MAX];
        if (!realpath(argv[i], absPath)) {
            fprintf(stderr, "cannot resolve '%s'\n", argv[i]);
            continue;
        }
        ObjDirPtr<ObjectDir> dir;
        FilePath fp(absPath);
        dir.LoadFile(fp, false, false, kLoadFront, false);
        ObjectDir *scene = dir;
        if (!scene) {
            fprintf(stderr, "failed to load '%s'\n", absPath);
            continue;
        }
        int n = 0;
        for (ObjDirItr<RndPostProc> it(scene, true); it != nullptr; ++it) {
            Dump(argv[i], it);
            n++;
        }
        int envs = 0;
        for (ObjDirItr<RndEnviron> it(scene, true); it != nullptr; ++it) {
            const bool cancels = it->FadeOut() && it->FadeEnd() != it->FadeStart();
            printf("{\"file\":\"%s\",\"name\":\"%s\",\"class\":\"Environ\",\"fade_out\":%d,"
                   "\"fade_start\":%g,\"fade_end\":%g,\"fade_cancels_mask\":%d}\n",
                   argv[i], it->Name(), it->FadeOut() ? 1 : 0, it->FadeStart(), it->FadeEnd(),
                   cancels ? 1 : 0);
            envs++;
        }
        int blurs = 0;
        for (ObjDirItr<RndMotionBlur> it(scene, true); it != nullptr; ++it) {
            std::string o = "{\"file\":\"" + Esc(argv[i]) + "\",\"name\":\"" + Esc(it->Name())
                + "\",\"class\":\"MotionBlur\",\"showing\":" + (it->Showing() ? "1" : "0")
                + ",\"draws\":[";
            const MBPeek &m = MBPeek::Of(it);
            for (ObjPtrList<RndDrawable>::iterator d = m.mDrawList.begin(); d != m.mDrawList.end(); ++d) {
                RndDrawable *draw = *d;
                if (!draw) continue;
                RndMesh *mesh = dynamic_cast<RndMesh *>(draw);
                char b[64];
                snprintf(b, sizeof(b), "%d", mesh ? mesh->NumBones() : -1);
                o += "{\"name\":\"" + Esc(draw->Name()) + "\",\"class\":\""
                    + Esc(draw->ClassName().Str()) + "\",\"bones\":" + b + "},";
            }
            Close(o);
            o += "]}";
            printf("%s\n", o.c_str());
            blurs++;
        }
        fprintf(stderr, "%s: %d post-procs, %d environs, %d motion blurs\n", argv[i], n, envs, blurs);
        total += n;
    }
    fprintf(stderr, "total %d\n", total);
    fflush(stdout);
    _exit(0);
}
