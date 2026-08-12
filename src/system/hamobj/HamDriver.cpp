#include "hamobj\HamDriver.h"

#include "char\CharClipDisplay.h"
#include "math/Easing.h"
#include "utl\TimeConversion.h"
#include "char\Char.h"
#include "char\CharBones.h"
#include "char\CharClip.h"
#include "char\CharPollable.h"
#include "char\CharWeightable.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include "rndobj\Rnd.h"
#include "utl/BinStream.h"

HamDriver::HamDriver() : mBones(this), mDisplayBeat(-kHugeFloat) {}

HamDriver::~HamDriver() { Clear(); }

BEGIN_HANDLERS(HamDriver)
    HANDLE_SUPERCLASS(CharPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamDriver)
    SYNC_PROP(bones, mBones)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(CharPollable)
END_PROPSYNCS

BEGIN_SAVES(HamDriver)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mBones;
END_SAVES

BEGIN_COPYS(HamDriver)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(HamDriver)
    BEGIN_COPYING_MEMBERS
        mBones = (CharBonesObject *)c->mBones;
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(HamDriver)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(1, 0)

void HamDriver::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(CharWeightable)
    d >> mBones;
}

void HamDriver::PostLoad(BinStream &) {}

#ifdef HX_NATIVE
// Wave-4 Lane A: shared frame-local poll sequence counter, so HamDriver::Poll
// (the pose) and HamIKEffector::Poll (the foot-plant IK) can be ordered within
// one frame for a SPECIFIC character path. Resolves the HamDriver:95-101
// poll-order claim (UNCONFIRMED per Push 7b) empirically.
int g_ikPollSeq = 0;

// Knee .rotz instrumentation (DC3_KNEE_CLIP=1): splits the faithful-root
// hypotheses — (a) clip selection/weight vs (b) accumulation loss — by logging
// every LayerClip::Play's contribution to the bone_L-knee.rotz channel plus the
// post-blend final value. The knee is a .rotz SCALAR (not a quat); see
// docs/sessions/2026-07-02-feet-web-loop-plant-gap.md. LayerClip::Play has no
// driver identity, so Poll stashes it here for the frame.
static const char *g_dc3KneeDrvPath = nullptr;
static bool g_dc3KneeLogThis = false;
static bool Dc3KneeClipEnv() {
    static int e = -1;
    if (e < 0)
        e = getenv("DC3_KNEE_CLIP") ? 1 : 0;
    return e != 0;
}
#endif

