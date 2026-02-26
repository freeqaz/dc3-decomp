# Wibo CRC Investigation (String literal hash=0)

**Status:** Research / Investigation In-Progress
**Goal:** Fix `wibo` so that MSVC `cl.exe` produces correct JamCRC hashes for string literals (`??_C@`) instead of hash 0 (`A@`).

## The Problem

When compiling with the X360 MSVC compiler under `wibo`, all string literal symbols have a hash of `A@` (0). Under Wine or on native Windows, these same strings have unique 8-character JamCRC hashes.

Example:
- **Wibo:** `??_C@_0BA@A@CharBonesObject?$AA@`
- **Wine:** `??_C@_0BA@JPIJPPAL@CharBonesObject?$AA@`

This prevents 1:1 symbol matching and hampers the "Clean Link" project goals.

## What We've Learned

### 1. The Algorithm is JamCRC
The hash in `??_C@` is a **JamCRC** (standard CRC-32 with `XorOut=0` instead of `0xFFFFFFFF`).
- **Polynomial:** `0x04C11DB7`
- **Init:** `0xFFFFFFFF`
- **Reflected:** Yes
- **XorOut:** `0x00000000`
- **Input:** The raw bytes of the string literal including the null terminator(s).

Verified by manually calculating JamCRC for "A\0" (`0x9f89ff0b`) and comparing it to the Wine output `FHEEJDEE`. Encoding `0x9f89ff0b` using MSVC's A-P nibble scheme (`9=J, F=P, 8=I...` wait, mapping is `0=A, 1=B, ..., 15=P`) results in the expected string.

### 2. cl.exe does NOT use `ntdll!RtlComputeCrc32`
The initial assumption that `cl.exe` calls `ntdll` for CRC was **incorrect**. 
- Debug logs and Wine relay traces (`+relay`) show **zero** calls to `RtlComputeCrc32`.
- `cl.exe` and its sub-DLLs (`c1xx.dll`, `c2.dll`) do not import `ntdll.dll` directly (except for `tlbref.dll`).
- The CRC logic is **internal** to the compiler, likely residing in `c1xx.dll` or `mspdbXX.dll`.

### 3. `mspdbXX.dll` contains CRC constants
Grepping for the reflected CRC32 polynomial `0xEDB88320` reveals it exists in:
- `link.exe`
- `mspdbsrvx.exe`
- `mspdbXX.dll` (at multiple offsets, e.g., `10305696`)

It does **not** appear in `c1xx.dll` or `c2.dll` as a static constant, suggesting they might dynamically load it or use `mspdbXX.dll` for hashing services.

### 4. Wibo environment differences
We identified several missing or broken APIs in `wibo` that `cl.exe` uses:
- `advapi32!SystemFunction036` (RtlGenRandom): Used for randomness.
- `kernel32!InitOnceExecuteOnce`: Used for thread-safe one-time initialization.
- `kernel32!QueryPerformanceCounter`: `wibo` had a stub returning 0.

**I have implemented/fixed all of the above in the local `wibo` build**, but string hashes **remain 0**.

## Hypotheses for why it's 0

1. **Initialization Failure:** The internal CRC table (256 dwords) is likely initialized at runtime (perhaps using `InitOnceExecuteOnce`). If an API it relies on is failing or returning unexpected values, the table remains zeroed, leading to a hash of 0.
2. **CPU Feature Path:** The compiler might detect CPU features (like SSE4.2 CRC32 instructions) using `IsProcessorFeaturePresent` or `cpuid`. If `wibo` reports a feature as present but fails to execute the optimized path correctly (or vice versa), it might fallback to a "safe" result of 0.
    - Note: `wibo` currently reports `PF_XMMI64_INSTRUCTIONS_AVAILABLE` (SSE2) as `TRUE`.
3. **Internal Error State:** The mangler might encounter a non-fatal error during hashing and silently fallback to 0.

## Reproducing the Issue

1. Create `test_hash.cpp`:
   ```cpp
   const char* test1 = "CharBonesObject";
   ```
2. Compile with `cl.exe` via `wibo`:
   ```bash
   ../wibo/build/release/wibo build/compilers/X360/16.00.11886.00/cl.exe /nologo /c /GR /O1 /Oi /EHsc /TP /Fotest_hash_wibo.obj test_hash.cpp
   ```
3. Check symbols:
   ```bash
   strings test_hash_wibo.obj | grep '??_C@'
   ```

## Remaining Work / Next Steps

- **Trace `mspdbXX.dll` activity:** Since it contains the CRC table constants, we need to know if/when `c1xx.dll` calls into it.
- **Differential Debugging:** Compare a debugger trace of `c1xx.dll` in Wine vs `wibo` at the point of string mangling.
- **Check more stubs:** Look for any other `Missing function` or `STUB` messages in `WIBO_DEBUG=1` output that appear near the start of compilation.
- **Investigate `mspdb` initialization:** The compiler uses a lot of `mspdb` logic for symbol management. If the "Program Database" system isn't initializing fully, it might affect string pooling/hashing.

## Summary of `wibo` Changes Made
The following functions were added/improved in `wibo` during this investigation:
- `ntdll!RtlComputeCrc32` (Implemented but unused)
- `advapi32!SystemFunction036` (Implemented as dummy)
- `kernel32!QueryPerformanceCounter` (Implemented via `clock_gettime`)
- `kernel32!QueryPerformanceFrequency` (Implemented)
- `kernel32!InitOnceExecuteOnce` (Implemented)
