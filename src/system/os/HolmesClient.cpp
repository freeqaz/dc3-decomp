#include "HolmesClient.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Msg.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/HolmesUtl.h"
#include "os/NetworkSocket.h"
#include "os/System.h"
#include "os/Timer.h"
#include "utl/Cache.h"
#include "utl/Loader.h"
#include "utl/MemStream.h"
#include "utl/Option.h"
#include "utl/Symbol.h"
#include "utl/TextFileStream.h"
#include <cstdio>
#include <list>
#include <vector>

#pragma region Statics

#define HOLMES_CURRENT_VERSION 26
#define NETBIOS_NAME_MAX 64

String gLastCachedResource;
CacheResourceResult gLastCacheResult;

class HolmesInput {
public:
    void LoadKeyboard(BinStream &bs);
    void LoadJoypad(BinStream &bs);
    void SendKeyboardMessages();
};

namespace {
    struct HolmesProfileData {
        Timer wait;
        Timer work;
        int count;
        u32 pad;
    };

    struct ReadRequest {
        File *mRequestor;
        void *mBuffer;
        int mBytes;
    };

    BinStream *gHolmesStream;
    MemStream *gStreamBuffer;

    char gMachineName[NETBIOS_NAME_MAX] = { 0 };
    char gShareName[NETBIOS_NAME_MAX] = { 0 };
    bool gStackTraced;

    Holmes::Protocol gPendingResponse;
    int gRealMaxBufferSize;
    HolmesProfileData gProfile[20]; // to match protocol count
    CriticalSection gCrit;
    std::list<ReadRequest> gRequests;
    String gServerName;

    HolmesInput gInput;

    String gHolmesTarget;
    bool gPollStreamEof;

    bool PendingRead(File *f) {
        FOREACH (it, gRequests) {
            if (it->mRequestor == f)
                return true;
        }
        return false;
    }

#pragma region Private details

    void BeginCmd(Holmes::Protocol prot, bool b) {
        if (b) {
            gProfile[prot].count += 1;
        }
        gProfile[prot].work.Start();
    }

    static const int sEndCmdState = 0x2000d;

    void EndCmd(Holmes::Protocol prot) {
        gProfile[prot].work.Stop();
        if (gRealMaxBufferSize != 0) {
            MILO_NOTIFY_ONCE(
                "HolmesClient buffer exceeded %d < %d", sEndCmdState, gRealMaxBufferSize
            );
        }
    }

    void HolmesFlushStreamBuffer() {
        if (gStreamBuffer->Size() > 0x2000D) {
            gRealMaxBufferSize = gStreamBuffer->Size();
        }
        gHolmesStream->Write(gStreamBuffer->Buffer(), gStreamBuffer->Size());
        gStreamBuffer->Seek(0, BinStream::kSeekEnd);
        gStreamBuffer->Compact();
    }

    void WaitForAnyResponse(Holmes::Protocol prot) {
        if (gPendingResponse == Holmes::kInvalidOpcode
            && gHolmesStream->Eof() != NotEof) {
            AutoSlowFrame frame(__FUNCTION__, 5);
            gProfile[prot].wait.Start();
            float split = gProfile[prot].wait.SplitMs();
            float f9 = 2000;
            while (gHolmesStream->Eof() != NotEof) {
                Timer::Sleep(0);
                if (!gStackTraced && gProfile[prot].wait.SplitMs() - split > f9) {
                    printf(
                        "[Holmes] %s opcode blocked for %.0f seconds\n",
                        Holmes::ProtocolDebugString(prot),
                        f9 / 1000
                    );
                    f9 += 1000;
                }
            }
            gProfile[prot].wait.Stop();
        }
    }

    static const int sPossibleResponses[] = { Holmes::kReadFile,
                                              Holmes::kPollKeyboard,
                                              Holmes::kPollJoypad,
                                              Holmes::kPrint,
                                              Holmes::kInvalidOpcode };
    static Timer *holmesReadopcTimer;

