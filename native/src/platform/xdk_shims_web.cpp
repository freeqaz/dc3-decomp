// DC3 Web Port — XDK Shims (Emscripten)
// Replaces xdk_shims.cpp — no pthreads, simplified critical sections.
// On single-threaded WASM, critical sections are no-ops.

#ifdef __EMSCRIPTEN__

#include "xdk/XBOXKRNL.h"
#include "xdk/XAPILIB.h"
#include "xdk/XGRAPHICS.h"
#include "xdk/d3d9i/d3d9.h"
#include "os/Debug.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <vector>

// Windows constants not defined in XDK headers
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef CREATE_SUSPENDED
#define CREATE_SUSPENDED 0x00000004
#endif

// All Win32 / XDK stubs must be extern "C" to match the header declarations.
// Without this, WINAPI (__stdcall) on the definition causes the compiler to
// treat them as separate C++ overloads instead of matching the extern "C" decls.
extern "C" {

// ============================================================================
// Critical Section — no-ops on single-threaded WASM
// ============================================================================

void RtlInitializeCriticalSection(RTL_CRITICAL_SECTION *cs) {
    if (cs) memset(cs, 0, sizeof(*cs));
}

void RtlEnterCriticalSection(RTL_CRITICAL_SECTION *cs) {}
void RtlLeaveCriticalSection(RTL_CRITICAL_SECTION *cs) {}
void RtlDeleteCriticalSection(RTL_CRITICAL_SECTION *cs) {}

int RtlTryEnterCriticalSection(RTL_CRITICAL_SECTION *cs) {
    return 1;
}

// ============================================================================
// Handle table — minimal stub (C++ internals, but C-linkage API wrappers)
// ============================================================================

enum HandleType {
    HANDLE_EVENT = 0, HANDLE_THREAD, HANDLE_MUTEX, HANDLE_SEMAPHORE,
    HANDLE_FILE, HANDLE_TIMER, HANDLE_NOTIFICATION, HANDLE_FILE_FIND,
};

} // extern "C" — pause for C++ map/vector usage

struct HandleEntry {
    HandleType type;
    void *data;
};

static HANDLE sNextHandle = (HANDLE)0x100;

// Use function-local static to avoid static initialization order fiasco —
// CreateEventA can be called from global constructors before file-scope
// statics in this TU are initialized.
static std::map<HANDLE, HandleEntry> &GetHandles() {
    static std::map<HANDLE, HandleEntry> sHandles;
    return sHandles;
}

static HANDLE AllocHandle(HandleType type, void *data = nullptr) {
    HANDLE h = sNextHandle;
    sNextHandle = (HANDLE)((uintptr_t)sNextHandle + 1);
    GetHandles()[h] = {type, data};
    return h;
}

extern "C" {

BOOL CloseHandle(HANDLE h) {
    auto &handles = GetHandles();
    auto it = handles.find(h);
    if (it != handles.end()) {
        handles.erase(it);
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Event objects — stubs
// ============================================================================

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCSTR name) {
    return AllocHandle(HANDLE_EVENT);
}

BOOL SetEvent(HANDLE h) { return TRUE; }
BOOL ResetEvent(HANDLE h) { return TRUE; }

// ============================================================================
// Semaphore — no-ops on single-threaded WASM
// ============================================================================

HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR) {
    return AllocHandle(HANDLE_SEMAPHORE);
}

BOOL ReleaseSemaphore(HANDLE, LONG, LPLONG) { return TRUE; }

// ============================================================================
// Thread stubs — no threading in browser WASM
// ============================================================================

HANDLE CreateThread(LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE fn, LPVOID param,
                    DWORD flags, LPDWORD tid) {
    if (tid) *tid = 1;
    // Execute synchronously in single-threaded WASM
    if (fn && !(flags & CREATE_SUSPENDED)) {
        fn(param);
    }
    return AllocHandle(HANDLE_THREAD);
}

DWORD WaitForSingleObject(HANDLE h, DWORD timeout) { return 0; }
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD t) { return 0; }
BOOL GetExitCodeThread(HANDLE h, LPDWORD code) { if (code) *code = 0; return TRUE; }
DWORD ResumeThread(HANDLE h) { return 1; }
DWORD SuspendThread(HANDLE h) { return 0; }
void Sleep(DWORD ms) {}  // Can't sleep on main thread in browser

// ============================================================================
// Timing
// ============================================================================

