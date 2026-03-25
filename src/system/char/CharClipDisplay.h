#pragma once
#include "char/CharClip.h"
#include "math/Color.h"
#include "obj/Dir.h"
#include "obj/Object.h"

struct CharClipDisplay {
    CharClipDisplay()
        : mClip(0), mViewStartBeat(0), mViewEndBeat(0), mStartBeat(0), mEndBeat(0), mTextWidth(0), mDrawPosY(0), mCursorBeat(0),
          mBlendWeight(0), mPadding(0) {
        mClipNameBuffer[0] = '\0';
    }

    static float LineSpacing();
    static void Init(ObjectDir *);
    static Hmx::Object *FindSource(Hmx::Object *);
    void SetText(char const *);
    float GetX(float) const;
    void SetStartEnd(float, float, bool);
    void DrawBlend(float, float);
    void DrawBeatString(char const *, float, Hmx::Color const &);
    void DrawCursor();
    void SetClip(CharClip *, bool);
    void DrawBeatString(float, Hmx::Color const &);
    void DrawTrack();
    static float GetSEm() { return sEm; }

    CharClip *mClip;
    float mViewStartBeat;
    float mViewEndBeat;
    float mStartBeat;
    float mEndBeat;
    float mTextWidth;
    float mDrawPosY;
    float mCursorBeat;
    float mBlendWeight;
    char mClipNameBuffer[64];
    float mPadding;

    static float sZoom;

protected:
    static float sEm;
    static ObjectDir *sDir;
};
