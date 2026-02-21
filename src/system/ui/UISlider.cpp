#include "ui/UISlider.h"
#include "UIComponent.h"
#include "math/Mtx.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Joypad.h"
#include "os/JoypadMsgs.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "ui/UIPanel.h"
#include "ui/UI.h"
#include "ui/Utl.h"
#include "utl/BinStream.h"
#include "utl/Symbol.h"

extern UIComponent::State SymToUIComponentState(Symbol);

void UISlider::OldResourcePreload(BinStream &bs) {
    char buf[256];
    bs.ReadString(buf, 256);
    mSliderResource.SetName(buf, true);
}

UISlider::UISlider() : mSliderResource(this), mCurrent(0), mNumSteps(10), mVertical(0) {}

BEGIN_PROPSYNCS(UISlider)
    SYNC_PROP_MODIFY(slider_resource, mSliderResource, Update())
    SYNC_PROP(vertical, mVertical)
    SYNC_SUPERCLASS(ScrollSelect)
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

BEGIN_SAVES(UISlider)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(UIComponent)
    bs << mSliderResource;
    bs << mVertical;
END_SAVES

BEGIN_COPYS(UISlider)
    COPY_SUPERCLASS(UIComponent)
    CREATE_COPY_AS(UISlider, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(mSelectToScroll)
        COPY_MEMBER(mVertical)
        COPY_MEMBER(mSliderResource)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(UISlider)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

void UISlider::SetTypeDef(DataArray *da) {
    UIComponent::SetTypeDef(da);
    Update();
}

INIT_REVS(3, 0)

void UISlider::PreLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(3, 0);
    UIComponent::PreLoad(bs);
    if (d.rev >= 3)
        bs >> mSliderResource;
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void UISlider::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    UIComponent::PostLoad(bs);
    mSliderResource.PostLoad(0);
    if (d.rev > 0) {
        d >> mSelectToScroll;
    }
    if (d.rev > 1) {
        d >> mVertical;
    }
    Update();
}

void UISlider::DrawShowing() {
    SyncSlider();
    if (mSliderMesh) {
        mSliderMesh->SetMat(mStateMats[(int)DrawState(this)]);
    }
    if (mSliderResource) {
        mSliderResource->DrawShowing();
    }
}

RndDrawable *UISlider::CollideShowing(const Segment &seg, float &f, Plane &pl) {
    SyncSlider();
    return mSliderResource->CollideShowing(seg, f, pl) ? this : nullptr;
}

int UISlider::CollidePlane(const Plane &pl) {
    SyncSlider();
    return mSliderResource->CollidePlane(pl);
}

void UISlider::Enter() {
    UIComponent::Enter();
    Reset();
}

void UISlider::SetCurrent(int i) {
    if (i < 0 || i >= mNumSteps) {
        MILO_FAIL("Can't set slider to %i (%i steps)", i, mNumSteps);
    } else
        mCurrent = i;
}

int UISlider::SelectedAux() const { return Current(); }

void UISlider::SetSelectedAux(int i) { SetCurrent(i); }

DataNode UISlider::OnMsg(const ButtonDownMsg &msg) {
    Symbol cnttype = JoypadControllerTypePadNum(msg.GetPadNum());
    if (CanScroll()) {
        int act = ScrollDirection(msg, JoypadTypeHasLeftyFlip(cnttype), mVertical, 1);
        if (act != kAction_None) {
            if (mVertical)
                act = (JoypadAction)-act;
            int step = mCurrent + act;
            if (step >= 0 && step < mNumSteps) {
                SetCurrent(step);
                UIComponentScrollMsg scroll_msg(this, msg.GetUser());
                TheUI->Handle(scroll_msg, false);
            }
            return DataNode(1);
        }
        if (CatchNavAction(msg.GetAction())) {
            return DataNode(1);
        }
    }
    JoypadAction thisAct = msg.GetAction();
    LocalUser *user = msg.GetUser();
    if (thisAct == kAction_Confirm && SelectScrollSelect(this, user)) {
        return DataNode(1);
    } else if (thisAct == kAction_Cancel && RevertScrollSelect(this, user, 0)) {
        return DataNode(1);
    }
    return DataNode(kDataUnhandled, 0);
}

