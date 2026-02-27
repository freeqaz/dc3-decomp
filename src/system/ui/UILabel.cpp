#include "ui/UILabel.h"

#include "macros.h"
#include "math/Geo.h"
#include "ui/ResourceDirPtr.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Cam.h"
#include "rndobj/Text.h"
#include "rndobj/Trans.h"
#include "rndobj/Utl.h"
#include "ui/UI.h"
#include "ui/UIColor.h"
#include "ui/UILabelDir.h"
#include "ui/UIListWidget.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/Locale.h"
#include "utl/Str.h"
#include "utl/SuperFormatString.h"
#include "utl/Symbol.h"
#include "utl/UTF8.h"
#include <cmath>
#include <cstring>

bool UILabel::sDeferUpdate;
bool UILabel::sInDebugHighlight;
static UILabel *sLabel;

void UILabel::Load(BinStream &bs) {
    PreLoad(bs);
    PostLoad(bs);
}

UILabel::UILabel() : mDirty(1), mLabelStyles(this) {
    mLabelStyles.resize(1);
    mIconChar = 0;
    mTextEmpty = false;
}

BEGIN_PROPSYNCS(UILabel)
    SYNC_PROP_SET(text_token, mTextToken, SetTextToken(_val.ForceSym()))
    SYNC_PROP_SET(icon, mLabelText, SetIcon(_val.Str(0)[0]))
    SYNC_PROP_SET(edit_text, mLabelText, SetEditText(_val.Str(0)))
