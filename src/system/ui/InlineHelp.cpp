#include "ui/InlineHelp.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Joypad.h"
#include "rndobj/Dir.h"
#include "ui/UIComponent.h"
#include "ui/UILabel.h"
#include "utl/BinStream.h"
#include "utl/Locale.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

#ifdef HX_NATIVE
bool InlineHelp::sRotated;
bool InlineHelp::sHasFlippedTextThisRotation;
bool InlineHelp::sNeedsTextUpdate;
float InlineHelp::sLabelRot;
float InlineHelp::sLastUpdatedTime;
float InlineHelp::sRotationTime;
#endif

#pragma region InlineHelp

InlineHelp::InlineHelp()
    : mUseConnectedControllers(false), mHorizontal(true), mSpacing(0), mResourceDir(this),
      mTemplateLabel(0), mTextColor(this) {}

InlineHelp::~InlineHelp() {
    int siz = mTextLabels.size();
    for (int i = 0; i < siz; i++) {
        delete mTextLabels[i];
    }
}

BEGIN_LOADS(InlineHelp)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(5, 0)

void InlineHelp::PreLoad(BinStream &bs) {
    LOAD_REVS(bs);
    ASSERT_REVS(5, 0);
    d >> mHorizontal;
    d >> mSpacing;
    d >> mConfig;
    if (d.rev >= 1)
        d >> mTextColor;
    if (d.rev >= 2 && d.rev < 4) {
        int x;
        d.stream >> x;
    }
    if (d.rev >= 3) {
        d >> mUseConnectedControllers;
    }
    if (d.rev >= 5) {
        d.stream >> mResourceDir;
    }
    UIComponent::PreLoad(d.stream);
    bs.PushRev(packRevs(d.altRev, d.rev), this);
}

BEGIN_SAVES(InlineHelp)
    SAVE_REVS(5, 0)
    bs << mHorizontal;
    bs << mSpacing;
    bs << mConfig;
    bs << mTextColor;
    bs << mUseConnectedControllers;
    bs << mResourceDir;
    SAVE_SUPERCLASS(UIComponent)
END_SAVES

void InlineHelp::Copy(const Hmx::Object *o, Hmx::Object::CopyType ty) {
    UIComponent::Copy(o, ty);
    const InlineHelp *c = dynamic_cast<const InlineHelp *>(o);
    if (c) {
        mHorizontal = c->mHorizontal;
        mSpacing = c->mSpacing;
        mConfig = c->mConfig;
        mTextColor = c->mTextColor;
        mUseConnectedControllers = c->mUseConnectedControllers;
        mResourceDir = c->mResourceDir;
    }
    Update();
    UpdateIconTypes(0);
}

BEGIN_PROPSYNCS(InlineHelp)
    SYNC_PROP_MODIFY(resource, mResourceDir, Update())
    SYNC_PROP_MODIFY(config, mConfig, SyncLabelsToConfig())
    SYNC_PROP(horizontal, mHorizontal)
    SYNC_PROP(spacing, mSpacing)
    SYNC_PROP_MODIFY(text_color, mTextColor, UpdateTextColors())
    SYNC_PROP(use_connected_controllers, mUseConnectedControllers)
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

void InlineHelp::UpdateTextColors() {
    for (std::vector<UILabel *>::iterator it = mTextLabels.begin();
         it != mTextLabels.end();
         ++it) {
        (*it)->LStyle(0).mColorOverride = mTextColor;
    }
}

void InlineHelp::OldResourcePreload(BinStream &bs) {
    char name[256];
    bs.ReadString(name, 256);
    mResourceDir.SetName(name, true);
}

void InlineHelp::PostLoad(BinStream &bs) {
    bs.PopRev(this);
    mResourceDir.PostLoad(nullptr);
    UIComponent::PostLoad(bs);
    Update();
}

void InlineHelp::UpdateLabelText() {
    static Symbol inline_help_fmt("inline_help_fmt");
    int size = mConfig.size();
    for (int i = 0; i < size; i++) {
        String icon = GetIconStringFromAction(mConfig[i].mAction);
        if (icon.empty())
            mTextLabels[i]->SetTextToken(gNullStr);
        else
            mTextLabels[i]->SetTokenFmt(
                inline_help_fmt, icon.c_str(), mConfig[i].GetText(sRotated)
            );
    }
}
void InlineHelp::Init() { REGISTER_OBJ_FACTORY(InlineHelp) }

void InlineHelp::Enter() {
    UIComponent::Enter();
    UpdateIconTypes(true);
    SyncLabelsToConfig();
}

void InlineHelp::SetTypeDef(DataArray *d) {
    Hmx::Object::SetTypeDef(d);
    Update();
}

String InlineHelp::GetIconStringFromAction(int idx) {
    static Symbol action_chars("action_chars");
    String ret;
    const DataArray *t = TypeDef();
    MILO_ASSERT(t, 0x1cb);
    DataArray *actionArr = t->FindArray(action_chars);
    FOREACH (it, mIconTypes) {
        const char *str = actionArr->FindArray(*it)->Str(idx + 1);
        char c = *str;
        if (ret.find(c) == String::npos)
            ret += c;
    }
    return ret;
}

