# DC3 Runtime Bring-up Status Report — 2026-02-28

## Overview

This document summarizes the current state of the DC3 decomp project across two workstreams:
**static decomp matching** (dc3-decomp repo) and **runtime bring-up** (xenia fork).

---

## Workstream 1: Static Decomp — 100% Linked, 97.4% Matched

### Linking Milestone

As of commit `85af3aee`, the decomp is **100% linked** — every non-XDK translation unit
compiles and links into a valid Xbox 360 XEX. The `/FORCE:UNRESOLVED` linker flag was
dropped at commit `6158ec49`; all symbols resolve cleanly.

### Match Statistics

| Metric | Count | % |
|--------|------:|--:|
| Total functions | 50,454 | — |
| Excluded (XDK/SDK) | 16,766 | — |
| Non-excluded | 33,688 | 100% |
| COMPLETE (100% match) | 32,825 | 97.4% |
| AT_LIMIT (best-effort) | 885 | 2.6% |

### Known Issues with 100% Linking

The link uses `/FORCE:MULTIPLE` to handle duplicate symbols from different translation
units. This has real consequences at runtime:

- **Corrupted vtables**: When multiple TUs define the same class, the linker picks one
  arbitrarily. Some vtable entries point to the wrong TU's functions.
- **Corrupted global data**: Static initializers (`CRT$XCU`) may reference stale addresses
  for globals that got deduplicated.
- **Config/DTA parse failures**: `DataArray::FindArray` cascades happen because config
  files load but some keys get corrupted during `/FORCE` merging.

The linking issues are tracked as Phase 3 work (see Plans below).

### Uncommitted dc3-decomp Changes

10 files with minor decomp fixes (permuter improvements to AccomplishmentOneShot,
ClipDistMap, HamAudio, ShaderMgr, BoxMap, Env, etc.) — net -3 lines. These are
independent match-percentage improvements, not related to runtime.

---

## Workstream 2: Runtime Bring-up in Xenia — Boot to Main Loop

### Architecture

The runtime uses a **custom Xenia fork** (`xenia` repo) with `dc3_hack_pack.cc` (~2800
lines) that patches the decomp XEX at load time. The hack pack:

1. **Stubs ~130 subsystems** that don't exist in headless mode (Kinect/NUI, Holmes debug
   networking, XBC/SmartGlass, GPU init, XMP, Bink video)
2. **Overrides memory allocators** (`operator new`, `MemAlloc`, `PoolAlloc`, `MemFree`,
   etc.) with host-side implementations backed by Xenia's `SystemHeapAlloc`
3. **Overrides file I/O** (`ArkFile::Read`) with host VFS reads to bypass noop'd BlockMgr
4. **Patches PPC instructions** for various workarounds (NUI stubs, CriticalSection IAT
   fix, conditional sentinel init, etc.)

A **manifest system** maps symbolic names to guest addresses, so hack pack patches survive
XEX rebuilds. The manifest is generated from the linker MAP file.

### Boot Progression (current)

```
mainCRTStartup → _cinit → main() → App::App() → SystemPreInit → SystemInit →
  config parse → 56 factories → Synth init → App::Run() →
  RunWithoutDebugging → main loop (STABLE, ~175 fps)
```

The game reaches a stable main loop and runs for 60+ seconds with zero SIGSEGVs.

### Current Blocker: Stale kAddr Addresses (CRITICAL)

**This is the #1 issue right now.** The `Dc3Addresses` struct (`kAddr`) contains ~100
hardcoded guest addresses used for `RegisterGuestFunctionOverride` and PPC bytepatches.
After the decomp was rebuilt for 100% linking, the PE layout shifted and **nearly all
addresses became stale**.

The most critical stale addresses are the **memory allocator overrides**:

| kAddr field | Current (STALE) | Correct (from MAP) | Offset |
|-------------|-----------------|---------------------|--------|
| `operator_new` | `0x83406314` | `0x828649C8` | +99,032 |
| `mem_alloc` | `0x83405918` | `0x828649C8` | +96,476 |
| `mem_free` | `0x83404B98` | `0x82864580` | +93,020 |
| `operator_delete` | `0x833F7DA4` | `0x82864DF0` | +40,296 |
| `pool_alloc` | `0x835A445C` | `0x82A5B640` | +4,900 |
| `pool_free` | `0x835A4224` | `0x82A5B328` | +4,332 |
| `mem_or_pool_alloc` | `0x83406350` | `0x82864E08` | +96,476 |
| `mem_or_pool_free` | `0x83404F0C` | `0x82864988` | +93,020 |

