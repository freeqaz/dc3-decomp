#include "ui/UIList.h"
#include "ui/Utl.h"
#include "math/Geo.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/JoypadMsgs.h"
#include "os/User.h"
#include "rndobj/Draw.h"
#include "rndobj/FontBase.h"
#include "ui/UI.h"
#include "ui/UIComponent.h"
#include "ui/UIListArrow.h"
#include "ui/UIListCustom.h"
#include "ui/UIListDir.h"
#include "ui/UIListHighlight.h"
#include "ui/UIListLabel.h"
#include "ui/UIListMesh.h"
#include "ui/UIListProvider.h"
#include "ui/UIListSlot.h"
#include "ui/UIListState.h"
#include "ui/UIListSubList.h"
#include "ui/UIListWidget.h"
#include "ui/UITransitionHandler.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

static bool gLoading = false;

UIList::UIList()
    : UITransitionHandler(this), mListDir(this), mListState(this, this), mDataProvider(0),
      mNumData(100), mPaginate(0), mUser(0), mParent(0), mExtendedLabelEntries(this),
      mExtendedMeshEntries(this), mExtendedCustomEntries(this), mAutoScrollPause(2),
      mAutoScrollSendMsgs(0), unk150(1), mAutoScrolling(0), unk158(-1), unk15c(0),
      unk15d(0), unk160(1), mAllowHighlight(1) {}

UIList::~UIList() {
    DeleteAll(mWidgets);
    RELEASE(mDataProvider);
}

BEGIN_HANDLERS(UIList)
    HANDLE_MESSAGE(ButtonDownMsg)
    HANDLE(selected_sym, OnSelectedSym)
    HANDLE_EXPR(selected_pos, SelectedPos())
    HANDLE_EXPR(selected_data, SelectedData())
    HANDLE_EXPR(num_display, NumDisplay())
    HANDLE_EXPR(first_showing, FirstShowing())
    HANDLE_ACTION(set_provider, SetProvider(_msg->Obj<UIListProvider>(2)))
    HANDLE(set_data, OnSetData)
    HANDLE_EXPR(num_data, NumProviderData())
    HANDLE_ACTION(disable_data, DisableData(_msg->Sym(2)))
    HANDLE_ACTION(enable_data, EnableData(_msg->Sym(2)))
    HANDLE_ACTION(dim_data, DimData(_msg->Sym(2)))
    HANDLE_ACTION(undim_data, UnDimData(_msg->Sym(2)))
    HANDLE(set_selected, OnSetSelected)
    HANDLE(set_selected_simulate_scroll, OnSetSelectedSimulateScroll)
    HANDLE_ACTION(set_scroll_user, mUser = _msg->Obj<LocalUser>(2))
    HANDLE_ACTION(refresh, Refresh(true))
    HANDLE_ACTION(set_draw_manually_controlled_widgets, unk15d = _msg->Int(2))
    HANDLE(scroll, OnScroll)
    HANDLE_EXPR(is_scrolling, IsScrolling())
    HANDLE_EXPR(is_scrolling_down, mListState.CurrentScroll() > 0)
    HANDLE_ACTION(store, Store())
    HANDLE_ACTION(undo, RevertScrollSelect(this, _msg->Obj<LocalUser>(2), nullptr))
    HANDLE_ACTION(confirm, Reset())
    HANDLE_ACTION(set_num_display, SetNumDisplay(_msg->Int(2)))
    HANDLE_ACTION(set_grid_span, SetGridSpan(_msg->Int(2)))
    HANDLE_ACTION(auto_scroll, AutoScroll())
    HANDLE_ACTION(stop_auto_scroll, mAutoScrolling = false)
    HANDLE_EXPR(parent_list, mParent)
    HANDLE_ACTION(allow_highlight, mAllowHighlight = _msg->Int(2))
    HANDLE_SUPERCLASS(ScrollSelect)
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS

BEGIN_PROPSYNCS(UIList)
    SYNC_PROP_MODIFY(list_resource, mListDir, Update())
    SYNC_PROP_SET(display_num, NumDisplay(), SetNumDisplay(_val.Int()))
    SYNC_PROP_SET(grid_span, GridSpan(), SetGridSpan(_val.Int()))
    SYNC_PROP_SET(circular, Circular(), SetCircular(_val.Int()))
    SYNC_PROP_SET(scroll_time, Speed(), SetSpeed(_val.Float()))
    SYNC_PROP(paginate, mPaginate)
    SYNC_PROP_SET(
        min_display, mListState.MinDisplay(), mListState.SetMinDisplay(_val.Int())
    )
    SYNC_PROP_SET(
        scroll_past_min_display,
        mListState.ScrollPastMinDisplay(),
        mListState.SetScrollPastMinDisplay(_val.Int())
    )
    SYNC_PROP_SET(
        scroll_past_min_display,
        mListState.ScrollPastMinDisplay(),
        mListState.SetScrollPastMinDisplay(_val.Int())
    )
    SYNC_PROP_SET(
        max_display, mListState.MaxDisplay(), mListState.SetMaxDisplay(_val.Int())
    )
    SYNC_PROP_SET(
        scroll_past_max_display,
        mListState.ScrollPastMaxDisplay(),
        mListState.SetScrollPastMaxDisplay(_val.Int())
    )
    SYNC_PROP_MODIFY(num_data, mNumData, Update())
    SYNC_PROP(auto_scroll_pause, mAutoScrollPause)
    SYNC_PROP(auto_scroll_send_messages, mAutoScrollSendMsgs)
    SYNC_PROP(extended_label_entries, mExtendedLabelEntries)
    SYNC_PROP(extended_mesh_entries, mExtendedMeshEntries)
    SYNC_PROP(extended_custom_entries, mExtendedCustomEntries)
    SYNC_PROP_SET(in_anim, GetInAnim(), SetInAnim(_val.Obj<RndAnimatable>()))
    SYNC_PROP_SET(out_anim, GetOutAnim(), SetOutAnim(_val.Obj<RndAnimatable>()))
    SYNC_PROP_SET(
        limit_circular_display_num_to_data_num,
        mLimitCircularDisplayNumToDataNum,
        LimitCircularDisplay(_val.Int())
    )
    SYNC_SUPERCLASS(ScrollSelect)
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

BEGIN_SAVES(UIList)
    SAVE_REVS(0x15, 0)
    SAVE_SUPERCLASS(UIComponent)
    bs << mListDir;
    bs << NumDisplay();
    bs << GridSpan();
    bs << Circular();
    bs << Speed();
    bs << mListState.ScrollPastMinDisplay();
    bs << mListState.ScrollPastMaxDisplay();
    bs << mPaginate;
    bs << mSelectToScroll;
    bs << mListState.MinDisplay();
    bs << mListState.MaxDisplay();
    bs << mNumData;
    bs << mAutoScrollPause;
    bs << mAutoScrollSendMsgs;
    bs << mExtendedLabelEntries;
    bs << mExtendedMeshEntries;
    bs << mExtendedCustomEntries;
    SaveHandlerData(bs);
    bs << mLimitCircularDisplayNumToDataNum;
END_SAVES

BEGIN_LOADS(UIList)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

BEGIN_COPYS(UIList)
    COPY_SUPERCLASS(UIComponent)
    CREATE_COPY(UIList)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mListDir)
        mListState.SetCircular(c->mListState.Circular(), true);
        mListState.SetNumDisplay(c->mListState.NumDisplay(), true);
        mListState.SetGridSpan(c->mListState.GridSpan(), true);
        mListState.SetSpeed(c->mListState.Speed());
        mListState.SetMinDisplay(c->mListState.MinDisplay());
        mListState.SetScrollPastMinDisplay(c->mListState.ScrollPastMinDisplay());
        mListState.SetMaxDisplay(c->mListState.MaxDisplay());
        mListState.SetScrollPastMaxDisplay(c->mListState.ScrollPastMaxDisplay());
        COPY_MEMBER(mExtendedLabelEntries)
        COPY_MEMBER(mExtendedMeshEntries)
        COPY_MEMBER(mExtendedCustomEntries)
        CopyHandlerData(c);
    END_COPYING_MEMBERS
    Update();
