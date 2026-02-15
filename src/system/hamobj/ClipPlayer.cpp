#include "hamobj/ClipPlayer.h"
#include "HamRegulate.h"
#include "MoveMgr.h"
#include "char/CharClip.h"
#include "flow/PropertyEventProvider.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamDriver.h"
#include "hamobj/SongUtl.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "os/Debug.h"
#include "rndobj/PropAnim.h"
#include "rndobj/PropKeys.h"
#include "utl/Loader.h"

const char *ClipPlayer::sRestStepNames[4] = {
    "rest_step_left", "rest_step_right", "rest_step_fwd", "rest_step_back"
};

void Annotate(DataArray *a, float f, const char *cc) {
    a->Insert(a->Size(), DataArrayPtr(BeatToFrame(f), cc));
}

bool ClipPlayer::Init(RndPropAnim *anim) {
    mClipDir = TheHamDirector->ClipDir();
    if (anim) {
        PropKeys *clipKeys = anim->GetKeys(TheHamDirector, DataArrayPtr(Symbol("clip")));
        if (clipKeys) {
            mClipKeys = clipKeys->AsSymbolKeys();
        }
        PropKeys *clipCrossoverKeys =
            anim->GetKeys(TheHamDirector, DataArrayPtr(Symbol("clip_crossover")));
        if (clipCrossoverKeys) {
            mClipCrossoverKeys = clipCrossoverKeys->AsSymbolKeys();
        }
        PropKeys *masterKeys = TheHamDirector->GetMasterKeys("clip");
        if (masterKeys) {
            mMasterClipKeys = masterKeys->AsSymbolKeys();
        }
        if (mClipKeys && mMasterClipKeys && mClipDir) {
            Key<Symbol> *k1;
            Key<Symbol> *k2;
            if (TheHamDirector->GetPracticeFrames(k2, k1)) {
                unk20 = Round(FrameToBeat(k1->frame));
                unk24 = Round(FrameToBeat(k2->frame)) - 1.0f;
                String str(k1->value);
                str.ReplaceAll('*', '\0');
                mInClip =
                    mClipDir->Find<CharClip>(MakeString("%s_in", str.c_str()), false);
                str = k2->value;
                str.ReplaceAll('*', '\0');
                mOutClip =
                    mClipDir->Find<CharClip>(MakeString("%s_out", str.c_str()), false);
                mRestClip = mClipDir->Find<CharClip>("rest", false);
                for (int i = 0; i < 4; i++) {
                    mRestStepClips[i] =
                        mClipDir->Find<CharClip>(sRestStepNames[i], false);
                }
            }
            return true;
        }
    }
    return false;
}

bool ClipPlayer::Init(Difficulty d) {
    return Init(TheHamDirector->GetPropAnim(d, "song.anim", false));
}

bool ClipPlayer::Init(int x) { return Init(TheHamDirector->SongAnim(x)); }

bool ClipPlayer::CanUseRestStep() {
    // In non-edit mode (or when transitions are enabled), check if the out clip
    // is compatible with rest steps. Rest steps require a 3-beat clip with no flag 0x4.
    if (!TheLoadMgr.EditMode() || !TheHamDirector->NoTransitions()) {
        CharClip *clip = mOutClip;
        if (clip && (ClipLength(clip) != 3 || clip->Flags() & 4)) {
            return false;
        }
    }
    return true;
}

DataNode ClipPlayer::AnnotatePractice() {
    if (unk24 == kHugeFloat) {
        return 0;
    }
    DataArray *tempMem = new DataArray(0);
    DataArray *arr;
    if (tempMem) {
        arr = new DataArray(0);
    }
    float f30 = 1.0f;
    if (!TheLoadMgr.EditMode() || !TheHamDirector->NoTransitions()) {
        if (mInClip) {
            float f31 = unk20 + f30;
            Annotate(arr, f31 - ClipLength(mInClip), mInClip->Name());
            Annotate(arr, unk20 + f30, "");
        }
    }
    float f31 = unk24;
    if (!TheLoadMgr.EditMode() || !TheHamDirector->NoTransitions()) {
        if (mOutClip) {
            Annotate(arr, f31 - f30, mOutClip->Name());
            f31 += ClipLength(mOutClip) - 2.0f;
        }
    }
    if (CanUseRestStep()) {
        Annotate(arr, f31, "rest_step");
        f31 = unk24 + 2.0f;
    }
    Annotate(arr, f31, "rest");
    int startMarginFrames = (int)TheHamDirector->Property(Symbol("loop_frame_start"), true)->Int();
    long long loopStartVal = (long long)(startMarginFrames * 4);
    Annotate(arr, unk20 - (float)loopStartVal, "loop");
    int endMarginFrames = (int)TheHamDirector->Property(Symbol("loop_frame_end"), true)->Int();
    long long loopEndVal = (long long)(endMarginFrames * 4);
    Annotate(arr, (float)loopEndVal + unk24 + f30, "loop");
    DataNode node(arr, kDataArray);
    arr->Release();
    return node;
}

