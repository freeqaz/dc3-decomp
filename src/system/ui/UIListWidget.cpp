#include "ui/UIListWidget.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Utl.h"
#include "ui/UIColor.h"
#include "ui/UIComponent.h"
#include "math/Vec.h"
#include "utl/BinStream.h"

float UIListWidget::DrawOrder() const { return mDrawOrder; }

void UIListWidget::ResourceCopy(const UIListWidget *w) { Copy(w, kCopyShallow); }

void UIListWidget::CalcXfm(const Transform &tfin, const Vector3 &vin, Transform &out) {
    out.v.x += vin.x;
    out.v.y += vin.y;
    out.v.z += vin.z;
    Multiply(out, tfin, out);
}

void UIListWidget::SetParentList(UIList *list) { mParentList = list; }

UIColor *UIListWidget::DisplayColor(
    UIListWidgetState element_state, UIComponent::State list_state
) const {
#ifdef HX_NATIVE
    // HamListRibbon::DrawRibbon repurposes UIListElementDrawState fields
    // (mElementState, mComponentState, etc.) as a Hmx::Color overlay.
    // DrawWidgets then reads the corrupted mElementState. On Xbox,
    // MILO_ASSERT doesn't abort; on native it does. Return default color.
    if (element_state < 0 || element_state >= kNumUIListWidgetStates)
        return mDefaultColor;
    if (list_state < 0 || list_state >= UIComponent::kNumStates)
        return mDefaultColor;
#else
    MILO_ASSERT((0) <= (element_state) && (element_state) < (kNumUIListWidgetStates), 0x64);
    MILO_ASSERT((0) <= (list_state) && (list_state) < (UIComponent::kNumStates), 0x65);
#endif
    UIColor *color = mColors[element_state][list_state];
    if (color)
        return color;
    else if (mDefaultColor)
        return mDefaultColor;
    else
        return nullptr;
}

void UIListWidget::Save(BinStream &bs) {
    bs << 2;
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mDrawOrder << mDefaultColor << mWidgetDrawType;
    bs << mDisabledAlphaScale;
    for (int i = 0; i < kNumUIListWidgetStates; i++) {
        for (int j = 0; j < UIComponent::kNumStates; j++) {
            bs << mColors[i][j];
        }
    }
}

UIListWidget::UIListWidget()
    : mDrawOrder(0), mDisabledAlphaScale(1), mDefaultColor(this),
      mWidgetDrawType(kUIListWidgetDrawAlways), mParentList(nullptr) {
    for (int i = 0; i < kNumUIListWidgetStates; i++) {
        std::vector<ObjPtr<UIColor> > vec;
        for (int j = 0; j < UIComponent::kNumStates; j++) {
            vec.push_back(ObjPtr<UIColor>(this));
        }
        mColors.push_back(vec);
    }
}

void UIListWidget::SetColor(UIListWidgetState ws, UIComponent::State cs, UIColor *color) {
    MILO_ASSERT(ws < kNumUIListWidgetStates, 0x7C);
    MILO_ASSERT(cs < UIComponent::kNumStates, 0x7D);
    ObjPtr<UIColor> &theColor = mColors[ws][cs];
    theColor = color;
}

