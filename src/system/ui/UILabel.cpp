#include "ui/UILabel.h"

#include "macros.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Text.h"
#include "rndobj/Trans.h"
#include "ui/UI.h"
#include "ui/UILabelDir.h"
#include "ui/UIListWidget.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Locale.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "utl/UTF8.h"
#include <cstring>

bool UILabel::sDeferUpdate;

void UILabel::Load(BinStream &bs) {
    PreLoad(bs);
    PostLoad(bs);
}

UILabel::UILabel() : unk122(1), unk124(this) {
    unk124.resize(1);
    unk120 = 0;
    unk121 = false;
}

BEGIN_PROPSYNCS(UILabel)
    SYNC_PROP_SET(text_token, mTextToken, SetTextToken(_val.ForceSym()))
    SYNC_PROP_SET(icon, unk118, SetIcon(_val.Str(0)[0]))
    SYNC_PROP(edit_text, unk118)
    SYNC_PROP(draw_width, unkb4)
    SYNC_PROP(styles, unk124)
    SYNC_SUPERCLASS(RndText)
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

BEGIN_COPYS(UILabel)
    COPY_SUPERCLASS(UIComponent)
    COPY_SUPERCLASS(RndText)
    CREATE_COPY(UILabel)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mTextToken)
        COPY_MEMBER(unk118)
        //looks like an strcpy here
    END_COPYING_MEMBERS
    if (sDeferUpdate == false) {
        LabelUpdate(false);
    }
END_COPYS

BEGIN_SAVES(UILabel)
    int version = 0x10021;
    bs << version;
    SAVE_SUPERCLASS(UIComponent)
    bs << mTextToken;
    if ((version != 0) && !AllowEditText()) {
        bs << gNullStr;
    } else {
        bs << unk118;
    }
    bs << unk120;
    bs << *(float *)(((unsigned char *)this) - 0x11c);
    bs << *(float *)(((unsigned char *)this) - 0x128);
    bs << *(float *)(((unsigned char *)this) - 0x110);
    bs << *(float *)(((unsigned char *)this) - 0x10c);
    bs << *(unsigned char *)(((unsigned char *)this) - 0x108);
    bs << *(float *)(((unsigned char *)this) - 0x114);
    bs << *(float *)(((unsigned char *)this) - 0x124);
    bs << *(float *)(((unsigned char *)this) - 0x120);
    bs << *(float *)(((unsigned char *)this) - 0x118);

    int *begin = (int *)(((unsigned char *)this) - 0x14);
    int *end = (int *)(((unsigned char *)this) - 0x10);
    int numStyles = ((*end - *begin) / 0x2c);
    int numAsInt = numStyles;
    bs << numAsInt;

    if (numStyles != 0) {
        unsigned int styleIdx = 0;
        int offset = 0;
        do {
            bs << *(int *)(((unsigned char *)this) + offset - 0x14 + 0x14);
            bs << *(int *)(((unsigned char *)this) + offset - 0x14);
            RndText::Style *stylePtr = &Style(styleIdx);
            bs << stylePtr->mSize;
            bs << *(float *)(((unsigned char *)stylePtr) + 0x2c);
            bs << *(float *)(((unsigned char *)stylePtr) + 0x30);
            bs << *(float *)(((unsigned char *)stylePtr) + 0x28);
            bs << *(float *)(((unsigned char *)stylePtr) + 0x24);
            bs << *(unsigned char *)(((unsigned char *)stylePtr) + 0x48);
            styleIdx++;
            offset += 0x2c;
        } while (styleIdx < (unsigned int)numStyles);
    }

    bs << *(float *)(((unsigned char *)this) - 0x104);
    bs << *(float *)(((unsigned char *)this) - 0x100);
    bs << *(float *)(((unsigned char *)this) - 0xfc);
    bs << *(float *)(((unsigned char *)this) - 0xd4);
    bs << *(unsigned char *)(((unsigned char *)this) - 0x107);

    int numFonts = ((*end - *begin) / 0x2c);
    if (numFonts != 0) {
        unsigned int fontIdx = 0;
        do {
            bs << GetFontMat(fontIdx);
            fontIdx++;
        } while (fontIdx < (unsigned int)numFonts);
    }