These stale overrides are being registered on **completely wrong functions**, causing
memory corruption. Symptoms include garbled DTB filenames (`config/ptߐad.dta` instead
of `config/platform.dta`) and cascading init failures.

Many other addresses are also stale: `debug_print`, `debug_fail`, `output_l`, `woutput_l`,
`find_array`, `system_config_2`, `symbol_preinit`, `g_string_table_global`, `g_null_str`,
`g_num_heaps`, `g_system_config`, `the_archive`, and more. All globals in .data/.bss
shifted when the PE layout changed.

### Uncommitted Xenia Changes

- `dc3_hack_pack.cc`: Removed Rand2 host overrides (performance fix — 333K calls/sec
  native vs 350 calls/sec via host override), updated 5 BinStream/Rand2 addresses to
  correct values, added hex dump to ArkFile::Read logging. **~100 other addresses still
  need updating.**
- `mmio_handler.cc`: Minor changes
- `emulator.cc`: Minor changes
- `dc3_nui_fingerprints.txt`: Updated fingerprint cache

### Three Stubbed Subsystems That Block Game Progression

| Stub | Why stubbed | What it blocks |
|------|------------|----------------|
| `FlowManager::Poll` | Corrupt FlowNode vtable → infinite dispatch loop | Screen transitions, UI flow |
| `HamSongMgr::Init` | Config returns garbage → `vector::reserve(huge)` crash | Song selection |
| `Synth::InitSecurity` | DRM DTA parsing → yylex infinite loop | Nothing (DRM unnecessary) |

---

## Immediate Plans

### Plan A: Fix All Stale kAddr Addresses (IN PROGRESS)

**Goal**: Update all ~100 kAddr addresses from the MAP file so overrides target correct
functions.

**Approach**:
1. Cross-reference every kAddr field with the MAP file (done — have the mapping)
2. Update all addresses in the `Dc3Addresses` struct
3. Rebuild xenia, test boot
4. Expected outcome: memory allocator overrides work correctly, DTB decryption produces
   valid filenames, config loading improves significantly

**Priority**: This is the single highest-impact fix available. Every other runtime bug
may be a downstream symptom of stale allocator overrides corrupting memory.

### Plan B: Improve Manifest Auto-Resolution

The manifest system (`address_catalog` in `generate_xenia_dc3_patch_manifest.py`) was
designed to solve exactly this problem — auto-resolving addresses from the MAP file on
each rebuild. But:
- Not all kAddr fields are in the catalog yet (only ~73 of ~100)
- The manifest must be regenerated after each dc3-decomp rebuild
- Some addresses (BSS globals, CRT internals) aren't easily resolvable from MAP symbols

After Plan A, we should expand the catalog to cover all remaining fields.

### Plan C: Investigate /FORCE:MULTIPLE Corruption (Phase 3)

The `/FORCE:MULTIPLE` linker flag is the root cause of many runtime issues:
- Corrupted vtables (FlowManager, HamSongMgr)
- Duplicate symbol resolution picking wrong TU
- Static data layout shifts

Fixing this requires resolving the LNK4006 duplicate symbol warnings. There are ~300
duplicate symbols, mostly from:
- Template instantiations in multiple TUs
- Inline functions with static locals
- `ALTERNATENAME` directives conflicting with real implementations

### Plan D: Enable Rendering (Phase 4)

Once config loading works properly and the three stubbed subsystems are un-stubbed:
- Switch from `--gpu=null` to Vulkan backend
- Fix GPU init sequence (currently stubbed)
- Enable screen rendering and UI transitions

---

## Key Technical Learnings

1. **JIT-to-host overhead**: Host function overrides cost ~3ms per call. For hot paths
   like `Rand2::Int()` (333K calls for ARK header decryption), this is catastrophic.
   Always let pure-arithmetic functions run natively.

2. **JIT embeds extern_handler_ permanently**: Once a guest function is overridden, the
   JIT hardcodes the host handler address. You cannot "override then forward" to the
   same guest address — it creates an infinite loop.

3. **Guest function overrides only work for `bl` calls**: The JIT only checks overrides
   on direct `bl` instructions, not indirect `bctrl` calls. CRT `_cinit` calls `__xc`
   entries via `bctrl`, so overrides at `__xc` entry addresses don't fire.

4. **PPC code pages are read-only**: Any bytepatch must call `heap->Protect()` first to
   make the page writable, then write the instruction.

5. **Manifest address resolution**: The `address_catalog` in the manifest generator maps
   kAddr field names to MAP symbols. When the decomp is rebuilt, regenerating the manifest
   auto-updates all addresses. But the kAddr defaults must also be kept current as
   fallbacks for fields not yet in the catalog.
