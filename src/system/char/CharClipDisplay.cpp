#include "char/CharClipDisplay.h"
#include "math/Geo.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Rnd.h"
#include <cmath>

float CharClipDisplay::sZoom;
float CharClipDisplay::sEm;
ObjectDir *CharClipDisplay::sDir;

void CharClipDisplay::Init(ObjectDir *dir) {
    sDir = dir;
    sEm = TheRnd.DrawString("", Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).y;
}

void CharClipDisplay::SetClip(CharClip *clip, bool b) {
    mClip = clip;
    SetText(clip->Name());
    SetStartEnd(clip->StartBeat(), clip->EndBeat(), b);
}

void CharClipDisplay::SetText(const char *text) {
    strcpy(mClipNameBuffer, text);
    mTextWidth = TheRnd.DrawString(text, Vector2(0, 0), Hmx::Color(1.0f, 0.0f, 0.0f), false).x
        + sEm;
}

float CharClipDisplay::LineSpacing() { return sEm * 2.0f; }

float CharClipDisplay::GetX(float beat) const {
    float endBeat = mEndBeat;
    float startBeat = mStartBeat;
    float beatRange = (endBeat > startBeat) ? (endBeat - startBeat) : 1.0f;
    float leftMargin = sEm * 3.0f;
    float textWidth = mTextWidth + mPadding + leftMargin;
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

__declspec(noinline) void
CharClipDisplay::SetStartEnd(float start, float end, bool resetZoom) {
    unk4 = start;
    unk8 = end;
    mStartBeat = start;
    mEndBeat = end;
    float zoomRange = 16.0f / sZoom;
    if (resetZoom) {
        float margin = sEm * 3.0f;
        float screenWidth = (float)(long long)TheRnd.Width();
        float textOffset = mPadding + mTextWidth + margin;
        mStartBeat =
            unk1c - ((screenWidth * 0.5f - textOffset) * zoomRange) / screenWidth;
        mEndBeat = (((screenWidth - margin) - textOffset) * zoomRange)
                / (float)(long long)TheRnd.Width()
            + mStartBeat;
    } else {
        if (end - start > zoomRange) {
            float cursor = unk1c;
            float halfZoom = zoomRange * 0.5f;
            if (cursor > halfZoom + start) {
                if (cursor > end - halfZoom) {
                    mStartBeat = end - zoomRange;
                    return;
                }
                mStartBeat = cursor - halfZoom;
                mEndBeat = halfZoom + cursor;
            } else {
                mEndBeat = zoomRange + start;
            }
        } else {
            if (end != start) {
                return;
            }
            mStartBeat = start - zoomRange * 0.5f;
            mEndBeat = zoomRange * 0.5f + end;
        }
    }
}

void CharClipDisplay::DrawBeatString(char const *c, float f1, Hmx::Color const &color) {
    float posX = GetX(f1) - 18.0f;
    float posY = mDrawPosY - 4.0f;
    TheRnd.DrawString(c, Vector2(posY, posX), color, true);
}

void CharClipDisplay::DrawBlend(float beat, float weight) {
    Hmx::Rect rect(0.0f, mDrawPosY + 1.0f, 0.0f, 2.0f);
    float x1 = GetX(beat);
    rect.x = x1;
    float x2 = GetX(beat + weight);
    rect.w = x2 - x1;
    Hmx::Color blendColor(0.0f, 0.0f, 1.0f, 0.4f);
    TheRnd.DrawRect(rect, blendColor, nullptr, nullptr, nullptr);
    rect.h = 4.0f;
    rect.y = mDrawPosY - 1.0f;
    rect.w = 3.0f;
    float midX = GetX(weight * 0.5f + beat);
    rect.x = midX - 1.0f;
    Hmx::Color markerColor(0.0f, 0.0f, 1.0f, 1.0f);
    TheRnd.DrawRect(rect, markerColor, nullptr, nullptr, nullptr);
}

void CharClipDisplay::DrawBeatString(float beat, Hmx::Color const &color) {
    const char *text;
    if (beat == (float)std::floor(beat)) {
        text = MakeString("%d", (int)beat);
    } else {
        text = MakeString("%.2f", beat);
    }
    DrawBeatString(text, beat, color);
}

void CharClipDisplay::DrawCursor() {
    Hmx::Color yellow(1.0f, 1.0f, 0.0f, 1.0f);
    float x = GetX(unk1c);
    Hmx::Rect rect(x, mDrawPosY - 3.0f, 1.0f, 9.0f);
    TheRnd.DrawRect(rect, yellow, nullptr, nullptr, nullptr);
    const char *text;
    if (!(unk20 >= 1.0f)) {
        text = MakeString("%.1f", unk1c);
    } else {
        text = MakeString("%.1f (%.2f)", unk1c, unk20);
    }
    DrawBeatString(text, unk1c, yellow);
}