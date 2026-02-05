#include "ui/UIFontImporter.h"
#include "ui/UILabelDir.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/System.h"
#include "rndobj/Font.h"
#include "rndobj/FontBase.h"
#include "rndobj/Mat.h"
#include "rndobj/Text.h"
#include "stl/_vector.h"
#include "ui/ResourceDirPtr.h"
#include "utl/Std.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "utl/UTF8.h"

#define HEIGHT_SD 480.0f
#define HEIGHT_HD 720.0f

float ConvertHeightOGToPctHeight(int i) { return std::fabs(i / HEIGHT_SD); }
float ConvertHeightNGToPctHeight(int i) { return std::fabs(i / HEIGHT_HD); }
int ConvertPctHeightToHeightNG(float f) { return -Round(f * HEIGHT_HD); }
int ConvertPctHeightToHeightOG(float f) { return -Round(f * HEIGHT_SD); }

UIFontImporter::UIFontImporter()
    : mUpperCaseAthroughZ(1), mLowerCaseAthroughZ(1), mNumbers0through9(1),
      mPunctuation(1), mUpperEuro(1), mLowerEuro(1), mRussian(0), mPolish(0),
      mIncludeLocale(0), mIncludeFile(""), mFontName("Arial"),
      mFontPctSize(ConvertHeightNGToPctHeight(-12)), mFontWeight(400), mItalics(false),
      mDropShadow(0), mDropShadowOpacity(128), mFontQuality(0), mPitchAndFamily(34),
      mFontCharset(0), mFontSupersample(0), mLeft(0), mRight(0), mTop(0), mBottom(0),
      mFillWithSafeWhite(false), mFontToImportFrom(this), mBitmapSavePath("ui/image/"),
      mBitMapSaveName("temp.bmp"), mGennedFonts(this), mReferenceKerning(this),
      mMatVariations(this), mHandmadeFont(this), mCheckNG(false), mLastGenWasNG(true) {
    static Symbol objects("objects");
    static Symbol default_bitmap_path("default_bitmap_path");
    DataArray *cfgArr =
        SystemConfig(objects, StaticClassName())->FindArray(default_bitmap_path, false);
    if (cfgArr) {
        mBitmapSavePath = cfgArr->Str(1);
    }
    GenerateBitmapFilename();
}