void InlineHelp::ResetRotation() {
    sRotated = 0;
    sHasFlippedTextThisRotation = 0;
    sRotationTime = TheTaskMgr.UISeconds() + 5.0f;
    sLabelRot = -0.0f;
}

void InlineHelp::Update() {
    const DataArray *pTypeDef = TypeDef();
    if (pTypeDef && mResourceDir) {
        static Symbol text_label("text_label");
        mTemplateLabel = mResourceDir->Find<UILabel>(pTypeDef->FindStr(text_label), true);
        SyncLabelsToConfig();
    }
}

void InlineHelp::UpdateIconTypes(bool b) {
    mIconTypes.clear();
    const DataArray *pTypeDef = TypeDef();
    if (pTypeDef) {
        static Symbol action_chars("action_chars");
        DataArray *charArray = pTypeDef->FindArray(action_chars);
        for (int i = 1; i < charArray->Size(); i++) {
            mIconTypes.push_back(charArray->Array(i)->Sym(0));
        }
    }
}

void InlineHelp::SetLabelRotationPcts(float f) {
    if (f < 0.5f)
        sLabelRot = f * -240.0f;
    else
        sLabelRot = f * -240.0f - 120.0f;
}

void InlineHelp::Poll() {
    UIComponent::Poll();
    float uisecs = TheTaskMgr.UISeconds();
    if (uisecs != sLastUpdatedTime) {
        sNeedsTextUpdate = false;
        if (uisecs > sRotationTime) {
            float f1 = uisecs - sRotationTime;
            if (f1 >= 1.0f) {
                sHasFlippedTextThisRotation = false;
                sRotationTime = uisecs + 5.0f;
                SetLabelRotationPcts(0);
            } else {
                if (!sHasFlippedTextThisRotation && f1 >= 0.5f) {
                    sHasFlippedTextThisRotation = true;
                    sRotated = sRotated == 0;
                    sNeedsTextUpdate = true;
                }
                SetLabelRotationPcts(f1);
            }
        }
        sLastUpdatedTime = uisecs;
    }
    if (sNeedsTextUpdate)
        UpdateLabelText();
}

void InlineHelp::DrawShowing() {
    int numLabels = mTextLabels.size();
    const Transform &parentXfm = mTemplateLabel->WorldXfm();
    Transform worldXfm;
    memcpy(&worldXfm, &parentXfm, sizeof(Transform));
    UILabel *t = mTemplateLabel;
    MILO_ASSERT(t, 0x117);

    Transform offsetXfm;
    offsetXfm.m.Identity();
    offsetXfm.v.Zero();

    Transform rotXfm;
    if (sLabelRot != 0.0f) {
        Vector3 angles(DegreesToRadians(sLabelRot), 0.0f, 0.0f);
        Hmx::Matrix3 rotMtx;
        MakeRotMatrix(angles, rotMtx, true);
        Multiply(offsetXfm, rotMtx, rotXfm);
    } else {
        rotXfm.m.Identity();
        rotXfm.v.Zero();
    }

    for (int i = 0; i < numLabels; i++) {
        if (i > 0) {
            if (mHorizontal) {
                offsetXfm.v.x += mSpacing;
            } else {
                offsetXfm.v.z += mSpacing;
            }
        }
        Transform labelXfm;
        Multiply(offsetXfm, worldXfm, labelXfm);
        if (*mConfig[i].mSecondaryStr.c_str() != '\0') {
            Multiply(rotXfm, labelXfm, labelXfm);
        }
        mTextLabels[i]->SetWorldXfm(labelXfm);
        mTextLabels[i]->DrawShowing();
    }
}

void InlineHelp::SetActionToken(JoypadAction a, DataNode &node) {
    bool found = false;
    FOREACH (it, mConfig) {
        if ((*it).mAction == a) {
            (*it).SetConfig(node, false);
            found = true;
            break;
        }
    }
    if (!found) {
        ActionElement el(a);
        el.SetConfig(node, false);
        mConfig.push_back(el);
    }
    SyncLabelsToConfig();
}

void InlineHelp::SyncLabelsToConfig() {
    ResetRotation();
    int cfg_size = (int)mConfig.size();
    int labels_size = (int)mTextLabels.size();
    if (cfg_size > labels_size) {
        for (int i = labels_size; i < cfg_size; i++) {
            UILabel *lbl = Hmx::Object::New<UILabel>();
            if (mTemplateLabel != nullptr) {
                ((UILabel *)mTemplateLabel)->Copy(lbl, Hmx::Object::kCopyShallow);
            }
            lbl->LStyle(0).mColorOverride = mTextColor;
            mTextLabels.push_back(lbl);
        }
    } else if (labels_size > cfg_size) {
        for (int i = cfg_size; i < labels_size; i++) {
            delete mTextLabels[i];
        }
        mTextLabels.resize(cfg_size);
    }
    UpdateLabelText();
}