END_COPYS

UIListDir *UIList::GetUIListDir() const { return mListDir; }

int UIList::SelectedPos() const { return mListState.Selected(); }

bool UIList::IsScrolling() const { return mListState.IsScrolling(); }

void UIList::SetSpeed(float speed) { mListState.SetSpeed(speed); }

float UIList::Speed() const { return mListState.Speed(); }

void UIList::SetParent(UIList *uilist) { mParent = uilist; }

// noinline: Prevents inlining of this stub implementation. Remove once fully implemented.
// TODO: implement properly - 524 bytes in target
// See RB3: box.Set(WorldXfm().v, WorldXfm().v);
//          mListDir->DrawWidgets(mListState, mWidgets, WorldXfm(), DrawState(this), &box, ...);
__declspec(noinline) void UIList::CalcBoundingBox(Box &box) {
    Transform xfm = WorldXfm();
    box.Set(xfm.v, xfm.v);

    int selectedDisplay = mListState.SelectedDisplay();
    UIList *subList = mListDir->SubList(selectedDisplay, mWidgets);

    float elementSpacing = 0.0f;
    if (subList) {
        int subSelectedDisplay = mListState.SelectedDisplay();
        float spacing = mListDir->ElementSpacing();
        elementSpacing = spacing * (float)(double)subSelectedDisplay;
    }

    UIListWidgetDrawState drawState;
    UIComponent::State state = DrawState(this);
    mListDir->BuildDrawState(drawState, mListState, state, elementSpacing, true);

    Transform xfm2 = WorldXfm();
    UIComponent::State state2 = DrawState(this);
    mListDir->DrawWidgets(drawState, mListState, mWidgets, xfm2, state2, &box, (bool)unk15d);
}

Symbol UIList::SelectedSym(bool fail) const {
    Symbol sym = mListState.Provider()->DataSymbol(mListState.SelectedData());
    if (fail) {
        if (sym == gNullStr)
            MILO_FAIL("DataSymbol() not implemented in UIList provider");
    }
    return sym;
}

void UIList::Scroll(int i) {
    unk15c = true;
    mListState.Scroll(i, false);
}

void UIList::StopAutoScroll() { mAutoScrolling = false; }

int UIList::NumProviderData() const {
    UIListProvider *p = mListState.Provider();
    if (p)
        return p->NumData();
    else
        return NumData();
}

int UIList::SelectedAux() const { return mListState.Selected(); }

bool UIList::IsEmptyValue() const { return SelectedData() == -1; }

void UIList::AutoScroll() {
    UIListProvider *prov = mListState.Provider();
    if (!prov)
        prov = this;
    if (prov->NumData() <= NumDisplay()) {
        StopAutoScroll();
    } else {
        mAutoScrolling = true;
        unk150 = 1;
        unk158 = mAutoScrollPause + TheTaskMgr.UISeconds();
    }
}

void UIList::Enter() {
    UIComponent::Enter();
    Reset();
    mListDir->ListEntered();
}

void UIList::Poll() {
    UIComponent::Poll();
    if (mAutoScrolling) {
        if (unk158 >= 0.0f && TheTaskMgr.UISeconds() >= unk158) {
            Scroll(unk150);
            unk158 = -1.0f;
        }
    }
    mListState.Poll(TheTaskMgr.UISeconds());
    mListDir->PollWidgets(mWidgets);
    unk15c = false;
    UpdateHandler();
}

int UIList::CollidePlane(std::vector<Vector3> const &vec, Plane const &p) { return 0; }

void UIList::StartScroll(UIListState const &state, int i, bool b) {
    mListDir->StartScroll(state, mWidgets, i, b);
    if (state.Provider()->IsActive(state.SelectedData())
        && (!mAutoScrolling || mAutoScrollSendMsgs)) {
        TheUI->Handle(UIComponentScrollStartMsg(this, mUser), false);
    }
}

void UIList::HandleSelectionUpdated() {
    UIList *child = mListDir->SubList(mListState.SelectedDisplay(), mWidgets);
    UITransitionHandler::StartValueChange();
    if (child != 0) {
        child->HandleSelectionUpdated();
    }
}

