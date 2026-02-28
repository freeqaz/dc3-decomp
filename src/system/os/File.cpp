#include "os/File.h"
#include "os/AsyncFile.h"
#include "os/FileCache.h"
#include "os/ArkFile_p.h"
#include "HolmesClient.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "os/Debug.h"
#include "os/OSFuncs.h"
#include "os/System.h"
#include "types.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "utl/MemMgr.h"
#include "utl/Option.h"
#include <cctype>
#include <cstdio>
#include <cstring>

static char gSystemRoot[256]; // 0x0
static char gExecRoot[256]; // 0x100
static char gRoot[256]; // 0x200
File *gOpenCaptureFile; // 0x300

bool gFakeFileErrors;
bool gNullFiles;
void *kNoHandle;
DataArray *gFrameRateArray;

std::vector<File *> gFiles(0x80); // 0x10...?
int gCaptureFileMode;
std::vector<String> gDirList;
// const int File::MaxFileNameLen = 0x100;

const char *FileRoot() { return gRoot; }
const char *FileExecRoot() { return gExecRoot; }
const char *FileSystemRoot() { return gSystemRoot; }

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

const char *FileGetDrive(const char *file) {
    static char drive[256];
    const char *p = strchr(file, ':');
    if (p != 0) {
        strncpy(drive, file, p - file);
        drive[p - file] = '\0';
    } else {
        drive[0] = '\0';
    }
    return drive;
}

const char *FileGetPath(const char *file) {
    static char static_path[256];
    if (file != 0) {
        strcpy(static_path, file);
        char *p2 = static_path + strlen(static_path) - 1;
        while (p2 >= static_path && *p2 != '/' && *p2 != '\\') {
            p2--;
        }
        if (p2 >= static_path) {
            if ((p2 == static_path) || (p2[-1] == ':'))
                p2[1] = '\0';
            else
                *p2 = '\0';
            return static_path;
        }
    }
    static_path[0] = '.';
    static_path[1] = '\0';
    return static_path;
}