    bool CheckForResponse(Holmes::Protocol prot, bool b) {
        if (gPendingResponse == Holmes::kInvalidOpcode) {
            bool b9;
            if (b) {
                gPollStreamEof = gHolmesStream->Eof() != NotEof;
                b9 = gPollStreamEof;
            } else {
                b9 = gHolmesStream->Eof() != NotEof;
            }
            if (!b9) {
                if (!holmesReadopcTimer) {
                    holmesReadopcTimer = AutoTimer::GetTimer("holmes_readopc");
                }
                AutoTimer _at(holmesReadopcTimer, 50.0f, NULL, NULL);
                unsigned char response;
                *gHolmesStream >> response;
                gPendingResponse = (Holmes::Protocol)response;
                MILO_ASSERT(gPendingResponse != Holmes::kInvalidOpcode, 0xEF);
            }
        }
        bool isPending = gPendingResponse == prot;
        if (!isPending) {
            for (int i = 0; i < DIM(sPossibleResponses); i++) {
                if (sPossibleResponses[i] == gPendingResponse
                    || sPossibleResponses[i] == prot) {
                    isPending = true;
                    break;
                }
            }
        }
        if (gHolmesStream->Fail()) {
            MILO_FAIL("holmes closed");
        } else if (!isPending) {
            MILO_FAIL(
                "this shouldn't be happening %s %s\n",
                Holmes::ProtocolDebugString(gPendingResponse),
                Holmes::ProtocolDebugString(prot)
            );
        }
        return gPendingResponse == prot;
    }

    bool CheckReads(bool b);
    void CheckInput(bool b);

    void WaitForResponse(Holmes::Protocol prot) {
        while (true) {
            if (CheckForResponse(prot, false)) {
                return;
            }
            WaitForAnyResponse(prot);
            if (CheckReads(false) && prot == 5) {
                return;
            }
            CheckInput(false);
        }
    }

    bool CheckReads(bool b) {
        FOREACH (it, gRequests) {
            if (!CheckForResponse(Holmes::kReadFile, b)) {
                return false;
            }
            BeginCmd(Holmes::kReadFile, false);
            ReadRequest &cur = *it;
            int i2 = gHolmesStream->ReadAsync(cur.mBuffer, cur.mBytes);
            char *buffer = (char *)cur.mBuffer;
            buffer += i2;
            cur.mBytes -= i2;
            EndCmd(Holmes::kReadFile);
            if (i2 <= 0) {
                return false;
            }
            if (cur.mBytes == 0) {
                gRequests.erase(it);
                gPendingResponse = Holmes::kInvalidOpcode;
                return true;
            }
        }
        return false;
    }

    void WaitForReads() {
        CritSecTracker tracker(&gCrit);
        while (true) {
            if (gRequests.empty()) {
                return;
            }
            while (!CheckForResponse(Holmes::kReadFile, false)) {
                WaitForAnyResponse(Holmes::kReadFile);
                if (CheckReads(false)) {
                    break;
                }
                CheckInput(false);
            }
            CheckReads(false);
        }
    }

    void CheckInput(bool b) {
        if (CheckForResponse(Holmes::kPollKeyboard, b)) {
            BeginCmd(Holmes::kPollKeyboard, true);
            gInput.LoadKeyboard(*gHolmesStream);
            gPendingResponse = Holmes::kInvalidOpcode;
            EndCmd(Holmes::kPollKeyboard);
        }

        if (CheckForResponse(Holmes::kPollJoypad, b)) {
            BeginCmd(Holmes::kPollJoypad, true);
            gInput.LoadJoypad(*gHolmesStream);
            gPendingResponse = Holmes::kInvalidOpcode;
            EndCmd(Holmes::kPollJoypad);
        }
    }

    void HolmesClientPollInternal(bool b) {
        CritSecTracker cst(&gCrit);

        if (!gHolmesStream)
            return;

        CheckInput(b);
        CheckReads(b);
    };

}

CacheResourceResult HolmesClientCacheResource(const char *c1, const char *c2) {
    AutoSlowFrame frame(__FUNCTION__, 1000);
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kCacheResource, true);
    gLastCachedResource = c2;
    MILO_ASSERT(gHolmesStream, 0x4CC);
    *gStreamBuffer << (unsigned char)Holmes::kCacheResource;
    *gStreamBuffer << c1;
    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kCacheResource);
    char result;
    *gHolmesStream >> result;
    gPendingResponse = Holmes::kInvalidOpcode;
    gLastCacheResult = (CacheResourceResult)result;
    EndCmd(Holmes::kCacheResource);
    return gLastCacheResult;
}

#pragma region Public API

bool UsingHolmes(int p1) {
    if (!gHolmesStream)
        return false;

    return CanUseHolmes(p1);
}

NetAddress HolmesResolveIP() {
    if (CanUseHolmes(3))
        return HolmesClient::PlatformResolveIP();
    else
        return NetAddress();
}

