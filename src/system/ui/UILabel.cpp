#include "ui/UILabel.h"

#include "macros.h"
#include "ui/ResourceDirPtr.h"
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
    SYNC_PROP(width, *(float *)(((unsigned char *)this) - 0x128))
    SYNC_PROP(height, *(float *)(((unsigned char *)this) - 0x124))
    SYNC_PROP(circle, *(float *)(((unsigned char *)this) - 0x120))
    SYNC_PROP(alignment, *(int *)(((unsigned char *)this) - 0x11C))
    SYNC_PROP(fit_type, *(int *)(((unsigned char *)this) - 0x118))
    SYNC_PROP(caps_mode, *(int *)(((unsigned char *)this) - 0x114))
    SYNC_PROP(markup, *(bool *)(((unsigned char *)this) - 0x108))
    SYNC_PROP(scroll_delay, *(float *)(((unsigned char *)this) - 0x104))
    SYNC_PROP(scroll_rate, *(float *)(((unsigned char *)this) - 0x100))
    SYNC_PROP(scroll_pause, *(float *)(((unsigned char *)this) - 0xFC))
    SYNC_PROP(leading, *(float *)(((unsigned char *)this) - 0x110))
    SYNC_PROP(indentation, *(float *)(((unsigned char *)this) - 0xD4))
    SYNC_PROP(basic_markup, *(bool *)(((unsigned char *)this) - 0x107))
    SYNC_PROP(fixed_length, mFixedLength)
    SYNC_PROP(draw_width, unkbc)
    SYNC_PROP(styles, unk124)
    SYNC_SUPERCLASS(UIComponent)
    SYNC_SUPERCLASS(RndText)
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

INIT_REVS(0x18, 0)

void UILabel::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0x18, 0)
    UIComponent::PreLoad(bs);
    if (d.rev > 0 && d.rev < 0xE) {
        bool deprecated;
        d >> deprecated;
    }
    d >> mTextToken;
    if (d.rev > 0xD) {
        String str;
        d >> str;
    }
    if (d.rev > 0xE)
        d >> unk118;
    if (d.rev > 1) {
        int alignment, capsMode;
        d >> unk120 >> alignment >> capsMode;
        MILO_ASSERT(alignment < 255, 0xFF);
        MILO_ASSERT(capsMode < 255, 0x100);
        if (d.rev > 7) {
            LOAD_BITFIELD(bool, mMarkup)
        }
    }
    if (d.rev > 4) {
        // No-op in current revision
    }
    if (d.rev > 6 && d.rev < 0x1b) {
        int styleVal;
        d >> styleVal;
    }
    if (d.rev > 8 && d.rev < 0x10) {
        int shadowVal;
        d >> shadowVal;
    }
    if (d.rev > 9 && d.rev < 0x1a) {
        String str;
        d >> str;
    }
    if (d.rev > 10) {
        int val;
        d >> val;
    }
    if (d.rev > 0xc) {
        int val;
        d >> val;
    }
    if (d.rev > 0x10 && d.rev < 0x1d) {
        int val;
        d >> val;
    }
    if (d.rev > 0x11) {
        int val;
        d >> val;
        String str;
        d >> str;
    }
    if (d.rev < 0x13) {
        int val;
        d >> val;
    } else {
        int val;
        d >> val;
    }
    if (d.rev > 0x13) {
        int val;
        d >> val;
        if (d.rev < 0x19) {
            int val2;
            d >> val2;
        }
    }
    if (d.rev > 0x14) {
        String str;
        d >> str;
    }
    if (d.rev > 0x15) {
        int elemCount = (*(int *)(((unsigned char *)this) - 0x10) - *(int *)(((unsigned char *)this) - 0x14)) / 0x2c;
        if (elemCount == 2) {
            String str;
            d >> str;
        } else {
            String str;
            d >> str;
        }
    }
    if (d.rev > 0x16) {
        String str;
        d >> str;
    }
    if (d.rev > 0x17) {
        int val1, val2;
        d >> val1 >> val2;
    }
}