#define LABEL_UPDATE_IF_NEEDED if (!sDeferUpdate) LabelUpdate(false)
    SYNC_PROP_MODIFY(width, mWidth, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(height, mHeight, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(circle, mCircle, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(alignment, (int&)mAlignment, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(fit_type, (int&)mFitType, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(caps_mode, (int&)mCapsMode, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(markup, mMarkup, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(scroll_delay, mScrollDelay, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(scroll_rate, mScrollRate, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(scroll_pause, mScrollPause, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(leading, mLeading, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(indentation, mIndentation, LABEL_UPDATE_IF_NEEDED)
    SYNC_PROP_MODIFY(basic_markup, mBasicMarkup, LABEL_UPDATE_IF_NEEDED)
#undef LABEL_UPDATE_IF_NEEDED
    SYNC_PROP_SET(fixed_length, mFixedLength, SetFixedLength(_val.Int(0)))
    SYNC_PROP(draw_width, mBoundsRight)
    {
        _NEW_STATIC_SYMBOL(styles)
        if (sym == _s) {
            sLabel = this;
            return PropSync(mLabelStyles, _val, _prop, _i + 1, _op);
        }
    }
    SYNC_SUPERCLASS(UIComponent)
END_PROPSYNCS

BEGIN_COPYS(UILabel)
    COPY_SUPERCLASS(UIComponent)
    COPY_SUPERCLASS(RndText)
    CREATE_COPY(UILabel)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mTextToken)
        COPY_MEMBER(mLabelText)
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
    if (bs.Cached() && !AllowEditText()) {
        bs << gNullStr;
    } else {
        bs << mLabelText;
    }
    bs << mIconChar;
    bs << (int&)mAlignment;
    bs << mWidth;
    bs << mLeading;
    bs << mFixedLength;
    bs << mMarkup;
    bs << (int&)mCapsMode;
    bs << mHeight;
    bs << mCircle;
    bs << (int&)mFitType;

    int numStyles = mLabelStyles.size();
    bs << numStyles;

    if (numStyles != 0) {
        unsigned int styleIdx = 0;
        int offset = 0;
        LabelStyle *basePtr = &mLabelStyles[0];
        do {
            LabelStyle *ls = (LabelStyle *)((unsigned char *)basePtr + offset);
            bs << ls->mLabelDir;
            bs << ls->mColorOverride;
            RndText::Style &style = Style(styleIdx);
            bs << style.mSize;
            bs << style.mKerning;
            bs << style.mZOffset;
            bs << style.mItalics;
            bs << style.mFontColor.alpha;
            bs << style.mBlacklight;
            styleIdx++;
            offset += 0x2c;
        } while (styleIdx < (unsigned int)numStyles);
    }

    bs << mScrollDelay;
    bs << mScrollRate;
    bs << mScrollPause;
    bs << mIndentation;
    bs << mBasicMarkup;

    if (numStyles != 0) {
        unsigned int fontIdx = 0;
        do {
            bs << GetFontMat(fontIdx);
            fontIdx++;
        } while (fontIdx < (unsigned int)numStyles);
    }
END_SAVES

// Custom revision limits structure to match target binary layout
static const struct {
    int gRev;
    int gAltRev;
} sUILabelRevLimits = { 0x21, 1 };

#undef gRev
#undef gAltRev
#define gRev sUILabelRevLimits.gRev
#define gAltRev sUILabelRevLimits.gAltRev

void UILabel::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(0x21, 1)
    UIComponent::PreLoad(bs);
    if (d.rev > 0x1b) {
        // New format: reads all members directly
        d >> mTextToken;
        d >> mLabelText;
        d.stream.Read(&mIconChar, 1);
        d.stream.ReadEndian(&mAlignment, 4);
        d >> mWidth;
        d >> mLeading;
        int fixedLength;
        d >> fixedLength;
        SetFixedLength(fixedLength);
        d >> mMarkup;
        d.stream.ReadEndian(&mCapsMode, 4);
        d >> mHeight;
        if (d.altRev > 0) {
            d >> mCircle;
        }
        d.stream.ReadEndian(&mFitType, 4);
        unsigned int numStyles;
        d >> numStyles;
        mLabelStyles.resize(numStyles);
        mStyles.resize(numStyles);
        unsigned int i = 0;
        if (mLabelStyles.size() > 0) {
            int offset = 0;
            do {
                LabelStyle *ls = &mLabelStyles[0];
                ls = (LabelStyle *)((unsigned char *)ls + offset);
                ResourceDirPtr<UILabelDir> &resPtr =
                    *(ResourceDirPtr<UILabelDir> *)((unsigned char *)ls + 0x14);
                d.stream >> resPtr;
                ls->mColorOverride.Load(d.stream, true, 0);
                RndText::Style &style = Style(i);
                d >> style.mSize;
                d >> style.mKerning;
                d >> style.mZOffset;
                d >> style.mItalics;
                d >> style.mFontColor.alpha;
                if (d.rev >= 0x1e) {
                    d >> style.mBlacklight;
                }
                i++;
                offset += 0x2c;
            } while (i < mLabelStyles.size());
        }
        if (d.rev >= 0x1f) {
            d >> mScrollDelay;
            d >> mScrollRate;
            d >> mScrollPause;
        }
        if (d.rev >= 0x20) {
            d >> mIndentation;
        }
        if (d.rev >= 0x21) {
            d >> mBasicMarkup;
        }
    } else {
        // Old format: revision-guarded member reads
        if (d.rev > 0 && d.rev < 0xE) {
            bool deprecated;
            d >> deprecated;
        }
        d >> mTextToken;
        if (d.rev > 0xD) {
            d >> mLabelText;
        }
        if (d.rev > 0xE) {
            if (d.rev < 0x19) {
                String str;
                d >> str;
                mIconChar = str.c_str()[0];
            } else {
                d.stream.Read(&mIconChar, 1);
            }
        }
        if (d.rev > 1) {
            d >> Style(0).mSize;
            d.stream.ReadEndian(&mAlignment, 4);
            d.stream.ReadEndian(&mCapsMode, 4);
            if (d.rev > 7) {
                d >> mMarkup;
            }
            d >> mLeading;
            d >> Style(0).mKerning;
        }
        if (d.rev > 4) {
            d >> Style(0).mItalics;
        }
        if (d.rev > 2) {
            d.stream.ReadEndian(&mFitType, 4);
            d >> mWidth;
            d >> mHeight;
        }
        if (d.rev < 4) {
            Transform &xfm = DirtyLocalXfm();
            if (mAlignment & 1) {
                xfm.v.x -= mWidth / 2.0f;
            } else if (mAlignment & 4) {
                xfm.v.x += mWidth / 2.0f;
            }
            if (mAlignment & 0x10) {
                xfm.v.z += mHeight / 2.0f;
            } else if (mAlignment & 0x40) {
                xfm.v.z -= mHeight / 2.0f;
            }
        }
        if (d.rev > 5) {
            int fixedLength;
            d >> fixedLength;
            SetFixedLength(fixedLength);
        }
        if (d.rev > 6 && d.rev < 0x1b) {
            int reserveLines;
            d >> reserveLines;
        }
        if (d.rev > 8 && d.rev < 0x10) {
            bool shadowEnabled;
            d >> shadowEnabled;
            float shadowR, shadowG, shadowB;
            d >> shadowR >> shadowG >> shadowB;
        }
        if (d.rev > 9 && d.rev < 0x1a) {
            String str;
            d >> str;
        }
        if (d.rev > 10) {
            d >> Style(0).mFontColor.alpha;
        }
        if (d.rev > 0xc) {
            LStyle(0).mColorOverride.Load(d.stream, true, 0);
        }
        if (d.rev > 0x10 && d.rev < 0x1d) {
            bool useHighlightMesh;
            d >> useHighlightMesh;
        }
        if (d.rev > 0x11) {
            float altTextSize;
            d >> altTextSize;
            ObjPtr<UIColor> altTextColor(this, 0);
            altTextColor.Load(d.stream, true, 0);
            bool altStyleEnabled;
            d >> altStyleEnabled;
            unsigned int numStyles = (altStyleEnabled ? 1 : 0) + 1;
            if (altStyleEnabled) {
                ObjDirPtr<UILabelDir> &dirPtr =
                    *(ObjDirPtr<UILabelDir> *)((unsigned char *)&mLabelStyles[0] + 0x14);
                FilePath path = dirPtr.GetFile();
                mLabelStyles.resize(numStyles);
                ResourceDirPtr<UILabelDir> &resPtr =
                    *(ResourceDirPtr<UILabelDir> *)((unsigned char *)&mLabelStyles[0] + 0x2c + 0x14);
                resPtr.LoadFile(path, true, true, kLoadFront, false);
                mStyles.resize(numStyles);
            }
            Style(1).mSize = altTextSize;
            LStyle(1).mColorOverride.CopyRef(altTextColor);
        }
        if (d.rev > 0x12) {
            d >> Style(1).mKerning;
        } else {
            Style(1).mKerning = Style(0).mKerning;
        }
        if (d.rev > 0x13) {
            d >> Style(1).mZOffset;
            if (d.rev < 0x19) {
                Style(1).mZOffset = Style(1).mZOffset / Style(1).mSize;
            }
        }
        if (d.rev > 0x14) {
            Symbol fontMatVar;
            d >> fontMatVar;
            d.stream.PushRev((int)fontMatVar, this);
        }
        if (d.rev > 0x15) {
            if (mLabelStyles.size() == 2) {
                LabelStyle &ls = LStyle(1);
                char buffer[0x100];
                d.stream.ReadString(buffer, 0x100);
                ResourceDirPtr<UILabelDir> &resPtr =
                    *(ResourceDirPtr<UILabelDir> *)((unsigned char *)&ls + 0x14);
                resPtr.SetName(buffer, true);
            } else {
                char buffer[0x100];
                d.stream.ReadString(buffer, 0x100);
            }
        }
        if (d.rev > 0x16) {
            Symbol altMatVar;
            d >> altMatVar;
            d.stream.PushRev((int)altMatVar, this);
        }
        if (d.rev > 0x17) {
            d >> Style(1).mItalics;
            d >> Style(1).mFontColor.alpha;
        }
    }
    d.PushRev(this);
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
    if (!mLabelText.empty()) {
        // If edit text is set, use first character as icon
        mIconChar = mLabelText[0];
    } else if (mTextToken.Null() || (!TheLoadMgr.EditMode() && !AllowEditText())) {
        // Set text from token (will localize)
        SetTextToken(mTextToken);
    } else {
        // In edit mode with allowed edit text, use token string directly
        RndText::SetText(mTextToken.Str());
    }

    // Validate fixed length requirement for preloaded labels
    if (sRequireFixedLength) {
        if (mIconChar == 0) {
            MILO_NOTIFY(
                "%s: %s is preloaded, but doesn't have fixed length",
                PathName(Dir()),
                Name()
            );
        }
    }

    // Re-enable updates and refresh label display if needed
    sDeferUpdate = false;
    if (!mTextToken.Null() || !mLabelText.empty() || mIconChar != 0) {
        LabelUpdate(false);
    } else {
        mTextEmpty = true;
    }
}

Symbol UILabel::TextToken() { return mTextToken; }

void UILabel::Poll() { UIComponent::Poll(); }

void UILabel::Highlight() {
    RndTransformable::Highlight();
    Box box;
    GetWidthHeightBox(box);
    Hmx::Color color(1.0f, 1.0f, 0.5f, 1.0f);
    if (!CheckValid(false)) {
        int secs = (int)(TheTaskMgr.UISeconds() * 2.0f);
        if (secs % 2 == 0) {
            color.red = 1.0f;
            color.alpha = 1.0f;
            color.green = 0.2f;
            color.blue = 0.2f;
        }
    }
    RndText::Highlight();
    const Transform &xfm = WorldXfm();
    UtilDrawBox(xfm, box, color, false);
}

void UILabel::DrawShowing() {
    if (((const UILabel *)this)->Style(0).mFontColor.alpha > 0.0f) {
        if (mDirty && !sDeferUpdate) {
            LabelUpdate(false);
        }

        MILO_ASSERT(mLabelStyles.size() == mStyles.size(), 0x1EF);

        LabelStyle *it = mLabelStyles.begin();
        UILabelDir *labelDir = it->mLabelDir;
        if (labelDir) {
            UIColor *stateColor = labelDir->GetStateColor(mState);
            int i = 0;
            while (i < mLabelStyles.size()) {
                RndText::Style &style = Style(i);
                style.mFontColorOverride = true;
                UIColor *uiColor = it->mColorOverride;
                if (!uiColor) {
                    uiColor = stateColor;
                }
                const Hmx::Color &color = uiColor->GetColor();
                style.mFontColor.red = color.red;
                style.mFontColor.green = color.green;
                style.mFontColor.blue = color.blue;
                i++;
                it++;
            }
        }

        RndText::DrawShowing();

        if (sDebugHighlight && !sInDebugHighlight) {
            sInDebugHighlight = true;
            Highlight();
            sInDebugHighlight = false;
        }
    }
}

void UILabel::SetTextToken(Symbol s) {
    mTextToken = s;
    if (TheLoadMgr.EditMode()) {
        if (!mLabelText.empty())
            return;
        if (mIconChar != '\0')
            return;
    }
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
    mIconChar = c;
    if (c == '\0' && TheLoadMgr.EditMode()) {
        SetEditText(mLabelText.c_str());
        return;
    }
    SetDisplayText(&mIconChar, !TheLoadMgr.EditMode());
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

void UILabel::SetTimeHMS(int seconds, bool showHours) {
    int hours = seconds / 3600;
    if (hours >= 99) {
        hours = 99;
    }
    int minutes = seconds / 60 - hours * 60;
    if (minutes >= 99) {
        minutes = 99;
    }
    int secs = seconds - (hours * 60 + minutes) * 60;
    if (secs >= 99) {
        secs = 99;
    }
    if (hours > 0 || showHours) {
        SetDisplayText(MakeString("%02d:%02d:%02d", hours, minutes, secs), true);
    } else {
        SetDisplayText(MakeString("%d:%02d", minutes, secs), true);
    }
}

bool UILabel::CheckValid(bool warn) {
    if (mFixedLength != 0 && UTF8StrLen(mText.c_str()) > (unsigned int)mFixedLength) {
        if (warn) {
            int len = UTF8StrLen(mText.c_str());
            MILO_WARN(
                "%s: %s has fixed length of %i but text is %i long (%s)",
                PathName(Dir()),
                Name(),
                mFixedLength,
                len,
                mText
            );
        }
        return false;
    }
    return true;
}

void UILabel::SetEditText(const char *c) {
    if (!TheLoadMgr.EditMode()) {
        bool allowed = AllowEditText();
        if (!allowed) {
            MILO_FAIL(
                "Called SetEditText, not in milo and type %s does not allow edit text",
                Type()
            );
        }
    }
    mLabelText = c;
    if (mIconChar == '\0') {
        if (mLabelText.empty()) {
            SetTextToken(mTextToken);
        } else {
            char buf[0x100];
            ASCIItoUTF8(buf, 0x100, c);
            SetDisplayText(buf, !TheLoadMgr.EditMode());
        }
    }
}

char const *UILabel::GetDefaultText() const {
    if (mIconChar != 0) {
        return &mIconChar;
    }

    if (TheLoadMgr.EditMode() && !mLabelText.empty())
        return mLabelText.c_str();
    else
        return Localize(mTextToken, nullptr, TheLocale);
}

void UILabel::CenterWithLabel(UILabel *label, bool b, float f) {
    MILO_ASSERT(
        (mAlignment & RndText::kCenter) || (label->mAlignment & RndText::kCenter),
        0x400
    );
    int num = b ? -1 : 1;
    Transform thisXfm = LocalXfm();
    Transform otherXfm = label->LocalXfm();
    float halfF = f * 0.5f;
    thisXfm.v.x = -((mBoundsRight * 0.5f + halfF) * (float)num - thisXfm.v.x);
    otherXfm.v.x = (label->mBoundsRight * 0.5f + halfF) * (float)num + otherXfm.v.x;
    SetLocalXfm(thisXfm);
    label->SetLocalXfm(otherXfm);
}

UILabel::LabelStyle &UILabel::LStyle(int i) {
    if ((unsigned int)i < mLabelStyles.size()) {
        return mLabelStyles[i];
    }
    static LabelStyle s(0);
    return s;
}

const UILabel::LabelStyle &UILabel::LStyle(int i) const {
    if ((unsigned int)i < mLabelStyles.size()) {
        return mLabelStyles[i];
    }
    static LabelStyle s(0);
    return s;
}

void UILabel::OldResourcePreload(BinStream &bs) {
    char buffer[0x100];
    LabelStyle &style = LStyle(0);
    ResourceDirPtr<UILabelDir> &ptr = *(ResourceDirPtr<UILabelDir> *)&style.mLabelDir;
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
) {
    mTextToken = s;
    if (mTextToken.Null()) {
        SetDisplayText(gNullStr, true);
    } else {
        bool found;
        const char *localized = Localize(mTextToken, &found, TheLocale);
        if (found) {
            SuperFormatString str(localized, da1, b, TheLocale, gNullStr);
            if (da2) {
                int size = da2->Size();
                if (i < size) {
                    do {
                        const DataNode &n = da2->Evaluate(i);
                        if (n.Type() == kDataSymbol) {
                            str << Localize(n.Sym(da2), 0, TheLocale);
                        } else {
                            str << n;
                        }
                        i++;
                    } while (i < size);
                }
            }
            SetDisplayText(str.FinalStr(), false);
        } else {
            SetDisplayText(localized, false);
        }
    }
}

DataNode UILabel::OnSetPrelocalizedString(DataArray const *arr) {
    const DataNode &stringNode = arr->Node(2).Evaluate();
    MILO_ASSERT(stringNode.Type() == kDataString, 0x386);
    String str(stringNode.Str(0));
    SetPrelocalizedString(str);
    return 1;
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
    int val;
    if (da->Node(2).Type() == kDataFloat) {
        val = (int)da->Float(2);
    } else {
        val = da->Int(2);
    }
    bool b = false;
    if (da->Size() > 3)
        b = da->Int(3) != 0;
    SetInt(val, b);
    return DataNode(1);
}

DataNode UILabel::OnSetTimeHMS(DataArray const *arr) {
    int val;
    if (arr->Node(2).Type() == kDataFloat) {
        val = (int)arr->Float(2);
    } else {
        val = arr->Int(2);
    }
    SetTimeHMS(val, true);
    return 1;
}

bool UILabel::AllowEditText() const {
    if (TheUI->DefaultAllowEditText()) {
        return true;
    }
    if (LStyle(0).mLabelDir == 0) {
        FormatString fs("LabelDir is not yet loaded, can't tell if edit text is allowed");
        TheDebug.Notify(fs.Str());
        return false;
    }
    return LStyle(0).mLabelDir->AllowEditText();
}

void UILabel::LabelUpdate(bool b) {
    mDirty = false;

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
    if (mLabelStyles.size() > 1) {
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
        } while (i < mLabelStyles.size());
    }

    UpdateText();
    CheckValid(!TheLoadMgr.EditMode());
}

DataNode UILabel::OnSetHeightFromText(DataArray *da) {
    if (mFitType == 0 && Style(0).mFont) {
        float height;
        mHeight = ComputeHeight(mCurScrollChars, 1.0f, height);
    } else {
        FormatString fs("Could not set height, either no default font set, or fit type is not kFitWrap");
        TheDebug.Notify(fs.Str());
    }
    return DataNode(0);
}

void UILabel::SetFontMat(char const *c, int i) {
    RndFontBase *font = 0;
    LabelStyle &ls = LStyle(i);
    UILabelDir *dir = ls.mLabelDir;
    if (dir) {
        font = dir->FontObj(Symbol(c));
        if (!font) {
            if (*c) {
                TheDebug.Notify(MakeString(
                    "%s is referencing a mat variation '%s' that no longer exists, trying default...",
                    PathName(this), c
                ));
                font = dir->FontObj(Symbol(""));
            }
            if (!font) {
                TheDebug.Notify(MakeString(
                    "%s in resource %s has no default mat variation",
                    PathName(this), PathName(dir)
                ));
            }
        }
    } else if (*c) {
        TheDebug.Notify(MakeString(
            "%s [styles 0 font_resource] is NULL, can't set fontmat %s",
            PathName(this), c
        ));
    }
    if ((unsigned int)i < mStyles.size()) {
        mStyles[i].mFont = font;
    }
}

char const *UILabel::GetFontMat(int i) {
    RndFontBase *font = NULL;
    if ((unsigned int)i < mStyles.size()) {
        font = mStyles[i].mFont;
    }
    LabelStyle &ls = LStyle(i);
    UILabelDir *dir = ls.mLabelDir;
    if (dir) {
        return dir->GetMatVariationName(font);
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
    HANDLE(set_token_fmt, OnSetTokenFmt)
    HANDLE(set_prelocalized_string, OnSetPrelocalizedString)
    HANDLE(set_int, OnSetInt)
    HANDLE_ACTION(set_float, SetFloat(_msg->Str(2), _msg->Float(3)))
    HANDLE(set_time_hms, OnSetTimeHMS)
    HANDLE_ACTION(center_with_label, CenterWithLabel(_msg->Obj<UILabel>(2), _msg->Int(3), _msg->Float(4)))
    HANDLE_EXPR(get_font_mats, UILabelDir::GetMatVariations(LStyle(_msg->Int(2)).mLabelDir))
    HANDLE(set_height_from_text, OnSetHeightFromText)
    HANDLE_EXPR(draw_rect_width, mBoundsRight)
    HANDLE_ACTION(reload_string, (SetTextToken(mTextToken), mDirty = true))
    HANDLE_SUPERCLASS(UIComponent)
END_HANDLERS

float GetTextSizeFromPctHeight(float f) {
    if (TheLoadMgr.EditMode()) {
        float depth = -TheUI->GetCam()->LocalXfm().v.y;
        Vector2 v2a(0.0f, 0.0f);
        Vector3 v3a;
        TheUI->GetCam()->ScreenToWorld(v2a, depth, v3a);
        Vector2 v2b(0.0f, f);
        Vector3 v3b;
        TheUI->GetCam()->ScreenToWorld(v2b, depth, v3b);
        return std::fabs(v3a.z - v3b.z);
    } else
        return f;
}

float GetPctHeightFromTextSize(float f) {
    if (TheLoadMgr.EditMode()) {
        Vector3 v3a(0.0f, 0.0f, 0.0f);
        Vector2 v2a;
        TheUI->GetCam()->WorldToScreen(v3a, v2a);
        Vector3 v3b(0.0f, 0.0f, -f);
        Vector2 v2b;
        TheUI->GetCam()->WorldToScreen(v3b, v2b);
        return std::fabs(v2a.y - v2b.y);
    } else
        return f;
}

bool PropSync(UILabel::LabelStyle &style, DataNode &_val, DataArray *_prop, int _i, PropOp _op) {
    if (_i == _prop->Size())
        return true;

    Symbol sym = _prop->Sym(_i);
    int styleIdx = &style - &sLabel->LStyle(0);

    SYNC_PROP_MODIFY(font_resource, style.mLabelDir, sLabel->RefreshFontMat(styleIdx))
    SYNC_PROP(color_override, style.mColorOverride)

    {
        _NEW_STATIC_SYMBOL(font_mat_variation)
        if (sym == _s) {
            if (_op == kPropSet) {
                sLabel->SetFontMat(_val.Str(0), styleIdx);
                if (!UILabel::sDeferUpdate) {
                    sLabel->LabelUpdate(false);
                }
            } else {
                if (_op == (PropOp)0x40)
                    return false;
                _val = DataNode(sLabel->GetFontMat(styleIdx));
            }
            return true;
        }
    }

    RndText::Style &textStyle = sLabel->Style(styleIdx);

    {
        _NEW_STATIC_SYMBOL(text_size)
        if (sym == _s) {
            if (_op == kPropSet) {
                textStyle.mSize = GetTextSizeFromPctHeight(_val.Float(0));
                if (!UILabel::sDeferUpdate) {
                    sLabel->LabelUpdate(false);
                }
            } else {
                if (_op == (PropOp)0x40)
                    return false;
                _val = DataNode(GetPctHeightFromTextSize(textStyle.mSize));
            }
            return true;
        }
    }

    SYNC_PROP_SET(font_alpha, textStyle.mFontColor.alpha, textStyle.mFontColor.alpha = _val.Float(0))

    SYNC_PROP_MODIFY(italics, textStyle.mItalics, if (!UILabel::sDeferUpdate) sLabel->LabelUpdate(false))
    SYNC_PROP_MODIFY(kerning, textStyle.mKerning, if (!UILabel::sDeferUpdate) sLabel->LabelUpdate(false))
    SYNC_PROP_MODIFY(z_offset, textStyle.mZOffset, if (!UILabel::sDeferUpdate) sLabel->LabelUpdate(false))
    SYNC_PROP_MODIFY(blacklight, textStyle.mBlacklight, if (!UILabel::sDeferUpdate) sLabel->LabelUpdate(false))

    return false;
}

