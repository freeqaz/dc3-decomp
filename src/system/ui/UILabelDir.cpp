#include "ui/UILabelDir.h"
#include "UIColor.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Dir.h"
#include "rndobj/Font.h"
#include "rndobj/FontBase.h"
#include "ui/UIComponent.h"
#include "ui/UIFontImporter.h"
#include "utl/BinStream.h"
#include "utl/Str.h"
#include "utl/Symbol.h"

static UIColor *gUILabelDefaultColor;

UILabelDir::UILabelDir()
    : mDefaultColor(this), mFocusAnim(this), mPulseAnim(this),
      mFocusedBackgroundGroup(this), mUnfocusedBackgroundGroup(this),
      mAllowEditText(false) {
    for (int i = 0; i < UIComponent::kNumStates; i++) {
        mColors.push_back(ObjPtr<UIColor>(this));
    }
}

BEGIN_PROPSYNCS(UILabelDir)
    SYNC_PROP(allow_edit_text, mAllowEditText)
    SYNC_PROP(focus_anim, mFocusAnim)
    SYNC_PROP(pulse_anim, mPulseAnim)
    SYNC_PROP(focused_background_group, mFocusedBackgroundGroup)
    SYNC_PROP(unfocused_background_group, mUnfocusedBackgroundGroup)
    SYNC_PROP(default_color, mDefaultColor)
    SYNC_PROP_SET(
        normal_color,
        (Hmx::Object *)mColors[UIComponent::kNormal],
        mColors[UIComponent::kNormal] = _val.Obj<UIColor>()
    )
    SYNC_PROP_SET(
        focused_color,
        (Hmx::Object *)mColors[UIComponent::kFocused],
        mColors[UIComponent::kFocused] = _val.Obj<UIColor>()
    )
    SYNC_PROP_SET(
        disabled_color,
        (Hmx::Object *)mColors[UIComponent::kDisabled],
        mColors[UIComponent::kDisabled] = _val.Obj<UIColor>()
    )
    SYNC_PROP_SET(
        selecting_color,
        (Hmx::Object *)mColors[UIComponent::kSelecting],
        mColors[UIComponent::kSelecting] = _val.Obj<UIColor>()
    )
    SYNC_PROP_SET(
        selected_color,
        (Hmx::Object *)mColors[UIComponent::kSelected],
        mColors[UIComponent::kSelected] = _val.Obj<UIColor>()
    )
    SYNC_SUPERCLASS(UIFontImporter)
    SYNC_SUPERCLASS(RndDir)
END_PROPSYNCS

BEGIN_SAVES(UILabelDir)
    SAVE_REVS(11, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mFocusAnim;
    bs << mPulseAnim;
    bs << mFocusedBackgroundGroup;
    bs << mUnfocusedBackgroundGroup;
    bs << mAllowEditText;
    bs << mDefaultColor;
    // Serialize all UIComponent state colors
    for (int i = 0; i < UIComponent::kNumStates; i++) {
        bs << mColors[i];
    }
    UIFontImporter::Save(bs);
END_SAVES

BEGIN_COPYS(UILabelDir)
    COPY_SUPERCLASS(RndDir)
    COPY_SUPERCLASS(UIFontImporter)
    CREATE_COPY(UILabelDir)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mDefaultColor)
        COPY_MEMBER(mColors)
        COPY_MEMBER(mAllowEditText)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(UILabelDir)
    ObjectDir::Load(bs);
END_LOADS

INIT_REVS(11, 0)

void UILabelDir::PreLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(11, 0);
    RndDir::PreLoad(bs);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

void UILabelDir::PostLoad(BinStream &bs) {
    BinStreamRev d(bs, bs.PopRev(this));
    RndDir::PostLoad(bs);
    if (d.rev < 10) {
        String str;
        bs >> str;
    }
    if (d.rev >= 3 && d.rev < 9) {
        ObjPtr<RndFont> fontPtr(this);
        bs >> fontPtr;
    }
    if (d.rev >= 1)
        bs >> mFocusAnim;
    if (d.rev >= 2)
        bs >> mPulseAnim;
    if (d.rev >= 4 && d.rev < 11) {
        Symbol s1; bs >> s1;
        Symbol s2; bs >> s2;
        Symbol s3; bs >> s3;
    }
    if (d.rev >= 5 && d.rev < 11) {
        Symbol s1; bs >> s1;
        Symbol s2; bs >> s2;
    }
    if (d.rev >= 6) {
        bs >> mFocusedBackgroundGroup;
        bs >> mUnfocusedBackgroundGroup;
    }
    if (d.rev >= 7) {
        d >> mAllowEditText;
    }
    bs >> mDefaultColor;
    for (int i = 0; i < UIComponent::kNumStates; i++) {
        ObjPtr<UIColor> uiCol(this);
        bs >> uiCol;
        mColors[i] = uiCol;
    }
    if (d.rev >= 8) {
        UIFontImporter::Load(bs);
    }
}

bool UILabelDir::AllowEditText() const { return mAllowEditText; }

RndFontBase *UILabelDir::FontObj(Symbol s) const {
    if (NumGennedFonts() > 0) {
        return GetGennedFont(s);
    }
    TheDebug.Notify(MakeString("%s has no genned fonts", PathName(this)));
    return nullptr;
}

UIColor *UILabelDir::GetStateColor(UIComponent::State state) const {
    MILO_ASSERT(state < UIComponent::kNumStates, 0x39);
    UIColor *c = mColors[state];
    if (c) return c;
    c = mDefaultColor;
    return c ? c : gUILabelDefaultColor;
}

DataNode UILabelDir::GetMatVariations(UILabelDir *pThis) {
    s32 numVariations = 0;
    DataArray *pArray;

    if (pThis != NULL) {
        numVariations = pThis->NumMatVariations();
    }

    pArray = new DataArray(numVariations + 1);

    // First element is always the null string (empty material variation)
    pArray->Node(0) = DataNode(Symbol());

    // Add each material variation name
    for (s32 index = 1; index <= numVariations; index++) {
        pArray->Node(index) = DataNode(pThis->GetMatVariationName(index - 1));
    }

    DataNode result(pArray, kDataArray);
    pArray->Release();

    return result;
}

void UILabelDir::Init() {
    REGISTER_OBJ_FACTORY(UILabelDir)
    gUILabelDefaultColor = Hmx::Object::New<UIColor>();
    Hmx::Color color(1.0f, 1.0f, 1.0f, 1.0f);
    gUILabelDefaultColor->SetColor(color);
}

BEGIN_HANDLERS(UILabelDir)
    HANDLE_EXPR(font_obj, FontObj(_msg->Sym(2)))
    HANDLE_SUPERCLASS(UIFontImporter)
    HANDLE_SUPERCLASS(RndDir)
END_HANDLERS
