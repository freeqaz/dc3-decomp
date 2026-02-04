# LZX Compression Support for XEX Files - Investigation Complete

**Date:** 2026-01-25
**Status:** Blocked - LZX CAB format not available in Rust
**Priority:** Low (only 2 files affected out of 52 tested)

## Summary

After extensive research, we determined that **XEX LZX decompression cannot be implemented using available Rust crates**. The XEX format uses Microsoft CAB-style LZX compression, while the only available Rust LZX crate (`lzxd`) implements LZX Delta (LZXD), which is an incompatible format.

## Affected Files

Only 2 XEX files in milo-executable-library use LZX compression (~4%):
- `/home/free/code/milohax/milo-executable-library/gh2/360 TU1 Strum Limit Fix/default.xex`
- `/home/free/code/milohax/milo-executable-library/rb2/360 TU0 Strum Limit Fix/default.xex`

All other 50+ XEX files work correctly.

## Work Location

**Git Worktree:** `/tmp/claude/jeff-lzx`
**Branch:** `feature/lzx-compression`
**Base repo:** `/home/free/code/milohax/jeff`

## Technical Findings

### LZX vs LZXD - Different Formats

| Format | Used In | Rust Crate | Status |
|--------|---------|------------|--------|
| **LZX (CAB)** | XEX files, CAB archives | None available | Not supported |
| **LZX Delta (LZXD)** | Windows Update patches, XNB files | `lzxd` | Incompatible |

The `lzxd` crate was designed for XNB files from XNA Game Studio, which use LZXD. XEX files use the original CAB-style LZX which has a different bitstream format.

### XEX Block Structure (Correctly Identified)

The XEX compressed data format was correctly reverse-engineered:

1. **Outer XEX blocks:**
   - 4-byte offset to next block (big-endian)
   - 20-byte SHA1 hash
   - Variable-length data

2. **Inner chunks within each block:**
   - 2-byte chunk size (big-endian)
   - Chunk data

3. **Raw LZX data** is formed by concatenating all chunk data (stripping framing)

This structure matches what Xenia (Xbox 360 emulator) uses.

### Why libmspack Doesn't Work

The `libmspack` C library supports CAB LZX decompression, but:
- The LZX routines (`lzxd_init`, `lzxd_decompress`, `lzxd_free`) are **internal symbols**
- They are **not exported** in the public API
- The public API only exposes container-level functions (CAB, CHM, etc.)

Confirmed via:
```bash
nm -D /usr/lib/libmspack.so | grep lzx  # Returns nothing
```

### What Would Be Required

To properly implement LZX support, one of these approaches would be needed:

1. **Compile libmspack from source** with internal symbols exposed
2. **Port mspack's LZX algorithm to Rust** (significant effort, ~2000 lines of C)
3. **Create Rust bindings to Xenia's LZX wrapper** (requires mspack internally)

## Current Implementation

The code in `/tmp/claude/jeff-lzx/src/util/lzx.rs` now provides:
- Clear error message explaining the limitation
- Workaround instructions for users
- Documentation of the technical constraints

Error message when attempting to load LZX-compressed XEX:
```
LZX-compressed XEX files are not supported.

This XEX uses Microsoft CAB-style LZX compression, which differs from
LZX Delta (LZXD). No pure-Rust implementation exists for this format.

Workaround: Pre-decompress the XEX file using xextool:
  xextool -cu <xexfile.xex>

This creates an uncompressed version that can then be processed.

Note: Only ~4% of XEX files use LZX compression. Most files work without this.
```

## Workaround for Users

Users with LZX-compressed XEX files can use `xextool` to decompress first:

```bash
xextool -cu default.xex
```

This creates an uncompressed version that can then be processed by jeff.

## References

| Resource | Notes |
|----------|-------|
| [Xenia xex_module.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/xex_module.cc) | Xbox 360 emulator XEX loader |
| [Xenia lzx.cc](https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/lzx.cc) | Wrapper around mspack |
| [GoobyCorp Xbox-360-Crypto](https://github.com/GoobyCorp/Xbox-360-Crypto/blob/master/lzx.py) | Python LZX wrapper (uses native DLL) |
| [libmspack](https://www.cabextract.org.uk/libmspack/) | C library with CAB LZX support |
| [lzxd crate](https://crates.io/crates/lzxd) | Rust LZXD (incompatible format) |

## Recommendation

Given the low impact (2 out of 52+ files), the current approach of providing a clear error message with workaround instructions is appropriate. A full LZX implementation would require significant effort for minimal benefit.

If LZX support becomes more important in the future, the best approach would be to port the mspack LZX algorithm to Rust, creating a new `lzx-cab` crate.