BEGIN_PROPSYNCS(UIFontImporter)
    SYNC_PROP(UPPER_CASE_A_Z, mUpperCaseAthroughZ)
    SYNC_PROP(lower_case_a_z, mLowerCaseAthroughZ)
    SYNC_PROP(numbers_0_9, mNumbers0through9)
    SYNC_PROP(punctuation, mPunctuation)
    SYNC_PROP(UPPER_EURO, mUpperEuro)
    SYNC_PROP(lower_euro, mLowerEuro)
    SYNC_PROP(russian, mRussian)
    SYNC_PROP(polish, mPolish)
    SYNC_PROP(include_locale, mIncludeLocale)
    SYNC_PROP(include_file, mIncludeFile)
    SYNC_PROP_SET(plus, GetASCIIPlusChars(), ASCIItoWideVector(mPlus, _val.Str()))
    SYNC_PROP_SET(minus, GetASCIIMinusChars(), ASCIItoWideVector(mMinus, _val.Str()))
    SYNC_PROP(font_name, mFontName)
    SYNC_PROP_MODIFY(font_pct_size, mFontPctSize, GenerateBitmapFilename())
    SYNC_PROP_SET(
        font_point_size,
        mLastGenWasNG ? ConvertPctHeightToHeightNG(mFontPctSize)
                      : ConvertPctHeightToHeightOG(mFontPctSize),
        mFontPctSize = mLastGenWasNG ? ConvertHeightNGToPctHeight(-_val.Int())
                                     : ConvertHeightOGToPctHeight(-_val.Int())
    )
    SYNC_PROP_SET(
        font_pixel_size,
        std::abs(
            mLastGenWasNG ? ConvertPctHeightToHeightNG(mFontPctSize)
                          : ConvertPctHeightToHeightOG(mFontPctSize)
        ),
        mFontPctSize = mLastGenWasNG ? ConvertHeightNGToPctHeight(-_val.Int())
                                     : ConvertHeightOGToPctHeight(-_val.Int())
    )
    SYNC_PROP_MODIFY(weight, mFontWeight, GenerateBitmapFilename())
    SYNC_PROP_SET(
        bold, std::abs(mFontWeight), mFontWeight = _val.Int() != 0 ? 800 : 400;
        GenerateBitmapFilename()
    )
    SYNC_PROP_MODIFY(italics, mItalics, GenerateBitmapFilename())
    SYNC_PROP_MODIFY(drop_shadow, mDropShadow, GenerateBitmapFilename())
    SYNC_PROP(drop_shadow_opacity, mDropShadowOpacity)
    SYNC_PROP(font_quality, (int &)mFontQuality)
    SYNC_PROP(pitch_and_family, mPitchAndFamily)
    SYNC_PROP(font_charset, mFontCharset)
    SYNC_PROP_MODIFY(font_supersample, (int &)mFontSupersample, GenerateBitmapFilename())
    SYNC_PROP(left, mLeft)
    SYNC_PROP(right, mRight)
    SYNC_PROP(top, mTop)
    SYNC_PROP(bottom, mBottom)
    SYNC_PROP(fill_with_safe_white, mFillWithSafeWhite)
    SYNC_PROP(font_to_import_from, mFontToImportFrom)
    SYNC_PROP(bitmap_save_path, mBitmapSavePath)
    SYNC_PROP(bitmap_save_name, mBitMapSaveName)
    SYNC_PROP(gened_fonts, mGennedFonts)
    SYNC_PROP(reference_kerning, mReferenceKerning)
    SYNC_PROP_MODIFY(mat_variations, mMatVariations, SyncWithGennedFonts())
    SYNC_PROP_MODIFY(handmade_font, mHandmadeFont, HandmadeFontChanged())
    SYNC_PROP(resource_name, mSyncResource)
    SYNC_PROP(last_genned_ng, mLastGenWasNG)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(UIFontImporter)
    bs.WriteEndian(&mUpperCaseAthroughZ, 4);
    bs << mUpperCaseAthroughZ;
    bs << mLowerCaseAthroughZ;
    bs << mNumbers0through9;
    bs << mPunctuation;
    bs << mUpperEuro;
    bs << mLowerEuro;
    bs << mRussian;
    bs << mPolish;
    bs << mIncludeLocale;
    bs << mIncludeFile;
    String plusStr;
    WideVectorToUTF8(mPlus, plusStr);
    bs << plusStr;
    String minusStr;
    WideVectorToUTF8(mMinus, minusStr);
    bs << minusStr;
    bs << mFontName;
    bs << mFontPctSize;
    bs << mFontWeight;
    bs << mItalics;
    bs << mDropShadow;
    bs << mDropShadowOpacity;
    bs << mFontQuality;
    bs << mPitchAndFamily;
    bs << mFontCharset;
    bs << mFontSupersample;
    bs << mBitmapSavePath;
    bs << mBitMapSaveName;
    bs << mLeft;
    bs << mRight;
    bs << mTop;
    bs << mBottom;
    bs << mFillWithSafeWhite;
    bs << mGennedFonts;
    bs << mReferenceKerning;
    bs << mMatVariations;
    bs << mHandmadeFont;
    bs << mSyncResource;
    bs << mLastGenWasNG;
END_SAVES

