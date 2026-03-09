#include "ui/UIListDir.h"
#include "obj/Object.h"
#include "rndobj/Dir.h"
#include "ui/UIListState.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Std.h"

namespace {
    class WidgetDrawSort {
    public:
        bool operator()(UIListWidget *w1, UIListWidget *w2) {
            return w1->DrawOrder() < w2->DrawOrder();
        }
    };
}

UIListDir::UIListDir()
    : mOrientation(kUIListVertical), mFadeOffset(0), mElementSpacing(50.0f),
      mScrollHighlightChange(0.5f), mTestMode(0), mTestState(this, this),
      mTestNumData(100), mTestGapSize(0.0f), mTestComponentState(UIComponent::kFocused),
      mTestDisableElements(0), mDirection(0) {
    mTestState.SetNumDisplay(5, true);
    mTestState.SetGridSpan(1, true);
    mTestState.SetSelected(0, -1, true);
}

UIListDir::~UIListDir() { DeleteAll(mTestWidgets); }

BEGIN_PROPSYNCS(UIListDir)
    SYNC_PROP_SET(orientation, mOrientation, mOrientation = (UIListOrientation)_val.Int())
    SYNC_PROP(fade_offset, mFadeOffset)
    SYNC_PROP(element_spacing, mElementSpacing)
    SYNC_PROP(scroll_highlight_change, mScrollHighlightChange)
    SYNC_PROP(test_mode, mTestMode)
    SYNC_PROP(test_num_data, mTestNumData)
    SYNC_PROP(test_gap_size, mTestGapSize)
    SYNC_PROP_SET(
        test_num_display,
        mTestState.NumDisplay(),
        mTestState.SetNumDisplay(_val.Int(), true)
    )
    SYNC_PROP_SET(
        test_grid_span, mTestState.GridSpan(), mTestState.SetGridSpan(_val.Int(), true)
    )
    SYNC_PROP_SET(test_scroll_time, mTestState.Speed(), mTestState.SetSpeed(_val.Float()))
    SYNC_PROP_SET(
        test_list_state,
        mTestComponentState,
        mTestComponentState = (UIComponent::State)_val.Int()
    )
    SYNC_PROP_MODIFY(test_disable_elements, mTestDisableElements, Reset())
    SYNC_SUPERCLASS(RndDir)
END_PROPSYNCS

// DECOMP: 87.9% match - AT_LIMIT
// Unfixable diffs:
// - Target uses __savegprlr_29/__restgprlr_29 helpers, base inlines register saves
// - Stack frame 0x90 vs 0x70: target allocates separate temps (0x58-0x6c), base reuses 0x54
// - Target pre-loads NumDisplay() into r29 across Write call
// - Speed() call is ICF-merged to merged_82752368 (verified: UIListState::Speed)
BEGIN_SAVES(UIListDir)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mOrientation;
    bs << mFadeOffset;
    bs << mTestMode;
    auto& testState = mTestState;
    bs << testState.NumDisplay();
    bs << mElementSpacing;
    bs << testState.Speed();
    bs << mTestNumData;
    bs << mTestComponentState;
    bs << mTestGapSize;
    bs << mTestDisableElements;
    bs << mScrollHighlightChange;
END_SAVES

BEGIN_COPYS(UIListDir)
    COPY_SUPERCLASS(RndDir)
    CREATE_COPY_AS(UIListDir, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(mOrientation)
        COPY_MEMBER(mFadeOffset)
        COPY_MEMBER(mElementSpacing)
        COPY_MEMBER(mScrollHighlightChange)
        COPY_MEMBER(mTestMode)
        mTestState.SetNumDisplay(c->mTestState.NumDisplay(), true);
        mTestState.SetGridSpan(c->mTestState.GridSpan(), true);
        mTestState.SetSpeed(c->mTestState.Speed());
        COPY_MEMBER(mTestNumData)
        COPY_MEMBER(mTestComponentState)
        COPY_MEMBER(mTestGapSize)
        COPY_MEMBER(mTestDisableElements)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(1, 0)

void UIListDir::PreLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(1, 0);
    RndDir::PreLoad(bs);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void UIListDir::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    RndDir::PostLoad(bs);
    int orientation, numdisplay, compstate;
    float speed;
    d >> orientation >> mFadeOffset;
    mOrientation = (UIListOrientation)orientation;
    d >> mTestMode >> numdisplay >> mElementSpacing >> speed >> mTestNumData >> compstate
        >> mTestGapSize >> mTestDisableElements;
    if (d.rev > 0) d >> mScrollHighlightChange;
    mTestState.SetNumDisplay(numdisplay, true);
    mTestState.SetSpeed(speed);
    mTestComponentState = (UIComponent::State)compstate;
}

