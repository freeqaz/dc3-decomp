# XEXP Patch Generation Investigation (2026-02-11)

## Context

The runtime A/B testing workflow (see `2026-02-08-onbeat-runtime-validation-tooling-handoff.md`) requires replacing decomp'd functions in the running binary. The intended mechanism is Xenia's existing `.xexp` patch loading path. This session investigates the critical blocker: **no tool exists to generate `.xexp` files**.

## The Issue

> "No patch generation tooling exists. jeff doesn't support .xexp creation — DeltaPatchDescriptor handling is still tagged TODO. This is the critical path for A/B testing."

### jeff status (`/home/free/code/milohax/jeff/src/util/xex.rs:294-296`)

```rust
XexOptionalHeaderID::DeltaPatchDescriptor => {
    log::debug!("TODO: handle patch descriptor");
}
```

- Header ID `0x5FF` is recognized but completely skipped
- No struct for patch descriptor data
- `XexOptionalHeaderData` has a comment `// PatchDescriptor` where a field would go (line 261) but no actual field
- jeff is read-only for XEX, write-only for COFF `.obj` — no XEX generation path exists

### Xenia status

Xenia's `ApplyPatch()` (`xex_module.cc:200-467`) is fully implemented and functional. It loads `.xexp` files automatically by looking for `path + "p"` in `UserModule::LoadFromFile`. But it's consume-only — no generation.

## DC3 Title Update History

DC3 shipped a real `.xexp` title update, confirming the format is the correct distribution mechanism:

| Version | Type | PE Name | Format |
|---|---|---|---|
| 9.16.12 | Debug (our target) | `ham_xbox_r.exe` | Full `.xex` |
| TU0 | Retail base | `ham_xbox_h.exe` | Full `.xex` |
| TU1 | Retail patch | `ham_xbox_h.exe` | `.xexp` delta patch |

TU1 was packaged in an STFS container with `TUPD` (`0x54555044`) magic and version metadata.

## The XEXP Format

An `.xexp` file is itself a valid XEX2 file with patch-specific flags and headers.

### Module Flags

The XEX header `module_flags` field must have at least one patch bit set:

| Flag | Bit | Value |
|---|---|---|
| `XEX_MODULE_MODULE_PATCH` | 4 | `0x10` |
| `XEX_MODULE_PATCH_FULL` | 5 | `0x20` |
| `XEX_MODULE_PATCH_DELTA` | 6 | `0x40` |

### Delta Patch Descriptor (Optional Header `0x5FF`)

From Xenia's `xex2_info.h`:

```c
struct xex2_opt_delta_patch_descriptor {
    uint32_t size;                         // 0x00
    uint32_t target_version_value;         // 0x04
    uint32_t source_version_value;         // 0x08
    uint8_t  digest_source[0x14];          // 0x0C - SHA1 of base XEX's RSA signature
    uint8_t  image_key_source[0x10];       // 0x20 - AES-encrypted patch image key
    uint32_t size_of_target_headers;       // 0x30
    uint32_t delta_headers_source_offset;  // 0x34
    uint32_t delta_headers_source_size;    // 0x38
    uint32_t delta_headers_target_offset;  // 0x3C
    uint32_t delta_image_source_offset;    // 0x40
    uint32_t delta_image_source_size;      // 0x44
    uint32_t delta_image_target_offset;    // 0x48
    xex2_delta_patch info;                 // 0x4C - first patch entry
};
```

All integer fields are big-endian.

### Delta Patch Operations

Each `xex2_delta_patch` entry describes one operation:

```c
struct xex2_delta_patch {
    uint32_t old_addr;           // Source offset in base image
    uint32_t new_addr;           // Target offset in patched image
    uint16_t uncompressed_len;   // Size after decompression
    uint16_t compressed_len;     // Operation type (see below)
    char     patch_data[1];      // Variable-length payload
};
```

| `compressed_len` | Operation | Description |
|---|---|---|
| `0` | Zero-fill | `memset(dest + new_addr, 0, uncompressed_len)` |
| `1` | Copy | `memcpy(dest + new_addr, dest + old_addr, uncompressed_len)` |
| `>= 2` | LZX delta | LZX decompression with `dest + old_addr` as reference window |

Terminator: an all-zero entry (all four fields = 0).

### Block Structure

Patch image data is organized in blocks:

```
[4 bytes] block_size (total including hash)
[20 bytes] SHA1 hash of entire block
[N bytes] one or more xex2_delta_patch entries
```

Blocks are chained: the first 24 bytes of each block's data are read as the next block descriptor.

### File Format Info

The `.xexp`'s `xex2_opt_file_format_info` header specifies:
- `encryption_type`: 0 = none, 1 = AES-CBC
- `compression_type`: should be 2 (normal) for patched XEX
- `window_size`: LZX window size (typically `0x8000`)
- Block descriptors with SHA1 hashes for integrity

### Patch Application Flow (Xenia `ApplyPatch`)

1. Validate `is_patch()` — checks module flags
2. Get `DeltaPatchDescriptor` optional header (`0x5FF`) — returns error if absent
3. SHA1 hash base XEX's RSA signature, compare against `digest_source` — warns on mismatch but continues
4. Bounds-check delta header/image offsets against base XEX
5. Patch headers: copy source region, apply LZX delta decompression
6. Re-read security info from patched headers, derive AES session key
7. Reallocate memory if new image is larger
8. Decrypt patch image if AES-encrypted
9. Apply image patches block-by-block: verify SHA1, apply LZX delta per block
10. Decommit freed pages if new image is smaller

## Existing Tool Landscape

### Tools that APPLY .xexp (consume-only)