void UIList::UpdateExtendedEntries(UIListState const &state) {
    UIListProvider *prov = state.Provider();
    if (prov && prov->NumData() > 0) {
        UIList *pMainList = mParent ? mParent : this;
        MILO_ASSERT(pMainList, 0x3FD);
        for (ObjPtrList<UILabel, ObjectDir>::iterator it =
                 pMainList->mExtendedLabelEntries.begin();
             it != pMainList->mExtendedLabelEntries.end();
             ++it) {
            UILabel *label = *it;
            MILO_ASSERT(label, 0x404);
            prov->UpdateExtendedText(state.SelectedDisplay(), state.SelectedData(), label);
        }
        for (ObjPtrList<RndMesh, ObjectDir>::iterator it =
                 pMainList->mExtendedMeshEntries.begin();
             it != pMainList->mExtendedMeshEntries.end();
             ++it) {
            RndMesh *mesh = *it;
            MILO_ASSERT(mesh, 0x40F);
            prov->UpdateExtendedMesh(state.SelectedDisplay(), state.SelectedData(), mesh);
        }
        for (ObjPtrList<Hmx::Object, ObjectDir>::iterator it =
                 pMainList->mExtendedCustomEntries.begin();
             it != pMainList->mExtendedCustomEntries.end();
             ++it) {
            Hmx::Object *custom = *it;
            MILO_ASSERT(custom, 0x41A);
            prov->UpdateExtendedCustom(
                state.SelectedDisplay(), state.SelectedData(), custom
            );
        }
    }
}

DataNode UIList::OnScroll(DataArray *da) {
    int scroll = da->Int(2);
    mUser = da->Size() > 3 ? da->Obj<LocalUser>(3) : 0;
    Scroll(scroll);
    return 1;
}

DataNode UIList::OnSelectedSym(DataArray *da) {
    if (da->Size() > 2) {
        return SelectedSym(da->Int(2));
    } else {
        return SelectedSym(true);
    }
}

void UIList::FinishValueChange() {}

void UIList::PreLoadWithRev(BinStreamRev &bs) {}

void UIList::SetSelected(int i, int j) {
    mListDir->CompleteScroll(mListState, mWidgets);
    mListState.SetSelected(i, j, true);
    Refresh(false);
    mListDir->Poll();
}

bool UIList::SetSelected(Symbol sym, bool b, int i) {
    int index = mListState.Provider()->DataIndex(sym);
    if (index == -1) {
        if (b) {
            MILO_NOTIFY("Couldn't find %s in UIList provider", sym);
        }
        return false;
    } else {
        SetSelected(index, i);
        return true;
    }
}

void UIList::Refresh(bool b) {
    mListDir->FillElements(mListState, mWidgets);
    if (b) {
        int nowrap = mListState.SelectedNoWrap();
        if (nowrap >= NumProviderData() && nowrap != 0)
            SetSelected(NumProviderData() - 1, -1);
        else {
            if (!mListState.Provider()->IsActive(mListState.SelectedData())
                && !mListState.IsScrolling()) {
                SetSelected(nowrap, -1);
            }
        }
    }
}

void UIList::EnableData(Symbol s) {
    MILO_ASSERT(mDataProvider, 0x382);
    mDataProvider->Enable(s);
    Refresh(false);
}

void UIList::DisableData(Symbol s) {
    MILO_ASSERT(mDataProvider, 0x389);
    mDataProvider->Disable(s);
    Refresh(false);
    if (!mDataProvider->IsActive(SelectedData())) {
        mListState.SetSelected(0, -1, true);
    }
}

void UIList::DimData(Symbol s) {
    MILO_ASSERT(mDataProvider, 0x396);
    mDataProvider->Dim(s);
    Refresh(false);
}

void UIList::UnDimData(Symbol s) {
    MILO_ASSERT(mDataProvider, 0x39d);
    mDataProvider->UnDim(s);
    Refresh(false);
}

void UIList::SetSelectedAux(int i) { SetSelected(i, -1); }