END_SAVES

void UILabel::PreLoad(BinStream &) {}

void UILabel::PostLoad(BinStream &bs) {
    UIComponent::PostLoad(bs);

    LabelUpdate(false);
    sDeferUpdate = true;
    if (!unk118.empty()) {
        unk120 = unk118[0];
    } else {
        SetTextToken(mTextToken);
    }
    if (sRequireFixedLength) {
        if (unk120 == 0) {
            MILO_WARN(
                "%s: %s is preloaded, but doesn't have fixed length",
                PathName(Dir()),
                Name()
            );
        }
    }
    sDeferUpdate = false;
    if (!mTextToken.Null() || !unk118.empty()) {
        LabelUpdate(false);
    }
}

Symbol UILabel::TextToken() { return mTextToken; }

void UILabel::Poll() {}

void UILabel::Highlight() {}

void UILabel::DrawShowing() {}

void UILabel::SetTextToken(Symbol s) {
    mTextToken = s;

    SetTokenFmtImp(mTextToken, 0, 0, 0, true);
}

void UILabel::SetInt(int i, bool b) {
    if (b) {
        SetDisplayText(LocalizeSeparatedInt(i, TheLocale), true);
    } else
        SetDisplayText(MakeString("%d", i), true);
}

void UILabel::SetFloat(const char *cc, float f) {
    SetDisplayText(LocalizeFloat(cc, f), true);
}

void UILabel::SetDateTime(DateTime const &dt, Symbol s) {
    String str(Localize(s, false, TheLocale));
    dt.Format(str);
    SetDisplayText(str.c_str(), true);
}

void UILabel::SetIcon(char c) {
    unk120 = c;
    if (c == '\0' && TheLoadMgr.EditMode() != 0) {
        SetEditText(unk118.c_str());
        return;
    }
}

void UILabel::SetTokenFmt(const DataArray *da) {
    const DataNode &n = da->Evaluate(0);
    bool b = (da->Size() > 1) && (da->Evaluate(1).Type() == kDataArray);
    if (b) {
        SetTokenFmtImp(da->ForceSym(0), da->Array(1), da, 2, false);
    } else {
        SetTokenFmtImp(da->ForceSym(0), 0, da, 1, false);
    }
}

RndText::Style &UILabel::Style(int) { return RndText::Style(0); }

void UILabel::SetPrelocalizedString(String &s) {}

void UILabel::SetSubtitle(const DataArray *) {}

void UILabel::SetTimeHMS(int, bool) {}

bool UILabel::CheckValid(bool warn) {
    if (mFixedLength != 0 && UTF8StrLen(mText.c_str()) > (unsigned int)mFixedLength) {
        if (warn) {
            MILO_WARN(
                "%s: %s has fixed length of %i but text is %i long (%s)",
                PathName(Dir()),
                Name(),
                mFixedLength,
                UTF8StrLen(mText.c_str()),
                mText.c_str()
            );
        }
        return false;
    }
    return true;
}

void UILabel::SetEditText(const char *c) {}

char const *UILabel::GetDefaultText() const {
    if (unk120 != 0) {
        return &unk120;
    }

    if (TheLoadMgr.EditMode() && !unk118.empty())
        return unk118.c_str();
    else
        return Localize(mTextToken, nullptr, TheLocale);
}

void UILabel::CenterWithLabel(UILabel *, bool, float) {}

// UILabel::LabelStyle &UILabel::LStyle(int) { return new LabelStyle(0); }

void UILabel::OldResourcePreload(BinStream &) {}

void UILabel::SetDisplayText(const char *cc, bool b) {
    if (b)
        mTextToken = gNullStr;
    RndText::SetText(cc);
    if (strchr(cc, 60)) {
        mMarkup = true;
    }
    if (!sDeferUpdate)
        LabelUpdate(false);
}

void UILabel::Init() {
    REGISTER_OBJ_FACTORY(UILabel);
    UILabelDir::Init();
}

