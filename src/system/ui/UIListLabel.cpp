#include "ui/UIListLabel.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Utl.h"
#include "ui/UILabel.h"
#include "ui/UIListSlot.h"
#include "utl/Symbol.h"

#pragma region UIListLabel

UIListLabel::UIListLabel() : mLabel(this), mHighlightAltStyles(0) {}

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

INIT_REVS(1, 0)

BEGIN_LOADS(UIListLabel)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(UIListSlot)
    bs >> mLabel;
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
    UILabel *l = dynamic_cast<UILabel *>(Hmx::Object::NewObject(mLabel->ClassName()));
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
        int numFontMaps = mLabel->mFontMaps.size();
        for (int i = 0; i < numFontMaps; i++) {
            RndText::FontMapBase *fm = mLabel->mFontMaps[i];
            int numMeshes = fm->NumMeshes();
            for (int j = 0; j < numMeshes; j++) {
                Box meshbox;
                CalcBox(fm->Mesh(j), meshbox);
                localbox.GrowToContain(meshbox.mMin, false);
                localbox.GrowToContain(meshbox.mMax, false);
            }
        }
        box->GrowToContain(localbox.mMin, false);
        box->GrowToContain(localbox.mMax, false);
    } else {
        float oldAlpha = mLabel->Style(0).GetAlpha();
        UILabel::LabelStyle &ls0 = mLabel->LStyle(0);
        UIColor *oldColorOverride = ls0.mColorOverride;
        ls0.mColorOverride = col;
        mLabel->Style(0).SetAlpha(f * oldAlpha);
        mLabel->DrawShowing();
        mLabel->Style(0).SetAlpha(oldAlpha);
        ls0.mColorOverride = oldColorOverride;
    }
}

#pragma endregion UIListLabelElement
