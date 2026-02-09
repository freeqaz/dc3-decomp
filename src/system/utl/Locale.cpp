#include "utl/Locale.h"
#include "obj/DataFile.h"
#include "obj/DataFunc.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "utl/Str.h"
#include "utl/DataPointMgr.h"
#include "xdk/xbdm/xbdm.h"
#include <vector>

bool gShowTokensCheat = false;

DataNode DataSetLocaleVerboseNotify(DataArray *arr) {
    Locale::SetLocaleVerboseNotify(arr->Int(1));
    return DataNode(0);
}

DataNode DataToggleShowTokensCheat(DataArray *arr) {
    gShowTokensCheat = !gShowTokensCheat;
    return DataNode(0);
}

static int LocaleChunkSortFunc(const void *a, const void *b) {
    const LocaleChunkSort::OrderedLocaleChunk *chunkA = (const LocaleChunkSort::OrderedLocaleChunk *)a;
    const LocaleChunkSort::OrderedLocaleChunk *chunkB = (const LocaleChunkSort::OrderedLocaleChunk *)b;
    Symbol symA = chunkA->node1.LiteralSym(0);
    Symbol symB = chunkB->node1.LiteralSym(0);
    if (symA < symB) return -1;
    if (symA > symB) return 1;
    return chunkA->node2.Int(0) - chunkB->node2.Int(0);
}

void LocaleChunkSort::Sort(OrderedLocaleChunk *chunks, int count) {
    qsort(chunks, count, sizeof(OrderedLocaleChunk), LocaleChunkSortFunc);
}

const char *Locale::Localize(Symbol token, bool success) const {
    if (token == gNullStr) {
        return "";
    }
    if (!mSymTable) {
        MILO_ASSERT(mSymTable, 0x1D8);
    }

    // Static eng symbol
    static Symbol sEng("eng");

    // Check mMagnuStrings for language-specific strings
    if (mMagnuStrings != nullptr) {
        if (SystemLanguage() == sEng) {
            DataArray *langArray = mMagnuStrings->FindArray(token, false);
            if (langArray) {
                return langArray->Str(1);
            }
        }
    }

    // Search main symbol table
    for (int i = 0; i < mSize; i++) {
        if (mSymTable[i] == token) {
            return mStrTable[i];
        }
    }

    if (UsingCD()) {
        SendDebugDataPoint("debug/locale/token", "token", token, "success", false);
    }

    return nullptr;
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

void Locale::Init() {
    MILO_ASSERT(!mStrTable, 0x58);
    MILO_ASSERT(!mSymTable, 0x59);
    MILO_ASSERT(!mSize, 0x5A);
    MILO_ASSERT(!mStringData, 0x5B);
    MILO_ASSERT(!mNumFilesLoaded, 0x5C);

    mSize = 0;
    int totalStrLen = 0;  // Total length of all unique localized strings
    int numChunks = 0;     // Number of locale entries loaded from files
    LocaleChunkSort::OrderedLocaleChunk *chunks = 0;
    Symbol prevSym;        // Tracks previous symbol to deduplicate

    // Check for alternate devkit locale file
    String devkitPath(FileMakePath("devkit:\\locale", MakeString("%s\\locale_keep.dta", SystemLanguage())));
    FileQualifiedFilename(devkitPath, devkitPath.c_str());

    static Symbol locale("locale");
    DataArrayPtr altCfg(DataNode(devkitPath), DataNode(locale));

    DataArray *cfg = SystemConfig();
    if (!cfg) {
        goto done;
    }

    cfg = SystemConfig("locale");

    if (DmMapDevkitDrive() >= 0) {
        if (FileExists(devkitPath.c_str(), 0, 0)) {
            MILO_NOTIFY("Using alternate locale file from%s", devkitPath);
            cfg = (DataArray *)altCfg;
        }
    }

    MemPushTemp();
    {
        std::vector<DataArray *> arrVec(cfg->Size() - 1);
        mNumFilesLoaded = arrVec.size();

        int totalChunks = 0;
        // NOTE: mInitialized is uninitialized here (UB). RB3 doesn't have this check.
        // This appears to be dead code or a bug, but matches the original binary.
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

            numChunks = 0;
            for (int j = cfg->Size() - 2; j >= 0; j--) {
                DataArray *curArr = arrVec[j];
                for (int k = curArr->Size() - 1; k >= 0; k--, numChunks++) {
                    DataArray *chunkArr = curArr->Node(k).LiteralArray(curArr);
                    int size = chunkArr->Size();
                    if (size < 2) {
                        MILO_FAIL(
                            "%s line %d should have 2 entries, has %d, mismatched quotes?",
                            chunkArr->File(),
                            chunkArr->Line(),
                            size
                        );
                    }
                    chunks[numChunks].node1 = chunkArr->LiteralSym(0);
                    chunks[numChunks].node2 = numChunks;
                    chunks[numChunks].node3 = chunkArr->LiteralStr(1);
                }
                curArr->Release();
            }
        }

        if (cfg->Size() > 1) {
            LocaleChunkSort::Sort(chunks, numChunks);
        }

        mSize = 0;
        for (int i = 0; i < numChunks; i++) {
            Symbol curSym = chunks[i].node1.LiteralSym();
            if (curSym != prevSym) {
                totalStrLen += strlen(chunks[i].node3.LiteralStr());
                prevSym = curSym;
                mSize++;
            }
        }
    }

    mSymTable = new Symbol[mSize];
    mStringData = new StringTable(totalStrLen);
    mStrTable = new const char *[mSize];
    mUploadedFlags = new bool[mSize];

    prevSym = Symbol();

    if (numChunks > 0) {
        int chunkIdx = 0;
        for (int i = 0; i < numChunks; i++) {
            Symbol curSym = chunks[i].node1.LiteralSym();
            if (curSym != prevSym) {
                mUploadedFlags[chunkIdx] = 0;
                mSymTable[chunkIdx] = curSym;
                mStrTable[chunkIdx] = mStringData->Add(chunks[i].node3.LiteralStr());
                prevSym = curSym;
                chunkIdx++;
            } else
                MILO_WARN("Locale symbol '%s' redefined\n", curSym);
        }
    }

    delete[] chunks;
    MemPopTemp();

done:
    if (cfg && cfg->Size() > 1) {
        mFile = cfg->Str(1);
    }

    DataRegisterFunc("set_locale_verbose_notify", DataSetLocaleVerboseNotify);
    DataRegisterFunc("toggle_show_tokens_cheat", DataToggleShowTokensCheat);
}

// Static buffers and index
struct LocaleFloatBuffer {
    char buffers[4][0x32];
};
static LocaleFloatBuffer gLocalizeFloatBuf;
static int gLocalizeFloatIdx = 0;
static bool gLocalizeFloatInitialized = false;
static Symbol gLocalizeFloatSeparator;

const char *LocalizeFloat(const char *fmt, float num) {
    const char *str;
    const char *decimalStr;
    char *buf;
    char *p;

    str = MakeString(fmt, num);

    // Lazy initialization of the decimal separator symbol
    if (!gLocalizeFloatInitialized) {
        gLocalizeFloatInitialized = true;
        gLocalizeFloatSeparator = Symbol("locale_decimal_separator");
    }

    decimalStr = TheLocale.Localize(gLocalizeFloatSeparator, false);

    // If the localized decimal separator is not '.', we need to replace it
    if (decimalStr != 0 && *decimalStr != '.') {
        buf = gLocalizeFloatBuf.buffers[gLocalizeFloatIdx];
        strncpy(buf, str, 0x32);
        buf[0x31] = '\0';

        // Find and replace '.' with localized separator
        p = buf;
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

