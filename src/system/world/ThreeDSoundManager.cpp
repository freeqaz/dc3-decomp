#include "world/ThreeDSoundManager.h"
#include "math/Mtx.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "os/Debug.h"
#include "synth/ThreeDSound.h"
#include "world/Dir.h"

ThreeDSoundManager::ThreeDSoundManager(WorldDir *dir)
    : mParent(dir), mSounds(dir), mListener(dir), mListenerDirty(0), mDopplerPower(1) {}

ThreeDSoundManager::~ThreeDSoundManager() {}

void ThreeDSoundManager::SyncObjects() {
    ObjPtrList<ThreeDSound> sounds(mParent);
    HarvestSounds(mParent, sounds);
    FOREACH (it, sounds) {
        mSounds.remove(*it);
    }
    FOREACH (it, mSounds) {
        (*it)->Stop(nullptr, false);
    }
    mSounds = sounds;
}

void ThreeDSoundManager::HarvestSounds(ObjectDir *dir, ObjPtrList<ThreeDSound> &sounds) {
    MILO_ASSERT(dir, 0x31);
    for (ObjDirItr<ThreeDSound> it(dir, true); it != nullptr; ++it) {
        sounds.push_back(&*it);
        MILO_NOTIFY(
            "Warning, found ThreeDSound object %s in %s!", it->Name(), PathName(dir)
        );
    }
}

float ThreeDSoundManager::CalculateAngle(ThreeDSound *sound, const Transform &xfm) {
    Vector3 vout;
    MultiplyTranspose(sound->WorldXfm().v, xfm, vout);
    return atan2(vout.x, vout.y);
}

void ThreeDSoundManager::CalculateDistance(
    ThreeDSound *sound, const Transform &xfm, float &f1, float &f2
) {
    Vector3 vdiff;
    Subtract(xfm.v, sound->WorldXfm().v, vdiff);
    f1 = Length(vdiff);
    if (IsNaN(f1)) {
        MILO_NOTIFY("Sound %s is NaN meters away", PathName(sound));
        f1 = 0;
        f2 = 0;
    } else if (sound->GetShape() == 1) {
        Vector3 v50;
        Normalize(sound->WorldXfm().m.x, v50);
        Vector3 vneg;
        Negate(v50, vneg);
        float fscalar = Dot(vneg, vdiff);
        Vector3 vtotal;
        ScaleAdd(vdiff, v50, fscalar, vtotal);
        f2 = Length(vtotal);
    }
}
