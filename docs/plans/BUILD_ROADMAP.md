# Build Roadmap: Path to a Working Executable

## Major Milestone: Decompiled XEX Boots! 🎉

**2026-02-17**: The decompiled XEX boots successfully in Xenia headless mode.

### Verified Runtime

```bash
# Build and test
ninja && python3 scripts/build_xex.py
timeout 125 xenia-headless --target=build/373307D9/default.xex --headless_timeout_ms=115000

# Result:
# - XEX loads successfully
# - 293 pages loaded (CODE + RWDATA sections)
# - All 6 threads started (GPU, VSync, XMA, Audio, Dispatch, Main)
# - "BOOT: Title loaded successfully"
# - "BOOT: Kernel state initialized"
# - "BOOT: Title ID: 0x373307d9"
# - TIMEOUT: 115000ms reached (game ran for 2 minutes!)
# - ZERO errors or crashes
```

### Comparison with Original

| Metric | Original XEX | Decompiled XEX |
|--------|--------------|----------------|
| Boot success | ✅ | ✅ |
| Kernel state init | ✅ | ✅ |
| Main thread start | ✅ | ✅ |
| Errors | 0 | 0 |

Both XEX files exhibit identical boot behavior in headless mode - they wait for input/rendering that won't happen without a real GPU.

**Key Implementation Notes:**
- Import library header (0x103FF) is skipped because the original XEX is compressed
  and our PE structure is different (uncompressed)
- The game runs without import resolution - imports may be resolved lazily or not
  needed during boot
- Missing optional headers: achievement data, title name (cosmetic only)

**Next Steps:**
- Test with real GPU (non-headless Xenia)
- Investigate import resolution for full functionality
- Test game progression past boot

---

## Where We Are Now

### Progress Snapshot (2026-02-15)

```
All: 36.15% matched, 2.07% linked (164 / 2,223 files)
  Code: 4,094,828 / 11,326,196 bytes (24,095 / 47,124 functions)
  Data: 4,893 / 5,957,220 bytes (0.08%)

Game Code:  70.85% matched (765KB / 1,080KB)
Milo Engine: 63.82% matched (3,329KB / 5,217KB)
Third-Party:  0.00% matched (Bink, curl, etc.)
XDK Code:     0.01% matched (excluded)
```

After register swap patcher: +36KB code, +101 functions matched.

### Orchestrator DB Progress

```
Non-excluded functions: 31,814
COMPLETE:  29,252 (91.9%)
AT_LIMIT:   1,938 (6.1%)
Remaining:    624 (2.0%)
Done total: 31,190 (98.0%)
```

A batch sweep of 3,380 previously-untracked functions found **2,774 hidden
100% matches** — template instantiations and inline functions emitted in
compilation units different from their "home" unit. These were invisible to
per-unit batch checking but found by objdiff symbol lookup.

### What "Matched" Means

- **36.15% matched** = objdiff fuzzy match across all code bytes (report.json)
- **2.07% linked** = units where every function is 100% (164 files)
- **98.0% of functions** are COMPLETE or AT_LIMIT in the orchestrator DB
- The gap between "98% functions done" and "36% code matched" is because
  the report counts code bytes (not function count), includes third-party/XDK
  code, and many of the remaining large functions are in the 36% denominator

### Linking Status

**Hybrid linking works today.** `ninja link` produces a 19.6MB PE:

```bash
ninja link  # Uses wine + X360 link.exe → build/373307D9/default.exe
```

But it's held together with `/FORCE` flags and has issues.

---

## What's Between Us and a Real Build

### The Honest Assessment

A "real build" means: link all code, produce an XEX, boot on Xenia, get past
the title screen. Here's what that requires and where we stand:

| Requirement | Status | Blocking? |
|-------------|--------|-----------|
| Code compiles | Yes (all decomp source builds) | No |
| Linker runs | Yes (with /FORCE) | No |
| PE produced | Yes (19.6MB) | No |
| XEX packaging | **Done** — `scripts/build_xex.py` | No |
| Clean link (no /FORCE) | No — 81 unique unresolved symbols (437 errors) | **Yes** |
| Code matches original | 36% by bytes, 70% for game code | Partial |
| Data sections present | Yes (from split objects) | No |
| .pdata (exception tables) | Partial — split objects in .pdat0 (invisible to kernel) | **Yes for EH** |
| Xenia built | **Yes** — 56.9 MB, requires headless mode for testing | **No** |
| Boot on Xenia | Blocked by GUI dependencies (GTK+, Cg, SSG) | **Yes — Headless Mode** |