void HamDriver::Poll() {
#ifdef HX_NATIVE
    {
        extern int HamDirector_NativeSetFrameCount();
        static int sSeqLog = 0;
        const char *pp = PathName(this);
        bool isMain = pp && strstr(pp, "main.milo") && !strstr(pp, "backup");
        if (getenv("DC3_IK_DIAG") && sSeqLog < 40 && isMain
            && HamDirector_NativeSetFrameCount() > 3000) {
            sSeqLog++;
            fprintf(stderr, "DC3_IK_DIAG PollSeq[%d] f=%d seq=%d POSE(hdrv) %s\n",
                    sSeqLog, HamDirector_NativeSetFrameCount(), ++g_ikPollSeq, pp);
        }
    }
    // Bootstrap: Layer::mWeight is uninitialized (no initializer in ctor).
    // On Xbox, garbage heap memory provides a non-zero initial value.
    // On native, zero-initialized heap keeps mWeight at 0, so the guard
    // below (mWeight > 0) prevents Eval() from ever running.  Force one
    // evaluation when layers exist but mWeight hasn't been bootstrapped.
    if (mBones && mLayers.mWeight <= 0.0f && !mLayers.mLayers.empty()) {
        mLayers.Eval(1.0f);
    }
#endif
    if (mBones && mLayers.mWeight > 0.0f) {
        mLayers.Eval(mLayers.mWeight);
#ifdef HX_NATIVE
        if (Dc3KneeClipEnv()) {
            extern int HamDirector_NativeSetFrameCount();
            const char *pp = PathName(this);
            int f = HamDirector_NativeSetFrameCount();
            g_dc3KneeDrvPath = pp;
            // main-dancer drivers only, every 5th frame once gameplay is warm,
            // so the per-clip log stays readable across the whole routine
            g_dc3KneeLogThis = pp && strstr(pp, "main.milo") && !strstr(pp, "backup")
                && f > 3000 && (f % 5 == 0);
        }
        // Push 7 diagnostic: is the persistent-base ScaleDown(1-mWeight) leaving a
        // stale-pose residual (mWeight<1 ⇒ A2) or is mWeight==1 in steady state
        // (⇒ base is zeroed, A2 inert, sink is in the move data / A1)?
        {
            extern int HamDirector_NativeSetFrameCount();
            static int sWLog = 0;
            if (getenv("DC3_IK_DIAG") && sWLog < 40
                && HamDirector_NativeSetFrameCount() > 3000) {
                sWLog++;
                fprintf(stderr,
                    "DC3_IK_DIAG DriverWeight[%d] f=%d mWeight=%.4f scaleDown=%.4f nLayers=%d\n",
                    sWLog, HamDirector_NativeSetFrameCount(),
                    mLayers.mWeight, 1.0f - mLayers.mWeight,
                    (int)mLayers.mLayers.size());
            }
        }
        // Experiment (DC3_DRIVER_ZEROBASE=1): zero the base like CharClip::PoseMeshes
        // instead of scaling the persistent accumulator. If this plants the foot, the
        // stale residual (A2) is the bug.
        {
            static int sZeroBase = -1;
            if (sZeroBase < 0)
                sZeroBase = getenv("DC3_DRIVER_ZEROBASE") ? 1 : 0;
            if (sZeroBase) {
                mBones->ScaleDown(*mBones, 0.0f);
                mLayers.Play(*mBones);
                mDisplayBeat = TheTaskMgr.Beat();
                return;
            }
        }
#endif
        mBones->ScaleDown(*mBones, 1.0f - mLayers.mWeight);
        mLayers.Play(*mBones);
#ifdef HX_NATIVE
        if (g_dc3KneeLogThis) {
            static int sFinLog = 0;
            if (sFinLog < 800) {
                sFinLog++;
                extern int HamDirector_NativeSetFrameCount();
                extern long g_dc3ScaleAddCalls;
                extern long g_dc3DstPuntCount;
                float *knee = (float *)mBones->FindPtr(Symbol("bone_L-knee.rotz"));
                Vector3 *pel = (Vector3 *)mBones->FindPtr(Symbol("bone_pelvis.pos"));
                float kv = knee ? *knee : -999.0f;
                float pz = pel ? pel->z : -999.0f;
                fprintf(stderr,
                    "DC3_KNEE_FINAL[%d] f=%d drv=%s knee=%.4f (%.1fdeg) pelZ=%.2f "
                    "kneeMiss=%d pelMiss=%d scaleAdds=%ld punts=%ld nLayers=%d w=%.3f\n",
                    sFinLog, HamDirector_NativeSetFrameCount(), g_dc3KneeDrvPath,
                    kv, kv * 57.29578f, pz, knee == nullptr, pel == nullptr,
                    g_dc3ScaleAddCalls, g_dc3DstPuntCount,
                    (int)mLayers.mLayers.size(), mLayers.mWeight);
            }
            g_dc3KneeLogThis = false;
        }
        {
            extern void Dc3KneeLog(const char *);
            char evt[192];
            snprintf(evt, sizeof(evt), "HamDriverPoll-POST %s", PathName(this));
            Dc3KneeLog(evt);
        }
#endif
        mDisplayBeat = TheTaskMgr.Beat();
    }
}

