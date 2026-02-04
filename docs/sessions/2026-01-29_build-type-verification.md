# DC3 Build Type Verification

**Date:** 2026-01-29

## Purpose

Independently verify that the DC3 decomp target (`orig/373307D9/default.xex`) is a debug build with no LTCG, after a previous subagent incorrectly claimed it was a "Release build with LTCG."

## Binary Identity

| Property | Decomp Target (`orig/373307D9/default.xex`) | Library Match |
|---|---|---|
| MD5 | `a658576e7c60f2ad107a5a90f26ca546` | Identical to `9.16.12 (Final Debug) - No Checksum` |
| File size | 16,887,808 bytes | Exact match |

## XEX Header Comparison: All Builds

Used `dtk xex info` (built from [jeff](https://github.com/rjkiv/jeff)) to extract headers from every DC3 build in the executable library.

| Build | PE Name | Encryption | XBDM? | D3D9 Lib | EnabledForCallcap | .text Size |
|---|---|---|---|---|---|---|
| **Our target** (9.16.12 Debug, No Checksum) | `ham_xbox_r.exe` | Unencrypted | Yes | D3D9**I** | Yes | 0xBB6B14 (12.3 MB) |
| 9.16.12 (Final Debug) | `ham_xbox_r.exe` | Encrypted | Yes | D3D9**I** | Yes | 0xBB6B14 (12.3 MB) |
| 8.28.12 Prototype (Debug) | `ham_xbox_r.exe` | Encrypted | Yes | D3D9**I** | Yes | 0xBC45C4 (12.3 MB) |
| 8.28.12 Prototype (Debug, No Checksum) | `ham_xbox_r.exe` | Unencrypted | Yes | D3D9**I** | Yes | 0xBC45C4 (12.3 MB) |
| **TU0 (Retail)** | `ham_xbox_h.exe` | Encrypted | **No** | D3D9 | **No** | 0xA18EFC (10.1 MB) |
| TU0 (No Checksum) | `ham_xbox_h.exe` | Unencrypted | **No** | D3D9 | **No** | 0xA18EFC (10.1 MB) |
| **TU1 (Retail patch)** | `ham_xbox_h.exe` | Unencrypted | **No** | D3D9 | **No** | 0xA1AF8C (10.1 MB) |

Note: TU1's original directory only contains `default.xexp` (patch format), not a full `default.xex`.

## Key Differences: Debug vs Retail

1. **PE name**: Debug = `ham_xbox_r.exe`, Retail = `ham_xbox_h.exe` (different MSVC build configurations)
2. **XBDM library**: Debug links Xbox Debug Manager; retail does not
3. **D3D9I vs D3D9**: Debug uses instrumented D3D9 (debug variant); retail uses standard D3D9
4. **XRTLLIBI**: Debug links the instrumented runtime library; retail does not
5. **EnabledForCallcap**: Debug has profiling/callcap support; retail does not
6. **.text size**: Debug is ~2.2 MB larger (12.3 MB vs 10.1 MB) — extra assertion code, debug checks, instrumentation
7. **Encryption**: Checksum variants are dev-kit encrypted; no-checksum variants are unencrypted

Both debug and retail share the same SDK versions (XDK 21173.0, compiler 11886.0) and the same build date of Sep 16, 2012.

## Compiler Flags

From `config/373307D9/config.json`:

```
/O1 /Oi /EHsc /GR
```

- `/O1` — optimize for size
- `/Oi` — enable intrinsic functions
- `/EHsc` — standard C++ exception handling
- `/GR` — enable RTTI
- **No `/GL` or `/LTCG`** — no link-time code generation

## Function Byte Comparison: Debug vs Retail

Extracted the underlying PE from both XEXes (`dtk xex extract`) and compared function bytes from 6 HiResScreen functions. Searched the retail PE's entire .text section for exact byte matches.

| Function | Size | Relocations? | In Retail? |
|---|---|---|---|
| `GetPaddingX` | 8 bytes | None (`li r3, 0x1E0; blr`) | Not found |
| `GetPaddingY` | 8 bytes | None (`li r3, 0x10E; blr`) | Not found |
| `DeleteCache` | 80 bytes | Yes (branches) | Not found |
| `BmpCache::~BmpCache` | 144 bytes | Yes (branches) | Not found |
| `GetPixelColor` | 280 bytes | Yes (branches) | Not found |
| `ScreenRect` | ~576 bytes | Yes (branches) | Not found |

The trivial getters are particularly telling — `li r3, <imm>; blr` has no relocations, so differing section base addresses cannot explain the mismatch. The retail build produces fundamentally different code.

## PE Name Convention

- `ham_xbox_r.exe` — the `_r` suffix likely indicates the MSVC "Release" configuration (optimized but with debug support)
- `ham_xbox_h.exe` — the `_h` suffix likely indicates "Ship"/"Hardened" retail configuration

Both are optimized with `/O1`, but the debug build includes XBDM, D3D9I, XRTLLIBI, and callcap instrumentation. The retail build strips all of this.

## Conclusion

The decomp target is definitively a debug/dev-kit build, confirmed by:

- MD5 match to the library's "Final Debug" XEX
- Presence of debug-only libraries (XBDM, D3D9I, XRTLLIBI)
- EnabledForCallcap header (debug profiling)
- 2.2 MB larger .text section than retail
- No `/GL` or `/LTCG` compiler flags
- Function code differs entirely from retail binary

This means LTCG is not a concern for matching, and the vast majority of functions should be matchable.