namespace {
    bool gInputPolling = false;
}

void HolmesClientPollKeyboard() {
    HolmesClientPollInternal(true);
    if (!gInputPolling) {
        gInputPolling = true;
        gInput.SendKeyboardMessages();
        gInputPolling = false;
    }
}

DataNode DumpHolmesLog(DataArray *) {
    TextFileStream *log = new TextFileStream("holmes.csv", true);
    FileStream &fs = log->File();
    if (!fs.Fail()) {
        *log << HolmesClient::PlatformGetHostName() << ", ";
        *log << -1 << ", ";
        *log << -1 << "\n";
        for (int i = 0; i < 20; i++) {
            int count = gProfile[i].count;
            float wait = gProfile[i].wait.SplitMs();
            float work = gProfile[i].work.SplitMs() - wait;
            *log << Holmes::ProtocolDebugString(i) << ", ";
            *log << count << ", ";
            *log << wait << ", ";
            *log << work << ", ";
        }
        fs.Flush();
    }
    delete log;
    return 0;
}

static const int kHolmesCurrentVersion = HOLMES_CURRENT_VERSION;

bool HolmesClientInitOpcode(bool quiet) {
    bool fail = 0;
    *gStreamBuffer << u8(Holmes::kVersion) << HOLMES_CURRENT_VERSION;
    *gStreamBuffer << HolmesClient::PlatformGetHostName();
    *gStreamBuffer << gHolmesTarget;
    *gStreamBuffer << &gMachineName[0x40];
    *gStreamBuffer << FileSystemRoot();
    *gStreamBuffer << u8(TheLoadMgr.GetPlatform());
    *gStreamBuffer << u8(GetGfxMode());
    HolmesFlushStreamBuffer();
    if (!quiet) {
        WaitForAnyResponse(Holmes::kVersion);
        u8 response;
        *gHolmesStream >> response;
        fail = response != 0;
    } else {
        WaitForAnyResponse(Holmes::kVersion);
    }
    s32 host_ver = -1;
    if (!fail) {
        *gHolmesStream >> host_ver;
        fail = host_ver != HOLMES_CURRENT_VERSION;
    }
    if (fail) { // host/client version mismatch
        RELEASE(gHolmesStream);
        RELEASE(gStreamBuffer);
        if (gHostLogging) {
            gPendingResponse = Holmes::kInvalidOpcode;
            return fail;
        }
        if (host_ver >= 0) {
            MILO_FAIL(
                "Holmes version mismatch\nResync/rebuild both projects\nHolmes=%d  Console=%d",
                host_ver,
                kHolmesCurrentVersion
            );
        } else {
            MILO_FAIL("Holmes protocol mismatch\nCould not connect to console");
        }
    }
    if (!fail) {
        *gHolmesStream >> gServerName;
    }
    if (gHolmesTarget.c_str()[0] != 0) {
        bool b;
        *gHolmesStream >> b;
        if (b == 0) {
            MILO_FAIL("Failed to find holmes target '%s'", gHolmesTarget);
        }
    }
    if (!fail && gMachineName[0x40] == 0) {
        String my_name(gMachineName), host_name;
        *gHolmesStream >> host_name;
        if (host_name.c_str()[0] == 0) {
            MILO_FAIL(
                "Holmes fileroot missing!\nplease add -holmes_target <target> or -holmes_share <rootpath> to your commandline\n(-holmes_target is the preferred usage)"
            );
        }
        HolmesSetFileShare(my_name.c_str(), host_name.c_str());
    }
    gPendingResponse = Holmes::kInvalidOpcode;
    return fail;
}

