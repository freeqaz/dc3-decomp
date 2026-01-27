#include "os/File.h"
#include "os/AsyncFile.h"
#include "os/FileCache.h"
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
    const char *dir = strrchr(file, '/');
    if ((dir != 0) || (dir = strrchr(file, '\\'), (dir != 0))) {
        file = dir + 1;
    }
    return file;
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

DataNode OnFileGetDrive(DataArray *);
DataNode OnFileGetPath(DataArray *);
DataNode OnFileGetBase(DataArray *);
DataNode OnFileAbsolutePath(DataArray *);
DataNode OnFileRelativePath(DataArray *);
DataNode OnToggleFakeFileErrors(DataArray *);
DataNode OnEnumerateFrameRateResults(DataArray *);

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

const char *FileRelativePath(const char *root, const char *filepath) {
    MainThread();
    static char relative[256];
    return FileRelativePathBuf(root, filepath, relative);
}

bool FileReadOnly(const char *filepath) { return true; }

File *NewFile(const char *iFilename, int iMode) {
    if (gNullFiles) {
        return new NullFile();
    }

    // Check if we're on the main thread, if not, fail
    if (!MainThread()) {
        MILO_FAIL("NewFile(%s) from non-MainThread()");
        return nullptr;
    }

    // Check if filename is provided
    if (!iFilename || *iFilename == '\0') {
        return nullptr;
    }

    const char *filename = iFilename;
    char localized[256];

    // Localize the filename if needed (mode bit 30 set)
    if (iMode & 0x40000000) {
        filename = FileLocalize(iFilename, localized);
    }

    // Check if file is local and update mode flags
    if (FileIsLocal(filename)) {
        iMode |= 0x10000;
    }

    // Check if we can use FileCache (mode bit 30 and not bit 14)
    if ((iMode & 0x40000000) && !(iMode & 0x4000)) {
        File *cachedFile = FileCache::GetFileAll(filename);
        if (cachedFile) {
            return cachedFile;
        }
    }

    // Determine which file type to create
    File *file = nullptr;

    // TODO: ArkFile support when UsingCD()
    // For now, just use AsyncFile
    iMode &= ~0x4000; // Clear bit 14
    file = AsyncFile::New(filename, iMode);

    // Validate the file
    if (file) {
        if (file->Fail()) {
            // File failed to open - delete and return nullptr
            delete file;
            return nullptr;
        }
    }

    return file;
}
