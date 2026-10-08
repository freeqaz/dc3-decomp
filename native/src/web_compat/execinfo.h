#pragma once
// dc3-web only (on the dc3-web include path, nowhere else): Emscripten's libc
// has no <execinfo.h>. The decomp's native-only diagnostics (RefAudit in
// obj/Object.cpp, the task-lifetime audit in obj/Task.cpp, Debug.cpp's
// MILO_BT) capture glibc backtraces; on web they record zero frames, and every
// caller already treats depth 0 / a null symbol table as "no trace".
#ifdef __cplusplus
extern "C" {
#endif
static inline int backtrace(void **, int) { return 0; }
static inline char **backtrace_symbols(void *const *, int) { return 0; }
static inline void backtrace_symbols_fd(void *const *, int, int) {}
#ifdef __cplusplus
}
#endif
