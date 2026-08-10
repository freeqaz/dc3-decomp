#include "os\File.h"
#include "os\AsyncFile.h"
#include "os\Block.h"
#include "os\FileCache.h"
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
#include "utl\BinStream.h"
#include "utl\Loader.h"
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

static File *gOpenCaptureFile;
static int gCaptureFileMode;
static char gRoot[256];
static char gExecRoot[256];
static char gSystemRoot[256];

bool gFakeFileErrors;
bool gNullFiles;
void *kNoHandle;
DataArray *gFrameRateArray;

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
    char *c = oBuf;
    if (*iFilepath == '/' || *iFilepath == '\\' || *iFilepath == '\0') {
        if (*fileDrive != '\0') {
            sprintf(oBuf, "%s:%s", fileDrive, iFilepath);
            c = oBuf + strlen(fileDrive) + 1;
        } else {
            const char *rootDrive = FileGetDriveBuf(iRoot, driveBuf);
            if (*rootDrive != '\0') {
                sprintf(oBuf, "%s:%s", rootDrive, iFilepath);
                c = oBuf + strlen(rootDrive) + 1;
            } else {
                strcpy(oBuf, iFilepath);
            }
        }
    } else {
        sprintf(oBuf, "%s/%s", iRoot, iFilepath);
        const char *rootDrive = FileGetDriveBuf(iRoot, driveBuf);
        if (*rootDrive != '\0') {
            c = oBuf + strlen(rootDrive) + 1;
        }
    }
    FileNormalizePath(oBuf);
    bool curSlash = (*c == '/');
    const char *dirs[32];
    const char **endDir = &dirs[0];
    char *p = strtok(c, "/");
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
    GfxMode mode = GetGfxMode();
    bool isOg = (mode == kNewGfx);
    if (!SystemLanguage().Null() || isOg) {
        Symbol lang2 = SystemLanguage();
        if (!lang2.Null()) {
            for (const char *p = iFilename; *p != '\0'; p++) {
                if (*p == '/' && p[1] == 'e' && p[2] == 'n' && p[3] == 'g'
                    && p[4] == '/') {
                    static char mybuffer[256];
                    if (!buffer)
                        buffer = mybuffer;
                    strcpy(buffer, iFilename);
                    if (!HongKongExceptionMet()
                        || (strstr(iFilename, "sfx/loc/") == 0
                            && strstr(iFilename, "barks.milo") == 0)) {
                        Symbol lang3 = SystemLanguage();
                        const char *langStr = lang3.Str();
                        buffer[p + 1 - iFilename] = langStr[0];
                        buffer[p + 2 - iFilename] = langStr[1];
                        buffer[p + 3 - iFilename] = langStr[2];
                    } else {
                        buffer[p + 1 - iFilename] = 'e';
                        buffer[p + 2 - iFilename] = 'n';
                        buffer[p + 3 - iFilename] = 'g';
                    }
                    return buffer;
                }
            }
        }
        if (isOg) {
            for (const char *p = iFilename; *p != '\0'; p++) {
                if (*p == '/' && p[1] == 'o' && p[2] == 'g' && p[3] == '/') {
                    if (buffer == iFilename) {
                        ((char *)p)[1] = 'n';
                        return iFilename;
                    }
                    if (!buffer) {
                        static char mybuffer[256];
                        buffer = mybuffer;
                    }
                    strcpy(buffer, iFilename);
                    buffer[p + 1 - iFilename] = 'n';
                    return buffer;
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

#ifndef HX_NATIVE
// PPC (Xbox 360) implementation — logic derived from Ghidra decompile
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
        int pttnLen = (int)pttnStr.length() - 1;
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
            String subPattern = pttnStr.substr((unsigned int)forwardPos);
            pttnStr = pttnStr.substr(0, (unsigned int)forwardPos);

            // Enumerate subdirectories at this level
            RecursePatternInternal(pttnStr.c_str(), DirListCB, false, true);
            std::vector<String> dirs(gDirList);
            if (gDirList.begin() != gDirList.end()) {
                gDirList.erase(gDirList.begin(), gDirList.end());
            }

            MainThread();
            static char pathBuf[256];
            const char *dirBase = FileGetPathBuf(pttnStr.c_str(), pathBuf);
            pttnStr = dirBase;

            unsigned int numDirs = dirs.size();
            for (unsigned int i = 0; i < numDirs; i++) {
                const char *combined = MakeString(
                    "%s/%s%s", pttnStr, dirs[i], subPattern
                );
                RecursePatternInternal(combined, cb, recurse, recurse_dirs);
            }
            return;
        }
    }

    // Walk backward from splitPos to find last path separator
    String dirStr;
    if (splitPos > 0) {
        int pos = splitPos;
        while (pos >= 0 && pttnStr[pos] != '/' && pttnStr[pos] != '\\') {
            pos--;
        }
        if (pos > 0) {
            dirStr = pttnStr.substr(0, (unsigned int)pos);
        } else {
            dirStr = ".";
        }
    } else {
        dirStr = ".";
    }
    FileEnumerate(dirStr.c_str(), cb, recurse, pttnStr.c_str(), recurse_dirs);
}
#endif