### The Three Real Blockers

#### 1. Unresolved Symbols (81 unique, 437 total errors)

| Category | Errors | Unique Symbols | Fix |
|----------|--------|----------------|-----|
| DataArray::Node (ICF) | 86 | 1 | ICF merged — needs COMDAT aliasing |
| operator delete (ICF) | 64 | 1 | ICF merged — same address as other deletes |
| MemOrPoolFreeSTL (ICF) | 39 | 1 | ICF merged |
| .CRT dynamic initializers (`??__E`) | 24 | 24 | Expected for hybrid build |
| Exception handling (`__unwind`) | 15 | 15 | EH symbol mismatches |
| Local data labels (`lbl_*`) | 14 | 14 | dtk needs to globalize these |
| Ogg Vorbis | 13 | 5 | Missing split objects |
| Merged symbols (`merged_*`) | 11 | 4 | ICF aliasing needed |
| Other (individual symbols) | ~170 | ~17 | Mixed: decomp cross-refs, jump tables |

**Most impactful:** The ICF-merged symbols (DataArray::Node, operator delete,
MemOrPoolFreeSTL) account for 189 of 437 errors but are just 3 unique symbols.
These are structural — the original linker merged identically-compiled functions
to single addresses, and dtk's splitter can't reconstruct the aliases.

#### 2. .pdata Exception Handling

Split objects have their .pdata in renamed `.pdat0` sections (workaround for
a dtk bug). The Xbox 360 kernel's `RtlLookupFunctionEntry` only searches
`.pdata` — so exception unwinding won't work for functions from split objects.

**Impact:** C++ exceptions and stack unwinding in non-decomp code will crash.
DC3 rarely uses exceptions (mainly MIDI parsing), so this might not block
basic testing, but it's a correctness issue.

**Fix:** Either merge .pdat0 into .pdata post-link, or get dtk's section
merging working properly.

#### 3. XEX Packaging — DONE

The linker produces a PE, but Xbox 360 (and Xenia) expects an XEX container.

**Status:** `scripts/build_xex.py` creates a minimal XEX2 container around the PE.
Copies optional headers from the original XEX (entry point, execution ID, imports,
game ratings, TLS info, etc.). Unencrypted, raw compression — suitable for devkit
and Xenia testing.

---

## Phases to a Bootable Build

### Phase 0: What Works Today

```bash
ninja                              # Build all decomp .obj files
python3 scripts/fix_pdata.py       # Rename .pdata→.pdat0 in split objects (avoids LNK1223)
ninja link                         # Link hybrid PE (with /FORCE)
python3 scripts/build_xex.py       # Package PE → XEX2 container
scripts/compare_pe.py              # Compare against original (anchor-based)
```

The hybrid PE links decomp code alongside split objects from the original
binary. The XEX packer wraps it in a valid XEX2 container suitable for
Xenia testing. Output: `build/373307D9/default.xex` (~19.6MB).

### Phase 1: Clean Link (eliminate /FORCE)

**Goal:** Link without `/FORCE` — all symbols resolved, no duplicates.

| Task | Effort | Impact |
|------|--------|--------|
| Globalize `lbl_*` data labels in dtk | Medium (dtk PR) | Fixes 14 unresolved |
| Mark ICF functions as COMDAT | Medium (dtk/build change) | Fixes 189 errors (3 symbols) |
| Add missing split objects (Ogg Vorbis) | Small | Fixes 13 unresolved |
| Fix remaining jump table symbols | Small (dtk) | Fixes ~3 unresolved |
| Accept .CRT + decomp cross-refs | None | ~39 expected, resolve over time |

**Dependency:** Requires dtk changes (globalize locals, COMDAT marking).

### Phase 2: Fix .pdata + VA Shift

**Goal:** Valid exception tables, minimal section bloat.

