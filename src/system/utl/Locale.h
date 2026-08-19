#pragma once
#include "utl\Symbol.h"
#include "utl/StringTable.h"
#include "obj\Data.h"

enum LocaleGender {
    LocaleGenderMasculine = 0,
    LocaleGenderFeminine = 1,
};

enum LocaleNumber {
    LocaleSingular = 0,
    LocalePlural = 1,
};


class Locale {
private:
    int mSize; // 0x0
    Symbol *mSymTable; // 0x4
    const char **mStrTable; // 0x8
    StringTable *mStringData; // 0xc
    bool *mUploadedFlags; // 0x10
    Symbol mFile; // 0x14
    int mNumFilesLoaded; // 0x18
    bool mInitialized; // 0x1c - checked in Init
    DataArray *mMagnuStrings; // 0x20
public:
#ifdef HX_NATIVE
    // Native builds need explicit init since globals aren't BSS-zeroed
    Locale() : mSize(0), mSymTable(0), mStrTable(0), mStringData(0),
        mUploadedFlags(0), mNumFilesLoaded(0), mInitialized(true), mMagnuStrings(0) {}
#else
    // PPC: the shipped image places TheLocale in .data with 0x01 at offset 0x1c,
    // so mInitialized is STATICALLY TRUE -- it is not the uninitialized-read it
    // was long documented as. MSVC folds this constant member-init into the data
    // section and leaves only the non-constant mFile store in ??__ETheLocale, so
    // the dynamic initializer's instruction stream is unchanged. Everything else
    // is zero-initialized by the data image, as before.
    Locale() : mInitialized(true) {}
#endif
    ~Locale() {
        if (mMagnuStrings) {
            mMagnuStrings->Release();
            mMagnuStrings = 0;
        }
    }

    void Init();
    void Terminate();

    static const char *sIgnoreMissingText;

    void SetMagnuStrings(DataArray *);
    const char *Localize(Symbol, bool) const;

    static void SetLocaleVerboseNotify(bool set) { Locale::sVerboseNotify = set; }
    static bool GetLocaleVerboseNotify() { return sVerboseNotify; }


protected:
    // ?sVerboseNotify@Locale@@1_NA -- `1` is protected, ours emitted `2`.
    static bool sVerboseNotify;

    bool FindDataIndex(Symbol, int &, bool) const;
};

extern Locale TheLocale;
extern bool gShowTokensCheat;

const char *Localize(Symbol token, bool *success, Locale &locale);
const char *LocalizeSeparatedInt(int num, Locale &locale);
const char *LocalizeFloat(const char *fmt, float num);
void SyncReloadLocale();
