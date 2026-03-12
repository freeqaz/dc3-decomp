// DC3 Web Port — XDK Shims (Emscripten)
// Replaces xdk_shims.cpp — no pthreads, simplified critical sections.
// On single-threaded WASM, critical sections are no-ops.

#ifdef __EMSCRIPTEN__

#include "xdk/XBOXKRNL.h"
#include "xdk/XAPILIB.h"
#include "xdk/XGRAPHICS.h"
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
// Semaphore
// ============================================================================

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
