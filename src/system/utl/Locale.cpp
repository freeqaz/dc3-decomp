#include "utl\Locale.h"
#include "utl\LocaleChunkSort.h"

#include "DataPointMgr.h"
#include "obj\DataFile.h"
#include "obj\DataFunc.h"
#include "os\Debug.h"
#include "os\File.h"
#include "os\System.h"
#include "utl\Str.h"
#include "utl\DataPointMgr.h"
#include "xdk\xbdm\xbdm.h"
#include <vector>

// Lane history (11 lines moved): docs/decomp/notes/system/utl/Locale.md
Locale TheLocale;

// ~Locale() is inline in Locale.h (compiler inlines it into atexit destructor)
bool gShowTokensCheat = false;
bool Locale::sVerboseNotify;
const char *Locale::sIgnoreMissingText;

DataNode DataSetLocaleVerboseNotify(DataArray *arr) {
    Locale::SetLocaleVerboseNotify(arr->Int(1));
    return DataNode(0);
}

DataNode DataToggleShowTokensCheat(DataArray *arr) {
    gShowTokensCheat = !gShowTokensCheat;
    return DataNode(0);
}

// NOT the comparator the image uses -- kept only as the readable statement of
// what the sort is meant to mean.  LocaleChunkSort::Sort passes FastSort<3>;
// see the comment on Sort below.  This function is static and unreferenced, so
// it is discarded, exactly as in the shipped image (no ?LocaleChunkSortFunc@@
// symbol anywhere in orig/373307D9/ham_xbox_r.map).
static int LocaleChunkSortFunc(const void *a, const void *b) {
    const LocaleChunkSort::OrderedLocaleChunk *chunkA =
        (const LocaleChunkSort::OrderedLocaleChunk *)a;
    const LocaleChunkSort::OrderedLocaleChunk *chunkB =
        (const LocaleChunkSort::OrderedLocaleChunk *)b;
    Symbol symA = chunkA->node1.LiteralSym(0);
    Symbol symB = chunkB->node1.LiteralSym(0);
    if (symA < symB)
        return -1;
    if (symA > symB)
        return 1;
    return chunkA->node2.Int(0) - chunkB->node2.Int(0);
}

// BEHAVIOURAL: the comparator is FastSort<3>, not LocaleChunkSortFunc.  Sort is
// inlined into Locale::Init, and the image's qsort call there names it:
//   build/373307D9/asm/system/utl/Locale.s @827E9D2C
//     lis  r11, "??$FastSort@$02@LocaleChunkSort@@YAHPBX0@Z"@ha
//     addi r6,  r11, "??$FastSort@$02@LocaleChunkSort@@YAHPBX0@Z"@l
//     bl   qsort
// ?LocaleChunkSortFunc@@ appears in no map; ??$FastSort@$02@... is at
// 827e9460 in utl:Locale.obj (ham_xbox_r.map:109216).  The two orderings are
// not the same: FastSort<3> compares three consecutive 8-byte-strided int
// words, LocaleChunkSortFunc compares Symbol pointers then node2.
#ifdef HX_NATIVE
// LP64: FastSort<3> walks three 4-byte words at an 8-byte stride, which on
// the Xbox (8-byte DataNode) are node1/node2/node3's values. Natively
// DataNode is 16 bytes, so the same walk reads the signed LOW 32 bits of the
// symbol pointer, then node1.mType, then node2's low word -- an order that
// disagrees with the native FindDataIndex, which compares full pointers
// unsigned, whenever the interned strings straddle a low-32 value of
// 0x80000000 or a 4 GB line. The binary search then misses tokens. Sort by
// the key the search uses: the full symbol pointer, then node2 (the running
// chunk index, unique by construction, so node3 -- the image's third key --
// is never consulted). Same semantics as the image's signed-int32 sort and
// signed-int32 search, which agree with each other.
static int NativeLocaleChunkCompare(const void *a, const void *b) {
    const LocaleChunkSort::OrderedLocaleChunk *ca =
        (const LocaleChunkSort::OrderedLocaleChunk *)a;
    const LocaleChunkSort::OrderedLocaleChunk *cb =
        (const LocaleChunkSort::OrderedLocaleChunk *)b;
    uintptr_t sa = (uintptr_t)ca->node1.LiteralSym().Str();
    uintptr_t sb = (uintptr_t)cb->node1.LiteralSym().Str();
    if (sa != sb)
        return sa < sb ? -1 : 1;
    int ia = ca->node2.UncheckedInt();
    int ib = cb->node2.UncheckedInt();
    if (ia != ib)
        return ia < ib ? -1 : 1;
    return 0;
}
#endif