BEGIN_COPYS(UIFontImporter)
    CREATE_COPY(UIFontImporter)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mLowerCaseAthroughZ)
        COPY_MEMBER(mUpperCaseAthroughZ)
        COPY_MEMBER(mNumbers0through9)
        COPY_MEMBER(mPunctuation)
        COPY_MEMBER(mUpperEuro)
        COPY_MEMBER(mLowerEuro)
        COPY_MEMBER(mRussian)
        COPY_MEMBER(mPolish)
        COPY_MEMBER(mIncludeLocale)
        COPY_MEMBER(mIncludeFile)
        COPY_MEMBER(mPlus)
        COPY_MEMBER(mMinus)
        COPY_MEMBER(mFontName)
        COPY_MEMBER(mFontPctSize)
        COPY_MEMBER(mFontWeight)
        COPY_MEMBER(mItalics)
        COPY_MEMBER(mDropShadow)
        COPY_MEMBER(mDropShadowOpacity)
        COPY_MEMBER(mFontQuality)
        COPY_MEMBER(mPitchAndFamily)
        COPY_MEMBER(mFontQuality)
        COPY_MEMBER(mFontCharset)
        COPY_MEMBER(mBitmapSavePath)
        COPY_MEMBER(mBitMapSaveName)
        COPY_MEMBER(mFontSupersample)
        COPY_MEMBER(mLeft)
        COPY_MEMBER(mRight)
        COPY_MEMBER(mTop)
        COPY_MEMBER(mBottom)
        COPY_MEMBER(mFillWithSafeWhite)
        COPY_MEMBER(mGennedFonts)
        COPY_MEMBER(mReferenceKerning)
        COPY_MEMBER(mMatVariations)
        COPY_MEMBER(mHandmadeFont)
        COPY_MEMBER(mSyncResource)
        COPY_MEMBER(mLastGenWasNG)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(UIFontImporter)
    u32 vers;
    bs.ReadEndian(&vers, 4);
    u16 minVers = vers & 0xFFFF;
    u16 altVers = vers >> 16;

    BinStreamRev rev(bs, vers);
    rev >> mUpperCaseAthroughZ;
    rev >> mLowerCaseAthroughZ;
    rev >> mNumbers0through9;
    rev >> mPunctuation;
    rev >> mUpperEuro;
    rev >> mLowerEuro;

    if (altVers > 0) {
        rev >> mRussian;
        rev >> mPolish;
        rev >> mIncludeLocale;
        rev >> mIncludeFile;
    }

    String str1, str2;
    bs >> str1;
    bs >> str2;

    if (altVers < 4) {
        ASCIItoWideVector(mPlus, str1.c_str());
    } else {
        UTF8toWideVector(mPlus, str1.c_str());
    }

    bs >> mFontName;

    if (minVers <= 4) {
        s32 val;
        bs.ReadEndian(&val, 4);
        mFontPctSize = fabs((f32)(s64)(-val) * 0.0013888889f);
    } else {
        bs.ReadEndian(&mFontPctSize, 4);
    }

    bs.ReadEndian(&mFontWeight, 4);
    rev >> mDropShadow;

    if (altVers > 1) {
        bs.ReadEndian(&mFontWeight, 4);
    }

    if (altVers > 2) {
        bs.ReadEndian(&mFontSupersample, 4);
    }

    bs.ReadEndian(&mLeft, 4);
    bs.ReadEndian(&mRight, 4);
    bs.ReadEndian(&mTop, 4);
    bs.ReadEndian(&mBottom, 4);
    bs.ReadEndian(&mFontQuality, 4);

    if (minVers > 1) {
        bs.ReadEndian(&mFontWeight, 4);
    }

    bs >> mFontName;
    bs >> mIncludeFile;
    bs.ReadEndian(&mFontPctSize, 4);
    bs.ReadEndian(&mFontWeight, 4);
    bs.ReadEndian(&mFontWeight, 4);
    bs.ReadEndian(&mFontWeight, 4);
    rev >> mDropShadow;

    if (minVers < 8) {
        mFontToImportFrom.Load(bs, 1, 0);
    }

    if (minVers > 2) {
        mGennedFonts.Load(bs, 1, 0, 1);
        mReferenceKerning.Load(bs, 1, 0);
    }

    if (minVers == 3) {
        // Load temporary mat at version 3
        bs.ReadEndian(&mFontWeight, 4);
    }

    if (minVers > 3) {
        mMatVariations.Load(bs, 1, 0, 1);
    }

    if ((minVers > 5) && (minVers < 10)) {
        // Load temporary mat at version 5-9
        bs.ReadEndian(&mFontWeight, 4);
    }

    if (minVers > 6) {
        mHandmadeFont.Load(bs, 1, 0);
    }

    if (minVers > 7) {
        bs >> mBitMapSaveName;
    }

    if (minVers > 8) {
        rev >> mCheckNG;
    }

    if (altVers == 1) {
        bs.ReadEndian(&mSyncResource, 4);
    }
END_LOADS

