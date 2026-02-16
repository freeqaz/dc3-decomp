# Build Roadmap: Path to a Working Executable

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
| Boot on Xenia | Not attempted | Unknown |

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

2. **Boot test on Xenia** — Load `build/373307D9/default.xex` on Xenia with
   `--debug --log_level=3`. Even a crash is valuable — it tells us where the
   first failure is. Requires Xenia on a Windows machine (not available in dev env).

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

## Related Documentation

- [X360 Linking Pipeline](../sessions/2026-02-11-x360-linking-pipeline.md) — full link status
- [.pdata Role in Linking](../sessions/2026-02-12-pdata-role-in-x360-linking.md) — exception handling
- [XEXP Patch Investigation](../sessions/2026-02-11-xexp-patch-generation-investigation.md) — delta patch format
- [Runtime Validation Tooling](../sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md) — Xenia breakpoint probes
- [Runtime Validation Ideas](../sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md) — differential testing design
- [XEX Format Reference](../reference/FREE60_XEX_FORMAT.md) — XEX2 header structure
- [Compiler Instrumentation](compiler-instrumentation.md) — register allocator research (DONE)
- [LBL Symbol Matching](LBL_SYMBOL_MATCHING.md) — plan to fix `lbl_` symbol matching for function-local statics (match% accuracy)