#ifdef HX_NATIVE
void HamDriver::PreEvalClipWeights() {
    // The IK ankle/hand effectors read HamCharacter::GetNeutralSkeleton() during
    // the character poll. On a song-anim character that path calls
    // song.hdrv->SetClipWeightMap(), which builds the clip-weight map from the
    // per-clip LayerClip::mWeight values. Those leaf weights are computed by
    // mLayers.Eval() inside HamDriver::Poll().
    //
    // Every frame, HamDirector::Poll() calls ClipPlayer::PlayAnims(), which does
    // mDriver->Clear() and rebuilds the layer tree from scratch — the fresh
    // LayerClip objects start with mWeight == 0. The clip weights only become
    // non-zero once this driver's own Poll() runs mLayers.Eval(). On Xbox the
    // per-character pollable sort happens to poll song.hdrv before the IK
    // effectors, so by the time the IK reads GetNeutralSkeleton the weights are
    // already evaluated. On native the pollable sort orders the IK effectors
    // first (a separate sorter-ordering divergence), so GetNeutralSkeleton sees
    // an all-zero clip map, returns `this`, and the neutral skeleton collapses
    // onto the live (crouch-sunk) pose — the IK ankle clamp then loses its
    // planted anchor and the feet sink through the floor.
    //
    // Fix (HamDirector::Poll, native only): after PlayAnims rebuilds the layers
    // and before the characters are polled, evaluate just the clip *weights*
    // (NOT the bone posing in Poll's ScaleDown/Play) so the clip-weight map is
    // populated before any IK effector reads it. LayerArray/LayerClip::Eval are
    // pure, idempotent weight computations, so this neither poses bones nor
    // double-applies anything when the driver's real Poll() runs later this
    // frame. This deliberately does NOT reorder the per-character pollable sort
    // (doing so corrupts the bone-chain SetWorldXfm cascade on native), and does
    // NOT re-pose the skeleton mid-IK.
    if (mBones && mLayers.mWeight <= 0.0f && !mLayers.mLayers.empty()) {
        mLayers.Eval(1.0f);
    }
    if (mBones && mLayers.mWeight > 0.0f) {
        mLayers.Eval(mLayers.mWeight);
    }
}
#endif

float HamDriver::DisplayRecurse(Layer *layer, int indent, float y) {
    LayerArray *arr = dynamic_cast<LayerArray *>(layer);
    if (arr) {
        if (arr->mWeight != 0.0f) {
            float padding = (float)(int)indent * CharClipDisplay::GetSEm();
            CharClipDisplay display;
            display.mCursorBeat = mDisplayBeat;
            display.mDrawPosY = y;
            display.mPadding = padding;
            display.SetText(MakeString("(%s)", arr->mName));
            display.SetStartEnd(mDisplayBeat - 4.0f, mDisplayBeat + 4.0f, true);
            display.mBlendWeight = arr->mWeight;
            display.DrawTrack();
            display.DrawBlend(arr->mBeat, 1.0f);
            display.DrawCursor();
            y += CharClipDisplay::LineSpacing();
            int innerIndent = indent + 1;
            for (std::list<Layer *>::iterator it = arr->mLayers.begin(); it != arr->mLayers.end(); ++it) {
                y = DisplayRecurse(*it, innerIndent, y);
            }
        }
    } else {
        LayerClip *clip = dynamic_cast<LayerClip *>(layer);
        if (clip && clip->mWeight != 0.0f) {
            float padding = (float)(int)indent * CharClipDisplay::GetSEm();
            CharClipDisplay display;
            float beat = (mDisplayBeat - clip->mClipBeat) + clip->mClip->StartBeat();
            display.mPadding = padding;
            display.mCursorBeat = beat;
            display.mBlendWeight = clip->mWeight;
            display.SetClip(clip->mClip, true);
            display.mDrawPosY = y;
            display.DrawTrack();
            float blendBeat = (clip->mClip->StartBeat() + clip->mBeat) - clip->mClipBeat;
            display.DrawBlend(blendBeat, 1.0f);
            display.DrawCursor();
            y += CharClipDisplay::LineSpacing();
        }
    }
    return y;
}

void HamDriver::Enter() { Clear(); }

void HamDriver::Highlight() {
    if (gCharHighlightY == -1) {
        CharDeferHighlight(this);
    } else {
        gCharHighlightY = Display(gCharHighlightY);
    }
}

void HamDriver::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    change.push_back(mBones);
}

bool HamDriver::Replace(ObjRef *ref, Hmx::Object *obj) {
    mLayers.Replace(ref, obj);
    bool replaced = CharWeightable::Replace(ref, obj);
    return replaced;
}

float HamDriver::Display(float normalizedY) {
    // Scale screen position by normalized height
    float scaledHeight = TheRnd.Height() * normalizedY;
    const char *pathName = PathName(this);

    // Draw debug info: object name and beat position
    Hmx::Color color(1.0f, 1.0f, 1.0f, 1.0f);
    Vector2 screenPos(CharClipDisplay::GetSEm(), scaledHeight);
    TheRnd.DrawString(MakeString("%s beat: %.2f", pathName, mDisplayBeat), screenPos, color, true);

    // Initialize character clip display and advance line spacing
    CharClipDisplay::Init(Dir());
    float lineSpacing = CharClipDisplay::LineSpacing() + scaledHeight;

    // Recursively display layers if bones exist and weight is active
    if (mBones && Weight() != 0.0f) {
        FOREACH (it, mLayers.mLayers) {
            lineSpacing = DisplayRecurse(*it, 0, lineSpacing);
        }
    }

    // Return normalized line position
    return lineSpacing / TheRnd.Height();
}