void ClipPlayer::PlayAnims(HamCharacter *c, float f1, float f2, int x) {
    unk48 = x;
    unkc = FrameToBeat(f1);
    unk10 = FrameToBeat(f2);
    unk1c = c->SongDriver();
    unk44 = 0;
    unk1c->Clear();
    HamRegulate *reg = c->Regulator();
    PlayNormal(-kHugeFloat, nullptr, "");
    reg->RegulateWay(c->GetWaypoint(), 8);
}

namespace {
    float ClipStart(CharClip *, float, float &, float &);
}

void ClipPlayer::PlayClip(CharClip *clip, float f1, float f2, HamDriver::LayerArray *arr) {
    if (clip) {
        float f50, f4c;
        ClipStart(clip, f1, f50, f4c);
        unk44++;
        if (!TheLoadMgr.EditMode() || (unk48 <= 0 || unk44 == unk48)) {
            HamDriver::LayerClip *layerClip = unk1c->NewLayerClip();
            layerClip->unk10 = clip;
            layerClip->unkc = f50 - unk50;
            layerClip->unk4 = f2 - unk50;
            arr->unk2c.push_back(layerClip);
            if (TheLoadMgr.EditMode() && unk48 > 0) {
                layerClip->unk4 = -kHugeFloat;
            }
        }
    }
}

bool ClipPlayer::PushExpertClip(int i1, HamDriver::LayerArray *arr) {
    if (i1 < 0)
        return false;
    else {
        Key<Symbol> &curKey = mClipKeys->at(i1);
        float beat = FrameToBeat(curKey.frame);
        bool b2 = false;
        if (unkc < beat + 1.0f) {
            b2 = PushExpertClip(i1 - 1, arr);
        }
        float f6 = b2 ? beat : -kHugeFloat;
        PlayClip(mClipDir->Find<CharClip>(curKey.value.Str(), false), beat, f6, arr);
        return true;
    }
}

CharClip *ClipPlayer::GetTransitionBefore(Key<Symbol> *key) {
    if (key) {
        if (key != &mClipKeys->at(0)
            && (!TheLoadMgr.EditMode() || !TheHamDirector->NoTransitions())) {
            char name[256];
            strcpy(name, (key - 1)->value.Str());
            strcat(name, "_");
            strcat(name, key->value.Str());
            return mClipDir->Find<CharClip>(name, false);
        }
    }
    return nullptr;
}

CharClip *ClipPlayer::GetRoutineTransition(const char *cc, Key<Symbol> *key) {
    if (key) {
        if (key != &mClipKeys->at(0)
            && (!TheLoadMgr.EditMode() || !TheHamDirector->NoTransitions())) {
            char name[256];
            strcpy(name, cc);
            strcat(name, "_");
            strcat(name, key->value.Str());
            return mClipDir->Find<CharClip>(name, false);
        }
    }
    return nullptr;
}

void ClipPlayer::GetRoutineCrossoverClips(
    float f1, const char *cc, CharClip **c1, CharClip **c2
) {
    if (TheMoveMgr->HasRoutine()) {
        const std::pair<const MoveVariant *, const MoveVariant *> *moveVars =
            TheMoveMgr->GetRoutineMeasure(unk14, Round(f1 / 4.0f));
        if (moveVars) {
            if (moveVars->first) {
                *c1 = mClipDir->Find<CharClip>(moveVars->first->Name().Str(), false);
            }
            if (moveVars->second) {
                *c2 = mClipDir->Find<CharClip>(moveVars->second->Name().Str(), false);
            }
        }
    }
    if (!*c1) {
        *c1 = *c2;
        if (!*c1) {
            *c1 = mClipDir->Find<CharClip>(cc, false);
            if (!*c1) {
                *c1 = mClipDir->Find<CharClip>(mMasterClipKeys->at(0).value.Str(), false);
            }
        }
    }
    if (!*c2) {
        *c2 = *c1;
    }
}

void ClipPlayer::PlayNormal(float f1, HamDriver::LayerArray *arr, const char *cc) {
    HamDriver::LayerArray *newArr;
    if (arr != NULL) {
        newArr = new HamDriver::LayerArray();
        arr->unk2c.push_back(newArr);
        strncpy(newArr->unkc, cc, 0x1F);
    } else {
        newArr = &unk1c->Layers();
    }
    newArr->unk4 = f1 - unk50;
    if (!mClipKeys) {
        if (TheLoadMgr.EditMode()) {
            const char *msg = "No 'clips' keyframes in your song.anim.  Please don't save this song!";
            MILO_NOTIFY_ONCE(msg);
        }
    } else {
        static Symbol merge_moves("merge_moves");
        int prop = TheHamProvider->Property(merge_moves, true)->Int();
        float beat = unkc;
        if (prop != 0) {
            PushRoutineBuilderClip(mClipKeys->KeyLessEq(BeatToFrame(beat)), newArr);
        } else if (mClipKeys == mMasterClipKeys) {
            PushExpertClip(mClipKeys->KeyLessEq(BeatToFrame(beat)), newArr);
        } else {
            PushClip(mClipKeys->KeyGreaterEq(BeatToFrame(beat)), newArr);
        }
    }
}