void UIFontImporter::ImportSettingsFromFont(RndFontBase *font) {
    if (font && font->Type() == Symbol("imported_font")) {
        SetProperty("font_name", DataNode(font->Property("font_name", true)->Str()));
        SetProperty(
            "font_size",
            DataNode(
                ConvertHeightNGToPctHeight(-font->Property("font_size", true)->Int())
            )
        );
        SetProperty("weight", DataNode(font->Property("weight", true)->Int()));
        SetProperty("italics", DataNode(font->Property("italics", true)->Int()));
        SetProperty("drop_shadow", DataNode(font->Property("drop_shadow", true)->Int()));
        SetProperty(
            "drop_shadow_opacity",
            DataNode(font->Property("drop_shadow_opacity", true)->Int())
        );
        SetProperty("left", DataNode(font->Property("left", true)->Int()));
        SetProperty("right", DataNode(font->Property("right", true)->Int()));
        SetProperty("top", DataNode(font->Property("top", true)->Int()));
        SetProperty("bottom", DataNode(font->Property("bottom", true)->Int()));
    } else {
        MILO_NOTIFY(
            "Can't import settings from Font because it doesnt have import_font type"
        );
    }
}

Symbol UIFontImporter::GetMatVariationName(unsigned int ui) const { return Symbol(0); }

char const *UIFontImporter::GetMatVariationName(RndFontBase *font) const {
    if (!font)
        return "";

    if (!font->Type())
        return "";

    int count = mMatVariations.size();
    Symbol type = font->Type();

    if (count > 0) {
        RndMat *first = mMatVariations.front();
        if (first && first->Type() == type) {
            return "";
        }
    } else {
        return "";
    }

    FOREACH (it, mMatVariations) {
        RndMat *mat = *it;
        if (mat && mat->Type() == type) {
            return FileGetBase(type.Str());
        }
    }

    MILO_NOTIFY("%s not found in resource dir %s", PathName(this), PathName(font));
    return "";
}

int UIFontImporter::GetMatVariationIdx(Symbol s) const {
    int size = NumMatVariations();
    for (int ret = 0; ret < size; ret++) {
        if (GetMatVariationName(ret) == s)
            return ret;
    }
    return -1;
}

RndFontBase *UIFontImporter::GetGennedFont(Symbol s) const {
    if (s.Null()) {
        void *p = reinterpret_cast<void *>(
            reinterpret_cast<char *>(const_cast<UIFontImporter *>(this)) + 0x9c
        );
        return *reinterpret_cast<RndFontBase * const *>(p);
    }
    int idx = GetMatVariationIdx(s);
    if (idx == -1) {
        return nullptr;
    }
    unsigned int numVar = *reinterpret_cast<unsigned int const *>(
        reinterpret_cast<char *>(const_cast<UIFontImporter *>(this)) + 0xc0
    );
    if ((unsigned int)idx >= numVar) {
        return FindFontForMat(nullptr);
    }
    void *pPtr = reinterpret_cast<void *>(
        reinterpret_cast<char *>(const_cast<UIFontImporter *>(this)) + 0xc4
    );
    void *ptr = *reinterpret_cast<void * const *>(pPtr);
    for (; idx; idx--) {
        ptr = *reinterpret_cast<void * const *>(
            reinterpret_cast<char *>(ptr) + 0x14
        );
    }
    RndMat *mat = *reinterpret_cast<RndMat * const *>(
        reinterpret_cast<char *>(ptr) + 0xc
    );
    return FindFontForMat(mat);
}

void UIFontImporter::AttachImporterToFont(RndFontBase *font) {
    if (font) {
        if (font->Dir() != Dir())
            MILO_NOTIFY(
                "Cannot attach font %s to font resource %s because its in a different dir.  Notify a programmer!"
            );
        else {
            mGennedFonts.clear();
            mMatVariations.clear();
            mGennedFonts.push_back(font);
            mReferenceKerning = font;
            ImportSettingsFromFont(font);
        }
    }
}

void UIFontImporter::GenerateBitmapFilename() {
    const char *mult = "";
    if (mFontSupersample == kFontSuperSample_2x)
        mult = "2x";
    else if (mFontSupersample == kFontSuperSample_4x)
        mult = "4x";

    class String s28(MakeString("%.2f", mFontPctSize * 100.0f));
    s28.ReplaceAll('.', '_');
    const char *s = mDropShadow ? "S" : "";
    const char *b = (mFontWeight > 500) ? "B" : "";
    const char *i = mItalics ? "I" : "";
    mBitMapSaveName =
        MakeString("%s(%s)%s%s%s%s.bmp", mFontName.c_str(), s28.c_str(), i, b, s, mult);
    mBitMapSaveName.ReplaceAll(' ', '_');
}