void UIList::CompleteScroll(UIListState const &state) {
    mListDir->CompleteScroll(state, mWidgets);
    if (mAutoScrolling) {
        int firstshowing = FirstShowing();
        state.Provider();
        int i3 = unk150 > 0 ? mListState.MaxFirstShowing() : 0;
        if (firstshowing == i3) {
            unk150 = unk150 - unk150 * 2;
            unk158 = mAutoScrollPause + TheTaskMgr.UISeconds();
        } else
            Scroll(unk150);
    }
    if (state.Provider()->IsActive(state.SelectedData())) {
        if (!mAutoScrolling || mAutoScrollSendMsgs) {
            TheUI->Handle(UIComponentScrollMsg(this, mUser), false);
        }
        HandleSelectionUpdated();
    }
}

DataNode UIList::OnSetSelected(DataArray *) { return NULL_OBJ; }

void UIList::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0x15, 0)
    PreLoadWithRev(d);
}

void UIList::PostLoad(BinStream &bs) {
    UIComponent::PostLoad(bs);
    int local_maxdisplay = -1;
    int local_mindisplay = 0;
    bool local_scrollpastmax = true;
    bool local_scrollpastmin = false;
    float local_speed;
    bool local_circular;
    int local_gridspan = 1;
    int local_numdisplay;
    int rev = bs.PopRev(this);
    if (rev < 0xF) {
        int i;
        int x;
        int j;
        int k;
        bool ba;
        bool b9;
        bool b8;
        bs >> i;
        bs >> j;
        if (rev > 4) {
            if (rev > 6)
                bs >> k;
            else
                bs >> b8;
        }
        if (rev > 6) {
            bs >> b9;
        }
        if (rev > 8) {
            bs >> ba;
        }
        if (rev > 10) {
            int b;
            bs >> b;
        }
        bs >> x;
    }
    bs >> local_numdisplay;
    if (rev > 0x11)
        bs >> local_gridspan;
    bs >> local_circular;
    bs >> local_speed;
    if (rev > 0xC) {
        bs >> local_scrollpastmin;
    }
    if (rev > 7) {
        bs >> local_scrollpastmax;
    }
    if (rev > 2)
        bs >> mPaginate;
    if (rev > 3)
        bs >> mSelectToScroll;
    if (rev >= 10)
        bs >> local_mindisplay;
    if (rev >= 6)
        bs >> local_maxdisplay;
    gLoading = true;
    SetNumDisplay(local_numdisplay);
    SetGridSpan(local_gridspan);
    SetCircular(local_circular);
    SetSpeed(local_speed);
    mListState.SetScrollPastMinDisplay(local_scrollpastmin);
    mListState.SetScrollPastMaxDisplay(local_scrollpastmax);
    mListState.SetMinDisplay(local_mindisplay);
    mListState.SetMaxDisplay(local_maxdisplay);
    if (rev == 1) {
        int x, y;
        bs >> x >> y;
    }
    if (rev >= 12)
        bs >> mNumData;
    if (rev >= 14)
        bs >> mAutoScrollPause;
    if (rev < 19)
        mAutoScrollSendMsgs = true;
    else
        bs >> mAutoScrollSendMsgs;
    if (rev >= 0x10) {
        bs >> mExtendedLabelEntries;
        bs >> mExtendedMeshEntries;
        bs >> mExtendedCustomEntries;
    }
    if (rev >= 17)
        UITransitionHandler::LoadHandlerData(bs);
    gLoading = false;
    Update();
}

void UIList::SetSelectedSimulateScroll(int i) {
    mListDir->CompleteScroll(mListState, mWidgets);
    mListState.SetSelectedSimulateScroll(i);
    Refresh(false);
    mListDir->Poll();
    if (mListDir->SubList(mListState.SelectedDisplay(), mWidgets) != 0) {
        Poll();
    }
}

bool UIList::SetSelectedSimulateScroll(Symbol, bool) { return false; }

void UIList::Update() {
    if (!gLoading) {
        MILO_ASSERT(mListDir, 0x238);
        mListDir->CreateElements(this, mWidgets, mListState.NumDisplay());

        if (TheLoadMgr.EditMode())
            Refresh(false);
    }
}