BOOL QueryPerformanceCounter(LARGE_INTEGER *lpCounter) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpCounter->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    lpFrequency->QuadPart = 1000000000LL;  // nanoseconds
    return TRUE;
}

DWORD GetTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void GetSystemTimeAsFileTime(FILETIME *ft) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long t = ((long long)ts.tv_sec + 11644473600LL) * 10000000LL + ts.tv_nsec / 100;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
}

// ============================================================================
// Memory — passthrough to WASM heap
// ============================================================================

LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD protect) {
    return malloc(size);
}

BOOL VirtualFree(LPVOID addr, SIZE_T size, DWORD type) {
    free(addr);
    return TRUE;
}

// ============================================================================
// String / misc
// ============================================================================

int lstrlenA(const char *s) { return s ? (int)strlen(s) : 0; }
int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int cb,
                        wchar_t *ws, int cch) {
    if (!s) return 0;
    int len = (cb < 0) ? (int)strlen(s) + 1 : cb;
    if (!ws || cch == 0) return len;
    for (int i = 0; i < len && i < cch; i++) ws[i] = (wchar_t)(unsigned char)s[i];
    return len;
}

void OutputDebugStringA(const char *s) {
    if (s) fprintf(stderr, "[XDK] %s", s);
}

// GetLastError provided by engine_stubs_generated.cpp
void SetLastError(DWORD err) {}
DWORD GetCurrentThreadId() { return 1; }

// ============================================================================
// XGraphics stubs
// ============================================================================

HRESULT XGCopySurface(void* dst, void* rect, int w, int h,
                      int fmt, void* data1, int data2, void* src, void* srect,
                      int filter, float r) {
    return 0;
}

// ============================================================================
// Xbox-specific API stubs
// ============================================================================

DWORD XCancelOverlapped(XOVERLAPPED *) { return 0; }
DWORD XUserAwardGamerPicture(DWORD, DWORD, DWORD, XOVERLAPPED *) { return 0; }
DWORD XUserAwardAvatarAssets(DWORD, const XUSER_AVATARASSET *, XOVERLAPPED *) { return 0; }
DWORD XUserGetXUID(DWORD, XUID *) { return 0; }
DWORD XShowNuiGuideUI(DWORD) { return 0; }
void GetLocalTime(LPSYSTEMTIME) {}
VOID GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer) {
    if (lpBuffer) memset(lpBuffer, 0, sizeof(*lpBuffer));
}
DWORD XGetOverlappedExtendedError(XOVERLAPPED *) { return 0; }
DWORD XGetOverlappedResult(XOVERLAPPED *, DWORD *pdwResult, BOOL) {
    if (pdwResult) *pdwResult = 0;
    return 0;
}

// ============================================================================
// Physical memory — passthrough to WASM heap (same as VirtualAlloc)
// ============================================================================

LPVOID XPhysicalAlloc(SIZE_T dwSize, ULONG_PTR, ULONG_PTR ulAlignment, DWORD) {
    void *ptr = nullptr;
    if (ulAlignment > sizeof(void *)) {
        posix_memalign(&ptr, ulAlignment, dwSize);
    } else {
        ptr = malloc(dwSize);
    }
    return ptr;
}

VOID XPhysicalFree(LPVOID lpAddress) { free(lpAddress); }
DWORD XPhysicalSize(LPVOID) { return 0; }

// ============================================================================
// Time — GetSystemTime (UTC)
// ============================================================================

void GetSystemTime(LPSYSTEMTIME lpSystemTime) {
    if (!lpSystemTime) return;
    time_t now = time(nullptr);
    struct tm *utc = gmtime(&now);
    if (utc) {
        lpSystemTime->wYear = utc->tm_year + 1900;
        lpSystemTime->wMonth = utc->tm_mon + 1;
        lpSystemTime->wDayOfWeek = utc->tm_wday;
        lpSystemTime->wDay = utc->tm_mday;
        lpSystemTime->wHour = utc->tm_hour;
        lpSystemTime->wMinute = utc->tm_min;
        lpSystemTime->wSecond = utc->tm_sec;
        lpSystemTime->wMilliseconds = 0;
    } else {
        memset(lpSystemTime, 0, sizeof(*lpSystemTime));
    }
}

// ============================================================================
// Timezone
// ============================================================================

DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION *lpTimeZoneInformation) {
    if (lpTimeZoneInformation) memset(lpTimeZoneInformation, 0, sizeof(*lpTimeZoneInformation));
    return 0;
}

// ============================================================================
// D3D resource stubs — no GPU resources to release on web (WebGPU handles its own)
// ============================================================================