void HamDriver::SetClipMapRecurse(Layer *layer) {
    LayerArray *arr = dynamic_cast<LayerArray *>(layer);
    if (arr) {
        if (arr->mWeight != 0.0f) {
            FOREACH (it, arr->mLayers) {
                SetClipMapRecurse(*it);
            }
        }
    } else {
        LayerClip *clip = dynamic_cast<LayerClip *>(layer);
        if (clip && clip->mWeight != 0.0f) {
            CharClip *c = clip->mClip;
            std::map<CharClip *, float>::iterator it = mClipTimingMap.find(c);
            if (it != mClipTimingMap.end()) {
                it->second += clip->mWeight;
            } else {
                mClipTimingMap.insert(std::pair<CharClip *const, float>(c, clip->mWeight));
            }
        }
    }
}

void HamDriver::SetClipWeightMap() {
    mClipTimingMap.clear();
    SetClipMapRecurse(&mLayers);
    float total = 0.0f;
    for (std::map<CharClip *, float>::iterator it = mClipTimingMap.begin();
         it != mClipTimingMap.end(); ++it) {
        total += it->second;
    }
    if (total > 0.0f) {
        for (std::map<CharClip *, float>::iterator it = mClipTimingMap.begin();
             it != mClipTimingMap.end(); ++it) {
            it->second *= (1.0f / total);
        }
    }
}

void HamDriver::Clear() { mLayers.Clear(); }
HamDriver::LayerClip *HamDriver::NewLayerClip() { return new LayerClip(this); }
void HamDriver::OffsetSec(float seconds) { return mLayers.OffsetSec(seconds); }
CharClip *HamDriver::FirstClip() { return mLayers.FirstClip(); }

#pragma region HamDriver::Layer

void HamDriver::Layer::OffsetSec(float seconds) {
    mBeat = SecondsToBeat(BeatToSeconds(mBeat) + seconds);
}

#pragma endregion

#pragma region HamDriver::LayerClip

HamDriver::LayerClip::LayerClip(Hmx::Object *obj) : mClip(obj)
{
}

void HamDriver::LayerClip::OffsetSec(float seconds) {
    Layer::OffsetSec(seconds);
    mClipBeat = SecondsToBeat(BeatToSeconds(mClipBeat) + seconds);
}

void HamDriver::LayerClip::Eval(float parentWeight) {
    float beat = TheTaskMgr.Beat();
    auto clamped = Clamp(0.0f, 1.0f, beat - mBeat);
    mWeight = EaseSigmoid(clamped, 0.0, 0.0) * parentWeight;
}