DataNode UIList::OnMsg(const ButtonDownMsg &msg) {
    mUser = msg.GetUser();
    Symbol cntType = JoypadControllerTypePadNum(msg.GetPadNum());
    bool bLeftyFlip = JoypadTypeHasLeftyFlip(cntType);
    int gridspan = 0;
    UIList *childList = 0;
    UIListOrientation o = kUIListVertical;
    bool b1 = false;
    int scrollDir = 0;
    int oldSelData = 0;
    int oldNextFill = 0;

    if (CanScroll()) {
        gridspan = mListState.GridSpan();
        childList = mListDir->SubList(mListState.SelectedDisplay(), mWidgets);
        o = mListDir->Orientation();
        b1 = false;

        if (childList) {
            if (childList->Handle(msg, false) != DataNode(kDataUnhandled, 0)) {
                return 1;
            }

            scrollDir = ScrollDirection(
                msg,
                bLeftyFlip,
                childList->GetUIListDir()->Orientation() == 0,
                childList->GridSpan()
            );
            if ((scrollDir == 1
                 && childList->SelectedData() == childList->NumProviderData() - 1)
                || (scrollDir == -1 && childList->SelectedData() == 0)) {
                o = childList->GetUIListDir()->Orientation();
                b1 = true;
            }
        }

        scrollDir = ScrollDirection(msg, bLeftyFlip, o == 0, gridspan);
        if (scrollDir != 0) {
            if (gridspan == 1 || (scrollDir != 1 && scrollDir != -1)
                || ((scrollDir == 1 && (mListState.SelectedDisplay() + 1) % gridspan)
                    || (scrollDir == -1 && mListState.SelectedDisplay() % gridspan))) {
                oldSelData = SelectedData();
                Scroll(scrollDir);
                if (oldSelData == SelectedData() && !IsScrolling() && !mSelectToScroll) {
                    return DataNode(kDataUnhandled, 0);
                }

                oldNextFill = UIListSubList::sNextFillSelection;
                if (childList) {
                    UIList *curChild = mListDir->SubList(mListState.SelectedDisplay(), mWidgets);
                    bool b2 = false;
                    if (curChild == childList) {
                        int dispFill = scrollDir + mListState.SelectedDisplay();
                        if (dispFill < 0 || dispFill >= NumDisplay())
                            b2 = true;
                        else {
                            curChild = mListDir->SubList(dispFill, mWidgets);
                        }
                    }
                    if (curChild) {
                        if (b1) {
                            if (scrollDir > 0)
                                oldNextFill = 0;
                            else if (b2)
                                oldNextFill = 1000000;
                            else
                                oldNextFill = curChild->NumProviderData() - 1;
                        } else {
                            oldNextFill =
                                Min(curChild->NumProviderData() - 1,
                                    childList->SelectedData());
                        }
                        if (b2)
                            UIListSubList::sNextFillSelection = oldNextFill;
                        else
                            curChild->SetSelectedSimulateScroll(oldNextFill);
                    }
                }

                return 1;
            }

            return 1;
        }

        int pageDir = PageDirection(msg.GetAction());
        if (pageDir != 0) {
            if (mPaginate) {
                mListState.PageScroll(pageDir);
                return 1;
            }
        } else if (pageDir == 0) {
            if (CatchNavAction(msg.GetAction()))
                return 1;
        }
    }

    if (!IsScrolling()) {
        if (msg.GetAction() == kAction_Confirm) {
            if (SelectScrollSelect(this, mUser))
                return 1;
            SendSelect(mUser);
            return 1;
        }
        if (msg.GetAction() == kAction_Cancel) {
            if (RevertScrollSelect(this, mUser, nullptr))
                return 1;
        }
    }

    return DataNode(kDataUnhandled, 0);
}

DataNode UIList::OnSetSelectedSimulateScroll(DataArray *da) {
    DataNode node = da->Evaluate(2);
    if (node.Type() == kDataInt) {
        SetSelectedSimulateScroll(node.Int());
        return DataNode(1);
    } else if (node.Type() == kDataSymbol || node.Type() == kDataString) {
        bool b3 = da->Size() == 4 ? da->Int(3) : true;
        return SetSelectedSimulateScroll(node.ForceSym(), b3);
    } else {
        MILO_FAIL("bad arg to set_selected_simulate_scroll");
        return 0;
    }
}

