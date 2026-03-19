# Emscripten Web Build: Debugging and Developer Features Guide

Practical guide for debugging the DC3 native port's Emscripten/WASM build.
Researched March 2026 against Emscripten 5.0.x documentation and Chrome DevTools.

**Current build config** (`native/CMakeLists.txt`):
- `--profiling-funcs` (function names in stack traces)
- `-sJSPI` (async file loading)
- `-sNO_DISABLE_EXCEPTION_CATCHING` (C++ exceptions enabled)
- `-sALLOW_MEMORY_GROWTH=1` / `-sMAXIMUM_MEMORY=512MB`
- No debug symbols (`-g` not passed)
- No sanitizers

---

## 1. DWARF Debugging in Chrome DevTools

### How It Works

Compile with `-g` to embed DWARF debug info in the `.wasm` binary. Install the
[C/C++ DevTools Support (DWARF)](https://chrome.google.com/webstore/detail/pdcpmagijalfljmkmjngeonclgbbannb)
Chrome extension. This gives you:

- **Source-level stepping** through original C++ files in the Sources panel
- **Variable inspection** on hover and in the Scope pane (locals, globals, pointers)
- **Breakpoints** on C++ source lines
- **Call stack** with real C++ function names
- **Memory Inspector** for raw byte inspection of C++ objects

### Setup Steps

1. Compile with `-g` (or `-g2` for name sections only, or `-g3` for full DWARF)
2. Install the DWARF extension in Chrome
3. In DevTools Settings, enable "WebAssembly Debugging: Enable DWARF support"
4. In Sources > Filesystem, add the project directory so Chrome can find source files
5. Open DevTools *before* page load for best results

### Size Implications for a 23MB Binary

Full `-g` will **dramatically** increase the `.wasm` file, potentially to 100MB+
for a project this size. Mitigations:

| Flag | Effect |
|------|--------|
| `-g2` | Name sections only. Minimal size increase (~1-2MB). Good stack traces. |
| `-gseparate-dwarf[=PATH]` | **Recommended.** Strips DWARF into a side `.debug.wasm` file. Main binary stays lean. DevTools fetches debug file on demand. |
| `-gsplit-dwarf` | Compile-time only. Creates `.dwo` files per TU, speeding link. Use `emdwp` to bundle into `.dwp`. |
| `-gline-tables-only` | Compile-time. Smaller DWARF (no variable info). Good for crash symbolication. |

### JSPI Compatibility

No specific JSPI incompatibilities documented. DWARF debugging should work when
paused at a C++ breakpoint regardless of whether the call was through JSPI.
Stack traces through JSPI suspension points may show JS-to-Wasm boundary frames.
The DWARF extension handles Wasm frames; JS frames appear normally.

### Recommended for DC3

**Immediate**: Add `-g2` at link time (already have `--profiling-funcs`, `-g2`
is a superset). Minimal size cost, much better stack traces.

**For active debugging**: Use `-gseparate-dwarf` so the main `.wasm` stays
~23MB and a `.debug.wasm` ships alongside for DevTools to load on demand.

```cmake
# In CMakeLists.txt, Emscripten link options:
target_link_options(dc3-web PRIVATE
    -g2                         # name sections (always)
    -gseparate-dwarf            # full DWARF in side file
)
# Or for compile-time speedup on incremental builds:
target_compile_options(dc3-web PRIVATE -gsplit-dwarf)
```

---

## 2. Source Maps vs DWARF

| Feature | `-gsource-map` | `-g` (DWARF) |
|---------|----------------|---------------|
| Variable inspection | No | Yes |
| Step through C++ | Location only | Full source + variables |
| Browser support | All browsers | Chrome only (extension required) |
| Size impact | Separate `.map` file, 2-3x source map size with names | Embedded or separate `.debug.wasm` |
| Optimization compat | Works with full post-link optimizations | Degrades with -O2+ (line info survives, variable info may not) |

### Recommendation

Use **both**:
- `-g2` always (name sections, negligible cost)
- `-gseparate-dwarf` for Chrome DWARF debugging sessions
- `-gsource-map` if Firefox debugging is needed (source maps work in Firefox)

Note: As of Emscripten 4.0.15, `-gsource-map` is independent of other `-g`
flags. You need `-g2 -gsource-map` to get both name sections and source maps.

Source maps now support function names in the `names` field (Emscripten 4.0.22+),
though this increases `.map` file size 2-3x.

---

## 3. Sanitizers

### AddressSanitizer (`-fsanitize=address`)

**What it catches**: Buffer overflows (stack and heap), use-after-free,
use-after-return (optional), memory leaks, invalid access from both Wasm and JS.

**How to enable**: Pass `-fsanitize=address` at *both* compile and link time.

**Memory requirements**: Needs at least `INITIAL_MEMORY=64MB` or
`ALLOW_MEMORY_GROWTH=1` (we already have the latter). ASan instruments every
memory access, so expect ~2-3x memory overhead and significant slowdown.

**Wasm-specific behavior**: In Wasm, address 0 is valid memory (no segfaults on
null deref). ASan catches null derefs that would silently corrupt memory. This
is *especially* relevant for the abort() crash during milo deserialization --
ASan would catch buffer overflows, null pointer derefs, and use-after-free that
could cause the abort.

**Threading**: Fully supports multi-threaded environments. Also monitors JS
support code accessing Wasm memory.

**Performance tip**: For malloc-heavy code, set `Module.ASAN_OPTIONS =
'malloc_context_size=0'` to disable allocation stack traces (faster, less
diagnostic detail).

```cmake
# ASan debug build
option(DC3_WEB_ASAN "Enable AddressSanitizer for web build" OFF)
if(DC3_WEB_ASAN)
    target_compile_options(dc3-web PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(dc3-web PRIVATE -fsanitize=address)
endif()
```

### UndefinedBehaviorSanitizer (`-fsanitize=undefined`)

**What it catches**: Null pointer dereferences, signed integer overflow,
shift overflows, misaligned access, and other C++ undefined behavior.

**How to enable**: `-fsanitize=undefined` at compile and link time.

**Minimal runtime**: `-fsanitize-minimal-runtime` reduces overhead
(less detailed error messages but lower performance cost).

**Highly relevant for DC3**: The engine was originally compiled for Xbox 360
(big-endian PPC). Alignment issues, integer overflow, and null derefs that
"happened to work" on Xbox may silently corrupt state in Wasm.

### LeakSanitizer (`-fsanitize=leak`)

Lighter than full ASan -- only detects memory leaks, does not instrument all
memory accesses. Much faster. Requires `-sEXIT_RUNTIME=1` for automatic leak
reporting at exit, or call `__lsan_do_recoverable_leak_check()` manually.

### Compatibility Matrix

| Mode | ASan | UBSan | LSan | SAFE_HEAP |
|------|------|-------|------|-----------|
| ASan | -- | Compatible | Included | **Incompatible** |
| UBSan | Compatible | -- | Compatible | May conflict |
| LSan | Included | Compatible | -- | **Incompatible** |
| SAFE_HEAP | **Incompatible** | May conflict | **Incompatible** | -- |

Build separately for each mode to get maximum coverage.

### For the Milo Deserialization Abort

Priority order for diagnosing the crash:
1. **UBSan first** -- cheapest, catches undefined behavior that corrupts silently
2. **ASan second** -- heavier, catches buffer overflow / use-after-free
3. **SAFE_HEAP** if alignment is suspected

---

## 4. SAFE_HEAP and Memory Debugging

### `-sSAFE_HEAP=1`

Instruments every load and store via a Binaryen pass. Catches:
- Dereferencing address 0 (no segfault in Wasm)
- Memory alignment issues (important for struct packing mismatches)
- Out-of-bounds access

Performance: significant slowdown (every memory access is checked).
Use `-sSAFE_HEAP_LOG` for verbose logging.

Use `SAFE_HEAP=1` for Wasm+Wasm2JS, `SAFE_HEAP=2` for Wasm-only builds.

**Note**: ASan provides most of SAFE_HEAP's functionality plus more. Try ASan
first unless you specifically suspect alignment issues.

### `-sASSERTIONS=1` (default at -O0, off at -O1+)

Adds runtime checks for:
- Invalid function pointer calls
- Memory allocation failures
- Missing exported functions
- Stack overflow detection (when combined with `STACK_OVERFLOW_CHECK`)

`ASSERTIONS=2` adds more checks including function pointer table validation.
Significant performance cost.

### `-sSTACK_OVERFLOW_CHECK=2`

Level 1 (default with ASSERTIONS=1): Basic stack boundary check.
Level 2: More precise detection with better call stack info. Performance cost.

Relevant for DC3: the engine has deep call stacks during loading/deserialization.
Stack overflow could manifest as the abort() crash.

```cmake
# Debugging build variant
target_link_options(dc3-web PRIVATE
    -sASSERTIONS=2
    -sSTACK_OVERFLOW_CHECK=2
    -sSAFE_HEAP=1
)
```

### Demangled C++ Symbols

`-sDEMANGLE_SUPPORT` is deprecated / removed in recent Emscripten versions.
Instead, use:
- `--profiling-funcs` (already enabled) -- keeps Wasm name sections
- `-g2` at link time -- also includes name sections
- Browser DevTools automatically demangle names from the Wasm name section

---

## 5. Profiling

### `--profiling` vs `--profiling-funcs`

| Flag | Effect |
|------|--------|
| `--profiling` | Equivalent to `-g2`. Keeps function names, preserves JS whitespace. Larger output. |
| `--profiling-funcs` | **Currently used.** Keeps Wasm function names but minifies JS. Best balance for production profiling. |

### Chrome Performance Tab

With `--profiling-funcs`, the Chrome Performance tab shows C++ function names
in flame charts. Wasm functions appear as named entries in the call tree.

**Tips**:
- Record a trace, then look for `wasm-function[N]` entries -- with
  `--profiling-funcs` these show as `ClassName::MethodName`
- The "Bottom-Up" view is most useful for finding hot C++ functions
- CPU throttling in DevTools simulates slower devices

### Emscripten Tracing API

Compile with `--tracing` for custom instrumentation:
- Memory allocation tracking (malloc/realloc/free)
- Frame timing (FPS analysis)
- Custom named sections (e.g., "Physics Update", "Milo Load")
- Sends data to a collector server (emscripten-trace-collector on port 5000)

Heavyweight; best for targeted profiling sessions, not always-on.

---

## 6. Hot Reload / Incremental Builds

### Hot Reload

**No native hot-reload for Wasm modules.** The entire `.wasm` must be
reloaded (page refresh). There is no mechanism to patch a running Wasm instance.

### Incremental Build Strategies

1. **Object file compilation** (already used via CMake): Only modified `.cpp`
   files recompile. The link step concatenates Wasm objects.
2. **`-gsplit-dwarf`**: Speeds link by keeping DWARF in `.dwo` files.
3. **`-sREVERSE_DEPS=all`**: Includes all native deps upfront, skipping input
   file scanning. Helps link time for projects with hundreds of object files.
4. **Avoid `-g3` for iterative builds**: Use `-g2` during development, `-g3`
   only when you need full variable inspection.
5. **`ccache` works with emcc**: Set `CMAKE_C_COMPILER_LAUNCHER=ccache` and
   `CMAKE_CXX_COMPILER_LAUNCHER=ccache`.

### Practical Workflow

```bash
# Fast rebuild: only recompiles changed files, fast link
cmake --build native/build-web -- -j$(nproc)
# Copy just the wasm (skip JS if unchanged)
cp native/build-web/dc3-web.wasm native/web/build/
# Refresh browser
```

---

## 7. WASMFS

### Status (March 2026)

WASMFS is **stable but not feature-complete** with the legacy JS filesystem.
Known gaps:
- Hard-coded stdin/stdout/stderr behavior (no customization like legacy FS)
- Not all legacy JS FS backends have WASMFS equivalents
- Not yet the default in Emscripten

### Benefits

- Written in C++ (runs as Wasm, not JS) -- better performance
- Fully multithreaded (no JS main-thread bottleneck)
- OPFS backend for persistent storage
- Better performance for allocation-heavy / file-heavy apps

### Relevance to DC3

**Low priority.** DC3's web build uses FETCH API for file loading (via JSPI),
not the Emscripten filesystem. WASMFS would matter more if we were doing heavy
local file I/O. No debugging advantages over MEMFS for our use case.

Enable with `-sWASMFS` if filesystem performance becomes a bottleneck.

---

## 8. Emscripten Tracing and Logging

### `--emrun`

Captures stdout/stderr from the browser and pipes to the terminal.

```bash
# Build with --emrun linked in
target_link_options(dc3-web PRIVATE --emrun)
# Run
emrun --port 8421 native/web/build/index.html
# Flags: --verbose, --log_stdout FILE, --log_stderr FILE
```

**Caveat**: Known issue (Jan 2026) where emrun may not receive exit signals
in some scenarios. Our custom `native/web/server.py` is likely more reliable
for day-to-day use.

### Verbose Runtime Modes

| Setting | What it does |
|---------|--------------|
| `-sRUNTIME_DEBUG` | Logs runtime internals (module loading, memory ops) |
| `-sASSERTIONS=2` | Verbose function pointer and memory checks |
| `-sSAFE_HEAP_LOG` | Logs every SAFE_HEAP memory access check |
| `EMCC_DEBUG=1` (env var) | Compiler debug output (not runtime) |
| `EMCC_AUTODEBUG=1` (env var) | Instruments every store for regression testing |

### Custom Tracing

The Emscripten Tracing API (`emscripten/trace.h`, compile with `--tracing`)
provides:
- `emscripten_trace_record_frame_start/end()` -- frame timing
- `emscripten_trace_enter/exit_context()` -- named sections
- `emscripten_trace_log_message()` -- categorized log messages
- Automatic malloc/free tracking
- Client-server architecture (data sent to collector on port 5000)

**Practical alternative**: `console.time()`/`console.timeEnd()` from C++ via
`EM_ASM` for quick timing of specific operations.

---

## 9. WebAssembly Proposals

### Relevant to DC3 Today

| Proposal | Status (2026) | Relevance |
|----------|---------------|-----------|
| **Exception Handling** | Standardized (Wasm 3.0). All major browsers. | Switch from `-sNO_DISABLE_EXCEPTION_CATCHING` (JS-based) to `-fwasm-exceptions` for smaller code + better perf. |
| **JSPI / Stack Switching** | Chrome 137+, Firefox 139+. | Already using. Production-ready. |
| **Tail Calls** | Baseline. All major browsers. | Could benefit recursive engine code. Emscripten can emit tail calls. |
| **Relaxed SIMD** | Standardized. | Relevant if we ever port SIMD math. |

### Future / Aspirational

| Proposal | Status | Notes |
|----------|--------|-------|
| **Memory64** | Standardized (Wasm 3.0), but browsers require flags. `-sMEMORY64` no longer experimental. | Not needed -- 512MB max is plenty. 64-bit pointers have perf overhead. |
| **GC** | Standardized (Wasm 3.0). | Not relevant -- we manage our own memory. For Java/Kotlin/Dart targets. |
| **Threads** | Standardized (SharedArrayBuffer). | Already available via `-pthread`. Not currently used in DC3 web build. |
| **Component Model** | Phase 2. | Wasm composability standard. Not relevant for monolithic game engine. |

### Action Item: Switch to Wasm Exceptions

The current `-sNO_DISABLE_EXCEPTION_CATCHING` uses JavaScript-based exception
handling, which has "relatively high overhead." Switching to `-fwasm-exceptions`:

- Smaller code size (no JS trampolines for throw/catch)
- Better performance (native Wasm instructions)
- Stack traces work with `ASSERTIONS` + `--profiling-funcs`
- Supported in all browsers that support JSPI (Chrome 137+)

```cmake
# Replace:  -sNO_DISABLE_EXCEPTION_CATCHING
# With:
target_compile_options(dc3-web PRIVATE -fwasm-exceptions)
target_link_options(dc3-web PRIVATE -fwasm-exceptions)
# Remove: -sNO_DISABLE_EXCEPTION_CATCHING
```

**Caveat**: `std::set_terminate` is NOT supported with Wasm exceptions when
a thrown exception has no matching handler (neither JS-based nor Wasm EH
supports two-phase exception handling fully).

---

## 10. New in Emscripten 4.x/5.x

Notable features for this project:

| Version | Feature | Impact |
|---------|---------|--------|
| 4.0.15 | `-gsource-map` independent of `-g` | Need `-g2 -gsource-map` for both |
| 4.0.18 | `CROSS_ORIGIN` setting | Helps if assets hosted on CDN |
| 4.0.19 | Eliminated main module relocatability | Smaller Wasm, faster link |
| 4.0.22 | Source map `names` field support | Function names in source maps (2-3x size) |
| 4.0.22 | JS caching for generated code | Faster link for incremental builds |
| 5.0.0 | `__async: 'auto'` for JS library funcs | Simpler JSPI integration |
| 5.0.0 | Wasm EH refcount fix | More reliable exception handling |
| 5.0.0 | `WASM_BIGINT` default on | i64 as BigInt (no more i64-to-pair conversion) |
| 5.0.0 | `MEMORY64` no longer experimental | 64-bit pointers (not needed for DC3) |
| 5.0.3 | Pthread stubs removed from Wasm Workers | Cleaner single-threaded builds |

---

## Priority Actions (What to Enable NOW)

### Tier 1: Zero/Low Cost (enable immediately)

1. **`-g2` at link time** -- superset of `--profiling-funcs`, adds Wasm name
   sections. Minimal size increase. Better stack traces everywhere.
2. **`-sASSERTIONS=1`** -- catches invalid function pointers, memory errors,
   stack overflow. Some perf cost but invaluable for finding the abort() crash.
3. **`-sSTACK_OVERFLOW_CHECK=2`** -- the abort() during milo deserialization
   could be a stack overflow. Deep deserialization call chains are common.

### Tier 2: Moderate Cost (use for debugging sessions)

4. **`-fsanitize=undefined`** -- catches UB that "works" on Xbox but crashes
   in Wasm. Moderate performance cost. Build separately.
5. **`-gseparate-dwarf`** -- full C++ source-level debugging in Chrome. Side
   file keeps main binary lean.
6. **`-fwasm-exceptions`** -- replace JS-based exceptions for better perf
   and smaller code. Test thoroughly first.

### Tier 3: Heavy (targeted debugging only)

7. **`-fsanitize=address`** -- catches buffer overflow, use-after-free. Heavy
   memory and perf overhead. Build separately when hunting memory corruption.
8. **`-sSAFE_HEAP=1`** -- catches alignment issues. Use when ASan doesn't
   find the bug.

### Not Worth It Now

- WASMFS (we use FETCH, not FS)
- Memory64 (512MB is plenty)
- Emscripten Tracing API (custom console.time is simpler)
- `--emrun` (our custom server.py works better)
- Hot reload (not possible with Wasm)

---

## Quick Reference: CMake Debug Build

```cmake
# Add to CMakeLists.txt for a debug-friendly web build:
option(DC3_WEB_DEBUG "Enable web debugging aids" OFF)
option(DC3_WEB_ASAN "Enable AddressSanitizer" OFF)
option(DC3_WEB_UBSAN "Enable UBSan" OFF)

if(DC3_WEB_DEBUG)
    target_link_options(dc3-web PRIVATE
        -g2
        -gseparate-dwarf
        -sASSERTIONS=2
        -sSTACK_OVERFLOW_CHECK=2
    )
    target_compile_options(dc3-web PRIVATE -g2)
endif()

if(DC3_WEB_ASAN)
    target_compile_options(dc3-web PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(dc3-web PRIVATE -fsanitize=address)
endif()

if(DC3_WEB_UBSAN)
    target_compile_options(dc3-web PRIVATE -fsanitize=undefined)
    target_link_options(dc3-web PRIVATE -fsanitize=undefined)
endif()
```

Build with: `emcmake cmake -S native -B native/build-web-debug -DDC3_WEB_DEBUG=ON`

---

## Sources

- [Emscripten Debugging Documentation](https://emscripten.org/docs/porting/Debugging.html)
- [Emscripten Sanitizers Documentation](https://emscripten.org/docs/debugging/Sanitizers.html)
- [Chrome DevTools: Debug C/C++ WebAssembly](https://developer.chrome.com/docs/devtools/wasm)
- [Chrome Blog: Faster Wasm Debugging](https://developer.chrome.com/blog/faster-wasm-debugging)
- [Chrome Blog: Memory Inspector for C/C++](https://developer.chrome.com/blog/memory-inspector-extended-cpp)
- [Emscripten C++ Exceptions](https://emscripten.org/docs/porting/exceptions.html)
- [Emscripten Settings Reference](https://emscripten.org/docs/tools_reference/settings_reference.html)
- [Emscripten Tracing API](https://emscripten.org/docs/api_reference/trace.h.html)
- [Emscripten Changelog](https://github.com/emscripten-core/emscripten/blob/main/ChangeLog.md)
- [web.dev: Debugging Memory Leaks in WebAssembly](https://web.dev/articles/webassembly-memory-debugging)
- [web.dev: WasmFS and mimalloc for Multithreaded Apps](https://web.dev/articles/scaling-multithreaded-webassembly-applications)
- [WebAssembly 3.0 Release](https://webassembly.org/news/2025-09-17-wasm-3.0/)
- [WasmGC and Tail Calls Baseline](https://web.dev/blog/wasmgc-wasm-tail-call-optimizations-baseline)
- [Building, Shipping and Debugging a C++ WebAssembly App](https://www.willusher.io/blog/build-ship-debug-wasm/)