void LocaleChunkSort::Sort(OrderedLocaleChunk *chunks, int count) {
#ifdef HX_NATIVE
    qsort(chunks, count, sizeof(OrderedLocaleChunk), NativeLocaleChunkCompare);
#else
    qsort(chunks, count, sizeof(OrderedLocaleChunk), FastSort<3>);
#endif
}

namespace LocaleChunkSort {
// Lane history (6 lines moved): docs/decomp/notes/system/utl/Locale.md
// > Compares N keys, each the leading 4-byte word of an 8-byte (Xbox DataNode) element:
// > node1/node2/node3's values for an OrderedLocaleChunk.
template <int N>
int FastSort(const void *a, const void *b) {
    const int *keysA = (const int *)a;
    const int *keysB = (const int *)b;
    for (int i = 0; i < N; i++) {
        int keyA = keysA[i * 2];
        int keyB = keysB[i * 2];
        if (keyA < keyB)
            return -1;
        if (keyA > keyB)
            return 1;
    }
    return 0;
}

template int FastSort<3>(const void *, const void *);
}

const char *Locale::Localize(Symbol token, bool success) const {
    if (token.Null()) {
        return "";
    }
    if (!mSymTable) {
        MILO_ASSERT(mSymTable, 0x1D8);
    }

    static Symbol sEng("eng");

    if (mMagnuStrings != nullptr) {
        if (SystemLanguage() == sEng) {
            DataArray *langArray = mMagnuStrings->FindArray(token, false);
            if (langArray) {
                return langArray->Str(1);
            }
        }
    }

    int idx;
    if (FindDataIndex(token, idx, success)) {
        return mStrTable[idx];
    }

    if (UsingCD()) {
        SendDebugDataPoint("debug/locale/token", "token", token, "success", false);
    }

    return nullptr;
}