void UIListDir::SyncObjects() {
    RndDir::SyncObjects();
    if (TheLoadMgr.EditMode()) {
        CreateElements(0, mTestWidgets, mTestState.NumDisplay());
        FillElements(mTestState, mTestWidgets);
    }
}

void UIListDir::DrawShowing() {
    if (mTestMode && TheLoadMgr.EditMode()) {
        UIListWidgetDrawState drawState;
        BuildDrawState(drawState, mTestState, mTestComponentState, 0.0f, true);
        DrawWidgets(drawState, mTestState, mTestWidgets, WorldXfm(), mTestComponentState, nullptr, false);
    } else
        RndDir::DrawShowing();
}

void UIListDir::Poll() {
    if (TheLoadMgr.EditMode()) {
        RndDir::Poll();
        if (mTestMode) {
            mTestState.Poll(TheTaskMgr.Seconds(TaskMgr::kRealTime));
            PollWidgets(mTestWidgets);
        }
    }
}

int UIListDir::NumData() const { return mTestNumData; }

float UIListDir::GapSize(int, int, int, int) const { return mTestGapSize; }

bool UIListDir::IsActive(int i) const {
    if (mTestDisableElements)
        return !(i % 2);
    else
        return true;
}

void UIListDir::StartScroll(const UIListState &state, int i, bool b) {
    StartScroll(state, mTestWidgets, i, b);
}

void UIListDir::CompleteScroll(const UIListState &state) {
    CompleteScroll(state, mTestWidgets);
}

UIListOrientation UIListDir::Orientation() const { return mOrientation; }

float UIListDir::ElementSpacing() const { return mElementSpacing; }

UIList *UIListDir::SubList(int i, std::vector<UIListWidget *> &vec) {
    FOREACH (it, vec) {
        UIList *l = (*it)->SubList(i);
        if (l)
            return l;
    }
    return nullptr;
}

void UIListDir::DrawWidgets(
    UIListWidgetDrawState &drawState,
    UIListState const &state,
    std::vector<UIListWidget *> &widgets,
    class Transform const &tf,
    UIComponent::State compState,
    Box *box,
    bool bDrawFocusedOrManual
) {
    bool scrolling = state.IsScrolling();
    FOREACH (it, widgets) {
        UIListWidget *widget = *it;
        UIListWidgetDrawType drawType = widget->WidgetDrawType();
        bool shouldDraw = false;
        if (drawType == kUIListWidgetDrawAlways) {
            shouldDraw = true;
        } else if (drawType == kUIListWidgetDrawFocusedOrManual) {
            if (bDrawFocusedOrManual || compState == UIComponent::kFocused) {
                shouldDraw = true;
            }
        } else if (drawType == kUIListWidgetDrawOnlyFocused) {
            if (compState == UIComponent::kFocused) {
                shouldDraw = true;
            }
        }

        if (shouldDraw) {
            DrawCommand cmd = scrolling ? kExcludeFirst : kDrawAll;
            widget->Draw(drawState, state, tf, compState, box, cmd);
        }
    }

    if (scrolling) {
        FOREACH (it, widgets) {
            UIListWidget *widget = *it;
            UIListWidgetDrawType drawType = widget->WidgetDrawType();
            bool shouldDrawFirst = false;
            if (drawType == kUIListWidgetDrawAlways) {
                shouldDrawFirst = true;
            } else if (drawType == kUIListWidgetDrawOnlyFocused && compState == UIComponent::kFocused) {
                shouldDrawFirst = true;
            }

            if (shouldDrawFirst) {
                widget->Draw(drawState, state, tf, compState, box, kDrawFirst);
            }
        }
    }
}

void UIListDir::PollWidgets(std::vector<UIListWidget *> &widgets) {
    FOREACH (it, widgets) {
        (*it)->Poll();
    }
}