void UIList::OldResourcePreload(BinStream &bs) {
    char buf[0x100];
    bs.ReadString(buf, 0x100);
    mListDir.SetName(buf, true);
}

void UIList::SetNumDisplay(int i) {
    mListState.SetNumDisplay(i, gLoading == 0);
    Update();
}

void UIList::SetGridSpan(int i) {
    mListState.SetGridSpan(i, gLoading == 0);
    Update();
}

void UIList::SetCircular(bool b) {
    mListState.SetCircular(b, gLoading == 0);
    Update();
    if (!gLoading)
        Refresh(false);
}

void UIList::LimitCircularDisplay(bool b) {
    if (&mListState) {
        int val;
        mLimitCircularDisplayNumToDataNum = b;
        if (b) {
            int numprov = NumProviderData();
            int i = unk160;
            if (numprov < i)
                val = numprov;
            else
                val = 1;

        } else {
            val = unk160;
        }
        SetNumDisplay(val);
        Refresh(false);
    }
}

void UIList::SetProvider(UIListProvider *prov) {
    if (prov == mListState.Provider()) {
        LimitCircularDisplay(mLimitCircularDisplayNumToDataNum);
        Refresh(true);
    } else {
        mListState.SetProvider(prov, (RndDir *)mListDir);
        LimitCircularDisplay(mLimitCircularDisplayNumToDataNum);
        SetSelected(0, -1);
    }
    UIList *child = mListDir->SubList(mListState.SelectedDisplay(), mWidgets);
    if (child)
        child->Poll();
}

DataNode UIList::OnSetData(DataArray *da) {
    DataArray *arr = da->Array(2);
    int i3 = da->Size() > 3 ? da->Int(3) : 0;
    bool i4 = da->Size() > 4 ? da->Int(4) : 0;
    bool i5 = da->Size() > 5 ? da->Int(5) : 0;
    if (mDataProvider)
        mDataProvider->SetData(arr);
    else
        mDataProvider = new DataProvider(arr, i3, i4, i5, this);
    SetProvider(mDataProvider);
    return 1;
}

void UIList::DrawShowing() {
    if (unk15d) {
        mListState.Poll(TheTaskMgr.UISeconds());
        unk15d = false;
    }
    bool b = mAllowHighlight;
    if (mParent) {
        if (mParent->GetUIListDir()->SubList(mListState.SelectedDisplay(), mParent->mWidgets) == this) {
            b = mParent->mAllowHighlight;
        }
    }
    UIList *subList = mListDir->SubList(mListState.SelectedDisplay(), mWidgets);
    float offset = 0.0f;
    if (subList != NULL) {
        int subSelectedDisplay = subList->mListState.SelectedDisplay();
        float spacing = mListDir->ElementSpacing();
        offset = spacing * (float)subSelectedDisplay;
    }
    UIListWidgetDrawState drawState;
    mListDir->BuildDrawState(drawState, mListState, DrawState(this), offset, false);
    mListDir->DrawWidgets(drawState, mListState, mWidgets, WorldXfm(), DrawState(this), 0, b);
}

float UIList::GetDistanceToPlane(const Plane &p, Vector3 &v) {
    float ret = 0;
    bool first = true;
    Box box;
    CalcBoundingBox(box);
    Vector3 boxVecs[8] = { Vector3(box.mMin.x, box.mMin.y, box.mMin.z),
                           Vector3(box.mMax.x, box.mMin.y, box.mMin.z),
                           Vector3(box.mMax.x, box.mMax.y, box.mMin.z),
                           Vector3(box.mMin.x, box.mMax.y, box.mMin.z),
                           Vector3(box.mMin.x, box.mMin.y, box.mMax.z),
                           Vector3(box.mMax.x, box.mMin.y, box.mMax.z),
                           Vector3(box.mMax.x, box.mMax.y, box.mMax.z),
                           Vector3(box.mMin.x, box.mMax.y, box.mMax.z) };
    for (int i = 0; i < 8; i++) {
        float dot = p.Dot(boxVecs[i]);
        if (first || (std::fabs(dot) < std::fabs(ret))) {
            ret = dot;
            v = boxVecs[i];
            first = false;
        }
    }
    return ret;
}

