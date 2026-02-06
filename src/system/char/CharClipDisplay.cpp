#include "char/CharClipDisplay.h"
#include "obj/Object.h"
#include "rndobj/Rnd.h"

float CharClipDisplay::sEm;
ObjectDir *CharClipDisplay::sDir;

void CharClipDisplay::Init(ObjectDir *dir) {
    sDir = dir;
    sEm = TheRnd.DrawString("", Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).y;
}

void CharClipDisplay::SetClip(CharClip *clip, bool b) {
    unk0 = clip;
    SetText(clip->Name());
    SetStartEnd(clip->StartBeat(), clip->EndBeat(), b);
}

void CharClipDisplay::SetText(const char *text) {
    strcpy(unk24, text);
    unk14 = TheRnd.DrawString(text, Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).x
        + sEm;
}

float CharClipDisplay::LineSpacing() { return sEm * 2.0f; }

float CharClipDisplay::GetX(float beat) const {
    float endBeat = unk10;
    float startBeat = unkc;
    float beatRange = (endBeat > startBeat) ? (endBeat - startBeat) : 1.0f;
    float leftMargin = sEm * 3.0f;
    float textWidth = unk14 + unk64 + leftMargin;
    return ((TheRnd.Width() - leftMargin) - textWidth) * ((beat - startBeat) / beatRange) + textWidth;
}

Hmx::Object *CharClipDisplay::FindSource(Hmx::Object *obj) {
    for (ObjDirItr<Hmx::Object> it(ObjectDir::Main(), false); it != nullptr; ++it) {
        MsgSinks *sinks = it->Sinks();
        if (sinks != nullptr && sinks->HasSink(obj)) {
            return it;
        }
    }
    return nullptr;
}

void CharClipDisplay::DrawBeatString(char const *c, float f1, Hmx::Color const &color) {
    float posX, posY;
    posX = GetX(f1) - 18.0f;
    posY = f1 - 4.0f;
    TheRnd.DrawString(c, Vector2(posX, posY), color, true);
}