void HolmesClientInit() {
#ifdef HX_NATIVE
    return; // Holmes remote debug not needed on native
#endif
    if (!UsingCD() || gHostConfig || gHostLogging) {
        MILO_LOG("Trying to connect to Holmes...\n");
        bool conf, log;
        if (!UsingCD()) {
            conf = gHostConfig = 0;
            log = gHostLogging = 0;
        } else {
            conf = gHostConfig;
            log = gHostLogging;
        }
        bool unk = !conf || log ? 0 : 1;
        BeginCmd(Holmes::kVersion, true);
        gHolmesTarget = OptionStr("holmes_target", gNullStr);
        String share(gShareName);
        share = OptionStr("holmes_share", share.c_str());
        share = OptionStr("xb_share", share.c_str());
        gHolmesStream = HolmesClient::PlatformCreateServerStream(unk, share.c_str());
        if (gHolmesStream == nullptr) {
            if (!unk) {
                MILO_FAIL("COULD NOT CONNECT TO HOLMES");
            }
            EndCmd(Holmes::kVersion);
            return;
        }
        bool fail = gHolmesStream->Fail();
        if (!fail) {
            gStreamBuffer = new MemStream(true);
            gStreamBuffer->Reserve(0x2000D);
            fail = HolmesClientInitOpcode(false);
            if (fail != 0 && unk) {
                return;
            }
        }
        if (fail) {
            RELEASE(gHolmesStream);
            RELEASE(gStreamBuffer);
        }
        if (fail && !unk) {
            MILO_FAIL("COULD NOT CONNECT TO HOLMES");
        }
        DataRegisterFunc("dump_holmes_log", DumpHolmesLog);
        EndCmd(Holmes::kVersion);
    }
}

void HolmesClientReInit() {
    CritSecTracker cst(&gCrit);
    if (!gHolmesStream) {
        return;
    }
    BeginCmd(Holmes::kVersion, true);
    HolmesClientInitOpcode(1);
    EndCmd(Holmes::kVersion);
    return;
}

int HolmesClientSysExec(const char *cc) {
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kSysExec, true);
    MILO_ASSERT(gHolmesStream, 750);
    *gStreamBuffer << u8(Holmes::kSysExec) << cc;
    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kSysExec);
    int ret;
    *gHolmesStream >> ret;
    gPendingResponse = Holmes::kInvalidOpcode;
    EndCmd(Holmes::kSysExec);
    return ret;
}

int HolmesClientGetStat(const char *filename, FileStat &stat) {
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kGetStat, true);
    MILO_ASSERT(gHolmesStream, 770);
    *gStreamBuffer << u8(Holmes::kGetStat);
    *gStreamBuffer << filename;
    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kGetStat);
    bool exists;
    *gHolmesStream >> exists;
    if (exists) {
        *gHolmesStream >> stat;
    }
    gPendingResponse = Holmes::kInvalidOpcode;
    EndCmd(Holmes::kGetStat);
    if (exists)
        return 0;
    else
        return -1;
}

int HolmesClientMkDir(const char *cc) {
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kMkDir, true);
    MILO_ASSERT(gHolmesStream, 818);
    *gStreamBuffer << u8(Holmes::kMkDir);
    *gStreamBuffer << cc;
    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kMkDir);
    int ret;
    *gHolmesStream >> ret;
    gPendingResponse = Holmes::kInvalidOpcode;
    EndCmd(Holmes::kMkDir);
    return ret;
}

int HolmesClientDelete(const char *cc) {
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kDelete, true);
    MILO_ASSERT(gHolmesStream, 839);
    *gStreamBuffer << u8(Holmes::kDelete);
    *gStreamBuffer << cc;
    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kDelete);
    int ret;
    *gHolmesStream >> ret;
    gPendingResponse = Holmes::kInvalidOpcode;
    EndCmd(Holmes::kDelete);
    return ret;
}

const char *HolmesFileShare() { return gShareName; }

void HolmesSetFileShare(const char *machine, const char *share) {
    strncpy(gMachineName, machine, NETBIOS_NAME_MAX);
    strncpy(gShareName, share, NETBIOS_NAME_MAX);
}

void HolmesClientTerminate() {
    CritSecTracker cst(&gCrit);
    if (!gHolmesStream)
        return;
    else {
        BeginCmd(Holmes::kTerminate, true);
        DumpHolmesLog(nullptr);
        if (gHolmesStream) {
            if (!gHolmesStream->Fail()) {
                unsigned char uc = 0xD;
                *gStreamBuffer << uc;
                HolmesFlushStreamBuffer();
            }
            delete gHolmesStream;
        }
        gHolmesStream = nullptr;
        RELEASE(gStreamBuffer);
    }
}

void HolmesClientTruncate(int i1, int i2) {
    CritSecTracker cst(&gCrit);
    MILO_ASSERT(gHolmesStream, 0x3AD);
    if (!gHolmesStream->Fail() || !gHostLogging) {
        BeginCmd(Holmes::kTruncateFile, true);
        *gStreamBuffer << (unsigned char)Holmes::kTruncateFile << i1 << i2;
        HolmesFlushStreamBuffer();
        WaitForResponse(Holmes::kTruncateFile);
        int x;
        *gHolmesStream >> x;
        gPendingResponse = Holmes::kInvalidOpcode;
        EndCmd(Holmes::kTruncateFile);
        return;
    }
}

