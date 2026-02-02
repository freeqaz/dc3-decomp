#include "ui/UIListSlot.h"
#include "macros.h"
#include "obj/Object.h"
#include "ui/UIList.h"
#include "ui/UIListState.h"
#include "ui/UIListWidget.h"
#include "utl/Std.h"

UIListSlot::UIListSlot() : mSlotDrawType(kUIListSlotDrawAlways), mNextElement(0) {}

BEGIN_PROPSYNCS(UIListSlot)
    SYNC_PROP_SET(
        slot_draw_type, (int)mSlotDrawType, mSlotDrawType = (UIListSlotDrawType)_val.Int()
    )
    SYNC_SUPERCLASS(UIListWidget)
END_PROPSYNCS

BEGIN_SAVES(UIListSlot)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(UIListWidget)
    bs << mSlotDrawType;
END_SAVES

BEGIN_COPYS(UIListSlot)
    COPY_SUPERCLASS(UIListWidget)
    CREATE_COPY_AS(UIListSlot, s)
    MILO_ASSERT(s, 0xe1);
    COPY_MEMBER_FROM(s, mSlotDrawType)
END_COPYS

BEGIN_LOADS(UIListSlot)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(UIListWidget)
    int ty;
    bs >> ty;
    mSlotDrawType = (UIListSlotDrawType)ty;
END_LOADS

void UIListSlot::ResourceCopy(const UIListWidget *w) {
    UIListWidget::ResourceCopy(w);
    mMatchName = w->Name();
}

void UIListSlot::CreateElements(UIList *uilist, int count) {
    if (RootTrans()) {
        ClearElements();
        for (int i = 0; i < count; i++) {
            mElements.push_back(CreateElement(uilist));
        }
        mNextElement = CreateElement(uilist);
    }
}

void UIListSlot::Draw(
    const UIListWidgetDrawState &drawstate,
    const UIListState &liststate,
    const Transform &ctf,
    UIComponent::State compstate,
    Box *box,
    DrawCommand cmd
) {
    RndTransformable *root = RootTrans();
    if (root) {
        int thesize = drawstate.mElements.size();
        if (thesize > mElements.size()) {
            MILO_FAIL("%i isn't enough elements (need %i)", mElements.size(), thesize);
        }
        Transform tf78(root->WorldXfm());
        Transform tfa8;
        UIListProvider *prov = liststate.Provider();
        float d10;
        UIColor *uicolor;
        for (int i = 0; i < thesize; i++) {
            const UIListElementDrawState &curdrawstate = drawstate.mElements[i];
            if (curdrawstate.unk0) {
                d10 = 1.0f;
                uicolor = 0;
                if (!box) {
                    if (mSlotDrawType == kUIListSlotDrawHighlight
                            && curdrawstate.mDisplay != drawstate.mHighlightDisplay
                        || mSlotDrawType == kUIListSlotDrawNoHighlight
                            && curdrawstate.mDisplay == drawstate.mHighlightDisplay) {
                        continue;
                    }

                    UIListWidgetState slotoverride = prov->SlotElementStateOverride(
                        curdrawstate.mShowing,
                        curdrawstate.mData,
                        this,
                        curdrawstate.mElementState
                    );
                    UIComponent::State curcompstate = curdrawstate.mComponentState;
                    uicolor = DisplayColor(slotoverride, curcompstate);
                    uicolor = prov->SlotColorOverride(
                        curdrawstate.mShowing, curdrawstate.mData, this, uicolor
                    );
                    d10 = curdrawstate.mAlpha;
                    if (curcompstate == UIComponent::kDisabled)
                        d10 *= DisabledAlphaScale();
                    prov->PreDraw(curdrawstate.mShowing, curdrawstate.mData, this);
                }
                tfa8 = tf78;
                if (ParentList())
                    ParentList()->AdjustTrans(tfa8, curdrawstate);
                CalcXfm(ctf, curdrawstate.mPos, tfa8);
                if (cmd != kExcludeFirst || i > 0) {
                    mElements[i]->Draw(tfa8, d10, uicolor, box);
                }
                if (cmd == kDrawFirst)
                    return;
            }
        }
    }
}

void UIListSlot::Fill(const UIListProvider &prov, int display, int j, int k) {
    if (RootTrans()) {
        MILO_ASSERT(display < mElements.size(), 0x98);
        mElements[display]->Fill(prov, j, k);
    }
}

void UIListSlot::StartScroll(int i, bool b) {
    if (b && RootTrans()) {
        mElements.insert(i < 0 ? mElements.begin() : mElements.end(), mNextElement);
        mNextElement = 0;
    }
}

void UIListSlot::CompleteScroll(const UIListState &, int) {}

void UIListSlot::Poll() {
    FOREACH (it, mElements) {
        (*it)->Poll();
    }
}

bool UIListSlot::Matches(const char *cc) const {
    return strcmp(mMatchName.c_str(), cc) == 0;
}

const char *UIListSlot::MatchName() const { return mMatchName.c_str(); }

void UIListSlot::ClearElements() {
    DeleteAll(mElements);
    RELEASE(mNextElement);
}