bool Locale::FindDataIndex(Symbol s, int &idx, bool fail) const {
    int low = 0;
    int high = mSize - 1;
    while (high - low >= 0) {
        int mid = (low + high) >> 1;
#ifdef HX_NATIVE
        // On LP64, (int)Symbol truncates 8-byte pointer to 4 bytes.
        // Compare interned string pointers directly (sorted by address).
        if (s.Str() > mSymTable[mid].Str()) {
            low = mid + 1;
        } else if (s.Str() < mSymTable[mid].Str()) {
#else
        if ((int)s > (int)mSymTable[mid]) {
            low = mid + 1;
        } else if ((int)s < (int)mSymTable[mid]) {
#endif
            high = mid - 1;
        } else {
            idx = mid;
            return true;
        }
    }
    if (fail) {
        MILO_FAIL("Couldn't find '%s' in array (file %s)", s.Str(), mFile.Str());
    }
    return false;
}

void Locale::Terminate() {
    delete[] mSymTable;
    mSymTable = 0;
    delete[] mStrTable;
    mStrTable = 0;
    delete[] mUploadedFlags;
    mUploadedFlags = 0;
    RELEASE(mStringData);
    mSize = 0;
    mFile = Symbol();
    mNumFilesLoaded = 0;
}

void Locale::SetMagnuStrings(DataArray *da) {
    if (mMagnuStrings) {
        mMagnuStrings->Release();
        mMagnuStrings = 0;
    }
    mMagnuStrings = da;
}

// Lane history (40 lines moved): docs/decomp/notes/system/utl/Locale.md
// > Residual after the FastSort fix: 85 rows, all regalloc/slot permutation, no
// > structural difference.
void Locale::Init() {
    MILO_ASSERT(!mStrTable, 0x58);
    MILO_ASSERT(!mSymTable, 0x59);
    MILO_ASSERT(!mSize, 0x5A);
    MILO_ASSERT(!mStringData, 0x5B);
    MILO_ASSERT(!mNumFilesLoaded, 0x5C);
    mSize = 0;
    int totalStrLen = 0;  // Total length of all unique localized strings
    int totalChunks = 0;   // Sum of all loaded arrays' Size()
    int numChunks = 0;     // Number of locale entries loaded from files
    DataArray *cfg = 0;
    LocaleChunkSort::OrderedLocaleChunk *chunks = 0;
    Symbol prevSym;        // Tracks previous symbol to deduplicate

    // Check for alternate devkit locale file
    const char *keepFile = MakeString("%s\\locale_keep.dta", SystemLanguage());
    String devkitPath(FileMakePath("devkit:\\locale", keepFile));
    FileQualifiedFilename(devkitPath, devkitPath.c_str());

    static Symbol locale("locale");
    DataArrayPtr altCfg(DataNode(locale), devkitPath);

    // The image branches the no-config case to the ALLOCATION block, not past it:
    // `cmplwi r3, 0x0` / `beq .L_827E9DE8` at 827E99C0 lands on `mSymTable = new
    // Symbol[mSize]`, so the original wraps the locale load in an `if` and always
    // allocates the (then zero-length) tables.  It also tests SystemConfig()'s
    // return value directly rather than through `cfg`: `cfg` is zeroed up front
    // (`mr r16, r14` at 827E98E0) and only written by the SystemConfig("locale")
    // call inside the branch.
    if (SystemConfig()) {
        cfg = SystemConfig("locale");

        if (DmMapDevkitDrive() >= 0) {
            if (FileExists(devkitPath.c_str(), 0, 0)) {
                MILO_NOTIFY("Using alternate locale file from HDD: %s", devkitPath);
                cfg = (DataArray *)altCfg;
            }
        }

        {
            MemDoTempAllocations tmp;
            std::vector<DataArray *> arrVec(cfg->Size() - 1);
            mNumFilesLoaded = arrVec.size();

            // mInitialized is STATICALLY TRUE: the shipped image holds 0x01 at
            // TheLocale+0x1c in .data. This is not the uninitialized read it was long
            // documented as -- the gate is always taken, and it has to be, because
            // everything below it is the locale load. RB3 has no such check.
            if (mInitialized) {
                for (int i = 1; i < cfg->Size(); i++) {
                    const char *path = FileMakePath(FileGetPath(cfg->File()), cfg->Str(i));
                    arrVec[i - 1] = DataReadFile(path, true);
                    if (!arrVec[i - 1]) {
                        MILO_FAIL("could not load language file %s", path);
                    }
                    totalChunks += arrVec[i - 1]->Size();
                }

                chunks = new LocaleChunkSort::OrderedLocaleChunk[totalChunks];

                int n = 0;
                for (int j = cfg->Size() - 2; j >= 0; j--) {
                    DataArray *curArr = arrVec[j];
                    // Lane history (36 lines moved): docs/decomp/notes/system/utl/Locale.md
                    DataArray *chunkArr = curArr;
                    for (int k = curArr->Size() - 1; k >= 0; k--, n++) {
                        chunkArr = curArr->Node(k).LiteralArray(chunkArr);
                        if (chunkArr->Size() < 2) {
                            MILO_FAIL(
                                "%s line %d should have 2 entries, has %d, mismatched quotes?",
                                chunkArr->File(),
                                chunkArr->Line(),
                                chunkArr->Size()
                            );
                        }
                        chunks[n].node1 = chunkArr->LiteralSym(0);
                        chunks[n].node2 = n;
                        chunks[n].node3 = chunkArr->LiteralStr(1);
                    }
                    curArr->Release();
                }

                numChunks = n;

                if (cfg->Size() > 1) {
                    LocaleChunkSort::Sort(chunks, numChunks);
                }

                mSize = 0;
                for (int i = 0; i < numChunks; i++) {
                    Symbol curSym = chunks[i].node1.LiteralSym();
                    if (curSym != prevSym) {
                        totalStrLen += strlen(chunks[i].node3.LiteralStr()) + 1;
                        prevSym = curSym;
                        mSize++;
                    }
                }
            }
        }
    }

    mSymTable = new Symbol[mSize];
    mStringData = new StringTable(totalStrLen);
    mStrTable = new const char *[mSize];
    mUploadedFlags = new bool[mSize];

    if (chunks) {
        prevSym = Symbol();
        int chunkIdx = 0;
        for (int i = 0; i < numChunks; i++) {
            Symbol curSym = chunks[i].node1.LiteralSym();
            if (curSym != prevSym) {
                // w25-ha: the named pointer gives the image's stbx base/index order.
                bool *flags = mUploadedFlags;
                flags[chunkIdx] = 0;
                mSymTable[chunkIdx] = curSym;
                mStrTable[chunkIdx] = mStringData->Add(chunks[i].node3.LiteralStr());
                prevSym = curSym;
                chunkIdx++;
            } else
                // w25-ha: prevSym, as in the image (stw r27 = prevSym; equal to curSym
                // on this path, and it lifts `this` over the kDataSymbol constant).
                TheDebug << MakeString("Locale symbol '%s' redefined\n", prevSym.Str());
        }
        delete[] chunks;
    }

    if (cfg && cfg->Size() > 1) {
        mFile = cfg->Str(1);
    }

    DataRegisterFunc("set_locale_verbose_notify", DataSetLocaleVerboseNotify);
    DataRegisterFunc("toggle_show_tokens_cheat", DataToggleShowTokensCheat);
}

static char gLocalizeSepBuf[4][0x32];
static int gLocalizeSepIdx = 0;
static char gLocalizeFloatBuf[4][0x32];
static int gLocalizeFloatIdx = 0;

const char *LocalizeFloat(const char *fmt, float num) {
    const char *str = MakeString(fmt, num);

    static Symbol sDecimalSep("locale_decimal_separator");

    const char *decimalStr = TheLocale.Localize(sDecimalSep, false);

    // If the localized decimal separator is not '.', we need to replace it
    if (decimalStr != 0 && *decimalStr != '.') {
        char *buf = gLocalizeFloatBuf[gLocalizeFloatIdx];
        strncpy(buf, str, 0x32);
        buf[0x31] = '\0';

        // Find and replace '.' with localized separator
        char *p = buf;
        for (;;) {
            if (*p == 0) break;
            if (*p == '.') {
                *p = *decimalStr;
                break;
            }
            p++;
        }

        // Increment index with modulo 4 wrapping
        gLocalizeFloatIdx = (gLocalizeFloatIdx + 1) % 4;

        return buf;
    }

    return str;
}

const char *LocalizeSeparatedInt(int num, Locale &locale) {
    static Symbol sSep("locale_separator");
    bool success = false;
    char digitBuf[2];
    const char *sep = Localize(sSep, &success, locale);
    if (!success) {
        sep = ",";
    }
    if (strcmp(sep, gNullStr) == 0) {
        return (char *)MakeString("%i", num);
    }
    int sepLen = strlen(sep);
    char *buf = gLocalizeSepBuf[gLocalizeSepIdx];
    buf[0x31] = '\0';
    int pos = 0x31;
    int absNum = num;
    bool negative = num < 0;
    if (negative) {
        absNum = num < 0 ? -num : num;
    }
    int digitCount = 0;
    while (true) {
        if (digitCount != 0 && absNum <= 0)
            break;
        if (digitCount % 3 == 0 && digitCount > 0) {
            for (int j = sepLen - 1; j >= 0; j--) {
                pos--;
                buf[pos] = sep[j];
            }
        }
        Hx_snprintf(digitBuf, 2, "%d", absNum % 10);
        pos--;
        buf[pos] = digitBuf[0];
        digitCount++;
        absNum = absNum / 10;
    }
    if (negative) {
        pos--;
        buf[pos] = '-';
    }
    char *result = &buf[pos];
    gLocalizeSepIdx = (gLocalizeSepIdx + 1) % 4;
    return result;
}

void SyncReloadLocale() {
    static Symbol sLocale("locale");
    DataArray *cfg = SystemConfig(sLocale);
    for (int i = 1; i < cfg->Size(); i++) {
        const char *str = cfg->Str(i);
        char *path =
            (char *)FileMakePath(FileGetPath(cfg->File()), str);
        if (SystemExec(MakeString("p4 sync %s", path)) == 0) {
            TheDebug << MakeString("updated %s\n", path);
        } else {
            TheDebug << MakeString("failed to update %s\n", path);
        }
    }
    TheLocale.Terminate();
    TheLocale.Init();
}

const char *Localize(Symbol token, bool *success, Locale &locale) {
    if (gShowTokensCheat) {
        if (success)
            *success = true;
        return token.Str();
    }
    const char *textStr = locale.Localize(token, false);
    bool localized = textStr != 0;
    if (!localized) {
        textStr = token.Str();
        Locale::sIgnoreMissingText = textStr;
        if (Locale::GetLocaleVerboseNotify()) {
            MILO_NOTIFY("\"%s\" needs localization", token);
        }
    }
    if (success) {
        *success = localized;
    }
    return textStr;
}