bool HolmesClientOpen(const char *filename, int mode, unsigned int &fileSize, int &fd) {
    CritSecTracker cst(&gCrit);

    // Handle gHostLogging mode: read/write access checks
    if (gHostLogging) {
        if (mode & 1U) {
            if (!gHolmesStream) {
                return false;
            }
        } else if (!gHostConfig) {
            MILO_FAIL("gHostLogging tried to read file: %s", filename);
        }
    }

    MILO_ASSERT(gHolmesStream, 0x36A);
    if (gHolmesStream->Fail()) {
        return false;
    } else {
        BeginCmd(Holmes::kOpenFile, true);
        unsigned char val = 3;
        *gStreamBuffer << val << filename;
        val = (mode >> 1) & 1;
        *gStreamBuffer << val;
        *gStreamBuffer << (unsigned char)((mode >> 0x12) & 1); // truncate flag
        if (val == 0) {
            *gStreamBuffer << (unsigned char)((mode >> 8) & 1); // write mode
            val = (mode >> 9) & 1; // create flag
            *gStreamBuffer << val;
        }
        HolmesFlushStreamBuffer();
        WaitForResponse(Holmes::kOpenFile);
        int result;
        *gHolmesStream >> result;
        if (result != -1) {
            *gHolmesStream >> fd;
            fileSize = result;
        }
        gPendingResponse = Holmes::kInvalidOpcode;
        EndCmd(Holmes::kOpenFile);
        if (result != -1) {
            return true;
        }
    }
    return false;
}

void HolmesClientWrite(int i1, int i2, int i3, const void *v) {
    if (i3 != 0) {
        CritSecTracker cst(&gCrit);
        MILO_ASSERT(gHolmesStream, 0x395);
        if (!gHolmesStream->Fail() || !gHostLogging) {
            BeginCmd(Holmes::kWriteFile, true);
            *gStreamBuffer << (unsigned char)Holmes::kWriteFile << i1 << i2 << i3;
            gStreamBuffer->Write(v, i3);
            HolmesFlushStreamBuffer();
            WaitForResponse(Holmes::kWriteFile);
            int x;
            *gHolmesStream >> x;
            gPendingResponse = Holmes::kInvalidOpcode;
            EndCmd(Holmes::kWriteFile);
            return;
        }
    }
}

void HolmesClientRead(int i1, int i2, int i3, void *v, File *file) {
    if (i3 != 0) {
        CritSecTracker cst(&gCrit);
        MILO_ASSERT(gHolmesStream, 0x3C7);
        BeginCmd(Holmes::kReadFile, true);
        *gStreamBuffer << (unsigned char)Holmes::kReadFile << i1 << i2 << i3;
        HolmesFlushStreamBuffer();

        ReadRequest req;
        req.mRequestor = file;
        req.mBuffer = v;
        req.mBytes = i3;
        gRequests.push_back(req);
        EndCmd(Holmes::kReadFile);
        return;
    }
}

bool HolmesClientReadDone(File *f) {
    CritSecTracker cst(&gCrit);
    bool ret = PendingRead(f);
    if (ret) {
        HolmesClientPoll();
        ret = PendingRead(f);
    }
    return !ret;
}

void HolmesClientStackTrace(const char *cc, struct StackData *stack, int i, String &ret) {
    ret = "";
    CritSecTracker cst(&gCrit);
    if (!gHolmesStream || gHolmesStream->Fail()) {
        return;
    }
    BeginCmd(Holmes::kStackTrace, true);
    *gStreamBuffer << u8(Holmes::kStackTrace);
    *gStreamBuffer << cc;
    *gStreamBuffer << i;
    int j;
    for (j = 0; j < i; j++) {
        *gStreamBuffer << stack->mFailThreadStack[j];
    }
    HolmesFlushStreamBuffer();
    gStackTraced = true;
    WaitForResponse(Holmes::kStackTrace);
    *gHolmesStream >> ret;
    gPendingResponse = Holmes::kInvalidOpcode;
    EndCmd(Holmes::kStackTrace);
}

void HolmesClientSendMessage(const Message &msg) {
    DataNode dn(msg);
    CritSecTracker cst(&gCrit);
    if (gHolmesStream && !gHolmesStream->Fail()) {
        BeginCmd(Holmes::kSendMessage, true);
        *gStreamBuffer << u8(Holmes::kSendMessage) << dn;
        HolmesFlushStreamBuffer();
        WaitForResponse(Holmes::kSendMessage);
        unsigned char ret;
        *gHolmesStream >> ret;
        gPendingResponse = Holmes::kInvalidOpcode;
        EndCmd(Holmes::kSendMessage);
        return;
    }
}