| Task | Effort | Impact |
|------|--------|--------|
| Merge .pdata + .pdat0 post-link | Medium | Exception handling works |
| Eliminate extra sections (.xidata, .xedata, .CRT, .edata) | Medium | Reduce VA shift from +0x1800 |
| Relocation-aware PE comparison | Small | Accurate match % |

### Phase 3: XEX Packaging + Boot Test

**Goal:** Boot on Xenia, reach title screen.

Xenia does NOT load raw PE files — it requires XEX containers. The loader
checks magic bytes and rejects MZ/PE headers.

| Task | Effort | Impact |
|------|--------|--------|
| ~~XEX packaging~~ | ~~Medium~~ | **DONE** — `scripts/build_xex.py` |
| Xenia boot test with `--debug --break_on_start` | Small | First real validation |
| Analyze Xenia crash log (PC + register dump) | Small | Identifies first failure |
| Fix crashes iteratively (see debugging strategy below) | Ongoing | Progress toward boot |

**Alternative approach — patch original XEX:**
Instead of creating a new XEX from scratch, replace function bodies in the
original game's XEX with our recompiled code. This avoids the full XEX
creation pipeline and inherits all the original's section layout, imports,
and metadata. See `docs/sessions/2026-02-11-xexp-patch-generation-investigation.md`.

### Phase 4: Iterative Runtime Debugging

**Goal:** Debug and fix crashes until gameplay works.

This is where Xenia's debugging capabilities become critical. See the
debugging strategy section below for the full plan.

---

## Xenia Debugging Strategy

### What Xenia Gives Us

Xenia has extensive debugging infrastructure:

| Feature | How to Enable | What It Does |
|---------|---------------|--------------|
| Debug mode | `--debug` | Retains JIT debug info, disables context promotion |
| Break on start | `--break_on_start` | Pauses before guest code executes |
| Break on address | `--break_on_instruction=0xADDR` | int3 at specific PPC address |
| Conditional break | `--break_condition_gpr=N --break_condition_value=V` | Break when GPR matches value |
| ImGui debug window | `--imgui_debug` | Built-in PPC disassembly, registers, threads, memory |
| Verbose logging | `--log_level=3` | Full kernel call + module load logging |
| Shader dump | `--dump_shaders=path/` | Dump GPU shaders for rendering debug |

**On crash, Xenia logs:**
- Guest PPC address (program counter)
- Full register dump (r0-r31, FPRs)
- Exception type (memory violation, unimplemented instruction, etc.)
- Thread ID and kernel call history

### GDB Stub (Experimental)

