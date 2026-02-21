#pragma once
#include "char/CharClip.h"
#include "math/Color.h"
#include "obj/Dir.h"
#include "obj/Object.h"

class CharClipDisplay {
public:
    CharClipDisplay()
        : mClip(0), unk4(0), unk8(0), mStartBeat(0), mEndBeat(0), mTextWidth(0), mDrawPosY(0), unk1c(0),
          unk20(0), mPadding(0) {}

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
    float unk4;
    float unk8;
    float mStartBeat;
    float mEndBeat;
    float mTextWidth;
    float mDrawPosY;
    float unk1c;
    float unk20;
    char *mClipNameBuffer;
    int unk28;
    int unk2c;
    int unk30;
    int unk34;
    int unk38;
    int unk3c;
    int unk40;
    int unk44;
    int unk48;
    int unk4c;
    int unk50;
    int unk54;
    int unk58;
    int unk5c;
    int unk60;
    float mPadding;

protected:
    static float sZoom;
    static float sEm;
    static ObjectDir *sDir;
};
