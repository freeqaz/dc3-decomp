#include "os\File.h"
#include "os\AsyncFile.h"
#include "os\BlockMgr_p.h"
#include "os/FileCache.h"
#include "os\ArkFile_p.h"
#include "HolmesClient.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\Dir.h"
#include "obj\Msg.h"
#include "os\Debug.h"
#include "os\OSFuncs.h"
#include "os\System.h"
#include "types.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl\MemMgr.h"
#include "utl\Option.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <list>
#ifdef HX_NATIVE
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#undef st_ctime
#undef st_atime
#undef st_mtime
#endif

// MSVC emits this TU's uninitialized globals into .bss in *reverse* declaration
// order, so the block below is the reverse of the target's data layout, which
// symbols.txt gives verbatim from gSystemRoot at 0x82F658B8:
//
//   gSystemRoot +0x000  gExecRoot +0x100  gRoot +0x200
//   gOpenCaptureFile +0x300  gCaptureFileMode +0x304
//   gFakeFileErrors +0x308  gNullFiles +0x309  kNoHandle +0x30c
//   gFrameRateArray +0x310
//
// The four public globals therefore have to be declared *before* the five
// statics, not after: FileInit materialises the address of gSystemRoot once
// and reaches gExecRoot, gRoot and gOpenCaptureFile from it as +0x100, +0x200
// and +0x300, which only works if gSystemRoot is the first object in .bss.
DataArray *gFrameRateArray;
void *kNoHandle;
bool gNullFiles;
bool gFakeFileErrors;

static int gCaptureFileMode;
static File *gOpenCaptureFile;
static char gRoot[256];
static char gExecRoot[256];
static char gSystemRoot[256];

std::vector<File *> gFiles(0x80); // 0x10...?
std::vector<String> gDirList;
const int File::MaxFileNameLen = 0x100;

const char *FileRoot() { return gRoot; }
const char *FileExecRoot() { return gExecRoot; }
const char *FileSystemRoot() { return gSystemRoot; }

#ifdef HX_NATIVE
extern const char *NativeGetDataDir();

