#include "ui/UIListDir.h"
#include "obj/Object.h"
#include "rndobj/Dir.h"
#include "ui/UIComponent.h"
#include "ui/UIListState.h"
#include "ui/UIListWidget.h"
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

BEGIN_HANDLERS(UIListDir)
    HANDLE_ACTION(test_scroll, mTestState.Scroll(_msg->Int(2), false))
    HANDLE_SUPERCLASS(RndDir)
END_HANDLERS

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

BEGIN_SAVES(UIListDir)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mOrientation << mFadeOffset;
    int numDisp = mTestState.NumDisplay();
    bs << mTestMode << numDisp << mElementSpacing << mTestState.Speed() << mTestNumData
       << mTestComponentState << mTestGapSize << mTestDisableElements
       << mScrollHighlightChange;
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
    d.PushRev(this);
}

void UIListDir::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    RndDir::PostLoad(bs);
    int orientation;
    d >> orientation;
    d >> mFadeOffset;
    mOrientation = (UIListOrientation)orientation;
    int numDisp;
    float speed;
    int state;
    d >> mTestMode >> numDisp >> mElementSpacing >> speed >> mTestNumData >> state
        >> mTestGapSize >> mTestDisableElements;
    if (d.rev > 0) {
        d >> mScrollHighlightChange;
    }
    mTestState.SetNumDisplay(numDisp, true);
    mTestState.SetSpeed(speed);
    mTestComponentState = (UIComponent::State)state;
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
        BuildDrawState(drawState, mTestState, mTestComponentState, 0, true);
        DrawWidgets(
            drawState, mTestState, mTestWidgets, WorldXfm(), mTestComponentState, nullptr, false
        );
    } else {
        RndDir::DrawShowing();
    }
    // clang-format off
//       if ((this[0x178] == (UIListDir)0x0) || (LoadMgrEditMode == '\0')) {
//     RndDir::DrawShowing((RndDir *)this);
//   }
//   else {
//     local_38 = (UIListElementDrawState *)0x0;
//     local_34 = 0;
//     local_30[0] = 0;
//     BuildDrawState(this + -0x9c,aUStack_70,(UIListState *)(this + 0x17c),*(State *)(this + 0x1cc),
//                    0.0,in_r7);
//     if (this[0x10d] == (UIListDir)0x0) {
//       pTVar1 = (Transform *)(this + 0x98);
//     }
//     else {
//       pTVar1 = RndTransformable::WorldXfm_Force((RndTransformable *)(this + 0x50));
//     }
//     DrawWidgets(this + -0x9c,aUStack_70,(UIListState *)(this + 0x17c),(vector<> *)(this + 0x1d4),
//                 pTVar1,*(State *)(this + 0x1cc),(Box *)0x0,false);
//     if (local_38 != (UIListElementDrawState *)0x0) {
//       stlpmtx_std::StlNodeAlloc<>::deallocate
//                 ((StlNodeAlloc<> *)local_30,local_38,(local_30[0] - (int)local_38) / 0x3c);
//     }
//   }
//   return;
    // clang-format on
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

// void UIListDir::DrawWidgets(
//     UIListWidgetDrawState &,
//     UIListState const &,
//     std::vector<UIListWidget *> &,
//     class Transform const &,
//     UIComponent::State,
//     Box *,
//     bool
// ) {}

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
        bool isnegone = i == -1;
        ClampEq(i, 0, state.NumDisplay());
        FOREACH (it, vec) {
            (*it)->Fill(*state.Provider(), i, disp2show, disp);
            if (isnegone && snapped >= 0) {
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

// void UIListDir::BuildDrawState(
//     UIListWidgetDrawState &, UIListState const &, UIComponent::State, float, bool
// ) const {}

void UIListDir::CreateElements(UIList *uilist, std::vector<UIListWidget *> &vec, int i) {
    DeleteAll(vec);
    for (ObjDirItr<UIListWidget> it(this, true); it != 0; ++it) {
        UIListWidget *widget =
            dynamic_cast<UIListWidget *>(Hmx::Object::NewObject(it->ClassName()));
        widget->ResourceCopy(it);
        widget->SetParentList(uilist);
        vec.push_back(widget);
    }
    std::sort(vec.begin(), vec.end(), WidgetDrawSort());
    FOREACH (it, vec) {
        (*it)->CreateElements(uilist, i);
    }
}

float UIListDir::SetElementPos(Vector3 &v, float f1, int i2, float f3, float f4) const {
    v.Zero();
    int floored = std::floor(f1);
    float f3toset =
        mElementSpacing * ((f1 - (float)floored) + (float)(floored / i2)) + f3;
    float f2toset = mElementSpacing * (float)(floored % i2) + f4;
    if (mOrientation == kUIListVertical) {
        v.z -= f3toset;
        v.x += f2toset;
    } else {
        v.x += f3toset;
        v.z -= f2toset;
    }
    return f3toset;
}

void UIListDir::Reset() {
    mTestState.SetSelected(0, -1, true);
    FillElements(mTestState, mTestWidgets);
}
