# Curl Decomp - Final Push Session (2026-02-02)

## Summary

Single header change (`#define HAVE_LIBZ 1`) dramatically improved 4 large curl functions that were previously considered at-limit or low-priority.

## Key Change

Added `HAVE_LIBZ` to `src/system/net/curl/lib/config-win32.h`. The original Xbox 360 build was linked with zlib support, enabling `#ifdef HAVE_LIBZ` code paths throughout curl. Without this define, several functions were missing entire code blocks (TE/Connection header merging, content encoding switch statements).

## Results

| Function | Before | After | Change |
|----------|--------|-------|--------|
| `Curl_http` | 96.9% | **100%** | +3.1% (was "large complex, low priority") |
| `readwrite_data` | 93.0% | **100%** | +7.0% (was "large complex, low priority") |
| `Curl_httpchunk_read` | 88.0% | **99.9%** | +11.9% (was "large complex, low priority") |
| `Curl_getformdata` | 90.8% | **99.8%** | +9.0% (was "large complex, low priority") |

All 4 functions now have matching sizes. Remaining diffs are linker-level only (savegprlr suffix, LINKER_MERGED calls, jump table addresses, data label annotations).

## Stub Functions Investigation

Investigated `fn_8256A5D8` (8 bytes, hostip unit) and `fn_825876B4` (20 bytes, strequal unit). Decoded raw PPC instructions from target .obj files:

- **fn_8256A5D8**: `bne cr6, -12; blr` — this is the tail block of `Curl_num_addresses`, split as a separate symbol by the linker. Not a standalone function.
- **fn_825876B4**: `bne cr0, -32; subf; stb; add; blr` — tail block of `Curl_strlcat`, similarly split.

These cannot be implemented separately; they're artifacts of how the original linker laid out code blocks.

## Remaining Non-100% Curl Functions

### At Practical Limit (linker/compiler differences only)
- `ftp_state_use_port` (99.9%) - LINKER_MERGED
- `allocate_conn` (99.7%) - LINKER_MERGED + ICF
- `Curl_getformdata` (99.8%) - linker savegprlr + data label
- `Curl_httpchunk_read` (99.9%) - jump table addresses
- `multi_getsock` (99.6%) - jump table addresses
- `Curl_base64_encode` (99.6%) - data label + register shift
- `Curl_raw_toupper` (99.5%) - jump table addresses
- `Curl_HMAC_init` (99.3%) - data ordering
- `rtsp_do` (98.0%) - 64-bit codegen + savegprlr
- `file_upload` (95.3%) - LINKER_MERGED + 64-bit compare
- `Curl_strlcat` (85.7%) - loop exit codegen
- `Curl_num_addresses` (66.7%) - extra null check codegen
- `curl_global_init` (71.4%) - BOOL_MASK

### Infrastructure (significant restructuring needed)
- `singleipconnect` (81.0%) - control flow + register allocation
- `Curl_socket_ready` (76.3%) - control flow + register allocation

## Lessons Learned

1. **Check all `#ifdef` guards against the original build**: A single missing define can cause large code blocks to be excluded, showing as "structural differences" in the diff. The HAVE_LIBZ case affected 4 separate compilation units.

2. **"Large complex functions" may not be complex at all**: The plan categorized these as low-priority with ~30% success probability. The actual fix was a one-line config change.

3. **Size mismatches are the strongest signal**: When target and base sizes differ significantly, the root cause is almost always a missing `#ifdef` block or wrong struct layout, not register allocation issues.

4. **Stub functions in target .obj can be linker-split tail blocks**: When a tiny anonymous function appears between two known functions in the same compilation unit, decode the raw instructions — they're often just the exit path of the preceding function.