void HamDriver::LayerClip::Play(CharBones &bones) {
    if (mWeight > 0.0f) {
        float beat = mClip->StartBeat();
        float deltaBeat = (TheTaskMgr.Beat() - mClipBeat) + beat;
#ifdef HX_NATIVE
        float *kneeDst = nullptr;
        float kneeBefore = 0.0f;
        if (g_dc3KneeLogThis) {
            kneeDst = (float *)bones.FindPtr(Symbol("bone_L-knee.rotz"));
            if (kneeDst) {
                kneeBefore = *kneeDst;
            } else {
                // dst missing the knee channel = every clip's knee is punted →
                // this alone would be the under-accumulation root
                static int sMissLog = 0;
                if (sMissLog < 20) {
                    sMissLog++;
                    fprintf(stderr, "DC3_KNEE_CLIP dst-missing bone_L-knee.rotz drv=%s clip=%s\n",
                        g_dc3KneeDrvPath, mClip ? mClip->Name() : "?");
                }
            }
        }
#endif
        bones.ScaleAdd(mClip, mWeight, deltaBeat, TheTaskMgr.DeltaBeat());
#ifdef HX_NATIVE
        if (g_dc3KneeLogThis && mClip) {
            // one-shot per run: raw POS-channel dump of the playing clip's data
            // at the current sample — is the low pelvis IN the clip bytes?
            static int sPosDump = 0;
            if (sPosDump < 6) {
                sPosDump++;
                CharClip *c = mClip;
                float frac = 0.0f;
                int samp = c->BeatToSample(deltaBeat, &frac);
                fprintf(stderr,
                    "DC3_CLIP_POS clip=%s relative=%s beat=%.2f samp=%d frac=%.3f\n",
                    c->Name(), c->Relative() ? c->Relative()->Name() : "(none)",
                    deltaBeat, samp, frac);
                c->GetFull().Dc3DumpPosChannels(samp, "FULL");
                c->GetOne().Dc3DumpPosChannels(0, "ONE");
            }
        }
        if (g_dc3KneeLogThis && kneeDst) {
            static int sClipLog = 0;
            if (sClipLog < 4000) {
                sClipLog++;
                extern int HamDirector_NativeSetFrameCount();
                fprintf(stderr,
                    "DC3_KNEE_CLIP[%d] f=%d drv=%s clip=%s w=%.3f beat=%.2f "
                    "rotz %.4f->%.4f d=%.4f (deg %.1f->%.1f)\n",
                    sClipLog, HamDirector_NativeSetFrameCount(), g_dc3KneeDrvPath,
                    mClip ? mClip->Name() : "?", mWeight, deltaBeat,
                    kneeBefore, *kneeDst, *kneeDst - kneeBefore,
                    kneeBefore * 57.29578f, *kneeDst * 57.29578f);
            }
        }
    } else if (g_dc3KneeLogThis) {
        // a zero-weight clip contributes nothing — clip-selection/weight evidence
        static int sZeroLog = 0;
        if (sZeroLog < 300) {
            sZeroLog++;
            fprintf(stderr, "DC3_KNEE_CLIP skip-w0 drv=%s clip=%s\n",
                g_dc3KneeDrvPath, mClip ? mClip->Name() : "?");
        }
#endif
    }
}

CharClip *HamDriver::LayerClip::FirstClip() { return mClip; }

bool HamDriver::LayerClip::Replace(ObjRef *ref, Hmx::Object *obj) {
    if (&mClip == ref) {
        if (!mClip.SetObj(obj)) {
            delete this;
            return true;
        }
        return false;
    }
    return false;
}
#pragma endregion

#pragma region HamDriver::LayerArray

void HamDriver::LayerArray::Eval(float weight) {
    mWeight = 0;
    if (weight > 0.0f) {
        float elapsed = TheTaskMgr.Beat() - mBeat;
        if (elapsed > 0.0f) {
            float t = (elapsed - 1.0f < 0.0f) ? elapsed : 1.0f;
            float blend = EaseSigmoid(t, 0, 0) * weight;
            for (std::list<Layer *>::iterator it = mLayers.begin(); it != mLayers.end(); ++it) {
                (*it)->Eval(blend);
                float layerWeight = (*it)->mWeight;
                float consumed = (layerWeight - blend < 0.0f) ? layerWeight : blend;
                mWeight += consumed;
                blend -= consumed;
            }
        }
    }
}

void HamDriver::LayerArray::Clear() {
    FOREACH (it, mLayers) {
        delete *it;
    }
    mLayers.clear();
}

bool HamDriver::LayerArray::Replace(ObjRef *ref, Hmx::Object *obj) {
    FOREACH (it, mLayers) {
        if (it == mLayers.end()) {
            return false;
        }
        bool replaced = (*it)->Replace(ref, obj);
        if (replaced) {
            mLayers.erase(it);
            break;
        }
    }
    return false;
}

void HamDriver::LayerArray::Play(CharBones &bones) {
    if (mWeight > 0.0) {
        FOREACH (it, mLayers) {
            (*it)->Play(bones);
        }
    }
}

CharClip *HamDriver::LayerArray::FirstClip() {
    FOREACH (it, mLayers) {
        CharClip *clip = (*it)->FirstClip();
        if (clip != nullptr) {
            return clip;
        }
    }
    return nullptr;
}

void HamDriver::LayerArray::OffsetSec(float seconds) {
    Layer::OffsetSec(seconds);
    FOREACH (it, mLayers) {
        (*it)->OffsetSec(seconds);
    }
}

#pragma endregion
