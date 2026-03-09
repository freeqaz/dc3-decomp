#include "ui/UIListLabel.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Utl.h"
#include "ui/UILabel.h"
#include "ui/UIListSlot.h"
#include "utl/Symbol.h"

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
    if (d.altRev > 0) {
        bs >> mHighlightAltStyles;
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
    auto newObj = Hmx::Object::NewObject(mLabel->ClassName());
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
        Box localbox = *box;
        Vector3 minPt(mLabel->mBoundsLeft, mLabel->mBoundsTop, 0.0f);
        Vector3 maxPt(mLabel->mBoundsLeft + mLabel->mBoundsRight, mLabel->mBoundsTop + mLabel->mBoundsBottom, 0.0f);
        localbox.GrowToContain(minPt, false);
        localbox.GrowToContain(maxPt, false);
        box->GrowToContain(localbox.mMin, false);
        box->GrowToContain(localbox.mMax, false);
    } else {
        int numStyles = mLabel->NumStyles();
        float *savedAlphas = (float *)_alloca(numStyles * sizeof(float));
        for (int i = 0; i < numStyles; i++) {
            savedAlphas[i] = mLabel->Style(i).GetAlpha();
        }
        mLabel->LStyle(0).mColorOverride = col;
        if (mListLabel->mHighlightAltStyles) {
            for (int i = 1; i < numStyles; i++) {
                mLabel->LStyle(i).mColorOverride = col;
            }
        }
        for (int i = 0; i < numStyles; i++) {
            mLabel->Style(i).SetAlpha(f * savedAlphas[i]);
        }
        mLabel->DrawShowing();
        for (int i = 0; i < numStyles; i++) {
            mLabel->Style(i).SetAlpha(savedAlphas[i]);
        }
    }
}

#pragma endregion UIListLabelElement