const char *FileGetBase(const char *file) {
    static char my_path[256];
    const char *dir;
    char *ext;
    dir = strrchr(file, '/');
    if ((dir != 0) || (dir = strrchr(file, '\\'), (dir != 0)))
        strcpy(my_path, dir + 1);
    else
        strcpy(my_path, file);
    ext = strrchr(my_path, '.');
    if (ext != 0)
        *ext = 0;
    return my_path;
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

void DirListCB(const char *, const char *cc2) { gDirList.push_back(String(cc2)); }

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
    String ret(c1);
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

DataNode OnFileGetDrive(DataArray *da) { return FileGetDrive(da->Str(1)); }
DataNode OnFileGetPath(DataArray *da) { return FileGetPath(da->Str(1)); }
DataNode OnFileGetBase(DataArray *da) { return FileGetBase(da->Str(1)); }
DataNode OnFileAbsolutePath(DataArray *da) {
    return FileMakePath(da->Str(1), da->Str(2));
}
DataNode OnFileRelativePath(DataArray *da) {
    return FileRelativePath(da->Str(1), da->Str(2));
}
DataNode OnToggleFakeFileErrors(DataArray *) { return 0; }
DataNode OnEnumerateFrameRateResults(DataArray *) {
    // TODO: implement when FileRecursePattern is available
    return 0;
}

void FileInit() {
    strcpy(gRoot, ".");
    strcpy(gExecRoot, ".");
    strcpy(gSystemRoot, FileMakePath(gExecRoot, "../../system/run"));
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

const char *FileRelativePathBuf(
    const char *root, const char *filepath, char *relative
) {
    if (*filepath == '\0')
        return filepath;
    char rootBuf[256];
    char fpBuf[256];
    strcpy(rootBuf, root);
    strcpy(fpBuf, filepath);

    const char *rootToks[64];
    const char *fpToks[64];
    int rootCount = 0;
    int fpCount = 0;

    for (char *tok = strtok(rootBuf, "/"); tok != nullptr;
         tok = strtok(nullptr, "/"))
        rootToks[rootCount++] = tok;
    for (char *tok = strtok(fpBuf, "/"); tok != nullptr;
         tok = strtok(nullptr, "/"))
        fpToks[fpCount++] = tok;

    if (fpCount > 0 && rootCount > 0) {
        if (strcmp(fpToks[fpCount - 1], rootToks[rootCount - 1]) != 0)
            return filepath;
        while (rootCount > 0 && fpCount > 0
               && strcmp(fpToks[fpCount - 1], rootToks[rootCount - 1]) == 0) {
            rootCount--;
            fpCount--;
        }
        char *p = relative;
        for (int i = 0; i < rootCount; i++) {
            if (p != relative)
                *p++ = '/';
            *p++ = '.';
            *p++ = '.';
        }
        for (int i = fpCount - 1; i >= 0; i--) {
            if (p != relative)
                *p++ = '/';
            for (const char *pp = fpToks[i]; *pp != '\0'; pp++)
                *p++ = *pp;
        }
        if (p == relative)
            *p++ = '.';
        *p = '\0';
    } else {
        strcpy(relative, filepath);
    }
    return relative;
}

const char *FileRelativePath(const char *root, const char *filepath) {
    MainThread();
    static char relative[256];
    return FileRelativePathBuf(root, filepath, relative);
}

const char *FileMakePathBuf(const char *root, const char *file, char *buffer) {
    MILO_ASSERT(root && file, 0x320);
    if (!buffer) {
        static char static_buffer[256];
        buffer = static_buffer;
    }
    char buf[256];
    if (file >= buffer && file < buffer + 4) {
        strcpy(buf, file);
        file = buf;
    } else if (root >= buffer && root < buffer + 4) {
        strcpy(buf, root);
        root = buf;
    }
    const char *fileDrive = FileGetDrive(file);
    if (*fileDrive != '\0') {
        file += strlen(fileDrive) + 1;
    }
    char *c = buffer;
    if (*file == '/' || *file == '\\' || *file == '\0') {
        if (*fileDrive != '\0') {
            sprintf(buffer, "%s:%s", fileDrive, file);
            c = buffer + strlen(fileDrive) + 1;
        } else {
            const char *rootDrive = FileGetDrive(root);
            if (*rootDrive != '\0') {
                sprintf(buffer, "%s:%s", rootDrive, file);
                c = buffer + strlen(rootDrive) + 1;
            } else {
                strcpy(buffer, file);
            }
        }
    } else {
        sprintf(buffer, "%s/%s", root, file);
        const char *rootDrive = FileGetDrive(root);
        if (*rootDrive != '\0') {
            c = buffer + strlen(rootDrive) + 1;
        }
    }
    FileNormalizePath(buffer);
    char curC = *c;
    char *dirs[32];
    const char **endDir = (const char **)&dirs[0];
    for (char *p = strtok(c, "/"); p != nullptr; p = strtok(nullptr, "/")) {
        if (*p != '.')
            *endDir++ = p;
        else if (p[1] == '.' && p[2] == '\0') {
            if (endDir != dirs && *endDir[-1] != '.')
                endDir--;
            else
                *endDir++ = p;
        }
    }
    if (endDir == dirs) {
        if (curC == '/') {
            *c++ = '/';
        } else {
            *c++ = '.';
        }
    } else {
        for (const char **dir = (const char **)&dirs[0]; dir != endDir; dir++) {
            if (dir != dirs || curC == '/') {
                *c++ = '/';
            }
            for (char *p = (char *)*dir; *p != '\0'; p++) {
                *c++ = *p;
            }
        }
    }
    *c = '\0';
    return buffer;
}

const char *FileMakePath(const char *root, const char *file) {
    return FileMakePathBuf(root, file, NULL);
}

const char *FileLocalize(const char *iFilename, char *buffer) {
    // Simplified: no localization needed during boot
    return iFilename;
}

bool FileDiscSpinUp() { return true; }

bool FileReadOnly(const char *filepath) { return true; }

File *NewFile(const char *iFilename, int iMode) {
    const char *filename;
    int mode;
    File *result;

    filename = iFilename;
    mode = iMode;
    result = nullptr;

    if (gNullFiles) {
        return new NullFile();
    }

    if (!MainThread()) {
        TheDebug.Notify("NewFile(%s) from MainThread()");
    }

    if ((iFilename != nullptr) && (*iFilename != '\0')) {
        char localized[256];
        if (mode & 0x2) {
            filename = FileLocalize(iFilename, localized);
        }

        if (FileIsLocal(filename)) {
            mode |= 0x10000;
        }

        int mode_check = mode & 0x2;
        if ((mode_check == 0) || (mode & 0x20000) ||
            ((result = FileCache::GetFileAll(filename)) == nullptr)) {
            if ((UsingCD() != 0) && (mode_check != 0) && !(mode & 0x10000)) {
                void *mem = _MemAllocTemp(sizeof(ArkFile), __FILE__, 0x19, "ArkFile", 0);
                if (mem != nullptr) {
                    result = new (mem) ArkFile(filename, mode);
                } else {
                    result = nullptr;
                }
            } else {
                mode &= ~0x4000;
                result = AsyncFile::New(filename, mode);
            }

            if (result != nullptr) {
                if (result->Fail()) {
                    delete result;
                    return nullptr;
                }

                if ((gOpenCaptureFile != nullptr) && (mode & 0x2) &&
                    !(mode & 0x20000)) {
                    char path_buf[256];
                    sprintf(path_buf, "./%s", FileMakePath(".", filename));
                    const char *ptr = path_buf;
                    while (*ptr != '\0') {
                        ptr++;
                    }
                    gOpenCaptureFile->Write(path_buf, (ptr - path_buf) - 1);
                    gOpenCaptureFile->Flush();
                }
            }
        }
    }

    return result;
}