void UISlider::SyncSlider() {
    if (mSliderResource) {
        mSliderResource->SetFrame(Frame(), 1.0f);
        mSliderResource->SetWorldXfm(WorldXfm());
    }
}

float UISlider::Frame() const {
    if (mNumSteps == 1)
        return 0.0f;
    else
        return (float)(mCurrent) / (float)(mNumSteps - 1);
}

void UISlider::SetNumSteps(int i) {
    if (i < 1)
        MILO_FAIL("Can't set num steps to %i (must be >= 1)", i);
    else
        mNumSteps = i;
}

void UISlider::SetFrame(float frame) {
    MILO_ASSERT(frame >= 0 && frame <= 1.0f, 0xe2);
    mCurrent = frame * (mNumSteps - 1) + 0.5f;
}

int UISlider::Current() const { return mCurrent; }

void UISlider::Init() { REGISTER_OBJ_FACTORY(UISlider) }

void UISlider::Update() {
    static Symbol mesh("mesh");
    static Symbol mats("mats");

    // Clear material pointers for all states
    mSliderMesh = nullptr;
    for (int s = 0; s < UIComponent::kNumStates; s++) mStateMats[s] = nullptr;

    const DataArray *typeDef = TypeDef();
    if (!typeDef || !mSliderResource) {
        return;
    }

    // Load mesh resource if specified
    DataArray *meshArray = typeDef->FindArray(mesh, false);
    if (meshArray) {
        DataNode &meshNode = meshArray->Node(1);
        const char *meshStr = meshNode.Str(meshArray);
        if (meshStr) {
            mSliderMesh = mSliderResource->Find<RndMesh>(meshStr, true);
        }
    }

    // Load materials for each UI state
    DataArray *matsArray = typeDef->FindArray(mats, false);
    if (!matsArray) {
        return;
    }

    int matsArraySize = matsArray->Size();
    if (matsArraySize <= 1) {
        return;
    }

    for (int i = 1; i < matsArraySize; i++) {
        DataNode &arrayNode = matsArray->Node(i);
        DataArray *matItemArray = arrayNode.Array(matsArray);
        if (!matItemArray || matItemArray->Size() == 0) {
            continue;
        }

        Symbol itemSym = matItemArray->Sym(0);
        State itemState = SymToUIComponentState(itemSym);
        DataNode &matNode = matItemArray->Node(1);
        const char *matName = matNode.Str(matItemArray);
        if (matName) {
            mStateMats[itemState] = mSliderResource->Find<RndMat>(matName, true);
        }
    }
}

BEGIN_HANDLERS(UISlider)
    HANDLE_MESSAGE(ButtonDownMsg)
    HANDLE_EXPR(current, mCurrent)
    HANDLE_EXPR(num_steps, mNumSteps)
    HANDLE_EXPR(frame, Frame())
    HANDLE_ACTION(set_num_steps, SetNumSteps(_msg->Int(2)))
    HANDLE_ACTION(set_current, SetCurrent(_msg->Int(2)))
    HANDLE_ACTION(set_frame, SetFrame(_msg->Float(2)))
    HANDLE_ACTION(store, Store())
    HANDLE_ACTION(undo, RevertScrollSelect(this, _msg->Obj<LocalUser>(2), 0))
    HANDLE_ACTION(
        undo_handled_by,
        RevertScrollSelect(this, _msg->Obj<LocalUser>(2), _msg->Obj<UIPanel>(3))
    )
    HANDLE_ACTION(confirm, Reset())
    HANDLE_SUPERCLASS(ScrollSelect)
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS
