# Curl Library Decomp Audit (2026-02-02)

## Overview

- **Version**: curl 7.24.0-DEV
- **Reference**: `~/code/milohax/curl` @ `curl-7_24_0` tag
- **Total functions**: 264 (200 matched at 100% + 61 partial + 3 unimplemented)
- **Match rate**: 200/264 = 75.8% at 100%, 261/264 = 98.9% have some implementation

### Source File Status

All `.c` source files are **identical** to the reference curl 7.24.0 source. The differences are entirely in **headers** modified for Xbox 360 compatibility:

| Header | Diff Lines | Key Changes |
|--------|-----------|-------------|
| `setup.h` | 575 | XDK winsockx includes, `SIZEOF_TIME_T=8` (vs 4), Xbox platform config |
| `hostip.h` | 130 | `timestamp` field: `long long` (DC3) vs `time_t` (original); formatting |
| `strtoofft.h` | 40 | Different `curlx_strtoofft` path: `_strtoi64` (DC3) vs `strtol` fallback |
| `timeval.h` | 8 | Whitespace only (macro argument spacing) |
| `curlbuild.h` | 0 | Identical |

### Key Environment Differences

1. **64-bit time_t**: DC3's `setup.h` defines `SIZEOF_TIME_T 8` (original uses 4). The `hostip.h` `timestamp` field is `long long` instead of `time_t`.
2. **Xbox networking**: Uses `<xdk/xnet/winsockx.h>` instead of `<winsock2.h>`, no `<windows.h>`.
3. **String-to-offset**: Uses `_strtoi64` directly instead of the conditional strtoll/strtol chain.

---

## By File

### asyn-thread.c (976 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_resolver_is_resolved` | 100.0% | Done | |
| `init_thread_sync_data` | 96.0% | Needs investigation | 8-byte size delta (220 vs 228). Uses `Curl_cmalloc`/`Curl_cstrdup`/`Curl_cfree` function pointer indirection. |
| `destroy_async_data` | 95.9% | Needs investigation | 8-byte size delta (212 vs 220). Heavy `Curl_cfree` function pointer calls. |

### base64.c (identical to reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_base64_encode` | 99.6% | Needs investigation | Same size (468B). Very close; minor instruction differences in encoding loop. |

### connect.c (182 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_timeleft` | 100.0% | Done | |
| `Curl_persistconninfo` | 100.0% | Done | |
| `Curl_getconnectinfo` | 100.0% | Done | |
| `Curl_connecthost` | 100.0% | Done | |
| `Curl_is_connected` | 100.0% | Done | |
| `bindlocal` | 96.7% | **At limit (ICF)** | Contains `merged_Returns0` call. Size 772 vs 788. |
| `Curl_updateconninfo` | 92.5% | Needs investigation | 4-byte size delta. Uses `getpeername`/`getsockname`/`Curl_inet_ntop`. |
| `singleipconnect` | 60.4% | **Likely fixable** | 116-byte size gap. 14 register swaps + 10 control flow diffs. Needs major branch restructuring. |

### cookie.c (identical to reference)

All 5 functions at **100%**. No work needed.

### content_encoding.c (identical to reference)

All 4 functions at **100%**. No work needed.

### curl_addrinfo.c (598 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_he2ai` | 94.9% | Maybe fixable | 8-byte size delta. 50 register swap instructions (r30/r31, r10/r11, r29/r30). Variable reorder needed. |

### dict.c (identical to reference)

All functions at **100%**. No work needed.

### easy.c (1182 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `curl_easy_perform` | 100.0% | Done | |
| `Curl_easy_addmulti` | 100.0% | Done | |
| `Curl_easy_initHandleData` | 100.0% | Done | |
| `curl_easy_setopt` | 86.7% | Needs investigation | **Varargs ABI**: 12-byte size delta (92 vs 80). `va_list` setup differs. |
| `curl_easy_getinfo` | 81.0% | Needs investigation | **Varargs ABI**: 16-byte size delta (88 vs 72). Same `va_list` pattern. |
| `curl_global_init` | 71.4% | **At limit** | LINKER_MERGED + BOOL_MASK + 6 register swaps. 68-byte size gap. |

### file.c (33 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `file_connect` | 100.0% | Done | |
| `file_do` | 99.4% | Partially fixable | 1 ICF merged call (`merged_829A8550`) + 1 commutative operand order. |
| `file_upload` | 94.0% | Partially fixable | 3 ICF merged calls cap match. Register swap + 1 control flow diff. |

### formdata.c (2356 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_getformdata` | 90.0% | Likely fixable | 23 register swaps + 4 control flow diffs. Large function (1388 vs 1424B). |
| `AddFormDataf` | 79.1% | Likely fixable | **Varargs**: 2 control flow diffs + `__security_cookie`. 12-byte size delta. |

### ftp.c (584 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 20 functions | 100.0% | Done | |
| `ftp_state_pasv_resp` | 99.7% | Needs investigation | 4-byte size delta. Very close. |
| `AllowServerConnect` | 99.1% | Likely fixable | 1 control flow diff. Size matches. |
| `ftp_done` | 98.0% | Maybe fixable | Register swaps (r24/r25, r22/r23) across 61 instructions + 2 offset swaps. |
| `ftp_state_use_port` | 94.4% | Partially fixable | 3 ICF merged calls (cap match) + 16 register swaps + 1 control flow diff. |
| `Curl_ftpsendf` | 89.2% | Likely fixable | **Varargs**: 2 control flow diffs, 12-byte size gap. |

### getinfo.c (475 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_initinfo` | 100.0% | Done | |
| `Curl_getinfo` | 97.6% | Maybe fixable | 28-byte size delta. 4 register swaps (r1 vs r11). |

### gopher.c (identical to reference)

`gopher_do` at **100%**. No work needed.

### hmac.c (150 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_HMAC_init` | 96.9% | Likely fixable | Same size (420B). 2 control flow diffs in XOR 0x36/0x5C padding loop. |

### hostip.c (112 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `hostcache_timestamp_remove` | 100.0% | Done | |
| `Curl_hostcache_prune` | 100.0% | Done | |
| `remove_entry_if_stale` | 100.0% | Done | |
| `Curl_resolv` | 100.0% | Done | |
| `Curl_num_addresses` | 66.7% | Needs investigation | 8-byte size delta (24 vs 32). Simple linked-list counter; loop codegen differs. |
| `fn_8256A5D8` | 0.0% | **Unimplemented** | 8 bytes target. Tiny stub function. |

### http.c (12 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 11 functions | 100.0% | Done | |
| `Curl_http` | 96.9% | Likely fixable | 37 register swaps + 4 control flow diffs. 148-byte size gap. Large function. |
| `Curl_add_bufferf` | 93.8% | Likely fixable | **Varargs**: 1 control flow diff, 12-byte size gap. |

### http_chunks.c (identical to reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_httpchunk_read` | 87.9% | Maybe fixable | 75 register swaps. 116-byte size gap. Heavy register pressure. |

### http_proxy.c (36 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_proxyCONNECT` | 99.4% | Likely fixable | Register swaps (r21/r23, r19/r20, r22/r23) + 1 control flow diff. |

### imap.c (30 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 11 functions | 100.0% | Done | |
| `imapsendf` | 82.2% | Likely fixable | **Varargs**: 2 offset swaps + 1 control flow diff. 12-byte size gap. |

### inet_ntop.c (270 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_inet_ntop` | 79.5% | Needs investigation | 8-byte size delta. AF_INET dispatch; errno/branch pattern issue. |

### inet_pton.c (225 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_inet_pton` | 78.4% | Needs investigation | 8-byte size delta. Same errno/branch pattern as inet_ntop. |

### mprintf.c (2140 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `dprintf_Pass1` | 100.0% | Done | |
| `curl_maprintf` | 89.8% | Likely fixable | 1 control flow diff. 12-byte size gap. |
| `curl_msnprintf` | 83.0% | Needs investigation | **Varargs ABI**: 12-byte size gap (72 vs 60). |
| `curl_mfprintf` | 82.7% | Likely fixable | **Varargs**: 1 control flow diff. 12-byte size gap. |
| `dprintf_formatf` | 0.0% | **Unimplemented** | 2676 bytes target. Large printf formatting function. Major effort. |

### multi.c (4986 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 7 functions | 100.0% | Done | |
| `multi_getsock` | 99.6% | Needs investigation | Same size (280B). Mixed patterns, unclear cause. |
| `singlesocket` | 98.8% | Likely fixable | 4-byte size delta. 23 register swaps + 1 control flow diff. |

### nonblock.c (65 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `curlx_nonblock` | 81.4% | Likely fixable | 8-byte size delta. 1 control flow diff in ioctlsocket boolean conversion. |

### pingpong.c (20 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_pp_state_timeout` | 100.0% | Done | |
| `Curl_pp_init` | 100.0% | Done | |
| `Curl_pp_vsendf` | 100.0% | Done | |
| `Curl_pp_readresp` | 100.0% | Done | |
| `Curl_pp_multi_statemach` | 98.6% | Likely fixable | 1 mismatch in small function (176B). |
| `Curl_pp_easy_statemach` | 91.9% | Likely fixable | Control flow diff. 8-byte size delta. |
| `Curl_pp_sendf` | 83.9% | Needs investigation | **Varargs**: 12-byte size delta (76 vs 64). Varargs forwarding issue. |

### progress.c (identical to reference)

All 9 functions at **100%**. No work needed.

### rawstr.c (identical to reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_raw_toupper` | 99.5% | Needs investigation | Same size (264B). Minor diff in switch/case toupper logic. |

### rtsp.c (14 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 5 functions | 100.0% | Done | |
| `rtsp_do` | 98.0% | Likely fixable | 3 control flow diffs. 12-byte size gap. |
| `Curl_rtsp_connisdead` | 89.2% | Needs investigation | 8-byte size delta. 2 offset swaps; struct layout or field access order. |

### select.c (779 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_socket_ready` | 70.2% | Likely fixable | 92-byte size gap. 32 register swaps + 6 control flow diffs. Complex `select()` wrapper; major restructuring. |

### sendf.c (identical to reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_read` | 100.0% | Done | |
| `Curl_debug` | 100.0% | Done | |
| `Curl_send_plain` | 100.0% | Done | |
| `Curl_recv_plain` | 100.0% | Done | |
| `Curl_client_write` | 100.0% | Done | |
| `Curl_failf` | 93.0% | Likely fixable | **Varargs**: 12-byte size delta. Branch structure around `verbose`/`errorbuf`. |
| `Curl_sendf` | 91.7% | Needs investigation | **Varargs**: 12-byte size delta. Indirect call through function pointer table. |
| `Curl_infof` | 91.2% | Likely fixable | **Varargs**: 12-byte size delta. Branch structure around null/verbose check. |

### socks.c (1300 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_SOCKS5` | 99.2% | Likely fixable | 1 control flow diff. 4-byte size gap. |

### strerror.c (1120 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_strerror` | 88.6% | Likely fixable | 4-byte size delta. Control flow / branch condition. |

### strequal.c (identical to reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| `Curl_strlcat` | 85.7% | Needs investigation | 20-byte size gap (140 vs 160). Loop/string traversal logic differs. |
| `fn_825876B4` | 0.0% | **Unimplemented** | 20 bytes target. Small stub function. |

### tftp.c (12 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 4 functions | 100.0% | Done | |
| `tftp_disconnect` | 99.9% | Needs investigation | Same size (116B). Tiny diff. |
| `tftp_tx` | 99.8% | Needs investigation | Same size (740B). Minor codegen in switch/case. |
| `tftp_receive_packet` | 99.8% | Needs investigation | Same size (524B). Minor control flow. |
| `tftp_rx` | 99.8% | Needs investigation | Same size (660B). Minor switch/case sendto. |
| `tftp_multi_statemach` | 98.5% | Likely fixable | 4-byte size gap. Boolean expression pattern. |
| `tftp_easy_statemach` | 97.8% | Likely fixable | 4-byte size gap. 9 register swaps + 3 control flow diffs. |

### transfer.c (76 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 10 functions | 100.0% | Done | |
| `Transfer` | 97.6% | Likely fixable | Register swap (r24/r25) + 4 control flow diffs. |
| `Curl_readwrite` | 97.6% | Likely fixable | 2 control flow diffs. 4-byte size delta. |
| `readwrite_data` | 92.9% | Partially fixable | 3 ICF merged calls to `merged_82B05A40` (cap match) + 1 control flow diff. |
| `Curl_setup_transfer` | 96.6% | Likely fixable | 3 control flow diffs. 4-byte size delta. |

### url.c (9782 diff lines vs reference)

| Function | Match% | Verdict | Issue Summary |
|----------|--------|---------|---------------|
| 16 functions | 100.0% | Done | |
| `Curl_setopt` | 99.8% | Partially fixable | 3 LINKER_MERGED calls (ICF, cap match) + 1 control flow diff. |
| `allocate_conn` | 98.3% | Partially fixable | 1 LINKER_MERGED + 3 register swaps + 2 offset swaps + 2 control flow diffs. |
| `Curl_init_userdefined` | 58.8% | Likely fixable | 12-byte size gap. 41 register swap instructions (r31/r7 dominant). Struct pointer register assignment issue. |

### warnless.c (identical to reference)

`curlx_sotouz` at **100%**. No work needed.

---

## Summary by Pattern

### Varargs ABI Mismatch (12-byte size inflation)
**~12 functions affected**

Functions using `va_start`/`va_end` consistently produce code 12 bytes larger than target. This affects all varargs wrappers (`curl_easy_setopt`, `curl_easy_getinfo`, `curl_msnprintf`, `curl_mfprintf`, `Curl_failf`, `Curl_infof`, `Curl_sendf`, `Curl_ftpsendf`, `Curl_add_bufferf`, `AddFormDataf`, `imapsendf`, `Curl_pp_sendf`).

**Root cause**: Xbox 360 `va_list` setup/teardown generates different prologue/epilogue code. The DC3 compiler may use a different `va_list` implementation than what the headers currently define.

**Fix strategy**: Investigate the Xbox 360 `va_list` struct layout and `va_start` macro expansion. A single header fix could resolve all 12 functions simultaneously.

### ICF / LINKER_MERGED (unfixable)
**~8 functions affected**

Functions calling into ICF-merged addresses: `Curl_setopt`, `allocate_conn`, `readwrite_data`, `file_do`, `file_upload`, `ftp_state_use_port`, `bindlocal`, `curl_global_init`.

**Fix strategy**: None. These are linker artifacts where identical functions were merged to a single address. Accept current match as at_limit.

### Register Allocation (variable reorder)
**~15 functions affected**

Register swap patterns suggest different variable declaration ordering: `Curl_init_userdefined` (41 swaps), `Curl_httpchunk_read` (75 swaps), `ftp_done` (61 swaps), `Curl_he2ai` (50 swaps), `Curl_socket_ready` (32 swaps), `singlesocket` (23 swaps), `Curl_http` (37 swaps), `Curl_getformdata` (23 swaps), etc.

**Fix strategy**: Reorder local variable declarations to match register allocation. ~30% success rate for register swap fixes. Higher priority for functions with few other issues.

### Control Flow / Branch Ordering
**~20 functions affected**

Single or few branch condition mismatches: `AllowServerConnect`, `Curl_SOCKS5`, `Curl_pp_multi_statemach`, `Curl_HMAC_init`, `Curl_strerror`, `curlx_nonblock`, `rtsp_do`, etc.

**Fix strategy**: Adjust if/else ordering, comparison operators, or boolean expression structure. These are the most consistently fixable pattern.

### Unimplemented
**3 functions**

| Function | Size | Notes |
|----------|------|-------|
| `dprintf_formatf` | 2676B | Large printf formatter. Major effort. |
| `fn_8256A5D8` | 8B | Tiny stub in hostip.c |
| `fn_825876B4` | 20B | Small stub in strequal.c |

---

## Priority Recommendations

### Tier 1: Quick Wins (likely 100% achievable)
Single control flow fixes, no register swaps or merged calls:
- `AllowServerConnect` (99.1% -> 100%)
- `Curl_SOCKS5` (99.2% -> 100%)
- `Curl_pp_multi_statemach` (98.6% -> 100%)
- `Curl_HMAC_init` (96.9% -> 100%)
- `Curl_strerror` (88.6% -> 100%)
- `curlx_nonblock` (81.4% -> 100%)
- `tftp_multi_statemach` (98.5% -> 100%)

### Tier 2: Varargs Investigation
Fix the `va_list` ABI issue to unlock ~12 functions simultaneously. Single root cause, highest leverage.

### Tier 3: Register Reorder Attempts
Try variable reorder on functions with register swaps as the primary issue:
- `singlesocket` (98.8%)
- `tftp_easy_statemach` (97.8%)
- `Curl_proxyCONNECT` (99.4%)

### Tier 4: Large Restructuring
Functions needing significant branch restructuring:
- `singleipconnect` (60.4%)
- `Curl_init_userdefined` (58.8%)
- `Curl_socket_ready` (70.2%)

### Tier 5: Accept as At-Limit
Functions capped by ICF merged calls - accept current match:
- `bindlocal` (96.7%)
- `curl_global_init` (71.4%)
- `readwrite_data` (92.9%)
- `file_upload` (94.0%)
