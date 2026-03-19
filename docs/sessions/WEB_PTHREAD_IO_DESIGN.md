# Web Build: Pthread-Based File I/O Design Analysis

**Date**: 2026-03-19
**Status**: Research complete -- recommendation against pure pthread approach; JSPI recommended instead
**Related**: [WEB_ASYNCIFY_EXPERIMENT.md](WEB_ASYNCIFY_EXPERIMENT.md) (current ASYNCIFY approach)

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Emscripten Pthread Capabilities](#emscripten-pthread-capabilities)
3. [Current File I/O Architecture](#current-file-io-architecture)
4. [MEMFS vs WASMFS Analysis](#memfs-vs-wasmfs-analysis)
5. [Proposed Pthread Architecture](#proposed-pthread-architecture)
6. [Integration Points](#integration-points)
7. [Browser Compatibility Matrix](#browser-compatibility-matrix)
8. [Build System Changes](#build-system-changes)
9. [Tradeoff Analysis](#tradeoff-analysis)
10. [Complexity Estimate and Risks](#complexity-estimate-and-risks)
11. [Recommendation](#recommendation)

---

## Problem Statement

The DC3 web build loads `.milo_xbox` assets on-demand via HTTP fetches. The engine's
loading pipeline has multiple blocking spin loops that freeze the browser:

```
AsyncFile::Init()      line 282:  while (!_OpenDone()) ;
AsyncFile::Read()      line  92:  while (!ReadDone(iBytes)) ;
AsyncFile::Init()      line 290:  while (!_ReadDone()) ;
AsyncFile::Flush()     line 200:  while (!_WriteDone()) ;
LoadMgr::PollUntilLoaded()  line 421:  while (!ldr1->IsLoaded()) { ... }
```

The current solution uses ASYNCIFY (`-sASYNCIFY=1`), which instruments the entire
WASM binary so that `emscripten_sleep()` can unwind/rewind the call stack. This works
but has significant downsides:

- **~50% code size overhead** from Asyncify instrumentation
- **~50% runtime overhead** on instrumented call paths
- **262KB async stack** (`ASYNCIFY_STACK_SIZE`) consumed per yield point
- **Fragile**: any call path not in the ASYNCIFY allow-list silently blocks

The question: can pthreads provide a better alternative?

---

## Emscripten Pthread Capabilities

### How `-pthread` Works

Emscripten's `-pthread` flag (compile + link) enables POSIX threads via Web Workers:

- Each `pthread_create()` spawns a Web Worker that shares the same `WebAssembly.Memory`
  (backed by `SharedArrayBuffer`)
- Workers share the same linear memory -- globals, heap, and stack are shared
- Standard C/C++ atomics, mutexes, and condition variables work correctly
- Detected via `#ifdef __EMSCRIPTEN_PTHREADS__`

### SharedArrayBuffer Requirements

SharedArrayBuffer requires **cross-origin isolation** -- two HTTP headers on every response:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

**Already configured** in `native/web/server.py` (lines 35-36). These headers were
added proactively ("future pthreads support" per the comment on line 34).

### Worker Thread Pool

`-sPTHREAD_POOL_SIZE=N` pre-creates N workers before `main()` runs. This avoids the
latency of dynamic worker creation (which is always asynchronous). For a single I/O
thread, `-sPTHREAD_POOL_SIZE=1` suffices.

### Main Thread Blocking Restrictions

The main browser thread **cannot** use `Atomics.wait()`. This means:

- `pthread_mutex_lock()` on the main thread uses **busy-wait** (spin loop)
- `pthread_join()` on the main thread uses **busy-wait**
- `pthread_cond_wait()` on the main thread uses **busy-wait**

This is acceptable for short waits (checking a flag) but defeats the purpose if the
main thread needs to wait for I/O completion -- it would spin just like the current
blocking XHR.

### `PROXY_TO_PTHREAD`

`-sPROXY_TO_PTHREAD` moves `main()` to a worker thread and leaves the browser main
thread as a message proxy. This allows `main()` to use proper blocking synchronization.
However, this creates complications with WebGPU and canvas access (see Risks).

### Synchronous Fetch from Workers

The `emscripten_fetch()` API is callable from worker threads. With `-pthread` enabled:

- `EMSCRIPTEN_FETCH_SYNCHRONOUS` works on **pthreads** (blocks the worker, not the main thread)
- The main thread still cannot use synchronous fetch
- Without `-pthread`, only async fetch works

This is the key capability: **a worker thread can do blocking HTTP fetches without
freezing the browser**.

### Synchronous XHR from Workers

Web Workers have always supported synchronous `XMLHttpRequest`. No special Emscripten
support needed -- the web platform itself allows this. This is simpler than
`emscripten_fetch()` for our use case.

### Code Size Implications

Unlike ASYNCIFY, `-pthread` does **not** instrument the WASM binary. The overhead is:

- Small JS glue code for worker management (~10-20KB)
- `SharedArrayBuffer`-aware memory management
- If using `mimalloc` (`-sMALLOC=mimalloc`) for thread-safe allocation: larger allocator code
- Overall: **far less than ASYNCIFY's ~50% code size increase**

### Memory Growth Compatibility

`-pthread` + `-sALLOW_MEMORY_GROWTH=1` works but has a caveat: JS code accessing
WASM memory must handle memory view invalidation when the memory grows. This is
already a concern in our build (we use `ALLOW_MEMORY_GROWTH=1`). Emscripten handles
this automatically for its own JS glue, but custom JS code (like `EM_ASM` blocks)
needs care.

---

## Current File I/O Architecture

### Engine Loading Pipeline

```
LoadMgr::Poll()           -- called each frame from App::RunOneFrame()
  -> PollFrontLoader()
    -> Loader::PollLoading()  -- state machine per loader
      -> FileLoader::OpenFile()
        -> AsyncFile::New()   -- creates AsyncFileNative, calls Init()
          -> _OpenAsync()     -- fopen() + possibly WebAssetsFetchSync()
          -> while (!_OpenDone()) ;   <-- SPIN LOOP (always returns true on native)
      -> FileLoader::LoadFile()
        -> mFile->ReadDone()  -- polls until read complete
```

### AsyncFileNative (native/src/platform/AsyncFile_Native.cpp)

The native port's AsyncFile is **fully synchronous**:

- `_OpenAsync()` calls `fopen()` directly, optionally calling `WebAssetsFetchSync()` to
  download the file from the server first
- `_OpenDone()` always returns `true`
- `_ReadAsync()` calls `fread()` directly
- `_ReadDone()` always returns `true`

The "async" interface is a fiction -- everything completes immediately. The base class
spin loops (`while (!_OpenDone())`) execute exactly once.

### Xbox AsyncFileWin (src/system/os/AsyncFile_Win.h)

The original Xbox implementation has **true async**:

- `_ReadAsync()` starts an overlapped read via Windows API
- `_ReadDone()` returns `false` while `mReadInProgress` is set
- The `LoadMgr::Poll()` loop genuinely polls over multiple frames

This is the design the engine was built for. Each frame, the loader polls once, does
a small amount of work, and returns control to the game loop.

### WebAssets (native/src/platform/WebAssets.cpp)

Two code paths, selected at build time:

1. **DC3_WEB_ASYNCIFY** (current default): `WebAssetsFetchSync()` starts an async
   `emscripten_fetch()` and yields via `emscripten_sleep(16)` until the callback
   fires. Requires ASYNCIFY instrumentation.

2. **Non-ASYNCIFY fallback**: Synchronous XHR via `EM_ASM` inline JS. Blocks the
   main thread completely.

### ThreadCall (native/src/platform/ThreadCall_Native.cpp)

Already has `#ifdef __EMSCRIPTEN__` guards that make threading single-threaded
(synchronous execution in `ThreadCallPoll()`). The XDK shims for web
(`xdk_shims_web.cpp`) also make critical sections no-ops. Both would need updates
for a threaded approach.

---

## MEMFS vs WASMFS Analysis

### MEMFS (Current)

MEMFS is Emscripten's default in-memory filesystem. It is implemented in JavaScript
and has significant threading limitations:

- **Not thread-safe**: All filesystem operations are proxied to the main thread in
  pthreads builds. This means `fopen()` / `fread()` from a worker thread will
  block until the main thread handles the proxied call.
- **Proxying cost**: Each file operation requires a synchronous round-trip to the
  main thread via `Atomics.wait()` / `Atomics.notify()`. This adds latency and
  partially defeats the purpose of offloading I/O to a worker.
- **Main thread must be responsive**: If the main thread is busy (rendering, etc.),
  proxied filesystem calls from workers stall.

**Impact on proposed design**: A worker thread doing `WebAssetsFetchSync()` followed
by `fopen()`/`fwrite()` to write to MEMFS would have the write proxied back to the
main thread anyway. The fetch would be non-blocking, but the MEMFS write would block
on main-thread availability.

### WASMFS

WASMFS is Emscripten's newer filesystem implementation, written in C++ (not JS):

- **Thread-safe by design**: Built for multi-threaded access without proxying
- **OPFS backend**: Can use the browser's Origin Private File System for persistent,
  synchronous access from workers via `FileSystemSyncAccessHandle`
- **In-memory backend**: Can also work as a memory filesystem (like MEMFS) but
  thread-safe
- **Enabled via**: `-sWASMFS=1`

**OPFS capabilities** (relevant for persistent caching):
- `FileSystemSyncAccessHandle` provides synchronous `read()`, `write()`, `getSize()`,
  `truncate()`, `flush()`, `close()` -- all usable from Web Workers only
- Browser support: Chrome 102+, Firefox 111+, Safari 15.2+, Edge 86+
- Performance: "highly optimized" -- avoids promise overhead, direct in-place access

**Migration concerns**:
- WASMFS may not support all MEMFS features (e.g., `FS.writeFile()` from JS)
- The current `WebAssets.cpp` uses `FS.mkdir()` and `FS.writeFile()` in `EM_ASM`
  blocks -- these would need to change
- Bundle unpacking (`onBundleSuccess`) writes files from C callbacks -- should work
  with WASMFS
- WASMFS is still marked as relatively new in Emscripten; potential rough edges

### Verdict

**WASMFS is required** for a clean pthread-based design. Without it, MEMFS proxying
negates much of the benefit of worker-thread I/O. However, WASMFS adds migration risk.

---

## Proposed Pthread Architecture

### Option A: Single I/O Worker Thread (Recommended if pursuing pthreads)

```
Main Thread (browser)              I/O Worker Thread
========================           ==========================
App::RunOneFrame()                 io_worker_main():
  LoadMgr::Poll()                    while (!terminate):
    FileLoader::OpenFile()             wait on request_cond
      AsyncFile::New()                 req = dequeue(request_queue)
        _OpenAsync()                   // Synchronous XHR (OK on worker!)
          submit_io_request(path)      xhr.open("GET", url, false)
          return                       xhr.send()
        // _OpenDone() checks flag     // Write to WASMFS (thread-safe)
                                       fopen(path, "wb")
    FileLoader::LoadFile()             fwrite(data, size, 1, f)
      ReadDone() -> checks flag        fclose(f)
      // returns false while I/O       // Open file for engine
        pending                        FILE *fp = fopen(path, "rb")
      // returns true when done        // Signal completion
                                       atomic_store(&req.done, true)
```

**Communication mechanism**:

```cpp
struct IORequest {
    std::string path;           // MEMFS path to open
    std::atomic<bool> done;     // Set by worker when complete
    std::atomic<bool> success;  // Whether fetch + open succeeded
    FILE *fp;                   // Opened file handle (if WASMFS, thread-safe)
    int fileSize;               // Size of opened file
};

// Queue protected by mutex + condition variable
std::queue<IORequest*> gIOQueue;
pthread_mutex_t gIOMutex;
pthread_cond_t gIOCond;
```

**AsyncFileNative changes**:

```cpp
class AsyncFileNative : public AsyncFile {
protected:
    void _OpenAsync() override {
        // Submit request to I/O worker
        mIORequest = new IORequest{mFilename.c_str(), false, false, nullptr, 0};
        pthread_mutex_lock(&gIOMutex);
        gIOQueue.push(mIORequest);
        pthread_cond_signal(&gIOCond);
        pthread_mutex_unlock(&gIOMutex);
    }

    bool _OpenDone() override {
        // Non-blocking check -- called each frame from LoadMgr::Poll()
        return mIORequest->done.load(std::memory_order_acquire);
    }

    void _ReadAsync(void *buf, int size) override {
        // Once file is open, reads can be synchronous (WASMFS is thread-safe)
        // OR submit another request to the worker
        if (mFp) fread(buf, 1, size, mFp);
    }

    bool _ReadDone() override { return true; } // reads are synchronous once open
};
```

### Option B: Per-File Threads

Not recommended. Each `.milo_xbox` load would spawn a thread (Web Worker). Worker
creation is expensive (~5-10ms each), and the engine can have 20+ files loading
concurrently. The thread pool would be unmanageable.

### Option C: Thread Pool

Overcomplicated for this use case. The engine's loader is fundamentally single-file
sequential (one front loader at a time in `PollFrontLoader()`). A pool of N workers
would be underutilized. A single I/O worker suffices.

---

## Integration Points

### 1. AsyncFile_Native.cpp

The primary change. Must split `_OpenAsync()` into:
- Submit fetch+open request to worker (non-blocking)
- Return immediately

And make `_OpenDone()` return `false` until the worker signals completion.

### 2. WebAssets.cpp

Would change from main-thread fetch to worker-thread fetch. The `WebAssetsFetchSync()`
function would move to the I/O worker, where synchronous XHR is perfectly fine.
Alternatively, `emscripten_fetch()` with `EMSCRIPTEN_FETCH_SYNCHRONOUS` works on
worker threads with `-pthread`.

### 3. xdk_shims_web.cpp

Critical sections are currently no-ops. If any shared state is accessed from both
the main thread and I/O worker, these would need real locking. However, the proposed
design uses atomics for the request/response handoff, minimizing the need for
heavyweight synchronization.

### 4. ThreadCall_Native.cpp

Currently disabled for `__EMSCRIPTEN__`. Could potentially use the same I/O worker
for `ThreadCall` work items, but this is orthogonal to file I/O.

### 5. main_web.cpp

The boot state machine wouldn't change much. The bundle download (boot-time) would
still use async `emscripten_fetch()` on the main thread. Only on-demand `.milo_xbox`
loading during gameplay would use the I/O worker.

### 6. LoadMgr::PollUntilLoaded()

Currently has a web-specific safety valve (`maxIter = 10000`). With true async I/O,
this would need adjustment -- the loop would actually poll over multiple frames
instead of completing synchronously. The existing engine design already supports
this (it was built for Xbox's async I/O).

### 7. ChunkStream

`ChunkStream::ReadChunkAsync()` and `DecompressChunkAsync()` interact with the file
I/O layer. These should work without changes if the `AsyncFile` interface contract
is maintained (the decompress worker is separate from file I/O).

---

## Browser Compatibility Matrix

| Feature | Chrome | Firefox | Safari | Edge |
|---------|--------|---------|--------|------|
| SharedArrayBuffer | 68+ (91+ requires COOP/COEP) | 79+ (72+ requires COOP/COEP) | 15.2+ | 79+ |
| Cross-Origin Isolation (COOP/COEP) | 91+ | 72+ | 15.2+ | 91+ |
| WebGPU | 113+ | Nightly | Preview | 113+ |
| Web Workers (sync XHR) | All | All | All | All |
| OPFS + SyncAccessHandle | 102+ | 111+ | 15.2+ | 86+ |
| JSPI (no pthreads needed) | **137+** | **139+** | No | 137+ |

**Limiting factor**: WebGPU support (Chrome 113+) is already the most restrictive
requirement. Adding pthreads (SharedArrayBuffer, Chrome 91+) adds no additional
browser restriction beyond what WebGPU already imposes.

**JSPI note**: Chrome 137 and Firefox 139 are very recent (2025-2026). JSPI is now
phase 4 in W3C and available without flags. This is worth noting as an alternative.

---

## Build System Changes

### Required changes to `native/CMakeLists.txt`:

```cmake
# Replace ASYNCIFY with pthreads approach
option(DC3_WEB_PTHREAD_IO "Use pthreads for non-blocking file I/O" ON)

if(DC3_WEB_PTHREAD_IO)
    target_compile_options(dc3-web PRIVATE -pthread)
    target_compile_definitions(dc3-web PRIVATE DC3_WEB_PTHREAD_IO=1)
    target_link_options(dc3-web PRIVATE
        -pthread
        -sPTHREAD_POOL_SIZE=1      # Single I/O worker
        -sWASMFS=1                  # Thread-safe filesystem
    )
endif()
```

### Flags to remove (if replacing ASYNCIFY):

```cmake
# Remove these:
-sASYNCIFY=1
-sASYNCIFY_STACK_SIZE=262144
# Remove DC3_WEB_ASYNCIFY compile definition
```

### Potential incompatibilities:

| Current Flag | Compatible with `-pthread`? | Notes |
|---|---|---|
| `--use-port=emdawnwebgpu` | Unknown | WebGPU from main thread should be fine; not tested with `-pthread` |
| `-sALLOW_MEMORY_GROWTH=1` | Yes with caveats | JS memory view updates needed; Emscripten handles automatically |
| `-sFETCH=1` | Yes | Explicitly supported from pthreads |
| `-sENVIRONMENT=web,node` | Needs `worker` | Must add `worker` to support Web Worker threads |
| `-sWASMFS=1` | Yes | Required for thread-safe filesystem |
| `-sUSE_ZLIB=1` | Yes | Thread-safe |
| `--pre-js` stubs | Yes | Loaded in main thread only |

**Critical**: `-sENVIRONMENT=web,node` must become `-sENVIRONMENT=web,node,worker`
for the Web Worker to be created.

---

## Tradeoff Analysis

### vs ASYNCIFY (current approach)

| Dimension | ASYNCIFY | Pthreads |
|-----------|----------|----------|
| **Code size** | +~50% (whole-program instrumentation) | +~5% (worker glue, shared memory) |
| **Runtime overhead** | +~50% on instrumented paths | Negligible (atomic flag checks) |
| **Complexity** | Low (drop-in `emscripten_sleep`) | Medium (worker thread, mutex, atomics) |
| **Engine changes** | Zero (ASYNCIFY handles unwinding) | Moderate (AsyncFile split open/done) |
| **Filesystem** | MEMFS (current) | WASMFS required (migration) |
| **Browser compat** | All modern browsers | All modern browsers (SharedArrayBuffer) |
| **Debugging** | Hard (stack unwinding makes traces confusing) | Easier (real threads, normal stacks) |
| **WebGPU interaction** | Safe (main thread only) | Safe if WebGPU stays on main thread |
| **ASYNCIFY_STACK_SIZE** | Must tune (currently 262KB) | Not needed |
| **Correctness risk** | Low (well-tested Emscripten feature) | Medium (thread safety, WASMFS maturity) |

### vs Pre-Fetch (download everything at boot)

| Dimension | Pre-Fetch | Pthreads |
|-----------|-----------|----------|
| **Boot time** | Slow (hundreds of MB) | Fast (no change) |
| **Memory** | All assets in MEMFS | On-demand |
| **Implementation** | Simple (manifest + bulk download) | Medium |
| **User experience** | Long initial wait, then smooth | Short boots, brief per-asset pauses |

### vs JSPI (JavaScript Promise Integration)

| Dimension | JSPI | Pthreads |
|-----------|------|----------|
| **Code size** | **Zero overhead** (VM-native) | +~5% |
| **Runtime overhead** | **~1us per suspend** (constant time) | Atomic checks per poll |
| **Complexity** | **Low** (drop-in for ASYNCIFY) | Medium |
| **Engine changes** | **Zero** (same as ASYNCIFY) | Moderate |
| **Filesystem** | MEMFS (current) | WASMFS required |
| **Browser compat** | Chrome 137+, Firefox 139+ | Chrome 91+, Firefox 79+ |
| **Emscripten flag** | `-sJSPI` (was `-sASYNCIFY=2`) | `-pthread` |
| **Maturity** | Phase 4 W3C, shipping in stable Chrome/Firefox | Very mature |
| **Debug experience** | Normal stacks | Real threads, normal stacks |

---

## Complexity Estimate and Risks

### Implementation effort: ~3-5 days

| Task | Effort | Risk |
|------|--------|------|
| WASMFS migration (MEMFS to WASMFS) | 1-2 days | **High** -- EM_ASM FS calls must change, bundle unpacking, directory creation |
| I/O worker thread + request queue | 0.5 day | Low -- straightforward pthreads |
| AsyncFileNative split (open/done) | 0.5 day | Medium -- must not break read buffering |
| xdk_shims_web.cpp real locking | 0.5 day | Low -- minimal shared state |
| CMakeLists.txt flag changes | 0.25 day | Low |
| Testing + debugging | 1-2 days | **High** -- thread bugs are hard to reproduce |

### Risks

1. **WASMFS maturity**: WASMFS is newer and less battle-tested than MEMFS. The
   `FS.writeFile()` / `FS.mkdir()` calls in `EM_ASM` blocks (WebAssets.cpp line 393)
   use the JS filesystem API which may not be available with WASMFS. These would need
   to be rewritten as C `fopen`/`fwrite`/`mkdir` calls.

2. **WebGPU + pthreads interaction**: WebGPU is main-thread-only in current browsers.
   The I/O worker should never touch WebGPU, but shared memory and atomic operations
   could theoretically interfere. Untested combination.

3. **PROXY_TO_PTHREAD is NOT an option**: `PROXY_TO_PTHREAD` moves `main()` to a
   worker, but WebGPU canvas access and `emscripten_set_main_loop` require the main
   browser thread. The engine must stay on the main thread; only I/O goes to a worker.

4. **Race conditions**: If the engine accesses a file via WASMFS while the worker is
   still writing it, corruption could occur. The atomic `done` flag must be checked
   before any engine-side file access.

5. **Main thread polling must be responsive**: The `LoadMgr::Poll()` loop runs each
   frame. If `_OpenDone()` returns false, the loader stays in its current state and
   re-polls next frame. This matches the original Xbox design. However, if any code
   path calls `AsyncFile::Read()` (synchronous -- has `while (!ReadDone())` on
   line 92), it will spin on the main thread waiting for the worker. These call sites
   would need auditing.

6. **`-sALLOW_MEMORY_GROWTH` + pthreads**: Memory growth with pthreads requires
   all threads to update their memory views. Emscripten handles this, but it makes
   JS memory access slower and can cause subtle bugs in custom JS code.

---

## Recommendation

### Do not pursue the pure pthread approach. Use JSPI instead.

**Rationale**:

The pthread approach solves a real problem (ASYNCIFY's 50% code size overhead) but
introduces significant complexity:

- **WASMFS migration is risky** and adds a second major change alongside threading
- **Thread safety concerns** across the codebase (critical sections, global state)
- **3-5 days of implementation** for a feature that JSPI provides for free

**JSPI (`-sJSPI`) is the superior alternative**:

1. **Zero code size overhead** -- the VM handles stack switching natively, no
   instrumentation needed
2. **~1 microsecond overhead per suspend** -- constant time, far below I/O latency
3. **Drop-in replacement for ASYNCIFY** -- same `emscripten_sleep()` calls work,
   no engine changes needed
4. **No WASMFS migration** -- MEMFS works fine (everything stays on the main thread)
5. **No threading complexity** -- no mutexes, no atomics, no race conditions
6. **Now shipping in stable browsers** -- Chrome 137+ (June 2025), Firefox 139+
   (July 2025), phase 4 W3C standardization

**Migration path**:

```cmake
# Replace this:
target_link_options(dc3-web PRIVATE -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=262144)

# With this:
target_link_options(dc3-web PRIVATE -sJSPI)
```

No other changes needed. The `emscripten_sleep()` calls in `WebAssetsFetchSync()`
work with both ASYNCIFY and JSPI. Test, verify, ship.

**Fallback plan**: Keep ASYNCIFY as a build-time option for older browsers. The
`DC3_WEB_ASYNCIFY` CMake option already provides this toggle mechanism.

### When pthreads WOULD make sense

If the engine needed to do CPU-intensive work in parallel with rendering (e.g.,
decompressing large assets on a worker thread while the game loop continues),
pthreads would be the right tool. The current bottleneck is network I/O latency,
not CPU-bound work, so JSPI's suspend/resume model is sufficient.

If WASMFS matures and the engine needs persistent local caching of assets (via OPFS),
the pthread + WASMFS + OPFS combination would be compelling for an offline-capable
web build. That is a future consideration, not a current need.