void UIList::Init() {
    Register();
    REGISTER_OBJ_FACTORY(UIListArrow)
    REGISTER_OBJ_FACTORY(UIListCustom)
    REGISTER_OBJ_FACTORY(UIListDir)
    REGISTER_OBJ_FACTORY(UIListHighlight)
    REGISTER_OBJ_FACTORY(UIListLabel)
    REGISTER_OBJ_FACTORY(UIListMesh)
    REGISTER_OBJ_FACTORY(UIListSlot)
    REGISTER_OBJ_FACTORY(UIListSubList)
    REGISTER_OBJ_FACTORY(UIListWidget)
}

void UIList::BoundingBoxTriangles(std::vector<std::vector<Vector3> > &vec) {
    vec.clear();
    Box box;
    CalcBoundingBox(box);
    float boxMinX = box.mMin.x;
    float boxMaxX = box.mMax.x;
    float boxMinY = box.mMin.y;
    float boxMaxY = box.mMax.y;
    float boxMinZ = box.mMin.z;
    float boxMaxZ = box.mMax.z;
    std::vector<Vector3> locVec;
    for (int i = 0; i < 2; i++) {
        float f;
        if (i != 0)
            f = boxMinX;
        else
            f = boxMaxX;
        locVec.clear();
        locVec.push_back(Vector3(f, boxMinY, boxMinZ));
        locVec.push_back(Vector3(f, boxMinY, boxMaxZ));
        locVec.push_back(Vector3(f, boxMaxY, boxMaxZ));
        vec.push_back(locVec);
        locVec.clear();
        locVec.push_back(Vector3(f, boxMinY, boxMinZ));
        locVec.push_back(Vector3(f, boxMaxY, boxMinZ));
        locVec.push_back(Vector3(f, boxMaxY, boxMaxZ));
        vec.push_back(locVec);
    }
    for (int i = 0; i < 2; i++) {
        float f;
        if (i != 0)
            f = boxMinY;
        else
            f = boxMaxY;
        locVec.clear();
        locVec.push_back(Vector3(boxMinX, f, boxMinZ));
        locVec.push_back(Vector3(boxMinX, f, boxMaxZ));
        locVec.push_back(Vector3(boxMaxX, f, boxMaxZ));
        vec.push_back(locVec);
        locVec.clear();
        locVec.push_back(Vector3(boxMinX, f, boxMinZ));
        locVec.push_back(Vector3(boxMaxX, f, boxMinZ));
        locVec.push_back(Vector3(boxMaxX, f, boxMaxZ));
        vec.push_back(locVec);
    }
    for (int i = 0; i < 2; i++) {
        float f;
        if (i != 0)
            f = boxMinZ;
        else
            f = boxMaxZ;
        locVec.clear();
        locVec.push_back(Vector3(boxMinX, boxMinY, f));
        locVec.push_back(Vector3(boxMinX, boxMaxY, f));
        locVec.push_back(Vector3(boxMaxX, boxMaxY, f));
        vec.push_back(locVec);
        locVec.clear();
        locVec.push_back(Vector3(boxMinX, boxMinY, f));
        locVec.push_back(Vector3(boxMaxX, boxMinY, f));
        locVec.push_back(Vector3(boxMaxX, boxMaxY, f));
        vec.push_back(locVec);
    }
}

RndDrawable *UIList::CollideShowing(const Segment &, float &, Plane &) { return nullptr; }

int UIList::CollidePlane(const Plane &p) {
    std::vector<std::vector<Vector3> > triangles;
    BoundingBoxTriangles(triangles);
    int result = 0;
    if (triangles.size() > 0) {
        for (size_t i = 0; i < triangles.size(); ++i) {
            int ret = CollidePlane(triangles[i], p);
            if (ret) {
                result = ret;
            }
        }
    }
    return result;
}