| Tool | Open Source | Notes |
|---|---|---|
| Xenia | Yes | Full implementation in `xex_module.cc` |
| XexTool (xorloser) | No | Closed-source, `xextool -p patch.xexp -o out.xex in.xex` |
| XenonRecomp | Yes | Uses Xenia-derived code |
| idaxex/xex1tool (emoose) | Yes | Parse + display metadata only |
| xenon-bltool | Yes | System/bootloader patches only, not game TUs |

### Tools that CREATE .xexp

**None exist publicly.** Only Microsoft's proprietary SDK tool `imagexex.exe` could generate them. No open-source project has implemented .xexp creation.

### LZX Delta Compression (the hard piece)

Decompression is widely available (Xenia, mspack, ms-compress, wimlib). Compression is the gap:

| Resource | Compresses? | Format Match? |
|---|---|---|
| Windows `msdelta.dll` `CreateDeltaB()` | Yes | PA30 format, NOT raw LZXD blocks |
| wimlib | LZX yes, LZXD no | Standard LZX only |
| ms-compress | No (decompress only) | N/A |
| MS-PATCH spec | Documents format | Reference for building compressor |

The [MS-PATCH specification](https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/) documents the LZX Delta format. It extends standard LZX by pre-loading the decompression window with the reference (source) file.

## What It Takes to Build XEXP Generation

### The core realization: you need a full linked image first

`.xexp` is a binary diff between two complete XEX images. You cannot generate one from individual function patches in isolation. The workflow is:

```
Original XEX (full linked image)
        |
    binary diff  <--  New XEX (hybrid link: decomp .obj + original .obj)
        |
  encode as delta patch operations
        |
  wrap in XEX2 headers
        |
    default.xexp
```

For a partial decomp with N matched functions: use decomp'd `.obj` files for matched functions, original `.obj` files (from `jeff xex split`) for everything else. The linker produces a complete image. The diff naturally captures exactly what changed.

### Four hard dependencies (all currently missing)

#### 1. Hybrid link step

Produce a complete XEX image from mixed decomp/original `.obj` files.

- `tools/project.py` has `LinkStep` infrastructure but link rules are commented out for X360
- The X360 linker (`link.exe` from XDK) would run via wibo
- jeff's `xex split` already produces individual `.obj` files from the original XEX
- This is the **biggest blocker** and a prerequisite for all distribution approaches, not just `.xexp`

#### 2. Binary diffing

Diff old and new images to identify changed regions. Straightforward — just byte comparison to find changed ranges, then map to `xex2_delta_patch` entries.

#### 3. LZX Delta compression (or workaround)

Three options, in order of complexity:

**Option A: Skip compression entirely.** Use only `compressed_len=0` (zero-fill) and `compressed_len=1` (copy) operations. This produces larger `.xexp` files but avoids the compression problem entirely. Xenia's `lzxdelta_apply_patch` handles these cases. The question is whether the block hash / file format info expectations still work without LZXD blocks.

**Option B: Use Windows `msdelta.dll` via Wine/wibo.** `CreateDeltaB()` can produce LZX delta patches, but in PA30 container format. Would need format translation to extract raw LZXD blocks and rewrap in XEX block structure.

**Option C: Write an LZXD compressor from scratch.** Use the MS-PATCH spec as reference. Significant engineering effort.

#### 4. XEX2 header construction

Build the `.xexp` wrapper:
- XEX2 magic + header with patch module flags
- Optional header table with `DeltaPatchDescriptor` at `0x5FF`
- File format info with compression/encryption settings
- SHA1 block hashes
- AES encryption (or set `encryption_type=0` for Xenia-only use — Xenia warns on digest mismatch but continues)

## Alternative Distribution Approaches

If `.xexp` generation proves too costly, the hybrid link step still enables:

| Approach | Needs .xexp? | Needs link step? | Stock Xenia? | Real HW? |
|---|---|---|---|---|
| Ship `.xexp` delta | Yes | Yes | Yes | Yes |
| Ship full relinked XEX | No | Yes | Yes | Yes |
| Ship xdelta/bsdiff patch | No | Yes | User applies | User applies |
| Xenia memory patches (dev only) | No | No | No (modified) | No |

## Practical Approach for Dev A/B Testing vs Distribution

### Near-term: Xenia memory patches (dev workflow)

For unblocking A/B testing now, add a simple `--memory_patches` flag to Xenia that overwrites guest memory at specified VAs after XEX load. ~50-100 lines of C++. No link step needed — extract function bytes directly from decomp `.obj` with VA from map file.

### Medium-term: Hybrid link step

Get the X360 link step working in the build system. This is the shared prerequisite for all distribution formats and the highest-leverage investment.

### Long-term: XEXP generation

Once hybrid linking works, build a minimal `.xexp` generator. Start with uncompressed operations (Option A above) targeting Xenia-only use, add LZXD compression later if needed for real hardware or file size.

## Key References

- Xenia patch code: `vmx128-research/xenia-source/src/xenia/cpu/xex_module.cc` (ApplyPatch, lines 200-467)
- Xenia patch structures: `vmx128-research/xenia-source/src/xenia/kernel/util/xex2_info.h` (lines 439-477)
- Xenia LZX delta: `vmx128-research/xenia-source/src/xenia/cpu/lzx.cc` (lzxdelta_apply_patch, lines 148-191)
- jeff XEX parser: `jeff/src/util/xex.rs` (DeltaPatchDescriptor TODO at line 294)
- xbox-reversing templates: `xbox-reversing/templates/xbox-360/XEX2.bt`, `XEX2OptionalHeaders.bt`
- Free60 XEX format: https://free60.org/System-Software/Formats/XEX/
- MS-PATCH spec: https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/
- DC3 build verification: `docs/sessions/2026-01-29_build-type-verification.md`