void UIListDir::FillElement(
    UIListState const &state, std::vector<UIListWidget *> &vec, int i
) {
    int disp = state.Display2Data(i);
    if (disp != -1) {
        int snapped = state.SnappedDataForDisplay(i);
        if (snapped >= 0)
            disp = snapped;
        int disp2show = state.Display2Showing(i);
        bool wasNegOne = i == -1;
        ClampEq(i, 0, state.NumDisplay());
        FOREACH (it, vec) {
            (*it)->Fill(*state.Provider(), i, disp2show, disp);
            if (wasNegOne && snapped >= 0) {
                (*it)->Fill(
                    *state.Provider(), 1, state.Display2Showing(0), state.Display2Data(0)
                );
            }
        }
    }
}

void UIListDir::StartScroll(
    UIListState const &state, std::vector<UIListWidget *> &widgets, int i, bool b
) {
    mDirection = i;
    MILO_ASSERT(mDirection, 499);
    FOREACH (it, widgets) {
        (*it)->StartScroll(mDirection, b);
    }
    if (b) {
        FillElement(state, widgets, mDirection > 0 ? state.NumDisplay() : -1);
    }
}

void UIListDir::CompleteScroll(
    UIListState const &state, std::vector<UIListWidget *> &widgets
) {
    FOREACH (it, widgets) {
        (*it)->CompleteScroll(state, mDirection);
    }
    if (mDirection == 1 && state.SnappedDataForDisplay(0) >= 0) {
        FillElement(state, widgets, 0);
    }
}

void UIListDir::FillElements(UIListState const &state, std::vector<UIListWidget *> &vec) {
    int num = state.NumDisplayWithData();
    for (int i = 0; i < num; i++) {
        FillElement(state, vec, i);
    }
}

void UIListDir::ListEntered() {
    static Message start("start");
    Handle(start, false);
}

void UIListDir::BuildDrawState(
    UIListWidgetDrawState &drawState, UIListState const &state, UIComponent::State compState, float subListOffset, bool allowHighlight
) const {
    int numDisplay = state.NumDisplay();
    int numDisplayWithData = state.NumDisplayWithData();
    int gridSpan = state.GridSpan();

    int halfDisplay = numDisplay / 2;
    int fadeLimit = halfDisplay;
    if (halfDisplay > mFadeOffset) {
        fadeLimit = mFadeOffset;
    }

    if (mFadeOffset == 0) {
        fadeLimit = 0;
    }

    bool scrolling = state.IsScrolling();
    int selectedDisplay = state.SelectedDisplay();
    int selectedData = state.SelectedData();
    int currentScroll = state.CurrentScroll();

    float gapAccum = 0.0f;
    UIListProvider *provider = state.Provider();

    drawState.mElements.reserve(numDisplayWithData + 1);

    for (int i = 0; i < numDisplayWithData; i++) {
        UIListElementDrawState elem;
        int showing = state.Display2Showing(i);
        int data = state.Display2Data(i);

        if (i > 0) {
            int prevShowing = state.Display2Showing(i - 1);
            int prevData = state.Display2Data(i - 1);
            gapAccum += provider->GapSize(prevShowing, prevData, showing, data);
        }

        {
            Vector3 pos;
            SetElementPos(pos, (float)i, gridSpan, gapAccum + subListOffset, 0.0f);
            elem.mPosX = pos.x;
            elem.mPosY = pos.y;
            elem.mPosZ = pos.z;
        }

        float alpha = 1.0f;
        if (fadeLimit > 0) {
            if (i < fadeLimit) {
                alpha = (float)(i + 1) / (float)(fadeLimit + 1);
            }
            int fromEnd = (numDisplayWithData - 1) - i;
            if (fromEnd < fadeLimit) {
                float endAlpha = (float)(fromEnd + 1) / (float)(fadeLimit + 1);
                if (endAlpha < alpha) {
                    alpha = endAlpha;
                }
            }
        }

        bool active = (data != -1) && provider->IsActive(data);

        UIListWidgetState widgetState;
        if (allowHighlight && i == selectedDisplay) {
            widgetState = kUIListWidgetHighlight;
        } else if (active) {
            widgetState = kUIListWidgetActive;
        } else {
            widgetState = kUIListWidgetInactive;
        }

        UIComponent::State elemCompState;
        if (allowHighlight && i == selectedDisplay) {
            elemCompState = compState;
        } else {
            elemCompState = UIComponent::kNormal;
        }
        if (data != -1) {
            elemCompState = provider->ComponentStateOverride(showing, data, elemCompState);
        }

        elem.mActive = (data != -1);
        elem.mAlpha = alpha;
        elem.mElementState = widgetState;
        elem.mComponentState = elemCompState;
        elem.mDisplay = i;
        elem.mShowing = showing;
        elem.mData = data;

        drawState.mElements.push_back(elem);
    }

    if (scrolling) {
        int extraDisplay = currentScroll > 0 ? numDisplay : -1;
        int extraShowing = state.Display2Showing(extraDisplay);
        int extraData = state.Display2Data(extraDisplay);

        UIListElementDrawState scrollElem;
        scrollElem.mActive = (extraData != -1);

        float scrollGap = gapAccum;
        if (extraData != -1 && numDisplayWithData > 0) {
            int lastShowing = state.Display2Showing(numDisplayWithData - 1);
            int lastData = state.Display2Data(numDisplayWithData - 1);
            scrollGap += provider->GapSize(lastShowing, lastData, extraShowing, extraData);
        }

        {
            Vector3 pos;
            SetElementPos(pos, (float)extraDisplay, gridSpan, scrollGap + subListOffset, 0.0f);
            scrollElem.mPosX = pos.x;
            scrollElem.mPosY = pos.y;
            scrollElem.mPosZ = pos.z;
        }
        scrollElem.mAlpha = 0.0f;
        scrollElem.mElementState = kUIListWidgetActive;
        scrollElem.mComponentState = UIComponent::kNormal;
        if (extraData != -1) {
            scrollElem.mComponentState = provider->ComponentStateOverride(extraShowing, extraData, UIComponent::kNormal);
        }
        scrollElem.mDisplay = extraDisplay;
        scrollElem.mShowing = extraShowing;
        scrollElem.mData = extraData;
        drawState.mElements.push_back(scrollElem);
    }

    drawState.mHighlightDisplay = selectedDisplay;
    if (allowHighlight) {
        drawState.mHighlightElementState = kUIListWidgetHighlight;
    } else {
        drawState.mHighlightElementState = kUIListWidgetActive;
    }

    {
        Vector3 pos;
        SetElementPos(pos, (float)selectedDisplay, gridSpan, subListOffset, 0.0f);
        drawState.mHighlightPos = pos;
    }

    if (numDisplayWithData > 0) {
        drawState.mFirstPos.Set(drawState.mElements[0].mPosX, drawState.mElements[0].mPosY, drawState.mElements[0].mPosZ);
        drawState.mLastPos.Set(drawState.mElements[numDisplayWithData - 1].mPosX, drawState.mElements[numDisplayWithData - 1].mPosY, drawState.mElements[numDisplayWithData - 1].mPosZ);
    }
}