DataNode InlineHelp::OnSetConfig(const DataArray *da) {
    mConfig.clear();
    DataArray *arr = da->Array(2);
    for (int i = 0; i < arr->Size(); i++) {
        DataArray *loopArr = arr->Array(i);
        ActionElement el((JoypadAction)loopArr->Int(0));
        el.SetConfig(loopArr->Node(1), false);
        if (loopArr->Size() > 2)
            el.SetConfig(loopArr->Node(2), true);
        mConfig.push_back(el);
    }
    SyncLabelsToConfig();
    return DataNode(1);
}

BEGIN_HANDLERS(InlineHelp)
    HANDLE_ACTION(
        set_action_token, SetActionToken((JoypadAction)_msg->Int(2), _msg->Node(3))
    )
    HANDLE_ACTION(clear_action_token, ClearActionToken((JoypadAction)_msg->Int(2)))
    HANDLE(set_config, OnSetConfig)
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS

#pragma endregion InlineHelp
#pragma region InlineHelp::ActionElement

InlineHelp::ActionElement::ActionElement()
    : mAction(kAction_None), mPrimaryToken(gNullStr), mSecondaryToken(gNullStr) {}

InlineHelp::ActionElement::ActionElement(JoypadAction a)
    : mAction(a), mPrimaryToken(gNullStr), mSecondaryToken(gNullStr) {}

InlineHelp::ActionElement::ActionElement(InlineHelp::ActionElement const &ac)
    : mAction(ac.mAction), mPrimaryToken(ac.mPrimaryToken),
      mSecondaryToken(ac.mSecondaryToken), mPrimaryStr(ac.mPrimaryStr),
      mSecondaryStr(ac.mSecondaryStr) {}

InlineHelp::ActionElement::~ActionElement() {}

void InlineHelp::ActionElement::SetToken(Symbol s, bool secondary) {
    if (!secondary) {
        mPrimaryToken = s;
        mPrimaryStr = Localize(s, 0, TheLocale);
    } else {
        mSecondaryToken = s;
        mSecondaryStr = Localize(s, 0, TheLocale);
    }
}

void InlineHelp::ActionElement::SetString(const char *s, bool b) {
    if (!b) {
        mPrimaryToken = gNullStr;
        mPrimaryStr = s;
    } else {
        mSecondaryToken = gNullStr;
        mSecondaryStr = s;
    }
}

void InlineHelp::ActionElement::SetConfig(DataNode &dn, bool b) {
    if (dn.Type() == kDataArray) {
        DataArray *da = dn.Array();
        if (da->Size() == 0)
            return;
        FormatString fs(Localize(da->Sym(0), 0, TheLocale));
        for (int i = 1; i < da->Size(); i++) {
            const DataNode &dn2 = da->Evaluate(i);
            if (dn2.Type() == kDataSymbol) {
                fs << Localize(dn2.Sym(), 0, TheLocale);
            } else {
                fs << dn2;
            }
        }
        SetString(fs.Str(), b);
    } else {
        SetToken(dn.Sym(), b);
    }
}

Symbol InlineHelp::ActionElement::GetToken(bool b) const {
    if (b)
        return mSecondaryToken;
    return mPrimaryToken;
}

BinStream &operator<<(BinStream &bs, const InlineHelp::ActionElement &ae) {
    bs << (int)ae.mAction;
    Symbol primary = ae.mPrimaryToken;
    bs << primary;
    Symbol secondary = ae.mSecondaryToken;
    bs << secondary;
    return bs;
}

BinStream &operator>>(BinStream &bs, InlineHelp::ActionElement &ae) {
    LOAD_REVS(bs);
    {
        int x;
        bs >> x;
        ae.mAction = (JoypadAction)x;
    }
    Symbol s;
    bs >> s;
    ae.SetToken(s, false);
    if (d.rev >= 2) {
        bs >> s;
        ae.SetToken(s, true);
    }
    return bs;
}

const char *InlineHelp::ActionElement::GetText(bool b) const {
    if (b && HasSecondaryStr())
        return mSecondaryStr.c_str();
    return mPrimaryStr.c_str();
}

BEGIN_CUSTOM_PROPSYNC(InlineHelp::ActionElement)
    SYNC_PROP(action, (int &)o.mAction)
    SYNC_PROP_SET(text_token, o.GetToken(false), o.SetToken(_val.Sym(), false))
    SYNC_PROP_SET(secondary_token, o.GetToken(true), o.SetToken(_val.Sym(), true))
END_CUSTOM_PROPSYNC

BinStream &operator>>(BinStreamRev &bs, InlineHelp::ActionElement &ae) {
    int x;
    bs >> x;
    ae.mAction = (JoypadAction)x;
    Symbol s;
    bs >> s;
    ae.SetToken(s, false);
    if (bs.rev >= 2) {
        bs >> s;
        ae.SetToken(s, true);
    }
    return bs.stream;
}

void InlineHelp::ClearActionToken(JoypadAction a) {
    for (std::vector<ActionElement>::iterator it = mConfig.begin(); it != mConfig.end();
         ++it) {
        if ((*it).mAction == a) {
            mConfig.erase(it);
            SyncLabelsToConfig();
            return;
        }
    }
}

#pragma endregion InlineHelp::ActionElement