void HolmesClientClose(File *file, int handle) {
    CritSecTracker cst(&gCrit);

    BeginCmd(Holmes::kCloseFile, true);
    MILO_ASSERT(gHolmesStream, 1012);

    if (PendingRead(file)) {
        WaitForReads();
    }

    *gStreamBuffer << u8(Holmes::kCloseFile) << handle;
    HolmesFlushStreamBuffer();
    EndCmd(Holmes::kCloseFile);
}

void HolmesClientEnumerate(
    const char *path,
    void (*callback)(const char *, const char *),
    bool recurse,
    const char *ext,
    bool dirs
) {
    CritSecTracker cst(&gCrit);
    BeginCmd(Holmes::kEnumerate, true);

    *gStreamBuffer << u8(Holmes::kEnumerate);
    BinStream &bs = *gStreamBuffer << path;
    bs << u8(recurse);
    BinStream &bs2 = bs << ext;
    bs2 << u8(dirs);
    HolmesFlushStreamBuffer();

    std::vector<RecurseInfo> entries;
    WaitForResponse(Holmes::kEnumerate);

    while (true) {
        bool more;
        *gHolmesStream >> more;
        if (!more)
            break;
        entries.push_back(RecurseInfo());
        *gHolmesStream >> entries.back().mDir >> entries.back().mFile;
    }

    gPendingResponse = Holmes::kInvalidOpcode;

    for (unsigned int i = 0; i < entries.size(); i++) {
        callback(entries[i].mDir.c_str(), entries[i].mFile.c_str());
    }

    EndCmd(Holmes::kEnumerate);
}

bool CanUseHolmes(int p1) {
    if (!UsingCD())
        return true;

    if (gHostConfig != false && (p1 & 2U) != 0)
        return true;

    if (gHostLogging != false && (p1 & 1U) != 0)
        return true;

    return false;
}

void HolmesToLocal(char *p1, const char *p2) {
    String temp;
    temp = HolmesXboxPath(gServerName.c_str(), p2);

    const char *src = temp.c_str();
    char *dst = p1;

    s8 c;
    do {
        c = *src;
        *dst = c;
        src++;
        dst++;
    } while (c != 0);
}

char const *HolmesFileHostName() { return gMachineName; }

void HolmesClientPoll() {
    CritSecTracker cst(&gCrit);

    if (!gHolmesStream)
        return;

    gPollStreamEof = false;
    HolmesClientPollInternal(true);
}

bool HolmesClientCacheFile(char *arg0, const char *arg1) {
    CritSecTracker cst(&gCrit);
    AutoSlowFrame slow("HolmesClientCacheFile", 25.0f);

    BeginCmd(Holmes::kCacheFile, true);

    String str(arg1);
    HolmesToLocal(arg0, arg1);

    if (*arg0 == 0) {
        EndCmd(Holmes::kCacheFile);
        return false;
    }

    u8 fileInfo[0x20];
    int attrResult = GetFileAttributesExA(arg0, (GET_FILEEX_INFO_LEVELS)0, fileInfo);
    bool result = false;
    bool fileExists = (attrResult - 1) != (-1);
    if ((str != gLastCachedResource) && (gLastCacheResult > 0 || fileExists)) {
        EndCmd(Holmes::kCacheFile);
        return true;
    }

    u8 cmd = Holmes::kCacheFile;
    gStreamBuffer->Write(&cmd, 1);
    *gStreamBuffer << str;

    u8 hasFileFlag = fileExists;
    gStreamBuffer->Write(&hasFileFlag, 1);

    if (fileExists) {
        gStreamBuffer->WriteEndian(&*(s64*)(fileInfo + 0x14), 8);
    }

    HolmesFlushStreamBuffer();
    WaitForResponse(Holmes::kCacheFile);

    u8 response = 0;
    *gHolmesStream >> response;
    gPendingResponse = Holmes::kInvalidOpcode;

    if (response != 0) {
        result = true;
    }

    EndCmd(Holmes::kCacheFile);
    return result;
}

#ifdef HX_NATIVE
void HolmesClientPrint(const char *) {
    // Holmes remote debug not available on native
}
#endif