void UILabel::SetTokenFmtImp(
    Symbol s, const DataArray *da1, const DataArray *da2, int i, bool b
) {}

DataNode UILabel::OnSetPrelocalizedString(DataArray const *da) {
    return NULL_OBJ;
}

DataNode UILabel::OnSetTokenFmt(DataArray const *da) { return NULL_OBJ; }

DataNode UILabel::OnSetInt(DataArray const *da) { return DataNode(1); }

DataNode UILabel::OnSetTimeHMS(DataArray const *) { return NULL_OBJ; }

bool UILabel::AllowEditText() const { return false; }

void UILabel::LabelUpdate(bool b) { unk122 = false; }

DataNode UILabel::OnSetHeightFromText(DataArray *da) {
    if ((*(int *)((unsigned char *)da + 0x20) == 0) &&
        (*(int *)((unsigned char *)&Style(0) + 0x40) != 0)) {
        float height = 0.0f;
        ComputeHeight(*(int *)((unsigned char *)da + 0xc4), 1.0f, height);
        *(float *)((unsigned char *)da + 0x14) = height;
    } else {
        FormatString fs("Could not set height, either no style or no text");
        TheDebug.Notify(fs.Str());
    }
    *(int *)this = 0;
    *(int *)((unsigned char *)this + 4) = 0;
    return (DataNode)this;
}

void UILabel::SetFontMat(char const *c, int i) {
    RndMat *rndmat = nullptr;
    auto labelStyle = LStyle(i);

}

char const *UILabel::GetFontMat(int) { return 0; }

void UILabel::RefreshFontMat(int i) {
    auto mat = GetFontMat(i);
    SetFontMat(mat, i);
    if (sDeferUpdate == false) {
        LabelUpdate(false);
    }
}

BEGIN_HANDLERS(UILabel)
    HANDLE_ACTION(set_token_fmt, OnSetTokenFmt(_msg))
    HANDLE_ACTION(set_prelocalized_string, OnSetPrelocalizedString(_msg))
    HANDLE_ACTION(set_int, OnSetInt(_msg))
    HANDLE_ACTION(set_float, SetFloat(_msg->Str(2), _msg->Float(3)))
    HANDLE_ACTION(set_time_hms, OnSetTimeHMS(_msg))
    HANDLE_ACTION(center_with_label, CenterWithLabel(_msg->Obj<UILabel>(2), _msg->Int(3), _msg->Float(4)))
    HANDLE_EXPR(get_font_mats, UILabelDir::GetMatVariations(LStyle(_msg->Int(2)).unk14))
    HANDLE_ACTION(set_height_from_text, OnSetHeightFromText(_msg))
    HANDLE_EXPR(draw_rect_width, unkbc)
    HANDLE_ACTION(reload_string, UIComponent::Poll())
    HANDLE_SUPERCLASS(UIComponent)
    HANDLE_SUPERCLASS(RndText)
END_HANDLERS

// Static initialization for symbol caching - PropSync template for LabelStyle
static int g_PropSync_LabelStyle_init = 0;
static Symbol g_list_sym;
static Symbol g_file_path_sym;

bool PropSync(UILabel::LabelStyle &style, DataNode &node, DataArray *array, int index, PropOp op) {
    // Bounds check
    if (index >= array->Size()) {
        return false;
    }

    // Initialize symbols on first check
    if (!g_PropSync_LabelStyle_init) {
        g_PropSync_LabelStyle_init = 1;
        g_list_sym = Symbol("list");
        g_file_path_sym = Symbol("file_path");
    }

    // Get the current property symbol
    Symbol prop_sym = array->Sym(index);

    // Handle "list" property - recurse to UILabelDir PropSync
    if (prop_sym == g_list_sym) {
        return PropSync(style.unk14, node, array, index + 1, op);
    }

    // Handle "file_path" property - recurse to UILabelDir PropSync
    if (prop_sym == g_file_path_sym) {
        return PropSync(style.unk14, node, array, index + 1, op);
    }

    // Unknown property
    return false;
}

