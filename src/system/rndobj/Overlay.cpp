#include "rndobj/Overlay.h"
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
    if (mLine == mLines.begin()) {
        String newstr;
        mLines.pop_front();
        mLines.push_back(newstr);
        mLine = PrevItr(mLines.begin());
        mLine->reserve(0x7F);
    }
    return *mLine;
}

float RndOverlay::Height() const {
    return mLines.size() * 0.0212f;
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

void RndOverlay::Init() {
    DataArray *cfg = SystemConfig("rnd");
    DataArray *overlaysArr = cfg->FindArray("overlays");
    for (int i = 1; i < overlaysArr->Size(); i++) {
        sOverlays.push_back(new RndOverlay(overlaysArr->Array(i)));
    }
}
