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

    int fadeOffset = mFadeOffset;
    int fadeCountStart = numDisplay / 2;
    if (fadeOffset < fadeCountStart) {
        fadeCountStart = fadeOffset;
    }
    int fadeCountEnd = fadeCountStart;
    if (fadeOffset > 0) {
        int fadeEndCalc;
        if (!state.Circular()) {
            int firstShowing = state.FirstShowing();
            int adjustedFirstShowing = firstShowing;
            if (state.ScrollPastMinDisplay()) {
                adjustedFirstShowing = firstShowing - state.MinDisplay();
            }
            if (adjustedFirstShowing < 0) {
                adjustedFirstShowing = -adjustedFirstShowing;
            }
            if (adjustedFirstShowing < fadeCountStart) {
                fadeCountStart = adjustedFirstShowing;
            }
            fadeEndCalc = state.Provider()->NumData() - adjustedFirstShowing;
            fadeEndCalc = fadeEndCalc - numDisplay;
        } else {
            int selectedDisp = state.SelectedDisplay();
            if (selectedDisp < fadeCountStart) {
                fadeCountStart = selectedDisp;
            }
            fadeEndCalc = numDisplay - selectedDisp - 1;
        }
        if (fadeEndCalc < fadeCountEnd) {
            fadeCountEnd = fadeEndCalc;
        }
    }
    float fadeStartDist = (float)fadeCountStart * mElementSpacing;
    float fadeEndDist = (float)fadeCountEnd * mElementSpacing;

    int direction = 1;
    if (state.CurrentScroll() <= 0) {
        direction = -1;
    }
    int selected = state.Selected();
    int selectedData = state.SelectedData();
    int selectedDisplay = state.SelectedDisplay();
    drawState.mHighlightDisplay = selectedDisplay;

    int effectiveSelected = selected;
    if (state.IsScrolling()) {
        float speed = state.Speed();
        if (speed > mScrollHighlightChange) {
            effectiveSelected += direction;
            selectedDisplay += direction;
        }
        numDisplayWithData = numDisplayWithData + 1;
    }

    drawState.mElements.clear();
    drawState.mElements.reserve(numDisplayWithData);
    drawState.mHighlightElementState = allowHighlight ? kUIListWidgetHighlight : kUIListWidgetActive;

    float primaryOffset = 0.0f;
    float offsetBeforeFirst = 0.0f;
    float offsetBeforeSelected = 0.0f;
    float gapAccum = 0.0f;
    float firstGap = 0.0f;
    UIListProvider *provider = state.Provider();
    int prevData = 0;

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
            elem.mPosX = 0.0f;
            elem.mPosY = 0.0f;
            elem.mPosZ = 0.0f;
            elem.mAlpha = 0.0f;
            elem.mElementState = kUIListWidgetActive;
            elem.mComponentState = UIComponent::kNormal;
            elem.mDisplay = dispIndex;
            elem.mShowing = 0;
            elem.mData = -1;
            drawState.mElements.push_back(elem);
            continue;
        }

        if (!state.Circular() && data < prevData) {
            break;
        }

        int showing = state.Display2Showing(dispIndex);
        int snapped = state.SnappedDataForDisplay(dispIndex);
        if (snapped >= 0) {
            data = snapped;
        }
        prevData = data;

        float gap = provider->GapSize(showing, data, selectedData, direction);
        if (i == 0) {
            firstGap = gap;
        }
        offsetBeforeFirst = gapAccum;

        float primaryBase;
        float secondaryBase;
        if (state.ShouldHoldDisplayInPlace(dispIndex)) {
            primaryBase = 0.0f;
            secondaryBase = 0.0f;
            if (direction == -1) {
                gapAccum = 1.0f;
            }
        } else {
            primaryBase = -((scrollOffset * firstGap) - primaryOffset);
            secondaryBase = 0.0f;
            gapAccum = (float)dispIndex - scrollOffset;
        }

        float pos = SetElementPos(*(Vector3 *)&drawState.mElements[0], (float)dispIndex, gridSpan, gapAccum, primaryBase);

        float alpha = 1.0f;
        if (!state.ShouldHoldDisplayInPlace(dispIndex)) {
            float dist = pos + ((scrollOffset * firstGap) - primaryOffset);
            float fadeDist;
            int fadeCount;
            if (dist < fadeStartDist) {
                fadeDist = fadeStartDist - dist;
                fadeCount = fadeCountStart + 1;
            } else if (dist > fadeEndDist) {
                fadeDist = dist - fadeEndDist;
                fadeCount = fadeCountEnd + 1;
            } else {
                fadeDist = 0.0f;
            }
            if (fadeDist > 0.0f) {
                alpha = 1.0f - (fadeDist / ((float)fadeCount * mElementSpacing));
            }
            primaryOffset = alpha;
        }

        UIListWidgetState widgetState;
        if (!provider->IsActive(data)) {
            widgetState = kUIListWidgetInactive;
        } else if (showing == selected) {
            widgetState = kUIListWidgetHighlight;
        } else {
            widgetState = kUIListWidgetActive;
        }

        UIListWidgetState elemState = provider->ElementStateOverride(showing, data, widgetState);
        if (showing == selected) {
            drawState.mHighlightElementState = elemState;
        }

        UIListElementDrawState elem;
        elem.mActive = true;
        elem.mPosX = drawState.mElements[0].mPosX;
        elem.mPosY = drawState.mElements[0].mPosY;
        elem.mPosZ = drawState.mElements[0].mPosZ;
        elem.mAlpha = alpha;
        elem.mElementState = elemState;
        elem.mComponentState = provider->ComponentStateOverride(showing, data, compState);
        elem.mDisplay = dispIndex;
        elem.mShowing = showing;
        elem.mData = data;
        drawState.mElements.push_back(elem);

        primaryOffset += gap;
        if (dispIndex > 0 && dispIndex < numDisplay - 1) {
            offsetBeforeSelected += gap;
        }
        if (dispIndex < selectedDisplay) {
            offsetBeforeSelected += gap;
        }
    }

    SetElementPos(drawState.mFirstPos, 0.0f, gridSpan, 0.0f, 0.0f);
    SetElementPos(drawState.mLastPos, (float)(numDisplay - 1), gridSpan, offsetBeforeSelected, 0.0f);
    SetElementPos(drawState.mHighlightPos, (float)selectedDisplay, gridSpan, offsetBeforeSelected + subListOffset, 0.0f);
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