void UILabel::PostLoad(BinStream &bs) {
    int *end = (int *)(((unsigned char *)this) - 0x10);
    int *begin = (int *)(((unsigned char *)this) - 0x14);
    int rev = bs.PopRev(Dir());

    // Calculate number of styles from vector begin/end pointers
    int numStyles = (*end - *begin) / 0x2c;

    // PostLoad each style's UILabelDir resource pointer
    if (numStyles != 0) {
        int offset = 0;
        unsigned int i = 0;
        do {
            ResourceDirPtr<UILabelDir> *ptr = (ResourceDirPtr<UILabelDir> *)((unsigned char *)*begin + offset + 0x14);
            ptr->PostLoad(0);
            i++;
            offset += 0x2c;
        } while (i < (unsigned int)numStyles);
    }

    // Handle font mat loading based on file revision
    if (rev >= 0x1c) {
        // Rev 28+: Read font mat strings for each style from stream
        if (numStyles != 0) {
            unsigned int i = 0;
            do {
                char buffer[0x100];
                bs.ReadString(buffer, 0x100);
                SetFontMat(buffer, i);
                i++;
            } while (i < (unsigned int)numStyles);
        }
    } else if (rev > 0x14) {
        // Rev 21-27: Legacy revision handling with PopRev for old string fields
        if (rev > 0x16) {
            // Rev 23-27: Skip old string field at rev 0x17
            bs.PopRev(Dir());
            SetFontMat("", 1);
        }
        if (rev > 0x15) {
            // Rev 22-27: Skip old string field at rev 0x16
            bs.PopRev(Dir());
        }
        SetFontMat("", 0);
    } else {
        // Rev <= 20: Initialize all styles with empty font mat strings
        if (numStyles != 0) {
            unsigned int i = 0;
            do {
                SetFontMat("", i);
                i++;
            } while (i < (unsigned int)numStyles);
        }
    }

    UIComponent::PostLoad(bs);

    // Initialize label text - defer actual UI updates until end
    sDeferUpdate = true;
    if (!unk118.empty()) {
        // If edit text is set, use first character as icon
        unk120 = unk118[0];
    } else if (mTextToken.Null() || (!TheLoadMgr.EditMode() && !AllowEditText())) {
        // Set text from token (will localize)
        SetTextToken(mTextToken);
    } else {
        // In edit mode with allowed edit text, use token string directly
        RndText::SetText(mTextToken.Str());
    }

    // Validate fixed length requirement for preloaded labels
    if (sRequireFixedLength) {
        if (unk120 == 0) {
            MILO_NOTIFY(
                "%s: %s is preloaded, but doesn't have fixed length",
                PathName(Dir()),
                Name()
            );
        }
    }

    // Re-enable updates and refresh label display if needed
    sDeferUpdate = false;
    if (!mTextToken.Null() || !unk118.empty() || unk120 != 0) {
        LabelUpdate(false);
    } else {
        unk121 = true;
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

const RndText::Style &UILabel::Style(int idx) const {
    if ((unsigned int)idx < mStyles.size()) {
        return mStyles[idx];
    }
    static RndText::Style s(0);
    return s;
}

RndText::Style &UILabel::Style(int idx) {
    if ((unsigned int)idx < mStyles.size()) {
        return mStyles[idx];
    }
    static RndText::Style s(0);
    return s;
}

void UILabel::SetPrelocalizedString(String &s) { SetDisplayText(s.c_str(), true); }

void UILabel::SetSubtitle(const DataArray *da) { SetDisplayText(da->Str(2), true); }

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

UILabel::LabelStyle &UILabel::LStyle(int i) { return unk124[i]; }

const UILabel::LabelStyle &UILabel::LStyle(int i) const { return unk124[i]; }

void UILabel::OldResourcePreload(BinStream &bs) {
    char buffer[0x100];
    LabelStyle &style = LStyle(0);
    ResourceDirPtr<UILabelDir> &ptr = *(ResourceDirPtr<UILabelDir> *)&style.unk14;
    bs.ReadString(buffer, 0x100);
    ptr.SetName(buffer, true);
}

void UILabel::SetDisplayText(const char *cc, bool b) {
    if (b) {
        mTextToken = gNullStr;
    }
    RndText::SetText(cc);
    if (strchr(cc, '<')) {
        mMarkup = true;
    }
    if (!sDeferUpdate) {
        LabelUpdate(false);
    }
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

DataNode UILabel::OnSetTokenFmt(const DataArray *da) {
    const DataNode &n = da->Evaluate(2);
    if (n.Type() == kDataArray) {
        DataArray *arr = n.Array();
        bool b = arr->Size() > 1 && arr->Evaluate(1).Type() == kDataArray;
        if (b) {
            SetTokenFmtImp(arr->ForceSym(0), arr->Array(1), arr, 2, false);
        } else
            SetTokenFmtImp(arr->ForceSym(0), 0, arr, 1, false);
    } else {
        bool b = da->Size() > 3 && da->Evaluate(3).Type() == kDataArray;
        if (b) {
            SetTokenFmtImp(da->ForceSym(2), da->Array(3), da, 4, false);
        } else {
            SetTokenFmtImp(da->ForceSym(2), 0, da, 3, false);
        }
    }
    return 1;
}

DataNode UILabel::OnSetInt(DataArray const *da) {
    int i = da->Int(2);
    bool b = false;
    if (da->Size() > 3)
        b = da->Int(3) != 0;
    SetInt(i, b);
    return DataNode(1);
}

DataNode UILabel::OnSetTimeHMS(DataArray const *) { return NULL_OBJ; }

bool UILabel::AllowEditText() const { return false; }

void UILabel::LabelUpdate(bool b) {
    unk122 = false;

    // Get reference font from Style(0)
    RndFontBase *refFont = Style(0).mFont;

    // Set Style(0) text color to white
    RndText::Style &s0 = Style(0);
    float *color0 = &s0.mTextColor.red;
    int i = 1;
    color0[0] = 1.0f;
    color0[1] = 1.0f;
    color0[2] = 1.0f;
    color0[3] = 1.0f;

    // Loop through remaining styles (1 to n-1)
    if (unk124.size() > 1) {
        do {
            RndText::Style &style = Style(i);
            LabelStyle &lstyle = LStyle(i);

            // If colorOverride AND font AND font differs from reference
            if (lstyle.mColorOverride && style.mFont && style.mFont != refFont) {
                const Hmx::Color &c = lstyle.mColorOverride->GetColor();
                // Copy as words to match target codegen
                memcpy(&style.mTextColor, &c, sizeof(Hmx::Color));
            } else {
                style.mTextColor.red = 1.0f;
                style.mTextColor.green = 1.0f;
                style.mTextColor.blue = 1.0f;
                style.mTextColor.alpha = 1.0f;
            }
            i++;
        } while (i < unk124.size());
    }

    UpdateText();
    CheckValid(!TheLoadMgr.EditMode());
}

DataNode UILabel::OnSetHeightFromText(DataArray *da) {
    if (mFitType == 0 && Style(0).mFont) {
        float height;
        mHeight = ComputeHeight(unkc4, 1.0f, height);
    } else {
        FormatString fs("Could not set height, either no default font set, or fit type is not kFitWrap");
        TheDebug.Notify(fs.Str());
    }
    return DataNode(0);
}

void UILabel::SetFontMat(char const *c, int i) {
    RndFontBase *font = 0;
    LabelStyle &ls = LStyle(i);
    UILabelDir *dir = ls.unk14;
    if (dir) {
        font = dir->FontObj(Symbol(c));
        if (!font) {
            if (*c) {
                TheDebug.Notify(MakeString(
                    "%s is referencing a mat variation '%s' that is not found in the font resource",
                    PathName(this), c
                ));
                font = dir->FontObj(Symbol(""));
            }
            if (!font) {
                TheDebug.Notify(MakeString(
                    "%s in resource %s has no default font",
                    PathName(this), PathName(dir)
                ));
            }
        }
    } else if (*c) {
        TheDebug.Notify(MakeString(
            "%s [styles 0 font_resource] is NULL, couldn't set font mat %s",
            PathName(this), c
        ));
    }
    if ((unsigned int)i < mStyles.size()) {
        mStyles[i].mFont = font;
    }
}

char const *UILabel::GetFontMat(int i) {
    LabelStyle &ls = LStyle(i);
    UILabelDir *dir = ls.unk14;
    if (dir) {
        RndText::Style &s = Style(i);
        return dir->GetMatVariationName(s.mFont);
    }
    return "";
}

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