ULONG D3DResource_Release(struct D3DResource *) { return 0; }
VOID D3DCubeTexture_UnlockRect(struct D3DCubeTexture *, D3DCUBEMAP_FACES, UINT) {}
HRESULT DmCaptureStackBackTrace(ULONG, VOID *) { return -1; }
BOOL FileTimeToSystemTime(const FILETIME *, LPSYSTEMTIME) { return 0; }
DWORD XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE) { return 0; }
VOID XLaunchNewImage(LPCSTR, DWORD) {}
DWORD XEnumerate(HANDLE, void *, DWORD, DWORD *, XOVERLAPPED *) { return 0; }
DWORD XShowMarketplaceDownloadItemsUI(DWORD, DWORD, ULONGLONG *, DWORD, DWORD *, XOVERLAPPED *) { return 0; }
DWORD XShowNuiTroubleshooterUI() { return 0; }
DWORD XShowTokenRedemptionUI(DWORD) { return 0; }
DWORD XTitleServerCreateEnumerator(const char *, DWORD, DWORD *, HANDLE *) { return 0; }
DWORD XMarketplaceCreateOfferEnumerator(DWORD, DWORD, ULONGLONG, DWORD, DWORD *, HANDLE *) { return 0; }
DWORD XMarketplaceCreateOfferEnumeratorByOffering(DWORD, DWORD, ULONGLONG *, WORD, DWORD *, HANDLE *) { return 0; }
INT XNetConnect(const void *) { return 0; }
DWORD XNetGetConnectStatus(const void *) { return 0; }
DWORD XNetGetTitleXnAddr(void *) { return 0; }
INT XNetUnregisterInAddr(const void *) { return 0; }
INT XNetXnAddrToMachineId(const void *, void *) { return 0; }
HRESULT XNuiDelayUI(ULONG) { return 0; }
HRESULT NuiCameraGetProperty(int, void *, DWORD) { return -1; }
HRESULT NuiCameraGetPropertyF(int, float *, DWORD) { return -1; }
HRESULT NuiCameraGetExposureRegionOfInterest(int, void *) { return -1; }
HRESULT NuiCameraSetProperty(int, void *, DWORD) { return -1; }
HRESULT NuiCameraSetExposureRegionOfInterest(int, const void *) { return -1; }
HRESULT NuiFitnessStartTracking(DWORD, DWORD, void *) { return -1; }
HRESULT NuiFitnessPauseTracking(DWORD) { return -1; }
HRESULT NuiFitnessResumeTracking(DWORD, DWORD) { return -1; }
HRESULT NuiFitnessStopTracking(DWORD) { return -1; }
HRESULT NuiFitnessGetCurrentFitnessData(DWORD, void *) { return -1; }
HRESULT NuiIdentityAbort() { return -1; }
HRESULT NuiSkeletonSetTrackedSkeletons(DWORD *) { return -1; }
HRESULT NuiSpeechCreateGrammar(ULONG, void *) { return -1; }

// ============================================================================
// String conversion
// ============================================================================

int WideCharToMultiByte(unsigned int cp, DWORD flags, const wchar_t *wideStr,
                        int cchWideChar, char *multiByteStr, int cbMultiByte,
                        const char *defaultChar, int *usedDefaultChar) {
    if (!wideStr) return 0;
    int len = (cchWideChar < 0) ? (int)wcslen(wideStr) + 1 : cchWideChar;
    if (!multiByteStr || cbMultiByte == 0) return len;
    for (int i = 0; i < len && i < cbMultiByte; i++)
        multiByteStr[i] = (char)(unsigned char)wideStr[i];
    return len;
}

} // extern "C"

// ============================================================================
// WebSvcMgr stub — replaces curl-based networking (not available in WASM)
// ============================================================================

#include "net/WebSvcMgr.h"

class WebSvcMgrStub : public WebSvcMgr {
public:
    bool DoRequest(ReqType, unsigned int, unsigned short, const char*,
                   const char*, unsigned int, const char*, unsigned int) override { return false; }
    bool InitRequest(WebSvcRequest*, ReqType, const char*, unsigned short,
                     const char*, unsigned int) override { return false; }
    bool InitRequest(WebSvcRequest*, ReqType, unsigned int, unsigned short,
                     const char*, unsigned int) override { return false; }
};
static WebSvcMgrStub gWebSvcMgrStub;
WebSvcMgr &TheWebSvcMgr = gWebSvcMgrStub;

#endif // __EMSCRIPTEN__