[xenia-canary PR #388](https://github.com/xenia-canary/xenia-canary/pull/388)
adds a GDB stub server:

```bash
xenia --debug --gdbport 12345 game.xex
# Then: gdb -ex "target remote :12345"
```

Supports breakpoints, single-step, memory/register read, but is Windows-only
and unmerged. Would need a custom xenia-canary build.

### Debugging Workflow

```
1. Package PE → XEX (or patch original XEX)
2. Launch: xenia --debug --break_on_start --log_level=3 game.xex
3. Let it run → crashes → check xenia.log for PC + registers
4. Look up crash address in linker MAP file → identify function
5. Check if it's decomp code or split-object code:
   - Decomp: compare with objdiff, fix source, rebuild
   - Split: likely a seam issue (wrong struct layout, vtable offset)
6. Fix → rebuild → re-test → repeat
```

### The Two Testing Strategies

#### Strategy A: Full XEX Build (top-down)

Build the entire game from decomp+split objects, package as XEX, boot on
Xenia. Debug whatever crashes.

**Pros:** Tests everything at once, finds real integration issues
**Cons:** Many crashes at once, hard to isolate root cause

#### Strategy B: Function-Level Patching (bottom-up)

Start with the original working XEX. Patch individual decomp functions in,
test after each patch. If it crashes, the last patched function is the culprit.

**Pros:** Isolates failures precisely, incremental progress
**Cons:** Needs XEXP patch tooling (currently a gap — see below)

#### Recommended: Strategy A First, Strategy B for Debugging

Try booting the full hybrid build first — it might just work (or get
surprisingly far) since unmatched functions use original code. Use Strategy B
to isolate specific failures found during Strategy A.

### XEXP Patch Generation Gap

Xenia can consume `.xexp` delta patches (the same format as Xbox 360 title
updates), but no open-source tool can *generate* them. The XDK's `imagexex`
can, or we could build a custom patcher that:

1. Takes the original XEX
2. Decompresses/decrypts the embedded PE
3. Replaces specific function bodies with our compiled code
4. Repackages as a new XEX (or produces an XEXP delta)

See `docs/sessions/2026-02-11-xexp-patch-generation-investigation.md` for
the XEXP format details (Delta Patch Descriptor, block structure, SHA1
integrity).

### Runtime Validation Pipeline (Planned)

Previous design work (see `docs/sessions/2026-02-08-*`) outlined a
differential runtime validation approach:

1. Run original game on Xenia with breakpoints at function entry/exit
2. Capture register state snapshots (requires `--store_all_context_values`)
3. Run patched game with same breakpoints
4. Compare state vectors (floats with epsilon, pointer identity)
5. Report divergences with function-level attribution

This is the end goal — automated runtime regression testing for decomp
functions. Prerequisites: XEXP generation + Xenia GDB stub or headless
breakpoint capture.

---

## Code Coverage: What Matters for Booting

Not all code needs to be decomp'd to boot. The critical path is:

```
main() → App::Init() → loading screens → title screen
```

### Critical Subsystems

| Subsystem | Matched | Needed to Boot? | Notes |
|-----------|---------|-----------------|-------|
| App startup | ~48% | Yes | App.cpp, main entry |
| PlatformMgr | ~48% | Yes | Xbox platform init |
| Object system | ~85% | Yes | Core object model |
| File I/O | ~80% | Yes | Ark file loading |
| Rendering (rndobj) | ~64% | Yes | Must draw something |
| UI framework | ~75% | Yes | Menu rendering |
| Character system | ~70% | No (for boot) | Song gameplay |
| Networking | ~50% | No | Can stub/skip |
| Synth/audio | ~40% | Maybe | Audio init might be required |
| HAM gameplay | ~65% | No (for boot) | Song-specific |

### The Hybrid Advantage

Because we link decomp code alongside split objects from the original binary,
**unmatched functions still have working code** — they use the original
compiled objects. The build doesn't need 100% decomp coverage to run.

The risk is in the **seams**: places where decomp code calls into split-object
code or vice versa. If calling conventions, struct layouts, or vtable offsets
don't match exactly, these calls will crash.

---

## Data Matching (0.08%) — Is It a Problem?

**Short answer: No, for booting.**

Data sections contain globals, vtables, jump tables, string literals, and
RTTI. In the hybrid build, data comes from the original split objects —
it's already correct.

Data matching would matter for a **from-scratch build** (compiling everything
from decomp source with no original objects). That's a much later goal.

| Data category | Size | Status |
|---------------|------|--------|
| .data (globals) | ~500KB | From split objects (original) |
| .rdata (constants, vtables) | ~800KB | From split objects |
| .bss (uninitialized) | Variable | Generated at link time |
| .pdata (exception tables) | ~453KB | Split between .pdata/.pdat0 |

---

## Register Swap Patcher Impact

The patcher is a post-build tool (not run by default) that improves match%
by fixing register allocation differences in compiled .obj files:

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| All matched | 35.83% | 36.15% | +0.32% |
| Game Code | 69.92% | 70.85% | +0.93% |
| Milo Engine | 63.31% | 63.82% | +0.51% |
| Functions | 23,994 | 24,095 | +101 |

This improves objdiff reporting but **does not affect linking** — the linker
doesn't care about register allocation differences.

---

## Realistic Assessment

| Phase | Blocking On | Feasibility |
|-------|-------------|-------------|
| Phase 1 (clean link) | dtk changes | Achievable — well-understood fixes |
| Phase 2 (.pdata fix) | Post-link tooling | Achievable — known problem |
| Phase 3 (XEX + boot) | XDK imagexex or custom tooling | Medium — tooling exists, untested |
| Phase 4 (debug loop) | Xenia debug capabilities | Good — extensive debug infra available |

**The hybrid approach is our biggest advantage.** We don't need to decomp
everything to boot — the original code fills the gaps. The question is
whether the seams between decomp and original code are clean enough to
survive runtime.

**The fastest path to a first test** is to skip Phases 1-2 entirely:
take the current `/FORCE`-linked PE, wrap it in an XEX, and try to boot.
The crashes will tell us exactly what matters and what doesn't.

---

## Next Steps (Immediate)

1. ~~**Try XEX packaging now**~~ — **DONE.** `scripts/build_xex.py` wraps PE → XEX2.
   Full pipeline: `ninja && python3 scripts/fix_pdata.py && ninja link && python3 scripts/build_xex.py`

2. **Xenia Headless Mode** — **ACTIVE PRIORITY.** Modify Xenia to run without GUI
   dependencies (GTK+, NVIDIA Cg, SSG, GLXew). Current location: `/tmp/claude/xenia`

   Required changes:
   - Conditionally compile out `ui-*` targets (imgui, windowing)
   - Add `--headless` command-line flag
   - Implement console-based status output
   - Keep CPU/GPU emulation cores, drop UI layer

3. **Boot test on Xenia** — After headless fix, load `build/373307D9/default.xex` with
   `xenia --debug --headless --log_level=3`. Document:

3. **Investigate link quality** — Analyze the unresolved symbols and duplicate
   symbol warnings from the `/FORCE` link to understand what's actually broken
   vs. benign. Some unresolved symbols may not be on the boot path.

4. **dtk PR for symbol globalization** — The 96 `lbl_*` unresolved symbols
   are the biggest link-time blocker for a clean link. But the `/FORCE` link
   might work well enough to skip this for initial testing. See also:
   [LBL_SYMBOL_MATCHING.md](LBL_SYMBOL_MATCHING.md) for a two-phase plan to
   improve `lbl_` symbol matching in objdiff (Phase 1: map-based renaming,
   Phase 2: positional matching for function-local statics).

5. **Build XEXP patch tool** — For iterative debugging, we need a way to
   patch individual functions into the original XEX. This is the gap between
   "it crashes" and "we can isolate why."

## Import Resolution: Thunk Markers Plan

**Status:** ✅ COMPLETED (2026-02-17) — Full import resolution working with thunk section.

### Current State

The XEX boots successfully with **full import resolution** enabled:

| Component | Original PE | Decompiled PE |
|-----------|-------------|---------------|
| Variable imports (0x00XXXXXX) | RVA 0x600-0x1E48 | ✅ Copied from original |
| Thunk markers (0x01XXXXXX) | RVA 0xEE5544-0xEE6B04 | ✅ New section at RVA 0x140C000 |
| Import library header | VAs point to both locations | ✅ Patched to point to new thunk section |

### The Problem (SOLVED)

Xenia's import resolver expects the import_table to alternate between:
1. **Variable entries** — VAs pointing to `0x00XXXXXX` values (ordinal data)
2. **Thunk entries** — VAs pointing to `0x01XXXXXX` values (thunk markers)

The original thunk VAs (0x82EE5xxx) pointed to RVA 0xEE5xxx in the PE, which is in the middle of the `.text` section. Our decompiled PE has different code there, so we needed to create a new thunk section and patch the import_table VAs.

### Statistics

- 360 variable imports (type 0x00)
- 347 thunk imports (type 0x01)
- Thunk RVA range: 0xEE5544 - 0xEE6B04 (5 KB span)
- Space needed for thunk code: 347 × 16 bytes = 5552 bytes

### Solution: Add Thunk Section + Patch Import Header (IMPLEMENTED)

**Goal:** Create a dedicated thunk section and patch import_table VAs to point there. ✅ **COMPLETED**

#### Step 1: Create Thunk Marker Section

Add a new PE section (`.ithunk`) to hold thunk markers:

```
Location: After .idata at RVA 0x2B1000 (page-aligned)
Size: 347 thunks × 16 bytes = 0x2B00 (rounded to page = 0x3000)
Content: 0x01XXXXXX values (record_type=1, ordinal in low 16 bits)
```

#### Step 2: Generate Thunk Marker Data

For each thunk in the original import_table:
1. Extract the ordinal from the original thunk marker value
2. Generate new `0x01XXXXXX` value with same ordinal
3. Write to `.ithunk` section at sequential 16-byte offsets

#### Step 3: Patch Import Library Header

Modify `scripts/build_xex.py` to:
1. Parse the import_table entries
2. For each thunk entry (odd index):
   - Calculate new RVA in `.ithunk` section
   - Patch the VA to `image_base + new_rva`
3. Include the patched import library header in XEX

#### Implementation Details

```python
# Pseudocode for build_xex.py changes

def create_thunk_section(import_libs_info, image_base):
    """Create thunk marker section and return (data, va_mapping)."""
    thunk_data = bytearray()
    va_mapping = {}  # old_va -> new_va
    thunk_idx = 0

    for lib in import_libs_info['libraries']:
        for i, va in enumerate(lib['import_table']):
            if va == 0:
                continue
            rva = va - image_base
            # Thunks are at high RVAs (0xEE5xxx), vars at low (0x6xx)
            if rva > 0x1000000:  # Is thunk
                # Get ordinal from original thunk marker
                ordinal = read_ordinal_from_thunk(rva)
                # Generate new thunk marker
                marker = 0x01000000 | ordinal  # record_type=1
                offset = thunk_idx * 16
                struct.pack_into('>I', thunk_data, offset, marker)
                # Map old VA to new VA
                new_rva = 0x2B1000 + offset  # .ithunk section
                va_mapping[va] = image_base + new_rva
                thunk_idx += 1

    return bytes(thunk_data), va_mapping

def patch_import_table(orig_header, va_mapping):
    """Patch import_table VAs to point to new thunk section."""
    # For each import_table entry, if VA in va_mapping, patch it
    ...
```

#### Step 4: Integrate into PE

Options for adding `.ithunk` to the PE:
1. **Post-link patching** — Append section to linked PE
2. **Linker script** — Add section during link (requires XDK linker changes)
3. **XEX-level injection** — Add section when building XEX (simpler)

The XEX-level approach is cleanest: we already build the XEX from scratch, so we can inject a new section.

### Implementation Summary

| Task | Status | Notes |
|------|--------|-------|
| Create thunk section generator | ✅ Done | `generate_thunk_data()` in build_xex.py |
| Patch import_table VAs | ✅ Done | `patch_import_library_header()` in build_xex.py |
| Integrate into build_xex.py | ✅ Done | Lines 774-798 in `build_xex()` |
| Test with Xenia | ✅ Done | All 707 imports resolve successfully |

**Key Implementation Details:**
1. Fixed thunk detection threshold from 0x1000000 (16MB) to 0x100000 (1MB)
   - Thunks at RVA 0xEE5xxx (~15.6MB) were being missed
2. Added `orig_pe_data` parameter passing to `build_xex()` (line 1019)
3. Initialized `orig_pe_data = None` before try block to handle failures gracefully
4. Thunk section created at RVA 0x140C000 with 347 entries (5552 bytes + 2640 padding)
5. PE SizeOfImage extended from 0x12E0200 to 0x140E000

**Verification:**
```bash
python3 scripts/build_xex.py
# Output: Generated 347 thunk markers
#         Patched 347 thunk VA entries in import header

xenia-headless --target=build/373307D9/default.xex --headless_timeout_ms=25000
# Output: d3d9 - 318 imports
#         xboxkrnl - 379 imports
#         xbdm - 10 imports
#         BOOT: Title loaded successfully
```

---

## Related Documentation

- [X360 Linking Pipeline](../sessions/2026-02-11-x360-linking-pipeline.md) — full link status
- [.pdata Role in Linking](../sessions/2026-02-12-pdata-role-in-x360-linking.md) — exception handling
- [XEXP Patch Investigation](../sessions/2026-02-11-xexp-patch-generation-investigation.md) — delta patch format
- [Runtime Validation Tooling](../sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md) — Xenia breakpoint probes
- [Runtime Validation Ideas](../sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md) — differential testing design
- [XEX Format Reference](../reference/FREE60_XEX_FORMAT.md) — XEX2 header structure
- [Compiler Instrumentation](compiler-instrumentation.md) — register allocator research (DONE)
- [LBL Symbol Matching](LBL_SYMBOL_MATCHING.md) — plan to fix `lbl_` symbol matching for function-local statics (match% accuracy)
