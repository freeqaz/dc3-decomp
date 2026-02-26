# Wibo CRC Investigation (String literal hash=0)

**Status:** COMPLETE (2026-02-26)
**Goal:** Fix `wibo` so that MSVC `cl.exe` produces correct CRC-32 hashes for string literals (`??_C@`) instead of hash 0 (`A@`).

## The Problem

When compiling with the X360 MSVC compiler under `wibo`, all string literal symbols had a hash of `A@` (0). Under Wine or on native Windows, these same strings have unique 8-character CRC-32 hashes.

Example:
- **Wibo (before):** `??_C@_0BA@A@CharBonesObject?$AA@`
- **Wine:** `??_C@_0BA@JPIJPPAL@CharBonesObject?$AA@`

## Root Cause

`cl.exe` / `c1xx.dll` calls **`SigForPbCb`** from **`mspdbXX.dll`** for CRC hashing. Wibo embeds a dummy `mspdbXX.dll` (built from `dll/mspdb/mspdb_dll.cpp`) where `SigForPbCb` was hardcoded to `return 0;`.

**NOT** `ntdll!RtlComputeCrc32` — debug logs and Wine relay traces show zero calls to that function.

### Key Findings

1. **The algorithm**: CRC-32 with reflected polynomial `0xEDB88320` (ISO 3309). Called with `dwInitial=0xFFFFFFFF`. No final XOR — produces JamCRC-compatible results.

2. **`mspdbXX.dll` is the hashing provider** — confirmed via `objdump -p` and by fixing the stub.

3. **Wibo prioritizes builtin modules** — uses its internal `mspdb` builtin even if a real `mspdbXX.dll` is present in the compiler directory.

### SigForPbCb Signature
```cpp
uint32_t SigForPbCb(const unsigned char *pb, uint32_t cb, uint32_t dwInitial)
```

## Solution

### CRC Fix (Work Item #3) — DONE

Implemented proper CRC-32 in `SigForPbCb` (`wibo/dll/mspdb/mspdb_dll.cpp`). All string hashes now match Wine/Windows.

| String | Wine Hash | Wibo (Before) | Wibo (After) |
|--------|-----------|---------------|--------------|
| `""`   | `CNPNBAHC`| `A`           | `CNPNBAHC`   |
| `"A"`  | `FHEEJDEE`| `A`           | `FHEEJDEE`   |
| `"AB"` | `LDKJOMJN`| `A`           | `LDKJOMJN`   |

### Path Matching (Work Item #4) — DONE

The original build used two source trees:
- `e:\lazer_build_gmc1\system\src\` — Milo engine code (`char/`, `synth/`, `obj/`, `utl/`, etc.)
- `e:\lazer_build_gmc1\lazer\src\` — DC3 game code (`meta_ham/`, `game/`, etc.)

Our decomp tree has these at `src/system/` and `src/lazer/`.

**Fix (configure.py):**
```python
config.wibo_path_map = (
    f"E:/lazer_build_gmc1/system/src/={Path('src/system').absolute()};"
    f"E:/lazer_build_gmc1/lazer/src/={Path('src/lazer').absolute()}"
)
```

**Fix (defines_common.py):** Include paths changed from relative to absolute mapped:
```python
"/I E:/lazer_build_gmc1/system/src",    # was: /I src/system
"/I E:/lazer_build_gmc1/lazer/src",     # was: /I src/lazer
```

This ensures `__FILE__` in headers resolves to absolute Windows paths matching the original (e.g., `e:\lazer_build_gmc1\system\src\synth\Sfx.h`).

**Fix (project.py):** Shell quoting for semicolon-separated path map:
```python
msvc_cmd = f"{wrapper_cmd}WIBO_PATH_MAP='$wibo_path_map' {msvc} ..."
```

### Source String Fixes (5 bugs found via hash comparison)

| File | Original | Decomp (was) | Fix |
|------|----------|-------------|-----|
| `DirLoader.cpp:866` | "Proceeding as if file were empty." | "Processing could not be completed" | Fixed |
| `MetagameRank.cpp:222` | "not found in unlock list" | "not found in unlockables" | Fixed |
| `Accomplishment.cpp:116` | "more than the maximum" | "more than the minimum" | Fixed |
| `SongSequence.cpp:234` | "clear_all_flashcard_campaign_status" | "clear_all_flashcard_campaign_states" | Fixed |
| `CampaignPerformer.cpp:87` | "award_master_quest_accomplishemnts" (typo) | "award_master_quest_accomplishment" | Fixed (preserving original typo) |

## Final Results

**121 shared `??_C@` symbols compared: 121 matching, 0 mismatched.**

- 30 ORIG-ONLY: strings from undecompiled functions (expected)
- 3 DECOMP-ONLY: different strings sharing truncated prefix with original (not hash mismatches)

## Wibo Changes (committed on `x360-linker-support` branch)

- `dll/mspdb/mspdb_dll.cpp` — `SigForPbCb` CRC-32 implementation
- `src/files.cpp` — `WIBO_PATH_MAP` support with cached parser, `pathToWindows`/`pathFromWindows`
- `dll/kernel32/synchapi.h` — `InitOnceExecuteOnce` support
- `dll/advapi32/wincrypt.cpp` — `SystemFunction036` (RtlGenRandom)
- `dll/kernel32/profileapi.cpp` — `QueryPerformanceCounter/Frequency`
- `dll/ntdll.cpp` — `RtlComputeCrc32` (for completeness)
- `src/main.cpp` — environment variable passthrough, debug detection cleanup
