#include "ui/UIListLabel.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Utl.h"
#include "ui/UILabel.h"
#include "ui/UIListSlot.h"
#include "utl/Symbol.h"
#ifdef HX_NATIVE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

#ifdef HX_NATIVE
namespace {
bool DebugChooseModeLabel(const UILabel *label) {
    static int enabled = -1;
    if (enabled == -1) {
        const char *env = getenv("MILO_DEBUG_CHOOSE_MODE");
        enabled = (env && env[0] && strcmp(env, "0") != 0) ? 1 : 0;
    }
    if (!enabled || !label) {
        return false;
    }
    const char *path = PathName(label);
    return path && strstr(path, "choose_mode");
}
}
#endif

#pragma region UIListLabel

UIListLabel::UIListLabel() : mLabel(this), mHighlightAltStyles(0) {}

RndTransformable *UIListLabel::RootTrans() { return mLabel; }

BEGIN_HANDLERS(UIListLabel)
    HANDLE_SUPERCLASS(UIListSlot)
END_HANDLERS

BEGIN_PROPSYNCS(UIListLabel)
    SYNC_PROP(label, mLabel)
    SYNC_PROP(highlight_alt_styles, mHighlightAltStyles)
    SYNC_SUPERCLASS(UIListSlot)
END_PROPSYNCS

BEGIN_SAVES(UIListLabel)
    SAVE_REVS(1, 1)
    SAVE_SUPERCLASS(UIListSlot)
    bs << mLabel;
    bs << mHighlightAltStyles;
END_SAVES

BEGIN_COPYS(UIListLabel)
    COPY_SUPERCLASS(UIListSlot)
    CREATE_COPY_AS(UIListLabel, l)
    MILO_ASSERT(l, 0xba);
    COPY_MEMBER_FROM(l, mLabel)
    COPY_MEMBER_FROM(l, mHighlightAltStyles)
END_COPYS

INIT_REVS(1, 1)

BEGIN_LOADS(UIListLabel)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 1)
    LOAD_SUPERCLASS(UIListSlot)
    bs >> mLabel;
    if (d.altRev < 1) {
        String tmp;
        bs >> tmp;
    }
    if (d.rev >= 1) {
        d >> mHighlightAltStyles;
    }
END_LOADS

const char *UIListLabel::GetDefaultText() const {
    if (mLabel)
        return mLabel->GetDefaultText();
    return gNullStr;
}

UILabel *UIListLabel::ElementLabel(int display) const {
    size_t size = mElements.size();
    if (size == 0)
        return 0;

    MILO_ASSERT((0) <= (display) && (display) < (size), 0x74);
    UIListLabelElement *le = dynamic_cast<UIListLabelElement *>(mElements[display]);
    MILO_ASSERT(le, 0x77);
    return le->mLabel;
}

UIListSlotElement *UIListLabel::CreateElement(UIList *uilist) {
    MILO_ASSERT(mLabel, 0x86);
    Hmx::Object *newObj = Hmx::Object::NewObject(mLabel->ClassName());
    UILabel *l = dynamic_cast<UILabel *>(newObj);
    MILO_ASSERT(l, 0x89);
    l->Copy(mLabel, kCopyDeep);
    l->SetTextToken(gNullStr);
    return new UIListLabelElement(this, l);
}

#pragma endregion UIListLabel
#pragma region UIListLabelElement

UIListLabelElement::~UIListLabelElement() { delete mLabel; }

void UIListLabelElement::Draw(const Transform &tf, float f, UIColor *col, Box *box) {
    mLabel->SetWorldXfm(tf);
    if (box) {
        Vector3 minPt(mLabel->mBoundsLeft, 0.0f, mLabel->mBoundsTop);
        Box localbox = *box;
        Vector3 maxPt(mLabel->mBoundsLeft + mLabel->mBoundsRight, 0.0f, mLabel->mBoundsTop + mLabel->mBoundsBottom);
        localbox.GrowToContain(minPt, false);
        localbox.GrowToContain(maxPt, false);
        box->GrowToContain(localbox.mMin, false);
        box->GrowToContain(localbox.mMax, false);
    } else {
#ifdef HX_NATIVE
        static int sChooseModeLabelDiag = 0;
        if (DebugChooseModeLabel(mLabel) && sChooseModeLabelDiag < 80) {
            printf(
                "DC3 UIListLabelElement::Draw label=%s token=%s alpha=%.3f color=%s styles=%u bounds=(%.2f,%.2f,%.2f,%.2f)\n",
                PathName(mLabel),
                mLabel->TextToken().Str(),
                f,
                col ? PathName(col) : "<null>",
                mLabel->NumStyles(),
                mLabel->mBoundsLeft,
                mLabel->mBoundsTop,
                mLabel->mBoundsRight,
                mLabel->mBoundsBottom
            );
            sChooseModeLabelDiag++;
        }
#endif
        float *savedAlphas = (float *)_alloca(mLabel->NumStyles() * sizeof(float));
        for (unsigned int i = 0; i < mLabel->NumStyles(); i++) {
            savedAlphas[i] = mLabel->Style(i).GetAlpha();
        }
        mLabel->LStyle(0).mColorOverride = col;
        if (mListLabel->mHighlightAltStyles) {
            for (unsigned int i = 1; i < mLabel->NumStyles(); i++) {
                mLabel->LStyle(i).mColorOverride = col;
            }
        }
        for (unsigned int i = 0; i < mLabel->NumStyles(); i++) {
            mLabel->Style(i).SetAlpha(f * savedAlphas[i]);
        }
        mLabel->DrawShowing();
        for (unsigned int i = 0; i < mLabel->NumStyles(); i++) {
            mLabel->Style(i).SetAlpha(savedAlphas[i]);
        }
    }
}

#pragma endregion UIListLabelElement
