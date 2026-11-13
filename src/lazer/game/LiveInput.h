#pragma once
#include "game\GameInput.h"
#include "hamobj\HamAudio.h"

class LiveInput : public GameInput {
public:
    LiveInput(HamAudio &);

    virtual float CurrentMs(bool) const;
    virtual float GetSongToTaskMgrMs() const;
    virtual void SetPaused(bool);
    virtual void SetTimeOffset();
    virtual void SetPostWaitJumpOffset(float);
    virtual ~LiveInput() {}

protected:
    HamAudio &mAudio; // 0x4
    float mTimeOffset; // 0x8
    Timer mTimer; // 0x10
    int unk44; // 0x40
#ifdef HX_NATIVE
    mutable float mFastTimeMs; // synthetic time counter for DC3_FAST_TIME
    // DC3_FAST_TIME: the song stream is stopped by CurrentMs because it ran
    // ahead of the synthetic clock (FastTimeAudio.h), not by a game pause.
    mutable bool mFastTimeHeld;
#endif
};
