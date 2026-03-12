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

// ============================================================================
// Critical Section — no-ops on single-threaded WASM
// ============================================================================

void WINAPI RtlInitializeCriticalSection(RTL_CRITICAL_SECTION *cs) {
    if (cs) memset(cs, 0, sizeof(*cs));
}

void WINAPI RtlEnterCriticalSection(RTL_CRITICAL_SECTION *cs) {}
void WINAPI RtlLeaveCriticalSection(RTL_CRITICAL_SECTION *cs) {}
void WINAPI RtlDeleteCriticalSection(RTL_CRITICAL_SECTION *cs) {}

BOOLEAN WINAPI RtlTryEnterCriticalSection(RTL_CRITICAL_SECTION *cs) {
    return TRUE;
}

// ============================================================================
// Handle table — minimal stub
// ============================================================================

enum HandleType {
    HANDLE_EVENT = 0, HANDLE_THREAD, HANDLE_MUTEX, HANDLE_SEMAPHORE,
    HANDLE_FILE, HANDLE_TIMER, HANDLE_NOTIFICATION, HANDLE_FILE_FIND,
};

struct HandleEntry {
    HandleType type;
    void *data;
};

static std::map<HANDLE, HandleEntry> sHandles;
static HANDLE sNextHandle = (HANDLE)0x100;

static HANDLE AllocHandle(HandleType type, void *data = nullptr) {
    HANDLE h = sNextHandle;
    sNextHandle = (HANDLE)((uintptr_t)sNextHandle + 1);
    sHandles[h] = {type, data};
    return h;
}

BOOL WINAPI CloseHandle(HANDLE h) {
    auto it = sHandles.find(h);
    if (it != sHandles.end()) {
        sHandles.erase(it);
        return TRUE;
    }
    return FALSE;
}

// ============================================================================
// Event objects — stubs
// ============================================================================

HANDLE WINAPI CreateEventA(void *, BOOL manual, BOOL initial, const char *name) {
    return AllocHandle(HANDLE_EVENT);
}

BOOL WINAPI SetEvent(HANDLE h) { return TRUE; }
BOOL WINAPI ResetEvent(HANDLE h) { return TRUE; }

// ============================================================================
// Thread stubs — no threading in browser WASM
// ============================================================================

HANDLE WINAPI CreateThread(void *, DWORD, LPTHREAD_START_ROUTINE fn, LPVOID param,
                           DWORD flags, LPDWORD tid) {
    if (tid) *tid = 1;
    // Execute synchronously in single-threaded WASM
    if (fn && !(flags & CREATE_SUSPENDED)) {
        fn(param);
    }
    return AllocHandle(HANDLE_THREAD);
}

DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD timeout) { return 0; }
DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD t) { return 0; }
BOOL WINAPI GetExitCodeThread(HANDLE h, LPDWORD code) { if (code) *code = 0; return TRUE; }
DWORD WINAPI ResumeThread(HANDLE h) { return 1; }
DWORD WINAPI SuspendThread(HANDLE h) { return 0; }
void WINAPI Sleep(DWORD ms) {}  // Can't sleep on main thread in browser

// ============================================================================
// Timing
// ============================================================================

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpCounter) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpCounter->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    lpFrequency->QuadPart = 1000000000LL;  // nanoseconds
    return TRUE;
}

DWORD WINAPI GetTickCount() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void WINAPI GetSystemTimeAsFileTime(FILETIME *ft) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long t = ((long long)ts.tv_sec + 11644473600LL) * 10000000LL + ts.tv_nsec / 100;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
}

// ============================================================================
// Memory — passthrough to WASM heap
// ============================================================================

LPVOID WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD protect) {
    return malloc(size);
}

BOOL WINAPI VirtualFree(LPVOID addr, SIZE_T size, DWORD type) {
    free(addr);
    return TRUE;
}

// ============================================================================
// String / misc
// ============================================================================

int WINAPI lstrlenA(const char *s) { return s ? (int)strlen(s) : 0; }
int WINAPI MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int cb,
                               wchar_t *ws, int cch) {
    if (!s) return 0;
    int len = (cb < 0) ? (int)strlen(s) + 1 : cb;
    if (!ws || cch == 0) return len;
    for (int i = 0; i < len && i < cch; i++) ws[i] = (wchar_t)(unsigned char)s[i];
    return len;
}

void WINAPI OutputDebugStringA(const char *s) {
    if (s) fprintf(stderr, "[XDK] %s", s);
}

DWORD WINAPI GetLastError() { return 0; }
void WINAPI SetLastError(DWORD err) {}

// ============================================================================
// XGraphics stubs
// ============================================================================

HRESULT WINAPI XGCopySurface(void* dst, void* rect, int w, int h,
                             int fmt, void* data1, int data2, void* src, void* srect,
                             int filter, float r) {
    return 0;
}

#endif // __EMSCRIPTEN__