void UIListDir::CreateElements(UIList *uilist, std::vector<UIListWidget *> &vec, int i) {
    DeleteAll(vec);
    for (ObjDirItr<UIListWidget> it(this, true); it != 0; ++it) {
        auto newObj = Hmx::Object::NewObject(it->ClassName());
        UIListWidget *widget =
            dynamic_cast<UIListWidget *>(newObj);
        widget->ResourceCopy(it);
        widget->SetParentList(uilist);
        vec.push_back(widget);
    }
    std::sort(vec.begin(), vec.end(), WidgetDrawSort());
    FOREACH (it, vec) {
        (*it)->CreateElements(uilist, i);
    }
}

float UIListDir::SetElementPos(Vector3 &v, float position, int gridSpan, float primaryBase, float secondaryBase) const {
    v.Zero();

    float floored = std::floor(position);
    int intPos = (int)floored;

    int rowIndex = intPos / gridSpan;
    int colIndex = intPos % gridSpan;

    float colOffset = (float)colIndex;
    float secondaryOffset = colOffset * mElementSpacing + secondaryBase;

    float fractional = position - (float)intPos;
    float rowOffset = (float)rowIndex;
    float primaryOffset = (fractional + rowOffset) * mElementSpacing + primaryBase;

    if (mOrientation == kUIListVertical) {
        v.z -= primaryOffset;
        v.x += secondaryOffset;
    } else {
        v.x += primaryOffset;
        v.z -= secondaryOffset;
    }

    return primaryOffset;
}

void UIListDir::Reset() {
    mTestState.SetSelected(0, -1, true);
    FillElements(mTestState, mTestWidgets);
}

BEGIN_HANDLERS(UIListDir)
    HANDLE_ACTION(test_scroll, mTestState.Scroll(_msg->Int(2), false))
    HANDLE_SUPERCLASS(RndDir)
END_HANDLERS
