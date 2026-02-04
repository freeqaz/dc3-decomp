#include "ui/UIListMesh.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
#include "rndobj/Utl.h"
#include "ui/UIListSlot.h"
#include "utl/Loader.h"

#pragma region UIListMesh

UIListMesh::UIListMesh() : mMesh(this), mDefaultMat(this) {}

BEGIN_PROPSYNCS(UIListMesh)
    SYNC_PROP(mesh, mMesh)
    SYNC_PROP(default_mat, mDefaultMat)
    SYNC_SUPERCLASS(UIListSlot)
END_PROPSYNCS

BEGIN_SAVES(UIListMesh)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(UIListSlot)
    bs << mMesh << mDefaultMat;
END_SAVES

BEGIN_COPYS(UIListMesh)
    COPY_SUPERCLASS(UIListSlot)
    CREATE_COPY_AS(UIListMesh, m)
    MILO_ASSERT(m, 0x9F);
    COPY_MEMBER_FROM(m, mMesh)
    COPY_MEMBER_FROM(m, mDefaultMat)
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(UIListMesh)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(UIListSlot)
    bs >> mMesh >> mDefaultMat;
END_LOADS

void UIListMesh::Draw(
    const UIListWidgetDrawState &drawstate,
    const UIListState &liststate,
    const Transform &tf,
    UIComponent::State compstate,
    Box *box,
    DrawCommand cmd
) {
    if (mMesh) {
        float somefloat = 1.0f;
        RndMat *themat = 0;
        if (TheLoadMgr.EditMode()) {
            themat = mMesh->Mat();
            if (themat)
                somefloat = themat->Alpha();
        }
        Transform xfm1 = mMesh->LocalXfm();
        UIListSlot::Draw(drawstate, liststate, tf, compstate, box, cmd);
        mMesh->SetLocalXfm(xfm1);
        if (TheLoadMgr.EditMode()) {
            mMesh->SetMat(themat);
            if (themat) {
                themat->SetAlpha(somefloat);
            }
        }
    }
}

UIListSlotElement *UIListMesh::CreateElement(UIList *uilist) {
    MILO_ASSERT(mMesh, 0x5b);
    UIListSlotElement *element = new UIListMeshElement(this);
    return element;
}

RndTransformable *UIListMesh::RootTrans() { return mMesh; }

#pragma endregion UIListMesh
#pragma region UIListMeshElement

inline void
UIListMeshElement::Draw(const Transform &tf, float f, UIColor *col, Box *box) {
    RndMesh *mesh = mListMesh->Mesh();
    MILO_ASSERT(mesh, 0x1B);
    mesh->SetWorldXfm(tf);
    if (box != nullptr) {
        Box localbox = *box;
        CalcBox(mesh, localbox);
        box->GrowToContain(localbox.mMin, false);
        box->GrowToContain(localbox.mMax, false);
    } else if (mMat != nullptr) {
        float alpha = mMat->Alpha();
        mesh->SetMat(mMat);
        mMat->SetAlpha(f * alpha);
        if (col != nullptr) {
            const Hmx::Color &c = col->GetColor();
            mMat->SetColor(c.red, c.green, c.blue);
        }
        mesh->DrawShowing();
        mMat->SetAlpha(alpha);
    }
}

#pragma endregion UIListMeshElement