String UIFontImporter::GetASCIIPlusChars() { return String(); }

String UIFontImporter::GetASCIIMinusChars() {
    static String minusChars;
    static int initialized = 0;
    if (!initialized) {
        minusChars = WideVectorToASCII(mMinus);
        initialized = 1;
    }
    return minusChars;
}

void UIFontImporter::SyncWithGennedFonts() {
    int idx = 0;
    for (ObjPtrList<RndFontBase>::iterator it = mGennedFonts.begin(); it != mGennedFonts.end();) {
        RndFontBase *font = *it;
        bool matfound = false;
        if (idx == 0) {
            matfound = true;
        } else {
            for (ObjPtrList<RndMat>::iterator mit = mMatVariations.begin();
                 mit != mMatVariations.end();
                 ++mit) {
                if (font->Mat() == *mit) {
                    matfound = true;
                }
            }
        }
        if (!matfound) {
            RndText *text = FindTextForFont(font);
            it = mGennedFonts.erase(it);
            delete font;
            if (text)
                delete text;
        } else {
            it++;
        }
        idx++;
    }
}

void UIFontImporter::HandmadeFontChanged() {
    if (mHandmadeFont) {
        if (mGennedFonts.size() > 0) {
            RndFontBase *frontfont = *mGennedFonts.begin();
            if (frontfont != mHandmadeFont) {
                RndText *text = FindTextForFont(frontfont);
                delete frontfont;
                if (text) {
                    delete text;
                }
            }
            mGennedFonts.Set(mGennedFonts.begin(), mHandmadeFont);
            for (ObjPtrList<RndFontBase>::iterator it = ++mGennedFonts.begin();
                 it != mGennedFonts.end();
                 it++) {
                if (*it == mHandmadeFont) {
                    mGennedFonts.erase(it);
                    break;
                }
            }
        } else {
            mGennedFonts.push_back(mHandmadeFont);
        }
        mReferenceKerning = mHandmadeFont;
        mLowerEuro = false;
        mUpperEuro = false;
        mPunctuation = false;
        mNumbers0through9 = false;
        mLowerCaseAthroughZ = false;
        mUpperCaseAthroughZ = false;
        mMinus.clear();
        mPlus.clear();
    }
}

RndFontBase *UIFontImporter::FindFontForMat(RndMat *mat) const {
    if (mat) {
        static Symbol Font("Font");
        static Symbol Font3d("Font3d");
        FOREACH (it, mat->Refs()) {
            Hmx::Object *owner = (*it).RefOwner();
            if (owner) {
                if (owner->ClassName() == Font) {
                    return dynamic_cast<RndFont *>(owner);
                }
                if (owner->ClassName() == Font3d)
                    return dynamic_cast<RndFont3d *>(owner);
            }
        }
    }
    return nullptr;
}

DataNode UIFontImporter::OnShowFontPicker(DataArray *) {
    return DataNode(0);
}

DataNode UIFontImporter::OnGenerate(DataArray *) {
    return DataNode(0);
}

DataNode UIFontImporter::OnGenerateOG(DataArray *) {
    return DataNode(0);
}

DataNode UIFontImporter::OnGenerate3D(DataArray *) {
    return DataNode(0);
}

DataNode UIFontImporter::OnGetGennedBitmapPath(DataArray *da) {
    RndFont *font;
    const char *path = "";
    if (!mGennedFonts.empty()) {
        font = dynamic_cast<RndFont *>(mGennedFonts.front());
        if (font) {
            RndMat *mat = font->Mat(0);
            if (mat) {
                if (mat->GetDiffuseTex()) {
                    if (mat->GetDiffuseTex()) {
                        path = mat->GetDiffuseTex()->File().c_str();
                    }
                }
            }
        }
    }
    return DataNode(path);
}

DataNode UIFontImporter::OnImportSettings(DataArray *da) {
    ImportSettingsFromFont(mFontToImportFrom);
    return DataNode(0);
}

DataNode UIFontImporter::OnForgetGened(DataArray *) {
    mGennedFonts.clear();
    return 0;
}

DataNode UIFontImporter::OnAttachToImportFont(DataArray *) {
    AttachImporterToFont(mFontToImportFrom);
    return 0;
}

void UIFontImporter::OnSetCharsetUTF8(String const &s) {
    mLowerEuro = false;
    mUpperEuro = false;
    mPunctuation = false;
    mNumbers0through9 = false;
    mLowerCaseAthroughZ = false;
    mUpperCaseAthroughZ = false;
    mIncludeLocale = false;
    mPolish = false;
    mRussian = false;
    mIncludeFile = "";
    mMinus.clear();
    mPlus.clear();
    UTF8toWideVector(mPlus, s.c_str());
}

DataNode UIFontImporter::OnSyncWithResourceFile(DataArray *) {
    if (!mSyncResource.empty()) {
        FilePath path;
        static Symbol uilabeldirSym("UILabelDir");
        if (ResourceDirBase::MakeResourcePath(
                path, ClassName(), uilabeldirSym, mSyncResource.c_str()
            )) {
            ObjDirPtr<UILabelDir> labelDir;
            labelDir.LoadFile(path, false, true, kLoadFront, false);
            labelDir.PostLoad(0);
            if (labelDir.IsLoaded()) {
                mLowerCaseAthroughZ = labelDir->mLowerCaseAthroughZ;
                mUpperCaseAthroughZ = labelDir->mUpperCaseAthroughZ;
                mNumbers0through9 = labelDir->mNumbers0through9;
                mPunctuation = labelDir->mPunctuation;
                mUpperEuro = labelDir->mUpperEuro;
                mLowerEuro = labelDir->mLowerEuro;
                mPlus = labelDir->mPlus;
                mMinus = labelDir->mMinus;
                mFontName = labelDir->mFontName;
                mFontPctSize = labelDir->mFontPctSize;
                mFontWeight = labelDir->mFontWeight;
                mFontQuality = labelDir->mFontQuality;
                mPitchAndFamily = labelDir->mPitchAndFamily;
                mFontQuality = labelDir->mFontQuality;
                mFontCharset = labelDir->mFontCharset;
                mBitmapSavePath = labelDir->mBitmapSavePath;
                mBitMapSaveName = labelDir->mBitMapSaveName;
                mFontSupersample = labelDir->mFontSupersample;
                mItalics = labelDir->mItalics;
                mLeft = labelDir->mLeft;
                mRight = labelDir->mRight;
                mTop = labelDir->mTop;
                mBottom = labelDir->mBottom;
                mFillWithSafeWhite = labelDir->mFillWithSafeWhite;
                if (mReferenceKerning) {
                    if (labelDir->mReferenceKerning) {
                        std::vector<RndFontBase::KernInfo> kerninfo;
                        labelDir->mReferenceKerning->GetKerning(kerninfo);
                        mReferenceKerning->SetKerning(kerninfo);
                        // Cast to access protected mBaseKerning member
                        RndFontBase *pKern = labelDir->mReferenceKerning;
                        float baseKerning = *(float *)((char *)pKern + 0x3c);
                        mReferenceKerning->SetBaseKerning(baseKerning);
                    }
                }
            }
        }
    }
    return 0;
}

RndText *UIFontImporter::FindTextForFont(RndFontBase *font) const {
    if (font) {
        static Symbol Text("Text");
        FOREACH (it, font->Refs()) {
            Hmx::Object *owner = (*it).RefOwner();
            if (owner) {
                if (owner->ClassName() == Text) {
                    RndText *text = dynamic_cast<RndText *>(owner);
                    if (text) {
                        return text;
                    }
                }
            }
        }
    }
    return nullptr;
}

BEGIN_HANDLERS(UIFontImporter)
    HANDLE(show_font_picker, OnShowFontPicker)
    HANDLE(generate, OnGenerate)
    HANDLE(generate_og, OnGenerateOG)
    HANDLE(generate_og, OnGenerate3D)
    HANDLE(generate_3d, OnGenerate3D)
    HANDLE(forget_gened_fonts, OnForgetGened)
    HANDLE(attach_to_importfont, OnAttachToImportFont)
    HANDLE(import_from_importfont, OnImportSettings)
    HANDLE(sync_with_resource, OnSyncWithResourceFile)
    HANDLE_EXPR(get_resources_file_list, ResourceDirBase::GetFileList(Symbol("UILabel"), Symbol("UILabelDir")))
    HANDLE(get_bitmap_path, OnGetGennedBitmapPath)
    HANDLE_ACTION(set_charset_utf8, OnSetCharsetUTF8(_msg->Str(2)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