static bool NativeDirExists(const char *path) {
    struct stat st;
    return path && *path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void NativeSetCanonicalPath(char *dst, size_t dstSize, const char *path) {
    char resolved[PATH_MAX];
    if (path && *path && realpath(path, resolved)) {
        strncpy(dst, resolved, dstSize - 1);
    } else if (path) {
        strncpy(dst, path, dstSize - 1);
    } else {
        *dst = '\0';
        return;
    }
    dst[dstSize - 1] = '\0';
}

static void NativeInitSystemRoot() {
    char extractedSystemRun[PATH_MAX];
    const char *dataDir = NativeGetDataDir();
    if (dataDir && *dataDir) {
        snprintf(
            extractedSystemRun,
            sizeof(extractedSystemRun),
            "%s/extracted/(..)/(..)/system/run",
            dataDir
        );
        if (NativeDirExists(extractedSystemRun)) {
            NativeSetCanonicalPath(gSystemRoot, sizeof(gSystemRoot), extractedSystemRun);
            return;
        }
    }

    NativeSetCanonicalPath(gSystemRoot, sizeof(gSystemRoot), "../../system/run");
}
#endif

void FileTerminate() {
    RELEASE(gOpenCaptureFile);
    *gRoot = 0;
    *gExecRoot = 0;
    *gSystemRoot = 0;
    TheDebug.StopLog();
    HolmesClientTerminate();
}

void FileQualifiedFilename(String &out, const char *in) {
    char buf[256];
    FileQualifiedFilename(buf, 0x100, in);
    out = buf;
}

void FileNormalizePath(const char *cc) {
    for (char *ptr = (char *)cc; *ptr != '\0'; ptr++) {
        if (*ptr == '\\')
            *ptr = '/';
        else
            *ptr = tolower(*ptr);
    }
}

const char *FileGetDriveBuf(const char *iFilepath, char *oBuf) {
    MILO_ASSERT(iFilepath, 0x437);
    MILO_ASSERT(oBuf, 0x438);
    const char *p = strchr(iFilepath, ':');
    if (p != 0) {
        strncpy(oBuf, iFilepath, p - iFilepath);
        oBuf[p - iFilepath] = '\0';
    } else {
        oBuf[0] = '\0';
    }
    return oBuf;
}

const char *FileGetDrive(const char *file) {
    static char drive[256];
    MainThread();
    return FileGetDriveBuf(file, drive);
}

const char *FileGetPathBuf(const char *iBuf, char *oBuf) {
    MILO_ASSERT(oBuf, 0x3F6);
    if (iBuf != 0) {
        strcpy(oBuf, iBuf);
        char *p2 = oBuf + strlen(oBuf) - 1;
        while (p2 >= oBuf && *p2 != '/' && *p2 != '\\') {
            p2--;
        }
        if (p2 >= oBuf) {
            if ((p2 == oBuf) || (p2[-1] == ':'))
                p2[1] = '\0';
            else
                *p2 = '\0';
            return oBuf;
        }
    }
    oBuf[0] = '.';
    oBuf[1] = '\0';
    return oBuf;
}

const char *FileGetPath(const char *file) {
    static char static_path[256];
    MainThread();
    return FileGetPathBuf(file, static_path);
}

const char *FileGetBaseBuf(const char *iFilepath, char *oBuf) {
    MILO_ASSERT(iFilepath, 0x458);
    MILO_ASSERT(oBuf, 0x459);
    const char *dir = strrchr(iFilepath, '/');
    if ((dir == 0) && (dir = strrchr(iFilepath, '\\'), dir == 0))
        strcpy(oBuf, iFilepath);
    else
        strcpy(oBuf, dir + 1);
    char *ext = strrchr(oBuf, '.');
    if (ext != 0)
        *ext = 0;
    return oBuf;
}

const char *FileGetBase(const char *file) {
    static char my_path[256];
    MainThread();
    return FileGetBaseBuf(file, my_path);
}

const char *FileGetExt(const char *root) {
    const char *end = root + strlen(root);
    for (const char *search = end - 1; search >= root; search--) {
        if (*search == '.') {
            return search + 1;
        } else if (*search == '/' || *search == '\\') {
            return end;
        }
    }
    return end;
}

const char *FileGetName(const char *file) {
    const char *dir;
    dir = strrchr(file, '/');
    if (dir == 0) {
        dir = strrchr(file, '\\');
        if (dir == 0) {
            return file;
        }
    }
    return dir + 1;
}

static bool FileMatchInternal(const char *arg0, const char *arg1, bool arg2) {
    for (; *arg0 != 0; arg0++) {
        if (FileMatch(arg0, arg1))
            return true;
        if (!arg2 && (*arg0 == '/' || *arg0 == '\\'))
            return false;
    }
    return (*arg1 == *arg0);
}

bool FileMatch(const char *param1, const char *param2) {
    if (param2 == 0)
        return false;
    while (*param2 != '\0') {
        if (*param2 == '*')
            return FileMatchInternal(param1, param2 + 1, 0);
        if (*param2 == '&')
            return FileMatchInternal(param1, param2 + 1, 1);
        if (*param1 == '\0')
            break;
        if (*param2 == '?') {
            if ((*param1 == '\\') || (*param1 == '/'))
                return 0;
        } else if ((*param2 == '/') || (*param2 == '\\')) {
            if ((*param1 != '/') && (*param1 != '\\'))
                return 0;
        } else if (*param2 != *param1)
            return 0;
        param2++;
        param1++;
    }
    return (*param2 - *param1) == 0;
}

const char *FrameRateSuffix() {
    return MakeString("_keep_%s.dta", PlatformSymbol(TheLoadMgr.GetPlatform()));
}

// the weird __rs in the debug symbols here, is for a FileStat&
// so BinStream >> FileStat
BinStream &operator>>(BinStream &bs, FileStat &fs) {
    bs >> fs.st_mode >> fs.st_size;
    u64 ctime;
    bs >> ctime;
    fs.st_ctime = ctime;
    u64 atime;
    bs >> atime;
    fs.st_atime = atime;
    u64 mtime;
    bs >> mtime;
    fs.st_mtime = mtime;
    return bs;
}

DataNode OnFileExecRoot(DataArray *da) { return gExecRoot; }
DataNode OnFileRoot(DataArray *da) { return gRoot; }
DataNode OnFileGetExt(DataArray *da) { return FileGetExt(da->Str(1)); }
DataNode OnFileMatch(DataArray *da) { return FileMatch(da->Str(1), da->Str(2)); }

DataNode OnWithFileRoot(DataArray *da) {
    FilePathTracker fpt(da->Str(1));
    int thresh = da->Size() - 1;
    int i;
    for (i = 2; i < thresh; i++) {
        da->Command(i)->Execute(true);
    }
    return da->Evaluate(i);
}

DataNode OnSynchProc(DataArray *) {
    MILO_FAIL("calling synchproc on non-pc platform");
    return "";
}

void OnFrameRateRecurseCB(const char *cc1, const char *cc2) {
    MILO_ASSERT(gFrameRateArray, 0x120);
    String str(cc2);
    str = str.substr(0, str.length() - strlen(FrameRateSuffix()));
    gFrameRateArray->Insert(gFrameRateArray->Size(), str);
}

void DirListCB(const char *, const char *c) { gDirList.push_back(c); }

bool FileExists(const char *iFilename, int iMode, String *str) {
    MILO_ASSERT((iMode & ~FILE_OPEN_NOARK) == 0, 0x2A8);
    File *theFile = NewFile(iFilename, iMode | 0x40002);
    if (theFile) {
        if (str) {
            *str = theFile->Filename();
        }
        delete theFile;
        return true;
    } else
        return false;
}

String UniqueFilename(const char *c1, const char *c2) {
    String ret;
    int i = 0;
    File *file = nullptr;
    do {
        i++;
        ret = MakeString("%s_%06d.%s", c1, i, c2);
        delete file;
        file = NewFile(ret.c_str(), 1);
    } while (file);
    return ret;
}

DataNode OnFileGetDrive(DataArray *da) {
    static char drive[256];
    const char *str = da->Str(1);
    MainThread();
    return FileGetDriveBuf(str, drive);
}
DataNode OnFileGetPath(DataArray *da) {
    static char static_path[256];
    const char *str = da->Str(1);
    MainThread();
    return FileGetPathBuf(str, static_path);
}
DataNode OnFileGetBase(DataArray *da) {
    static char my_path[256];
    const char *str = da->Str(1);
    MainThread();
    return FileGetBaseBuf(str, my_path);
}
DataNode OnFileAbsolutePath(DataArray *da) {
    return FileMakePath(da->Str(1), da->Str(2));
}
DataNode OnFileRelativePath(DataArray *da) {
    return FileRelativePath(da->Str(1), da->Str(2));
}
DataNode OnToggleFakeFileErrors(DataArray *a) {
    gFakeFileErrors = !gFakeFileErrors;
    Hmx::Object *cheatDisplay = ObjectDir::Main()->Find<Hmx::Object>("cheat_display");
    if (cheatDisplay) {
        static Message msg("show_bool", "Fake File errors", 0);
        msg[1] = gFakeFileErrors;
        cheatDisplay->Handle(msg, false);
    }
    return 0;
}

DataNode OnEnumerateFrameRateResults(DataArray *da) {
    DataNode ret(new DataArray(0), kDataArray);
    gFrameRateArray = ret.Array();
    char *suffix = (char *)FrameRateSuffix();
    const char *pattern = MakeString("ui/framerate/venue_test/*%s", suffix);
    RecursePatternInternal(pattern, OnFrameRateRecurseCB, false, false);
    gFrameRateArray = 0;
    return ret;
}

void FileInit() {
    strcpy(gRoot, ".");
    strcpy(gExecRoot, ".");
#ifdef HX_NATIVE
    NativeInitSystemRoot();
#else
    strcpy(gSystemRoot, FileMakePath(gExecRoot, "../../system/run"));
#endif
    FilePath::Root().Set(gRoot, gRoot);
    DataRegisterFunc("file_root", OnFileRoot);
    DataRegisterFunc("file_exec_root", OnFileExecRoot);
    DataRegisterFunc("file_get_drive", OnFileGetDrive);
    DataRegisterFunc("file_get_path", OnFileGetPath);
    DataRegisterFunc("file_get_base", OnFileGetBase);
    DataRegisterFunc("file_get_ext", OnFileGetExt);
    DataRegisterFunc("file_match", OnFileMatch);
    DataRegisterFunc("file_absolute_path", OnFileAbsolutePath);
    DataRegisterFunc("file_relative_path", OnFileRelativePath);
    DataRegisterFunc("with_file_root", OnWithFileRoot);
    DataRegisterFunc("synch_proc", OnSynchProc);
    DataRegisterFunc("toggle_fake_file_errors", OnToggleFakeFileErrors);
    DataRegisterFunc("enumerate_frame_rate_results", OnEnumerateFrameRateResults);
    HolmesClientInit();
    const char *str = OptionStr("file_order", nullptr);
    if (str && *str) {
        gOpenCaptureFile = NewFile(str, 0x301);
        MILO_ASSERT(gOpenCaptureFile, 0x18F);
    }
    TheDebug.AddExitCallback(FileTerminate);
}

const char *FileRelativePathBuf(const char *iRoot, const char *iFilepath, char *oBuf) {
    MILO_ASSERT(iRoot, 0x38d);
    MILO_ASSERT(iFilepath, 0x38e);
    MILO_ASSERT(oBuf, 0x38f);
    if (*iFilepath != '\0') {
        char rootBuf[256];
        char fpBuf[256];
        strcpy(rootBuf, iRoot);
        strcpy(fpBuf, iFilepath);

        std::list<char *> rootToks;
        std::list<char *> fpToks;

        char *rootTok = strtok(rootBuf, "/");
        if (rootTok != nullptr) {
            do {
                rootToks.push_back(rootTok);
                rootTok = strtok(nullptr, "/");
            } while (rootTok != nullptr);
        }

        char *fpTok = strtok(fpBuf, "/");
        if (fpTok != nullptr) {
            do {
                fpToks.push_back(fpTok);
                fpTok = strtok(nullptr, "/");
            } while (fpTok != nullptr);
        }

        if (!fpToks.empty() && !rootToks.empty()) {
            if (strcmp(fpToks.front(), rootToks.front()) == 0) {
                while (rootToks.size() > 0 && fpToks.size() > 0
                       && strcmp(fpToks.front(), rootToks.front()) == 0) {
                    rootToks.pop_front();
                    fpToks.pop_front();
                }

                char *p = oBuf;
                while (rootToks.size() > 0) {
                    if (p != oBuf)
                        *p++ = '/';
                    *p++ = '.';
                    *p++ = '.';
                    rootToks.pop_front();
                }
                while (fpToks.size() > 0) {
                    if (p != oBuf)
                        *p++ = '/';
                    for (const char *pp = fpToks.front(); *pp != '\0'; pp++)
                        *p++ = *pp;
                    fpToks.pop_front();
                }
                MILO_ASSERT(p - oBuf < File::MaxFileNameLen, 0x3d9);
                if (p == oBuf)
                    *p++ = '.';
                *p = '\0';
                return oBuf;
            }
        }
    }
    return iFilepath;
}

const char *FileRelativePath(const char *root, const char *filepath) {
    MainThread();
    static char relative[256];
    return FileRelativePathBuf(root, filepath, relative);
}

const char *FileMakePathBuf(const char *iRoot, const char *iFilepath, char *oBuf) {
    MILO_ASSERT(iRoot, 0x300);
    MILO_ASSERT(iFilepath, 0x301);
    MILO_ASSERT(oBuf, 0x302);
    char buf[256];
    if (iFilepath >= oBuf && iFilepath < oBuf + File::MaxFileNameLen) {
        strcpy(buf, iFilepath);
        iFilepath = buf;
    } else if (iRoot >= oBuf && iRoot < oBuf + File::MaxFileNameLen) {
        strcpy(buf, iRoot);
        iRoot = buf;
    }
    char driveBuf[256];
    const char *fileDrive = FileGetDriveBuf(iFilepath, driveBuf);
    if (*fileDrive != '\0') {
        iFilepath += strlen(fileDrive) + 1;
    }
    char *start;
    if (*iFilepath == '/' || *iFilepath == '\\' || *iFilepath == '\0') {
        if (*fileDrive != '\0') {
            sprintf(oBuf, "%s:%s", fileDrive, iFilepath);
            start = oBuf + strlen(fileDrive) + 1;
        } else {
            const char *rootDrive = FileGetDriveBuf(iRoot, driveBuf);
            if (*rootDrive != '\0') {
                sprintf(oBuf, "%s:%s", rootDrive, iFilepath);
                start = oBuf + strlen(rootDrive) + 1;
            } else {
                strcpy(oBuf, iFilepath);
                start = oBuf;
            }
        }
    } else {
        sprintf(oBuf, "%s/%s", iRoot, iFilepath);
        const char *rootDrive = FileGetDriveBuf(iRoot, driveBuf);
        if (*rootDrive != '\0') {
            start = oBuf + strlen(rootDrive) + 1;
        } else {
            start = oBuf;
        }
    }
    FileNormalizePath(oBuf);
    bool curSlash = (*start == '/');
    const char *dirs[32];
    const char **endDir = &dirs[0];
    char *p = strtok(start, "/");
    while (p != nullptr) {
        if (*p != '.')
            *endDir++ = p;
        else if (p[1] == '.' && p[2] == '\0') {
            if (endDir != dirs && *endDir[-1] != '.')
                endDir--;
            else
                *endDir++ = p;
        }
        p = strtok(nullptr, "/");
    }
    MILO_ASSERT(endDir - dirs <= 32, 0x35c);
    char *c = start;
    if (endDir == dirs) {
        if (curSlash) {
            *c++ = '/';
        } else {
            *c++ = '.';
        }
    } else {
        for (const char **dir = (const char **)&dirs[0]; dir != endDir; dir++) {
            if (dir != dirs || curSlash) {
                *c++ = '/';
            }
            for (char *p = (char *)*dir; *p != '\0'; p++) {
                *c++ = *p;
            }
        }
    }
    MILO_ASSERT(c - oBuf < File::MaxFileNameLen, 0x372);
    *c = '\0';
    return oBuf;
}

const char *FileMakePath(const char *root, const char *file) {
    MainThread();
    static char static_buffer[256];
    return FileMakePathBuf(root, file, static_buffer);
}

const char *FileLocalize(const char *iFilename, char *buffer) {
    // ONE scratch buffer shared by both loops: retail computes its address
    // once, into r27, at 0x825D0AF8 (lbl_82F65BE8) and uses that same
    // register at BOTH `if (!buffer)` sites (0x825D0B64 and 0x825D0C94).
    // Two function-local statics would be two distinct .bss arrays.
    static char mybuffer[256];
    GfxMode mode = GetGfxMode();
    bool isOg = (mode == kNewGfx);
    if (!SystemLanguage().Null() || isOg) {
        if (!SystemLanguage().Null()) {
            for (const char *p = iFilename; *p != '\0'; p++) {
                if (*p == '/' && p[1] == 'e' && p[2] == 'n' && p[3] == 'g'
                    && p[4] == '/') {
                    if (!buffer)
                        buffer = mybuffer;
                    strcpy(buffer, iFilename);
                    // Retail reads the three bytes OUT of the "eng" string
                    // literal (lbz off ??_C@_03LKLGDMJI@eng at 0x825D0BD0);
                    // it does not materialise 'e'/'n'/'g' as immediates, so
                    // the pointer has to reach the stores as a value MSVC
                    // cannot constant-fold -- i.e. through this join.
                    // The "eng" arm is the THEN arm.  0x825D0B8C
                    // `beq .L_825D0BF0` sends !HongKongExceptionMet FORWARD
                    // past the eng block to the language block, 0x825D0BA4
                    // `bne .L_825D0BC0` takes the first strstr hit INTO it
                    // (the `||` short-circuit), and 0x825D0BBC `beq
                    // .L_825D0BF0` takes the second miss out; the block at
                    // 0x825D0BC0 is the fallthrough and ends `b .L_825D0C20`.
                    //
                    // NEGATIVE RESULT (w7-ap, 2026-09-14, 91.62 canonical):
                    // writing it the other way round -- `!HongKongExceptionMet
                    // || (strstr == 0 && strstr == 0)` with the language arm
                    // first -- is BYTE-IDENTICAL, same 24 rows.  MSVC
                    // normalises the two spellings and still emits the
                    // language arm at the fallthrough (idx 79 is `stb r8,
                    // 0x1(r11)` vs `bl SystemLanguage` either way), so the
                    // block order in this region is not reachable from the
                    // condition's polarity.  The form below is kept because it
                    // is the one the listing's branches describe.
                    //
                    // 100% (w7-bp, 2026-09-15, was 91.62): the residual was
                    // never the condition -- it was the two NAMED LOCALS the
                    // language arm used to carry (`char *dst = &buffer[...]`
                    // and `const char *langStr = SystemLanguage().Str()`).
                    // With them, both arms end in a textually identical
                    // `memcpy(dst, src, 3)` and MSVC CROSS-JUMPS them: the eng
                    // arm degenerates to one `lbz` plus a `b` into a shared
                    // tail that computes `subf/add/addi` once and does all
                    // three `stb`.  The image does not merge them -- it emits
                    // the address chain and the three stores TWICE, once per
                    // arm (0x825D0BC4-D8 `subf r11,r31,r30` / `add r11,r11,r29`
                    // / three `stb 0x1..0x3(r11)` / `b`, and again at
                    // 0x825D0BF0-0C1C into r31/r30 across the SystemLanguage
                    // call).  Spelling the destination expression inline in
                    // BOTH arms, so neither block has a common named value to
                    // merge on, reproduces that exactly: 141/141 equal.  The
                    // dead `addi rN, rM, 0x1` each arm emits is the `+ 1` of
                    // `&buffer[p + 1 - iFilename]`, which the inlined 3-byte
                    // memcpy then addresses as 0x1/0x2/0x3 off the pre-`+1`
                    // base -- keep the subscript spelling, not a `buffer + n`
                    // one.
                    if (HongKongExceptionMet()
                        && (strstr(iFilename, "sfx/loc/") != 0
                            || strstr(iFilename, "barks.milo") != 0)) {
                        memcpy(&buffer[p + 1 - iFilename], "eng", 3);
                    } else {
                        memcpy(
                            &buffer[p + 1 - iFilename], SystemLanguage().Str(), 3
                        );
                    }
                    // NOT `return buffer`.  Retail sets r31 = buffer at
                    // 0x825D0C20 and FALLS INTO the `isOg` test at
                    // 0x825D0C24, so a localized path is still scanned for
                    // "/og/" afterwards.
                    iFilename = buffer;
                    break;
                }
            }
        }
        if (isOg) {
            for (const char *p = iFilename; *p != '\0'; p++) {
                if (*p == '/' && p[1] == 'o' && p[2] == 'g' && p[3] == '/') {
                    // Both arms converge on the function's single epilogue at
                    // 0x825D0CC8; neither returns early.  The second one
                    // reaches it by assigning r31 (`mr r31, r29`) at
                    // 0x825D0CC0, i.e. iFilename = buffer.
                    if (buffer == iFilename) {
                        ((char *)p)[1] = 'n';
                        break;
                    }
                    if (!buffer) {
                        buffer = mybuffer;
                    }
                    strcpy(buffer, iFilename);
                    buffer[p + 1 - iFilename] = 'n';
                    iFilename = buffer;
                    break;
                }
            }
        }
    }
    return iFilename;
}

bool FileDiscSpinUp() { return TheBlockMgr.SpinUp(); }

bool FileReadOnly(const char *filepath) { return true; }

File *NewFile(const char *iFilename, int iMode) {
    if (gNullFiles) {
        return new NullFile();
    } else {
        if (!MainThread()) {
            MILO_NOTIFY("NewFile(%s) from !MainThread()", iFilename);
        }
        if (iFilename && *iFilename) {
            char pathBuf[256];
            char loc[256];
            if (iMode & 2) {
                iFilename = FileLocalize(iFilename, loc);
            }
            if (FileIsLocal(iFilename)) {
                iMode |= 0x10000;
            }
            if ((iMode & 2) && !(iMode & 0x20000)) {
                File *all = FileCache::GetFileAll(iFilename);
                if (all) {
                    return all;
                }
            }
            File *theFile;
            if (UsingCD() && (iMode & 2) && !(iMode & 0x10000)) {
                theFile = new ArkFile(iFilename, iMode);
            } else {
                iMode &= ~0x30000;
                theFile = AsyncFile::New(iFilename, iMode);
            }
            if (theFile->Fail()) {
                delete theFile;
                return nullptr;
            } else {
                if (!gOpenCaptureFile || !(iMode & 2) || 1 <= (unsigned int)(int)gCaptureFileMode) {
                    return theFile;
                }
                sprintf(pathBuf, "'%s'\n", FileMakePath(".", iFilename));
                gOpenCaptureFile->Write(pathBuf, strlen(pathBuf));
                gOpenCaptureFile->Flush();
                return theFile;
            }
        }
        return nullptr;
    }
}

void FileRecursePattern(
    const char *pattern, void (*cb)(const char *, const char *), bool recurse
) {
    RecursePatternInternal(pattern, cb, recurse, false);
}

// Logic derived from the Ghidra decompile of the PPC body.
//
// This used to sit behind #ifndef HX_NATIVE, which left dc3-native with an
// unresolved `RecursePatternInternal` that FileRecursePattern and
// OnEnumerateFrameRateResults both jump to. Nothing about the body is
// Xbox-specific -- the one platform primitive it reaches for, FileEnumerate,
// has a POSIX implementation in native/src/platform/File_Native.cpp -- so the
// guard was simply wrong.
void RecursePatternInternal(
    const char *pttn,
    void (*cb)(const char *, const char *),
    bool recurse,
    bool recurse_dirs
) {
    MILO_ASSERT(pttn && pttn[0], 0x5B8);
    String pttnStr(pttn);

    // Find split point: first '&', or end-of-string if absent
    unsigned int ampPos = pttnStr.find_first_of("&", 0);
    unsigned int wildcardPos = pttnStr.find_first_of("?*", 0);

    int splitPos;
    if (ampPos == FixedString::npos) {
        splitPos = (int)pttnStr.length() - 1;
    } else {
        splitPos = ampPos;
    }
    if (wildcardPos != FixedString::npos && wildcardPos < (unsigned int)splitPos) {
        splitPos = wildcardPos;
    }

    // If recurse enabled and no & wildcard: check for path-separator past splitPos
    if (recurse && ampPos == (int)FixedString::npos) {
        int pttnLen = (int)pttnStr.length();
        // Walk forward from splitPos looking for path separator
        int forwardPos = splitPos;
        while (forwardPos < pttnLen && pttnStr[forwardPos] != '/'
               && pttnStr[forwardPos] != '\\') {
            forwardPos++;
        }
        if (forwardPos == pttnLen) {
            // No path separator found — disable recurse for FileEnumerate
            recurse = false;
        } else {
            // Path separator found: we need to recurse into subdirectories
            String subPattern = pttnStr.substr(
                (unsigned int)forwardPos, (unsigned int)(pttnLen - forwardPos)
            );
            pttnStr = pttnStr.substr(0, (unsigned int)forwardPos);

            // Enumerate subdirectories at this level
            RecursePatternInternal(pttnStr.c_str(), DirListCB, false, true);
            std::vector<String> dirs(gDirList);
            if (gDirList.begin() != gDirList.end()) {
                gDirList.erase(gDirList.begin(), gDirList.end());
            }

            const char *dirPart = pttnStr.c_str();
            MainThread();
            static char pathBuf[256];
            const char *dirBase = FileGetPathBuf(dirPart, pathBuf);
            pttnStr = dirBase;

            for (unsigned int i = 0; i < dirs.size(); i++) {
                const char *combined = MakeString(
                    "%s/%s%s", pttnStr, dirs[i], subPattern
                );
                RecursePatternInternal(combined, cb, recurse, recurse_dirs);
            }
            return;
        }
    }

    // Walk backward from splitPos to find last path separator
    int pos = splitPos;
    while (pos >= 0 && pttnStr[pos] != '/' && pttnStr[pos] != '\\') {
        pos--;
    }
    String dirStr;
    dirStr = pos <= 0 ? String(".") : pttnStr.substr(0, (unsigned int)pos);
    FileEnumerate(dirStr.c_str(), cb, recurse, pttnStr.c_str(), recurse_dirs);
}