BEGIN_HANDLERS(UIListWidget)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(UIListWidget)
    SYNC_PROP(draw_order, mDrawOrder)
    SYNC_PROP(disabled_alpha_scale, mDisabledAlphaScale)
    SYNC_PROP(default_color, mDefaultColor)
    SYNC_PROP_SET(
        widget_draw_type,
        (int &)mWidgetDrawType,
        mWidgetDrawType = (UIListWidgetDrawType)_val.Int()
    )
    SYNC_PROP_SET(
        active_normal_color,
        (Hmx::Object *)mColors[kUIListWidgetActive][UIComponent::kNormal],
        SetColor(kUIListWidgetActive, UIComponent::kNormal, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        active_focused_color,
        (Hmx::Object *)mColors[kUIListWidgetActive][UIComponent::kFocused],
        SetColor(kUIListWidgetActive, UIComponent::kFocused, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        active_disabled_color,
        (Hmx::Object *)mColors[kUIListWidgetActive][UIComponent::kDisabled],
        SetColor(kUIListWidgetActive, UIComponent::kDisabled, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        active_selecting_color,
        (Hmx::Object *)mColors[kUIListWidgetActive][UIComponent::kSelecting],
        SetColor(kUIListWidgetActive, UIComponent::kSelecting, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        active_selected_color,
        (Hmx::Object *)mColors[kUIListWidgetActive][UIComponent::kSelected],
        SetColor(kUIListWidgetActive, UIComponent::kSelected, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        highlight_normal_color,
        (Hmx::Object *)mColors[kUIListWidgetHighlight][UIComponent::kNormal],
        SetColor(kUIListWidgetHighlight, UIComponent::kNormal, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        highlight_focused_color,
        (Hmx::Object *)mColors[kUIListWidgetHighlight][UIComponent::kFocused],
        SetColor(kUIListWidgetHighlight, UIComponent::kFocused, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        highlight_disabled_color,
        (Hmx::Object *)mColors[kUIListWidgetHighlight][UIComponent::kDisabled],
        SetColor(kUIListWidgetHighlight, UIComponent::kDisabled, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        highlight_selecting_color,
        (Hmx::Object *)mColors[kUIListWidgetHighlight][UIComponent::kSelecting],
        SetColor(kUIListWidgetHighlight, UIComponent::kSelecting, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        highlight_selected_color,
        (Hmx::Object *)mColors[kUIListWidgetHighlight][UIComponent::kSelected],
        SetColor(kUIListWidgetHighlight, UIComponent::kSelected, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        inactive_normal_color,
        (Hmx::Object *)mColors[kUIListWidgetInactive][UIComponent::kNormal],
        SetColor(kUIListWidgetInactive, UIComponent::kNormal, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        inactive_focused_color,
        (Hmx::Object *)mColors[kUIListWidgetInactive][UIComponent::kFocused],
        SetColor(kUIListWidgetInactive, UIComponent::kFocused, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        inactive_disabled_color,
        (Hmx::Object *)mColors[kUIListWidgetInactive][UIComponent::kDisabled],
        SetColor(kUIListWidgetInactive, UIComponent::kDisabled, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        inactive_selecting_color,
        (Hmx::Object *)mColors[kUIListWidgetInactive][UIComponent::kSelecting],
        SetColor(kUIListWidgetInactive, UIComponent::kSelecting, _val.Obj<UIColor>())
    )
    SYNC_PROP_SET(
        inactive_selected_color,
        (Hmx::Object *)mColors[kUIListWidgetInactive][UIComponent::kSelected],
        SetColor(kUIListWidgetInactive, UIComponent::kSelected, _val.Obj<UIColor>())
    )
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

INIT_REVS(2, 0)

BEGIN_LOADS(UIListWidget)
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    LOAD_SUPERCLASS(Hmx::Object)
    bs >> mDrawOrder;
    int x;
    if (d.rev < 1) {
        int i, j;
        bs >> i >> j;
    }
    bs >> mDefaultColor >> x;
    mWidgetDrawType = (UIListWidgetDrawType)x;
    if (d.rev >= 2)
        bs >> mDisabledAlphaScale;
    for (int i = 0; i < kNumUIListWidgetStates; i++) {
        for (int j = 0; j < UIComponent::kNumStates; j++) {
            ObjPtr<UIColor> tmp(this);
            bs >> tmp;
            mColors[i][j] = tmp;
        }
    }
END_LOADS

BEGIN_COPYS(UIListWidget)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY_AS(UIListWidget, w)
    MILO_ASSERT(w, 0xc7);
    COPY_MEMBER_FROM(w, mDrawOrder)
    COPY_MEMBER_FROM(w, mDisabledAlphaScale)
    COPY_MEMBER_FROM(w, mDefaultColor)
    COPY_MEMBER_FROM(w, mColors)
    COPY_MEMBER_FROM(w, mWidgetDrawType)
END_COPYS

void UIListWidget::DrawMesh(
    RndMesh *mesh,
    UIListWidgetState wstate,
    UIComponent::State cstate,
    const Transform &tf,
    Box *box
) {
    MILO_ASSERT(mesh, 0x40);
    mesh->SetWorldXfm(tf);
    if (box) {
        int minData[4];
        int maxData[4];
        const int *src = (const int *)box;
        minData[0] = src[0];
        minData[2] = src[2];
        minData[3] = src[3];
        maxData[0] = src[4];
        minData[1] = src[1];
        maxData[2] = src[6];
        maxData[3] = src[7];
        maxData[1] = src[5];
        Box localbox;
        int *dst = (int *)&localbox;
        dst[0] = minData[0];
        dst[1] = minData[1];
        dst[2] = minData[2];
        dst[3] = minData[3];
        dst[4] = maxData[0];
        dst[5] = maxData[1];
        dst[6] = maxData[2];
        dst[7] = maxData[3];
        CalcBox(mesh, localbox);
        box->GrowToContain(localbox.mMin, false);
        box->GrowToContain(localbox.mMax, false);
    } else {
        UIColor *col = DisplayColor(wstate, cstate);
        if (col) {
            RndMat *mat = mesh->Mat();
            if (mat) {
                const Hmx::Color &c = col->GetColor();
                mat->SetColor(c.red, c.green, c.blue);
            }
        }
        mesh->DrawShowing();
    }
}
