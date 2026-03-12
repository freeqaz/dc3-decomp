#include "ui/LabelShrinkWrapper.h"
#include "ui/UIComponent.h"
#include "macros.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Mesh.h"
#include "ui/UILabel.h"
#include "ui/UIPanel.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Symbol.h"

LabelShrinkWrapper::LabelShrinkWrapper()
    : mResourceDir(this), m_pLabel(this), m_pShow(0), mLeftBorder(0), mRightBorder(0),
      mTopBorder(0), mBottomBorder(0), m_pTopLeftBone(0), m_pTopRightBone(0),
      m_pBottomLeftBone(0), m_pBottomRightBone(0) {}

LabelShrinkWrapper::~LabelShrinkWrapper() {}

BEGIN_PROPSYNCS(LabelShrinkWrapper)
    SYNC_PROP_MODIFY(resource, mResourceDir, Update())
    SYNC_PROP_SET(
        label, m_pLabel.Ptr(), m_pLabel = dynamic_cast<UILabel *>(_val.GetObj())
    )
    SYNC_PROP_SET(show, m_pShow, m_pShow = _val.Int())
    SYNC_PROP(left_border, mLeftBorder)
    SYNC_PROP(right_border, mRightBorder)
    SYNC_PROP(top_border, mTopBorder)
    SYNC_PROP(bottom_border, mBottomBorder)
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

BEGIN_SAVES(LabelShrinkWrapper)
    SAVE_REVS(2, 0)
    bool show = m_pShow;
    bs << m_pLabel << show;
    bs << mResourceDir;
    bs << mLeftBorder;
    bs << mRightBorder;
    bs << mTopBorder;
    bs << mBottomBorder;
    SAVE_SUPERCLASS(UIComponent)
END_SAVES

void LabelShrinkWrapper::Copy(const Hmx::Object *o, Hmx::Object::CopyType ty) {
    UIComponent::Copy(o, ty);
    const LabelShrinkWrapper *c = dynamic_cast<const LabelShrinkWrapper *>(o);
    if (c) {
        m_pLabel = c->m_pLabel;
        m_pShow = c->m_pShow;
        mLeftBorder = c->mLeftBorder;
        mRightBorder = c->mRightBorder;
        mTopBorder = c->mTopBorder;
        mBottomBorder = c->mBottomBorder;
        mResourceDir = c->mResourceDir;
    }
    Update();
}

BEGIN_LOADS(LabelShrinkWrapper)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(2, 0)

void LabelShrinkWrapper::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    bs >> m_pLabel;
    bs >> m_pShow;
    if (d.rev >= 1)
        bs >> mResourceDir;
    if (2 <= d.rev) {
        bs >> mLeftBorder;
        bs >> mRightBorder;
        bs >> mTopBorder;
        bs >> mBottomBorder;
    }
    UIComponent::PreLoad(bs);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void LabelShrinkWrapper::PostLoad(BinStream &bs) {
    bs.PopRev(this);
    mResourceDir.PostLoad(nullptr);
    UIComponent::PostLoad(bs);
    Update();
}

void LabelShrinkWrapper::Enter() { UIComponent::Enter(); }

void LabelShrinkWrapper::SetTypeDef(DataArray *d) {
    Hmx::Object::SetTypeDef(d);
    Update();
}

void LabelShrinkWrapper::Update() {
    const DataArray *pTypeDef = TypeDef();
    if (pTypeDef && mResourceDir) {
        static Symbol topleft_bone("topleft_bone");
        static Symbol topright_bone("topright_bone");
        static Symbol bottomleft_bone("bottomleft_bone");
        static Symbol bottomright_bone("bottomright_bone");
        m_pTopLeftBone =
            mResourceDir->Find<RndMesh>(pTypeDef->FindStr(topleft_bone), true);
        MILO_ASSERT(m_pTopLeftBone, 0xc5);
        m_pTopRightBone =
            mResourceDir->Find<RndMesh>(pTypeDef->FindStr(topright_bone), true);
        MILO_ASSERT(m_pTopRightBone, 0xc7);
        m_pBottomLeftBone =
            mResourceDir->Find<RndMesh>(pTypeDef->FindStr(bottomleft_bone), true);
        MILO_ASSERT(m_pBottomLeftBone, 0xc9);
        m_pBottomRightBone =
            mResourceDir->Find<RndMesh>(pTypeDef->FindStr(bottomright_bone), true);
        MILO_ASSERT(m_pBottomRightBone, 0xcb);
    } else {
        m_pBottomRightBone = nullptr;
        m_pBottomLeftBone = nullptr;
        m_pTopRightBone = nullptr;
        m_pTopLeftBone = nullptr;
    }
}

void LabelShrinkWrapper::Init() { REGISTER_OBJ_FACTORY(LabelShrinkWrapper) }

void LabelShrinkWrapper::Poll() { UIComponent::Poll(); }

void LabelShrinkWrapper::DrawShowing() {
    if (m_pLabel && m_pShow) {
        RndDir *pDir = mResourceDir;
        MILO_ASSERT(pDir, 0xa7);
        UpdateAndDrawWrapper();
        auto _tmp0 = WorldXfm();
        pDir->SetWorldXfm(_tmp0);
        pDir->Draw();
    }
}

void LabelShrinkWrapper::UpdateAndDrawWrapper() {
    MILO_ASSERT(m_pLabel, 0x9e);
    Box box;
    m_pLabel->GetWidthHeightBox(box);
    float minX = box.mMin.x - mLeftBorder;
    float maxX = box.mMax.x + mRightBorder;
    float maxZ = box.mMax.z + mTopBorder;
    float minZ = box.mMin.z - mBottomBorder;
    SetWorldXfm(m_pLabel->WorldXfm());
    m_pTopLeftBone->SetLocalPos(Vector3(minX, 0.0f, maxZ));
    m_pTopRightBone->SetLocalPos(Vector3(maxX, 0.0f, maxZ));
    m_pBottomLeftBone->SetLocalPos(Vector3(minX, 0.0f, minZ));
    m_pBottomRightBone->SetLocalPos(Vector3(maxX, 0.0f, minZ));
}

BEGIN_HANDLERS(LabelShrinkWrapper)
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS
