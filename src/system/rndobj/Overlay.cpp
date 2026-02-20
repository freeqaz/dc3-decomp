#include "rndobj/Overlay.h"
#include "rndobj/Rnd.h"
#include "os/System.h"
#include "utl/Std.h"

bool RndOverlay::sTopAligned = true;
std::list<RndOverlay *> RndOverlay::sOverlays;

void RndOverlay::TogglePosition() { sTopAligned = !sTopAligned; }
void RndOverlay::SetTimeout(float seconds) { mTimeout = seconds * 1000.0f + SystemMs(); }

RndOverlay *RndOverlay::Find(Symbol name, bool fail) {
    for (std::list<RndOverlay *>::iterator it = sOverlays.begin(); it != sOverlays.end();
         it++) {
        if (name == (*it)->mName)
            return *it;
    }
    if (fail)
        MILO_FAIL("Could not find overlay \"%s\"", name);
    return 0;
}

void RndOverlay::Clear() {
    for (std::list<String>::iterator it = mLines.begin(); it != mLines.end(); ++it) {
        it->erase();
    }
    mLine = mLines.begin();
    mCursorChar = -1;
}

void RndOverlay::Terminate() {
    for (std::list<RndOverlay *>::iterator i = sOverlays.begin(); i != sOverlays.end();) {
        delete *i;
        i = sOverlays.erase(i);
    }
}

String &RndOverlay::CurrentLine() {
    if (mLine == mLines.end()) {
        String newstr;
        mLines.pop_front();
        mLines.push_back(newstr);
        std::list<String>::iterator it = mLines.end();
        mLine = --it;
        mLine->reserve(0x7F);
    }
    return *mLine;
}

float RndOverlay::Height() const {
    unsigned int numlines = NumLines();
    const Vector2 &v = TheRnd.DrawStringScreen("", Vector2(0, 0), mTextColor, true);
    return (float)numlines * v.y + 0.0268f;
}

void RndOverlay::DrawAll(bool b) {
    float toUse = sTopAligned ? 0.0212f : 0.9788f;
    FOREACH (it, sOverlays) {
        RndOverlay *cur = *it;
        if (!b || cur->mModal) {
            if (sTopAligned)
                toUse = cur->Draw(toUse);
            else if (cur->Showing()) {
                toUse -= cur->Height();
                cur->Draw(toUse);
            }
        }
    }
}

void RndOverlay::SetLines(int lines) {
    MILO_ASSERT(lines >= 1, 0x72);
    if (mLines.size() != lines) {
        mLines.resize(lines);
        mLine = mLines.begin();
    }
}

RndOverlay::RndOverlay(const DataArray *da)
    : mShowing(0), mLines(), mLine(), mBackColor(0.0f, 0.0f, 0.0f, 0.2f),
      mTextColor(1, 1, 1, 1), mCursorChar(-1), mCallback(0), mTimer(), mTimeout(0.0f),
      mModal(0), mDumpCount(0) {
    mName = da->Str(0);
    int lines = 1;
    da->FindData("lines", lines, false);
    SetLines(lines);
    da->FindData("showing", mShowing, false);
    da->FindData("color", mBackColor, false);
    da->FindData("modal", mModal, false);
    da->FindData("text_color", mTextColor, false);
}

void RndOverlay::Print(const char *str) {
    if (*str != '\0') {
        do {
            if (mLine == mLines.end()) {
                String newstr;
                mLines.pop_front();
                mLines.push_back(newstr);
                std::list<String>::iterator it = mLines.end();
                mLine = --it;
                mLine->reserve(0x7F);
            }
            if (*str == '\n') {
                ++mLine;
            } else {
                *mLine += *str;
            }
            str++;
        } while (*str != '\0');
    }
}

float RndOverlay::Draw(float y) {
    if (mTimeout > 0.0f && mShowing) {
        if (SystemMs() > mTimeout) {
            mShowing = false;
            mTimeout = 0.0f;
        }
    }
    if (!mShowing)
        return y;
    if (mCallback) {
        float updated = mCallback->UpdateOverlay(this, y);
        if (updated != y)
            return updated;
    }
    Hmx::Rect rect(0.0f, y, 1.0f, Height());
    TheRnd.DrawRectScreen(rect, mBackColor, TheRnd.OverlayMat(), nullptr, nullptr);
    Vector2 pos(0.0134f, y + 0.005f);
    if (mCursorChar > -1 && !mLines.empty()) {
        String str;
        for (int i = 0; i < mCursorChar; i++) {
            str += " ";
        }
        str += String("_");
        Vector2 cursorPos = pos;
        cursorPos.y += 0.005f;
        TheRnd.DrawStringScreen(str.c_str(), cursorPos, mTextColor, true);
    }
    for (std::list<String>::iterator it = mLines.begin(); it != mLines.end(); ++it) {
        const Vector2 &sz = TheRnd.DrawStringScreen(it->c_str(), pos, mTextColor, true);
        pos.y = sz.y;
    }
    if (mDumpCount > 0) {
        mDumpCount--;
        for (std::list<String>::iterator it = mLines.begin(); it != mLines.end(); ++it) {
            TheDebug << it->c_str() << "\n";
        }
    }
    return rect.y + rect.h;
}

void RndOverlay::Init() {
    DataArray *cfg = SystemConfig("rnd");
    DataArray *overlaysArr = cfg->FindArray("overlays");
    for (int i = 1; i < overlaysArr->Size(); i++) {
        sOverlays.push_back(new RndOverlay(overlaysArr->Array(i)));
    }
}
