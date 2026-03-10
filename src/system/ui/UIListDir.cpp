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
    std::vector<UIListWidget *>::iterator it = widgets.begin();
    if (it != widgets.end()) {
        bool isFocused = (compState == UIComponent::kFocused);
        do {
            UIListWidget *widget = *it;
            UIListWidgetDrawType drawType = widget->WidgetDrawType();
            if (drawType == kUIListWidgetDrawAlways
                || (drawType == kUIListWidgetDrawFocusedOrManual
                    && (bDrawFocusedOrManual || isFocused))
                || (drawType == kUIListWidgetDrawOnlyFocused && isFocused)) {
                DrawCommand cmd = scrolling ? kExcludeFirst : kDrawAll;
                widget->Draw(drawState, state, tf, compState, box, cmd);
            }
            ++it;
        } while (it != widgets.end());
    }

    if (scrolling) {
        for (std::vector<UIListWidget *>::iterator it = widgets.begin();
             it != widgets.end(); ++it) {
            UIListWidget *widget = *it;
            UIListWidgetDrawType drawType = widget->WidgetDrawType();
            if (drawType == kUIListWidgetDrawAlways
                || (drawType == kUIListWidgetDrawOnlyFocused
                    && compState == UIComponent::kFocused)) {
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

    int fadeCountStart = numDisplay / 2;
    if ((int)(unsigned long)(unsigned int)fadeCountStart >= mFadeOffset) {
        fadeCountStart = mFadeOffset;
    }
    int fadeCountEnd = fadeCountStart;
    if (mFadeOffset != 0) {
        int fadeEndCalc;
        if (state.Circular()) {
            int selectedDisp = state.SelectedDisplay();
            if (selectedDisp < fadeCountStart) {
                fadeCountStart = selectedDisp;
            }
            fadeEndCalc = numDisplay - state.SelectedDisplay() - 1;
        } else {
            int firstShowing = state.FirstShowing();
            int adjustedFirstShowing = firstShowing;
            if (state.ScrollPastMinDisplay()) {
                adjustedFirstShowing = firstShowing - state.MinDisplay();
            }
            adjustedFirstShowing &= ((unsigned int)adjustedFirstShowing >> 31) - 1;
            if (adjustedFirstShowing < fadeCountStart) {
                fadeCountStart = adjustedFirstShowing;
            }
            auto _tmp0 = state.Provider()->NumData();
            fadeEndCalc = _tmp0 - adjustedFirstShowing - numDisplay;
        }
        if (fadeEndCalc < fadeCountEnd) {
            fadeCountEnd = fadeEndCalc;
        }
    }
    float fadeStartDist = (float)fadeCountStart * mElementSpacing;
    float fadeEndDist = (float)((numDisplay - fadeCountEnd - 1) * mElementSpacing);

    int direction;
    if (state.CurrentScroll() > 0) {
        direction = 1;
    } else {
        direction = -1;
    }
    int selected = state.Selected();
    int selectedData = state.SelectedData();
    int selectedDisplay = state.SelectedDisplay();
    drawState.mHighlightDisplay = selectedDisplay;

    if (state.IsScrolling()) {
        float speed = state.Speed();
        if (speed > mScrollHighlightChange) {
            selected += direction;
            drawState.mHighlightDisplay += direction;
        }
        numDisplayWithData++;
    }

    drawState.mElements.clear();
    drawState.mElements.reserve(numDisplayWithData);
    drawState.mHighlightElementState = kUIListWidgetActive;

    int prevData = 0;
    float lastPosBase = 0.0f;
    float highlightBase = 0.0f;
    float firstGap = 0.0f;
    float totalGap = 0.0f;
    Vector3 elemPos;

    float scrollOffset = (float)direction * state.Speed();

    for (int i = 0; i < numDisplayWithData; i++) {
        int dispIndex = i;
        if (state.IsScrolling() && direction == -1) {
            dispIndex = i - 1;
        }

        int data = state.Display2Data(dispIndex);
        if (data == -1) {
            UIListElementDrawState elem;
            elem.mActive = false;
            drawState.mElements.push_back(elem);
            continue;
        }

        if (!state.Circular() && prevData > data) {
            break;
        }

        int showing = state.Display2Showing(dispIndex);
        int snapped = state.SnappedDataForDisplay(dispIndex);
        if (snapped >= 0) {
            data = snapped;
        }
        prevData = data;

        float gap = state.Provider()->GapSize(showing, data, selectedData, direction);
        if (i == 0) {
            firstGap = gap;
        }
        float position;
        float primaryBase;
        if (state.ShouldHoldDisplayInPlace(dispIndex)) {
            primaryBase = totalGap;
            if (direction == -1) {
                position = (float)dispIndex + 1.0f;
            } else {
                position = (float)dispIndex;
            }
        } else {
            primaryBase = -((scrollOffset * firstGap) - totalGap);
            position = (float)dispIndex - scrollOffset;
        }

        float pos = SetElementPos(elemPos, position, state.GridSpan(), primaryBase, 0.0f);

        float alpha = 1.0f;
        if (!state.ShouldHoldDisplayInPlace(dispIndex)) {
            float dist = pos - (-((scrollOffset * firstGap) - totalGap));
            if (dist < fadeStartDist) {
                float fadeDist = fadeStartDist - dist;
                float fadeCount = (float)(fadeCountStart + 1);
                alpha -= fadeDist / (fadeCount * mElementSpacing);
            } else if (dist > fadeEndDist) {
                float fadeDist = dist - fadeEndDist;
                float fadeCount = (float)(fadeCountEnd + 1);
                alpha -= fadeDist / (fadeCount * mElementSpacing);
            }
        }

        UIListWidgetState elemState;
        if (!state.Provider()->IsActive(data)) {
            elemState = kUIListWidgetInactive;
        } else if (showing == selected && allowHighlight) {
            elemState = kUIListWidgetHighlight;
        } else {
            elemState = kUIListWidgetActive;
        }

        UIListWidgetState widgetState = state.Provider()->ElementStateOverride(showing, data, elemState);
        if (showing == selected) {
            drawState.mHighlightElementState = widgetState;
        }

        UIListElementDrawState elem;
        elem.mActive = true;
        *(Vector3 *)&elem.mPosX = elemPos;
        elem.mScaleX = 1.0f;
        elem.mScaleY = 1.0f;
        elem.mScaleZ = 1.0f;
        elem.mAlpha = alpha;
        elem.mElementState = widgetState;
        elem.mComponentState = state.Provider()->ComponentStateOverride(showing, data, compState);
        elem.mDisplay = dispIndex;
        elem.mShowing = showing;
        elem.mData = data;
        drawState.mElements.push_back(elem);

        totalGap += gap;
        if (dispIndex > 0 && dispIndex < state.NumDisplay() - 1) {
            lastPosBase += gap;
        }
        if (dispIndex < selectedDisplay) {
            highlightBase += gap;
        }
    }

    SetElementPos(drawState.mFirstPos, 0.0f, state.GridSpan(), 0.0f, 0.0f);
    SetElementPos(drawState.mLastPos, (float)(state.NumDisplay() - 1), state.GridSpan(), lastPosBase, 0.0f);
    auto _tmp4 = state.GridSpan();
    SetElementPos(drawState.mHighlightPos, (float)selectedDisplay, _tmp4, highlightBase, subListOffset);
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
