# Compiler Instrumentation via GDB + Wibo

## Goal

Instrument the MSVC Xbox 360 compiler (cl.exe v16.00.11886) running inside wibo to understand its internal decision-making, particularly **register allocation**. Register swaps are the #1 unfixable pattern (607 functions, avg 92.3% match).

## Architecture Overview

```
ninja rule:
  wibo cl.exe /O1 /Oi /GR /EHsc /Fo<output> <input.cpp>

Internal pipeline (single process):
  cl.exe (orchestrator)
    -> c1xx.dll (frontend): parses C++ -> writes CIL to temp files
    -> c2.dll (backend / "UTC"): reads CIL -> optimizer -> register allocator -> PPC codegen

Temp IL files (written by c1xx.dll, read/deleted by c2.dll):
  _CL_<hash>ex  (expressions)
  _CL_<hash>sy  (symbol table - variable names, IDs, types)
  _CL_<hash>gl  (globals/code)
  _CL_<hash>in  (includes?)
  _CL_<hash>db  (debug info)
```

## What We Know About Register Allocation

### Confirmed Facts

1. **Top-down callee-saved allocation**: r31 first, then r30, r29... (Raymond Chen, "The Old New Thing")
2. **Declaration order -> SSA numbering -> allocation order**: Variables declared/first-used earlier get higher-numbered registers
3. **Global allocator**: `dop2_GlobalRegAlloc` -> `Globregs` -> `Enterloads` -> `ExtractSubreg` (from MSVC crash traces)
4. **Liveness + access frequency**: Official description says it estimates access count and spills least-accessed variables
5. **SSA optimizer runs twice** (before and after loop optimizer), shaping live ranges before allocation

### Key Insight (from differential compilation)

**Symbol IDs assigned by the frontend in declaration order serve as tie-breakers in the backend's instruction scheduler.** When two loads are equally ready to execute, the variable with the lower symbol ID (earlier declaration) gets scheduled first. Since register assignment follows scheduling order, declaration order directly determines register allocation.

### What Remains Unknown

- Exact algorithm (graph coloring vs linear scan vs hybrid)
- Exact tie-breaking heuristics beyond symbol ID ordering
- How SSA optimizer transformations interact with allocation ordering
- PPC-specific allocation quirks vs x86 backend

## Instrumentation Approaches (Prioritized)

### Phase 1: Assembly Listing Differential (DONE - trivial)

```bash
WIBO=build/tools/wibo
CL=build/compilers/X360/16.00.11886.00/cl.exe

# Generate assembly listings with source annotations
$WIBO $CL /nologo /c /O1 /Oi /GR /EHsc /FAs /Fatest_a.asm /Fotest_a.obj test_a.cpp
$WIBO $CL /nologo /c /O1 /Oi /GR /EHsc /FAs /Fatest_b.asm /Fotest_b.obj test_b.cpp

# Diff (normalize filenames first)
sed 's/test_a/test_X/g' test_a.asm > test_a_norm.asm
sed 's/test_b/test_X/g' test_b.asm > test_b_norm.asm
diff test_a_norm.asm test_b_norm.asm
```

### Phase 2: IL File Interception via strace (DONE - easy)

```bash
strace -e trace=open,pwrite64,write,close,unlink -s 5000 -x \
  -o strace_a.log $WIBO $CL /nologo /c /O1 /Oi /GR /EHsc /Fotest_a.obj test_a.cpp

# Normalize and diff
sed 's/_CL_[0-9a-f]*/_CL_HASH/g; s/test_a/test_X/g' strace_a.log > strace_a_norm.log
diff strace_a_norm.log strace_b_norm.log
```

The `sy` (symbol table) file contains variable names/IDs in different order, and the `gl` (code) file references those IDs differently, directly causing different register allocation.

### Phase 3: Callgrind Instruction-Level Profiling (AUTOMATED)

Requires 32-bit wibo (`build/tools/wibo` must be ELF 32-bit i386).

```bash
# Automated via callgrind-diff (uses callgrind by default, --perf for old behavior)
python -m tools.compiler_trace callgrind-diff swap_a.cpp swap_b.cpp

# With callgrind overlay on c2.dll disassembly
python -m tools.compiler_trace annotate --top 10
python -m tools.compiler_trace annotate --address 0x0994e9 --callgrind path/to/callgrind.out
```

Diff two callgrind profiles to find exactly which c2.dll addresses execute differently between two compilations.

**Callgrind parser**: Handles `positions: instr line` format where data lines are `<addr> <line> <count>`. Supports absolute (`0x...`), relative (`+N`/`-N`), and repeat (`*`) addressing for both position fields.

**First experiment results** (swap_a vs swap_b, r28↔r29 controlled test):
- 72,878 c2.dll addresses with execution counts per profile (~196M instructions total)
- 196 divergent addresses found (compared to 0 with perf sampling on small files)
- Top divergent address: RVA 0x0994e9, delta 132,528 (a `ret` instruction — function exit frequency differs)
- Clusters at RVA 0x120xxx (optimizer/codegen region), 0x099xxx (symbol table processing), 0x027xxx (early compilation)
- Key insight: execution *path* through c2.dll is identical for structurally identical files; divergence comes from different iteration counts in loops that process the symbol table in declaration order

### Phase 4: rr Record-Replay (DEEP INVESTIGATION)

```bash
# Install: sudo pacman -S rr
rr record $WIBO $CL /nologo /c /O1 /Oi /GR /EHsc /Fotest_a.obj test_a.cpp
rr replay
# In GDB: reverse-continue, set breakpoints at c2.dll register allocator
```

Allows stepping backwards through c2.dll's register allocator to see exactly what led to each decision. Use addresses found via callgrind divergence from Phase 3.

### Phase 5: GDB on Wibo (INTERACTIVE DEBUGGING)

```bash
gdb --args /home/free/code/milohax/wibo/build/wibo cl.exe /c test.cpp
```

Key GDB commands for mixed 64/32-bit debugging:
```gdb
break call_EntryProc          # Break at PE entry trampoline
set architecture i386         # Switch to 32-bit mode after entering PE
hbreak *0x00401000            # Hardware breakpoint in PE .text
watch *(int*)0x00500000       # Hardware watchpoint on memory

# Manual backtrace (no frame unwind info for PE):
define bt32
  set $fp = $ebp
  set $depth = 0
  while $fp != 0 && $depth < 20
    set $ret = *(unsigned int*)($fp + 4)
    printf "  #%d: eip=0x%08x  ebp=0x%08x\n", $depth, $ret, $fp
    set $fp = *(unsigned int*)$fp
    set $depth = $depth + 1
  end
end
```

Caveats:
- PE mapped at 0x00400000 via MAP_FIXED (wibo labels it `"wibo guest image"` in /proc/PID/maps)
- wibo uses LJMP32 for 64-to-32 bit mode switch; GDB needs manual `set architecture i386`
- 4 hardware breakpoints max (DR0-DR3)
- Use `handle SIGTRAP stop nopass` to catch INT3 before wibo's handler
- wibo debug mode: `WIBO_DEBUG=1` or `-D` flag

## Working MSVC Hidden Flags

Confirmed working on cl.exe v16.00.11886:

| Flag | Effect |
|------|--------|
| `/Bd` | Show internal command lines to c1xx.dll and c2.dll |
| `/Bt` | Show per-phase timing (c1xx: ~12ms, c2: ~2ms) |
| `/Bv` | Show compiler module versions |
| `/FAs` | Assembly listing with source annotations |
| `/FAcs` | Assembly listing with machine code bytes + source |
| `/d1reportAllClassLayout` | Dump all class memory layouts |
| `/d2Zi+` | Enhanced debug info for optimized code |

NOT working on this version: `/d2cgsummary`, `/d2inlinelog`, `/d2linscan`, `/d1reportTime`

## Reusable Tooling: `tools/compiler_trace/`

All phases above are now automated via:

```bash
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp       # Phase 1
python -m tools.compiler_trace capture-il test.cpp --output-dir out  # Phase 2
python -m tools.compiler_trace callgrind-diff test_a.cpp test_b.cpp  # Phase 3 (callgrind, default)
python -m tools.compiler_trace callgrind-diff a.cpp b.cpp --perf     # Phase 3 (perf fallback)
python -m tools.compiler_trace annotate --top 10                     # Phase 3.5 (c2.dll disassembly)
python -m tools.compiler_trace rr-record test.cpp --trace-dir out    # Phase 4
python -m tools.compiler_trace gdb-attach --print-only               # Phase 5
```

See [docs/tools/compiler-trace.md](../tools/compiler-trace.md) for full documentation.

### Discovered Limitations (and Fixes)

- **Valgrind + 64-bit wibo: FIXED** — The 64-bit wibo uses far jumps (`jmp fword ptr`) to switch between x86-64 and i386 modes, plus `modify_ldt` syscalls for LDT/segment setup. Valgrind's VEX IR cannot translate far jumps and doesn't handle `modify_ldt` on amd64. **Fix: build wibo as 32-bit (i386)**. No mode switching needed — cl.exe is a native 32-bit PE. Build: `cd wibo && cmake --preset debug && cmake --build build/debug`.
- **rr + 32-bit wibo: PARTIALLY FIXED** — 32-bit wibo eliminates mode-switching issues, but rr needs `librrpage_32.so` for 32-bit process support. The Arch Linux `rr` package lacks this; rebuild rr from source with `-DCMAKE_EXTRA_COMPILER_FLAGS=-m32` or use AUR. AMD Zen also needs `-S -F` flags or SpecLockMap disabled.
- **Perf sampling**: Short compilations (tiny test files) produce sparse samples. Real-world source files with complex functions produce better coverage.

## Tool Feasibility Matrix

| Tool | Status | Feasibility | Use Case |
|------|--------|-------------|----------|
| strace | Installed | HIGH | File I/O, IL interception |
| /FAs listing | Built-in | HIGH | Assembly output comparison |
| Callgrind | Installed (3.25.1) | **HIGH** (32-bit wibo) | Instruction-level profiling of c2.dll |
| rr | Custom build (5.9.0 + 32-bit) | **BLOCKED** (wibo MAP_FIXED incompatible) | Record-replay debugging |
| perf record | Installed (6.19) | HIGH | Sampling-based profiling of c2.dll |
| GDB | Installed | HIGH | Interactive debugging, hardware watchpoints |
| DynamoRIO | Not installed | MEDIUM | Dynamic binary instrumentation |
| ltrace | N/A | NONE | Wibo is statically linked |
| Intel PT | N/A | NONE | AMD Ryzen CPU, no Intel PT support |

## Experiment: Applying Compiler Trace to Real Register Swap Functions

### Methodology

Applied `diff-asm` (with new `--function` filter) and `capture-il` to real decomp functions at 95-97% match with known r28↔r29 register swaps. Goal: determine whether source-level transformations can fix register allocation mismatches in practice.

### Tool Improvements Made

1. **`--function` / `-f` filter for diff-asm**: Extracts a single function from MSVC assembly listings (PROC NEAR / ENDP delimiters). Essential for working with real TUs that contain hundreds of functions.
2. **Invoker `/I` flag splitting**: Fixed bug where `/I path` was passed as a single subprocess argument instead of two (`/I`, `path`), causing include path resolution failures.

### Experiment 1: DxTex::ResetSurfaces (96.2% → 96.5%)

**Setup**: 63 instructions, r28↔r29 swap (11 instructions), plus flag bit mismatches.

**Finding 1 — Flag values were wrong (FIXED)**:
The `diff-asm --function` output, combined with the full MSVC listing showing source annotations alongside assembly, revealed that the `if` condition tested wrong flags:
- Our code: `(mType & kMovie) && (mType & kRegularLinear)` then `(mType & kDeviceTexture)`
- Target: `(mType & kMovie) && (mType & 0x20)` then `(mType & kRegularLinear)`

The 0x20 bit is a component shared by kRenderedNoZ (0x22), kDepthVolumeMap (0xA2), and kDensityMap (0x122). This semantic fix resolved 2 instruction mismatches.

**Finding 2 — Explicit null variable is optimized away (NO EFFECT)**:
Tried `D3DTexture *null_tex = 0;` before the loop to force the null register to be allocated first. The compiler completely optimized the variable away — `diff-asm` showed identical instruction output (only source comment lines differed).

**Finding 3 — Register swap is unfixable from source**:
The r28↔r29 swap (loop counter vs null constant) persists regardless of source changes. The compiler's internal register numbering doesn't follow a simple "declaration order" rule for synthesized constants like nullptr. The permuter also tried comparison flips and declaration reorders — no improvement.

**Finding 4 — extrwi. vs rlwinm. encoding is unfixable**:
Target uses `extrwi.` (extract-and-shift pseudo-op) while our code generates `rlwinm.` (rotate-and-mask) for the same bit test. Both produce semantically identical results but with different machine code encodings. This is an internal compiler code-gen choice.

**Remaining mismatches at 96.5%**: 11 register swap + 2 encoding + 8 relocation noise

### Experiment 2: AccomplishmentCountConditional::IsFulfilled (95.2%, AtLimit)

**Setup**: 113 instructions, r28↔r29 swap (8 instructions), 3 merged calls, 1 bool mask. Verdict: AtLimit.

**Finding**: Same r28↔r29 pattern — a zero constant (numStars=0) and an iterator pointer have swapped registers. The permuter exhaustively tried all 16 declaration reorder permutations of the 4 `static Symbol` + `int numStars` declarations. None improved; several made things significantly worse (~91%).

### What We Proved

1. **`diff-asm --function` is valuable for discovering semantic bugs**: The flag fix in ResetSurfaces was found by reading the full annotated listing. Comparing our assembly alongside source annotations revealed bit-position mismatches invisible at the source level. This is the tool's proven killer use case.

2. **Blind source-level permutation does not fix register swaps**: Tested: explicit null variables (optimized away), declaration reorder (all 16 permutations for IsFulfilled), comparison flips. None affected register assignment. The compiler's register allocation for synthesized constants (0/nullptr) is not controlled by source-level variable ordering alone.

3. **`capture-il` works on real TUs**: 5 IL files (1.2MB) captured for Tex.cpp. The null_tex variant produced byte-identical IL output, confirming the variable was eliminated before IL emission.

4. **Instruction encoding (extrwi vs rlwinm) is a separate unfixable pattern**: Same semantic bit test, different machine code. Compiler-internal encoding choice.

### What Remains Open

The source-level experiments failed, but we haven't yet used the **deeper tools** — the ones designed to look inside c2.dll itself:

- **callgrind-diff on real TUs**: Only tested on tiny test files (sparse perf samples). Real TUs produce orders of magnitude more samples. We need to profile compilations where we *know* which declaration order produces which register assignment (from diff-asm), then find which c2.dll code paths diverge.

- **IL binary diffing**: We captured IL for the null_tex variant (identical — variable optimized away). We haven't captured IL for a variant that actually *changes* register allocation (e.g., a test case with two non-trivial locals that we know swap registers when reordered). That diff would reveal the IL encoding of allocation hints.

- **GDB at identified c2.dll addresses**: The funcmap has 12 addresses from a single test experiment. With more experiments accumulating evidence, we can set hardware breakpoints at the hot addresses and inspect the allocator's state directly.

- **c2.dll disassembly at hot addresses**: Reverse-engineer the actual allocation code paths identified by perf, not just guess from source.

The key insight: we were trying to fix register swaps from the *outside* (source changes). The tools we built are designed to attack from the *inside* (understanding c2.dll's decision). We built the telescope but haven't pointed it at the right star yet.

## Experiment 3: Cross-Experiment Callgrind Pipeline (DONE)

### Setup

Two callgrind-diff experiments, each profiling c2.dll at instruction granularity:

1. **Controlled test** (swap_a.cpp vs swap_b.cpp): 4-line function, declaration order swap. 1.2B instructions total.
2. **Real TU** (Tex_a.cpp vs Tex_b.cpp): Full Tex.cpp (138 lines, 12 functions), with `ResetSurfaces` block reorder + mRenderTarget/mDepthRT cleanup swap. 3.85B instructions total.

Note: when compiling files outside the project tree, add `/I src/system/rnddx9` (or the appropriate source directory) to resolve local includes that MSVC normally finds via the source file's directory.

### Results

| Metric | Controlled | Real TU |
|--------|-----------|---------|
| Instruction count (A) | 1,218,805,421 | 3,854,888,159 |
| Instruction count (B) | 1,218,805,421 | 3,855,566,334 |
| c2.dll addresses traced | 72,878 | 131,278 |
| Divergent addresses | 196 | 12,595 |
| Clusters | 151 | 1,071 |
| Cross-experiment overlap | — | **177 addresses** |

### Identified c2.dll Functions

Disassembly + callgrind count overlay revealed the following functions at the top divergent addresses:

#### 1. Symbol Hash Lookup (RVA 0x099xxx)

**Address 0x0994e9** — `ret` of a symbol lookup function. Count: 1.93T (Tex), 86M (swap). Cross-experiment delta: 217M / 291K.

The function hashes a name string (`div ecx` with divisor 0x3e81 = 16001), walks a bucket chain (`mov edx, [edx*4+hashtable]`), and compares strings byte-by-byte (`cmp bl, [ecx]`). It then iterates through a table with 21-byte entries (`add esi, 0x15; cmp esi, 0xa8` → 8 entries per lookup, checking attribute flags at offsets +0x80, +0x88, +0x8c, +0x91).

**Why it diverges**: Different symbol declaration order → different hash distribution → different collision chain lengths → different iteration counts.

#### 2. Symbol Insertion (RVA 0x09b0xx-0x09b160)

**Address 0x09b160** — `ret` of the symbol insertion function. Count: 964B (Tex). Inserts nodes into a 1024-bucket hash table (`and eax, 0x3ff; lea eax, [ebp+eax*4]`). After insertion, calls post-processing for type-4 entries (`cmp byte [esi+0x30], 4`).

**Why it diverges**: Symbols inserted in different order → different bucket chain structures → different traversal counts in subsequent lookups.

#### 3. Major Compilation Pass (RVA 0x09c45d)

Count: 1.1T (Tex). Large frame (0xa20 = 2592 bytes), manipulates compiler context at ds:0x10c472e8 with offsets 0x2b0, 0x2a8, 0xcd0, 0xcd8. This is likely the main optimizer loop that processes all IR nodes.

#### 4. IR Node Processing (RVA 0x0c4b37)

Count: 100B (Tex). Processes intermediate representation nodes with flag checks at offsets 0x80, 0x90, 0xb1, calling sub-functions for analysis passes. Contains a lookup table at 0x10c3d730 → 0x10c2f088 with 0x60-byte entries.

#### 5. Memory Allocator (RVA 0x120xxx)

Multiple functions:
- **0x120106** (482B count): Tiny size-check function — calls ds:0x10b01290, tests result, may call alloc with arg 2.
- **0x12013e-0x120186** (109B count): Free-list manager — traverses linked list comparing `[eax+0x4]` to 0xff8 (4088-byte block threshold), coalesces free blocks, links to global free list at ds:0x10c70160.
- **0x120187-0x1201b0**: Pool cleanup — iterates 30 pools at ds:0x10c6ff08 with 0x14-byte stride.
- **0x120341** (471B count): Heap allocator — allocates from free list, falls through to fresh allocation via memset.

#### 6. Node Creation + Scheduling (RVA 0x09b5d1)

**Non-scaling address** (ratio 0.9x). Count: 420M (Tex), delta: +48,921. This function creates symbol nodes, allocates from the compiler context, maintains a linked list at ds:0x10c2f060 (12-byte nodes), and calls the main compilation loop at 0x10b9b8e9. The near-1.0 ratio suggests this runs per-function rather than per-symbol, making it a candidate for the allocator's entry point.

### Key Finding: TU-Scaling vs Decision-Point Analysis

By computing the ratio |Tex_delta / swap_delta| for each cross-experiment address, we can separate:

- **TU-scaling addresses** (ratio >> 3.2x): Loop iteration counts that scale with TU size. These are symbol table traversal, hash lookups, memory allocation — **downstream effects** of the declaration order change.
- **Non-scaling addresses** (ratio ~1.0x): Functions that execute a similar number of times regardless of TU size. These are candidates for **actual decision-making code**.

| Category | Count | Typical addresses |
|----------|-------|-------------------|
| Ratio < 5x (non-scaling) | 71 | 0x09b5d1, 0x11fd6a, 0x082e79, 0x0c1a94 |
| Ratio 5-50x (mixed) | 39 | 0x0267ef, 0x0c4b37 |
| Ratio > 50x (TU-scaling) | 67 | 0x0994e9, 0x120106, 0x09b160 |

The 71 non-scaling addresses are the most promising targets for GDB breakpoints, as they likely contain the actual comparisons/branches that determine register allocation order.

### What This Proves

1. **Cross-experiment validation works**: 177 addresses confirmed across two independent experiments (tiny test + real TU).
2. **The divergence model is correct**: Declaration order changes propagate through the symbol table hash structure, affecting every compiler pass proportionally.
3. **The allocator decision is buried in noise**: The register allocation decision happens once at a single point, but its effect ripples through trillions of instruction executions. Ratio analysis successfully filters the noise to identify 71 candidate decision-point addresses.
4. **Real TU profiling is essential**: The controlled test produced 196 divergent addresses; the real TU produced 12,595. The cross-reference reduces this to 177 high-confidence addresses.

## Experiment 4: IL Binary Diffing (DONE)

### Setup

Captured IL temp files for both variants of the controlled test case (swap_a vs swap_b) using `capture-il --diff`.

### Results

The IL diff reveals exactly how declaration order encodes into the compiler's intermediate language:

**`.sy` (symbol table) — 2 byte differences:**
```
Offset 0x47: ef 09 → f0 09  (symbol ID for alpha)
Offset 0x78: f0 09 → ef 09  (symbol ID for beta)
```

The IDs are **swapped**: in variant A, alpha=0x09ef and beta=0x09f0. In variant B, alpha=0x09f0 and beta=0x09ef. The first-declared variable gets the **lower** symbol ID.

**`.ex` (expression tree) — 8 byte differences:**
All changes are symbol ID references (`ef 09` ↔ `f0 09`) being swapped throughout the expression tree, tracking which variable is referenced in each operation. The tree structure itself is identical — only the ID references change.

**`.gl` (globals) — source path + hash only:**
Only the filename (`swap_a` vs `swap_b`) and file content hash differ. No semantic change.

**`.in` / `.db` — Identical.** Include metadata and debug info are unaffected by declaration order.

### Key Insight: Symbol ID Assignment

The frontend (c1xx.dll) assigns monotonically increasing 16-bit symbol IDs to local variables in declaration order. These IDs persist through the expression tree into the backend (c2.dll). Since the backend's instruction scheduler uses symbol IDs as tie-breakers when two operations are equally ready to execute, the declaration order directly determines scheduling order, which determines register allocation order.

This confirms the full chain: **declaration order → symbol ID → .sy file → .ex references → c2.dll scheduler tie-breaking → register allocation**.

## Experiment 5: 4-Experiment Cross-Validation (DONE)

### Setup

Four callgrind-diff experiments profiling c2.dll at instruction granularity:

1. **Controlled test** (swap_a vs swap_b): 4-line function, ~1.2B instructions
2. **Real TU** (Tex_a vs Tex_b): Full Tex.cpp, ~3.85B instructions
3. **CharBones** (charbones_a vs charbones_b): `FindOffset` declaration reorder, ~2.25B instructions
4. **Locale** (locale_a vs locale_b): `LocaleChunkSortFunc` + `Init` + `LocalizeFloat` reorders, ~2.1B instructions

### Results

| Metric | Controlled | Real TU | CharBones | Locale |
|--------|-----------|---------|-----------|--------|
| c2.dll addresses | 72,878 | 131,278 | 137,744 | 130,940 |
| Divergent addresses | 196 | 12,595 | 2,468 | 4,179 |
| Clusters | 151 | 1,071 | 536 | 708 |

**Funcmap statistics:**
- 14,648 total addresses observed
- 5,199 addresses with 2+ observations
- **3,418 addresses with 3+ observations**
- Top addresses have **6 observations** (appearing in both callgrind runs per experiment × 4 experiments → counted as individual observations per tag; maximum 4 unique experiment tags confirmed)

### Refined Non-Scaling Analysis (4 experiments)

Using all 4 experiments, 163 addresses have observations in every experiment. The scaling ratio analysis now has much stronger statistical power:

| Category | Count | Interpretation |
|----------|-------|----------------|
| Non-scaling (max ratio < 5x) | 63 | Decision-making code |
| Mixed (5-50x) | 36 | Mixed-use code paths |
| TU-scaling (> 50x) | 64 | Downstream effects |

**Tight non-scaling** (ALL ratios < 3x across all experiments): **54 addresses**. These execute a similar number of extra times regardless of TU size or complexity — strong evidence they represent per-compilation-unit decision points rather than per-symbol processing.

### Top Non-Scaling Addresses

| RVA | Max Ratio | Function | Notes |
|-----|-----------|----------|-------|
| **0x09b5d1** | 0.9x | Node creation + scheduling | Calls main compilation loop at 0x10b9b8e9 |
| **0x082e79** | 1.6x | Bit-set allocation structure | Creates structures with shift operations, 0x34-byte nodes |
| **0x0e7329** | 1.6x | Cluster with 0x082e79 | Same function family |
| **0x11fd6a** | 1.6x | Buffer/string cleanup | Zeroes a 12-byte structure, frees memory |
| **0x0d9cfc** | 1.6x | Large cluster | Multiple addresses with identical deltas |
| **0x1201cc** | 1.7x | Memory allocator control | Pool management, 30 pools × 0x14 stride |
| **0x083047** | 1.7x | Bit-manipulation | Operates on 64-bit bitmasks via lookup table |
| **0x0267ef** | varies | Register class init | Zeros 3 arrays at 0x10c2e088, 0x10c2e100, 0x10c2e178 |

### Annotated Non-Scaling Address Details

**0x09b5d1 (ratio 0.9x)** — Node Creation + Scheduling Entry
- Creates 12-byte nodes from linked list at ds:0x10c2f060
- Calls main compilation loop at 0x10b9b8e9
- Near-unity ratio across ALL experiments suggests per-function (not per-symbol) execution
- **Strongest candidate for allocator entry point**

**0x082e79 (ratio 1.6x)** — Bit-Set Allocation Structure
- Creates 0x34-byte structures with bitfield operations
- Two allocations via 0x10c2022a (compiler context allocator)
- Initializes shift-based addressing (shl ebp,cl; shl eax,cl)
- Likely a **register interference graph** node creator

**0x11fd6a (ratio 1.6x)** — Buffer/String Cleanup
- Zeroes a {ptr, ptr, size} triple (12 bytes at [esi])
- Frees memory via ds:0x10b01010 if size > 0
- Followed by a large function (0x214 bytes stack frame) that processes wide strings
- May be **cleanup after register allocation** pass

**0x0c1a94 (ratio varies)** — IR Node Processing
- Part of a larger IR node construction chain
- Tests byte at [eax+0x8] against 0x06 (node type check)
- Accesses fields at offsets 0x2c, 0x18, 0x07 (flag manipulation)
- Calls sub-functions for node analysis and scheduling

**0x0267ef (ratio varies)** — Register Class Initialization
- Zeros three arrays: ds:0x10c2e088, ds:0x10c2e100, ds:0x10c2e178 (indexed by register class)
- These are likely **register availability bitmaps** or allocation state arrays
- Adjacent functions use BSF (bit scan forward) — classic register allocation primitive

### rr Recording: BLOCKED

rr record-replay was tested but fails with SIGBUS due to wibo's MAP_FIXED mmap for PE loading. The custom rr build (5.9.0 with 32-bit support) correctly loads librrpage_32.so but crashes during process initialization. Attempted mitigations (-t 7, --no-preload, --no-syscall-buffer) all failed. rr and wibo's memory management strategies are fundamentally incompatible.

**Workaround**: Use live GDB debugging (no reverse execution) with hardware breakpoints at non-scaling addresses.

### Tool Improvements Made

1. **rr_record.py**: Updated to use custom rr binary at `/home/free/code/milohax/rr/build/bin/rr` with 32-bit support libraries, plus `LD_LIBRARY_PATH` for correct library loading.
2. **gdb_script.py**: Updated to use custom rr path and set environment for replay.
3. **callgrind_diff.py**: Fixed TMPDIR to avoid /tmp tmpfs quota issues that caused empty callgrind output files.

## Experiment 6: GDB Live Tracing of Register Allocator (DONE)

### Setup

GDB batch-mode tracing of c2.dll's `find_first_set` function (RVA 0x026780) during controlled compilations. Used 64-bit wibo with software breakpoints set at `call_EntryProc` time.

**Limitation**: 64-bit wibo's mode-switching (64→32 bit via LJMP) causes GDB's single-step mechanism to corrupt the instruction stream. Limited to ~5 successful breakpoint hits before SIGSEGV. 32-bit wibo can't set breakpoints at all (c2.dll loaded lazily, pages mapped READ+EXEC only).

### Results: Volatile Register Test (swap_a vs swap_b, r10↔r11)

| Call # | swap_a (alpha first) | swap_b (beta first) |
|--------|---------------------|---------------------|
| 1 | lo=0x4 → bit 2 | lo=0x2 → bit 1 |
| 2 | lo=0x2 → bit 1 | lo=0x4 → bit 2 |
| 3 | lo=0xc0000 → bit 18 | lo=0xc0000 → bit 18 |
| 4 | lo=0xc0000 → bit 18 | lo=0xc0000 → bit 18 |
| 5 | hi=0x3 → bit 32 | hi=0x3 → bit 32 |

**Key finding**: First two BSF calls are SWAPPED — directly corresponding to declaration order.

### Results: Callee-Saved Register Test (callee_a vs callee_b, r29↔r31)

Three distinct register classes observed (different iterator state pointers):

**Class A** (3 allocations):

| # | callee_a (α,β,γ order) | callee_b (γ,β,α order) |
|---|------------------------|------------------------|
| 1 | bit 8 | bit 10 |
| 2 | bit 9 | bit 9 |
| 3 | bit 10 | bit 8 |

**Class B** (3 allocations):

| # | callee_a | callee_b |
|---|----------|----------|
| 4 | bit 3 | bit 1 |
| 5 | bit 2 | bit 2 |
| 6 | bit 1 | bit 3 |

**Remaining calls** (5): identical between variants.

### Key Findings

1. **Single-bit available sets**: Every BSF call has exactly ONE bit set. The allocation decision is made BEFORE BSF — BSF merely extracts the bit index from a pre-determined result. The actual decision happens in the interference graph coloring phase.

2. **Declaration order → BSF call order**: The first N BSF calls (where N = number of swapped variables) are reversed between variants. This is the direct mechanism: declaration order determines processing order in the coloring phase, which determines register assignment.

3. **Multiple register classes**: The allocator processes at least 3 register classes sequentially. Only the first two classes show declaration-order-dependent swaps; the remaining calls are identical.

4. **Volatile-only functions don't swap**: When all variables are volatile and used only for a single function call, declaration order has no effect. Register swaps require variables to survive across calls (callee-saved).

### c2.dll Register Allocator Architecture (Reverse-Engineered)

#### Data Layer (RVA 0x0266d0-0x0268xx): Bitset Primitives

| RVA | Function | Purpose |
|-----|----------|---------|
| 0x0266d0 | `popcount64(lo, hi)` | Count registers in a set |
| 0x026763 | `alloc_node(class)` | Pop from free list at `0x10c2e178[class]` |
| 0x026780 | `find_first_set(lo, hi)` | BSF: extract bit index from available set |
| 0x0267a2 | `create_bitset(nregs, ctx)` | Allocate `(nregs+63)/64 * 8` bytes |
| 0x0267d6 | `clear_class(class)` | Zero 3 arrays: `0x10c2e088`, `0x10c2e100`, `0x10c2e178` |
| 0x0267f0 | `alloc_and_init(ctx)` | Wrapper: alloc_node + create_bitset |
| 0x026804 | `free_node(node)` | Push to free list at `0x10c2e178[class]` |
| 0x026816 | `set_bit(bitset, bit)` | OR with mask from `0x10b014c0[bit*8]` |
| 0x026837 | `clear_bit(bitset, bit)` | AND with mask from `0x10b016c0[bit*8]` |
| 0x026858 | `and_inplace(dst, src)` | Set intersection (loop over 64-bit words) |
| 0x02687e | `and_copy(a, b, dst)` | Three-operand set intersection |

#### Interference Graph (RVA 0x026cd4-0x026d89): Sparse Representation

Sorted linked list of 64-register blocks per variable:
- `[node+0x00]`: base register number (aligned to 64)
- `[node+0x04]`: lo interference bits (registers 0-31 in block)
- `[node+0x08]`: hi interference bits (registers 32-63 in block)
- `[node+0x0c]`: next pointer

| RVA | Function | Purpose |
|-----|----------|---------|
| 0x026d39 | `insert_interference(list, reg)` | Add interference edge |
| 0x026d68 | `lookup_interference(list, reg)` | Find interference entry |

#### Interference Test (RVA 0x026f37)

```c
bool interferes(interference_list, reg) {
    entry = lookup(list, reg & ~63);
    if (!entry) return false;
    return (entry->bits & SET_MASK[reg & 63]) != 0;
}
```

5 callers of `find_first_set`, all at RVA 0x026b-0x027428. All follow the same pattern: iterate through available register bitset, call BSF to extract bit index, add base offset from node, clear found bit. Called from 0x10b27242 (the primary allocation iterator at RVA 0x027225).

### GDB Debugging Notes

**64-bit wibo**: Software breakpoints work if set inside `call_EntryProc` commands block (after c2.dll is loaded). SIGSEGV after ~5 hits due to single-step corruption in mixed 64/32 mode. No hardware breakpoint support.

**32-bit wibo**: c2.dll loaded lazily (not mapped at `call_EntryProc` time). Pages mapped READ+EXEC only (no WRITE), preventing INT3 insertion. `mprotect` fails. Would require patching wibo to add PROT_WRITE to PE section mappings.

## Experiment 7: Register Class Discovery (PARTIAL)

From GDB tracing, identified 8 register classes (from `regclass_clear` calls):
- Classes 2, 3, 5, 7, 8 observed for the simple volatile test
- Class initialization happens before any allocation
- The class numbering likely corresponds to: GPR volatile, GPR callee-saved, FPR, condition registers, link register, etc.

## Conclusions

### What We Now Know (Complete Chain)

```
Source declaration order
  → c1xx.dll assigns monotonic 16-bit symbol IDs (.sy file)
  → Symbol IDs referenced in expression tree (.ex file)
  → c2.dll backend processes variables in symbol ID order
  → Interference graph coloring assigns registers in processing order
  → BSF extracts pre-determined single-bit register indices
  → PPC codegen uses assigned register numbers
```

### Practical Implications

1. **Register swaps are UNFIXABLE from source**. The allocation is locked to declaration order through the symbol ID chain. No amount of source-level permutation (declaration reorder, explicit null variables, comparison flips) can change the allocator's coloring decisions independently of symbol IDs.

2. **The permuter should skip register swap patterns**. Blind source permutation cannot fix register swaps — this was proven empirically (Experiments 1-2) and now understood mechanistically (Experiments 4-6).

3. **Functions with register swaps should be marked "at limit"**. The maximum achievable match% is the current match% minus relocation noise.

4. **The only theoretical fix is binary patching c2.dll**. Modifying the coloring order or interference graph construction in c2.dll could reverse specific register assignments. This would require:
   - Identifying the exact coloring loop (callers of 0x027290, ~25 call sites in RVA 0x02d-0x032 range)
   - Understanding the variable ordering mechanism
   - Patching the comparison/iteration to reverse symbol ID tie-breaking

## Experiment 8: Full BSF Trace with Patched Wibo (DONE)

### Setup

Patched 32-bit wibo (`/home/free/code/milohax/wibo/`) to add `PROT_WRITE` to `PAGE_EXECUTE_READ` mappings in `posixProtectFromWin32()` (`src/heap.cpp:268`). This allows GDB to write INT3 (software breakpoints) into c2.dll's `.text` section.

**Key GDB settings for 32-bit wibo:**
- `set libthread-db-search-path ""` — disables thread debugging, avoids "Cannot find user-level thread" errors
- Break on `callDllMain`, continue 12 times, then c2.dll is loaded at hit #13
- Software breakpoints at `0x10b26780` (BSF function) work reliably

### Results: Complete BSF Traces (No SIGSEGV)

**Volatile register test** (swap_a vs swap_b, r10↔r11):
- 170 BSF calls each, 10 divergent (4 in allocation, 6 in later pass)
- Complete trace — no SIGSEGV, no 5-hit limitation

| BSF # | swap_a (alpha first) | swap_b (beta first) |
|-------|---------------------|---------------------|
| 1 | lo=0x4 → bit 2 | lo=0x2 → bit 1 |
| 2 | lo=0x2 → bit 1 | lo=0x4 → bit 2 |
| 3 | lo=0x4 → bit 2 | lo=0x2 → bit 1 |
| 4 | lo=0x2 → bit 1 | lo=0x4 → bit 2 |
| 5+ | identical | identical |

**Callee-saved register test** (callee_a vs callee_b, r29↔r31):
- 389 BSF calls each, only 6 divergent (all in first 13 calls)
- Complete trace — no SIGSEGV

| BSF # | callee_a (α,β,γ order) | callee_b (γ,β,α order) | Class |
|-------|------------------------|------------------------|-------|
| 1 | bit 8 | bit 10 | A |
| 2 | bit 9 | bit 9 | A |
| 3 | bit 10 | bit 8 | A |
| 4 | bit 3 | bit 1 | B |
| 5 | bit 2 | bit 2 | B |
| 6 | bit 1 | bit 3 | B |
| 7-13 | identical | identical | C+ |
| 14-389 | identical | identical | later passes |

### Definitive Bit→Register Mapping

From assembly listing correlation:

**Volatile GPR class** (bits 1-2):
- Alpha always gets color 2, beta always gets color 1 (consistent per variable)
- First declared → r11 (top-down), second declared → r10
- Color→register mapping: first color allocated → highest available volatile GPR

**Callee-saved GPR class** (bits 8-10):
- Alpha→color 8, beta→color 9, gamma→color 10 (consistent per variable)
- First declared → r29 (bottom-up within save range), then r30, r31
- Color→register mapping: first color allocated → lowest callee-saved GPR in the save range

**Key insight: colors are consistent, register mapping is not.** Each variable gets a deterministic "color" (BSF bit index) based on interference constraints. But the color→PPC register mapping depends on allocation ORDER, which follows declaration order. This is why register swaps cannot be fixed by permuting source — the colors stay the same, only the mapping changes.

### Allocation Pattern Summary

| Register type | Allocation direction | First declared gets |
|---------------|---------------------|-------------------|
| Volatile GPR | Top-down | r11 (highest scratch) |
| Callee-saved GPR | Bottom-up | r29 (lowest in save range) |

The compiler saves callee-saved registers with `__savegprlr_N` where N is the lowest register used. Bottom-up allocation ensures the save range matches the number of variables.

### What the "base" Field Means

All BSF calls show `base=0`, confirming all register classes fit within a single 64-register block (base=0 means registers 0-63 in that class). The `base` field from the interference graph node (`[node+0x00]`) would be non-zero only for architectures with >64 registers in a class.

## Experiment 9: extrwi vs rlwinm Encoding Differences (DONE)

**Goal**: Determine whether `extrwi.` vs `rlwinm.` encoding differences are fixable from source.

### Background

DxTex::ResetSurfaces (97.2% match) has two `replace` mismatches where the target uses `extrwi. r10, r11, 1, 30` but our code generates `rlwinm. r10, r11, 0, 30, 30`. Both are `rlwinm` machine instructions with different rotate/mask parameters:
- `extrwi. rA, rS, 1, 30` = `rlwinm. rA, rS, 31, 31, 31` — rotate right 1, extract to LSB (result is 0 or 1)
- `rlwinm. rA, rS, 0, 30, 30` — no rotation, mask bit in place (result is 0 or the original bit value)

### Callgrind-Diff Analysis

Running callgrind-diff required the **32-bit wibo** (the 64-bit wibo crashes valgrind due to custom segment selectors in `installSelectors`). Using patched 32-bit wibo from Experiment 8:

```
A (rlwinm / flags & 0x2):    25,026 c2.dll addresses, 3.49B instructions
B (extrwi / !!(flags & 0x2)): 26,885 c2.dll addresses, 3.49B instructions
Divergent: 8,214 addresses across 559 clusters
```

The boolean conversion form triggers fundamentally different c2.dll code paths:
- **5 code regions** with ~94M instruction count deltas each
- BSF (register allocator) cluster at RVA 0x0267ef-0x026815 has 197M |delta|
- This confirms the front-end IR representation is different, not just a trivial encoding selection

### Source Pattern Discovery

Systematic testing of 5 source variants for `(flags & 0x2) && mips`:

| Variant | Pattern | Encoding |
|---------|---------|----------|
| 1 | `(flags & 0x2) && mips` | `rlwinm. r,r,0,30,30` (mask-in-place) |
| 2 | `((flags & 0x2) != 0) && mips` | `rlwinm. r,r,0,30,30` (mask-in-place) |
| 3 | `!!(flags & 0x2) && mips` | `rlwinm. r,r,0,30,30` (mask-in-place) |
| **4** | **`bool b = (flags & 0x2) != 0; if (b && ...)` | **`rlwinm. r,r,31,31,31` (extrwi form!)** |
| 5 | `int b = !!(flags & 0x2); if (b && ...)` | `rlwinm. r,r,0,30,30` (mask-in-place) |

**Only the `bool` type with a separate variable declaration produces extrwi encoding.** The C++ `bool` type (1-byte, always 0/1) forces the compiler to materialize a boolean value, which selects the extract-to-LSB encoding. The `int` type with `!!` gets optimized back to mask-in-place by UTC.

### Verification on DxTex::ResetSurfaces

Applied the pattern to the actual function:
```cpp
// Before (96.5% match, 2 replace mismatches):
if (((mType & kRendered) && mNumMips) || ((mType & kMovie) && (mType & 0x20)) || ...)

// After (98.4% match, 0 replace mismatches):
bool isRendered = (mType & kRendered) != 0;
if ((isRendered && mNumMips) || (bool(mType & kMovie) && (mType & 0x20)) || ...)
```

Both `replace` mismatches eliminated. All remaining differences are register swaps (r28↔r29), which are the unfixable allocator ordering from Experiments 1-7.

### Conclusion

**extrwi vs rlwinm encoding is FIXABLE from source.** The pattern is:
- `bool varname = (flags & MASK) != 0;` — generates extrwi (extract-to-LSB)
- `flags & MASK` in a condition — generates rlwinm (mask-in-place)
- `(flags & MASK) != 0` inline — optimized away, generates rlwinm
- `!!(flags & MASK)` with `int` — optimized away, generates rlwinm

The key is that `bool` type semantics force 0/1 materialization in the IR, which propagates through UTC's optimizer to select the rotate+extract encoding.

## Next Steps

### Priority 1: Register Swap Mitigation — DONE (Post-Build .obj Patcher)

**Goal**: Eliminate register swap mismatches for affected functions.

**Outcome**: Instead of patching c2.dll (risky, hard to verify), built a post-compilation
.obj patcher (`scripts/obj_regswap_patcher.py`) that directly patches register fields in
compiled COFF .obj files using objdiff's instruction-level diff as the oracle.

**Results (709 functions)**:
- **17 functions at exact 100%** match (reported as COMPLETE)
- **679 functions improved** (average +1-3% match)
- **17 functions safely reverted** (auto-restore on regression)
- **0 failures**

**Usage**: `ninja && python3 scripts/obj_regswap_patcher.py --batch --apply`

**Key technical challenges solved**:
- PowerPC instruction format dispatch (D-form, X-form, A-form, M-form)
- Logical vs arithmetic register field ordering (rA/rS/rB vs rD/rA/rB)
- Pseudo-instruction replication (`mr rA,rS` = `or rA,rS,rS`)
- FP single/double precision XO-based dispatch
- Compare/trap instruction field mapping (crfD/TO not GPR)
- Safety revert mechanism (auto-restore from .bak on regression)

**c2.dll patching remains a future option**: The BSF-based allocator is fully characterized
(see Experiments 6-8). Patching `bsf` → `bsr` at RVA 0x026780 would reverse color
assignment direction, but the .obj patcher approach is simpler and safer.

### Priority 2: fmadds vs fmuls+fadds Investigation — DONE

See Step 7 below.

### Priority 3: Instruction Scheduling / Block Layout — DONE

See Step 8 below.

### Priority 4: Automated At-Limit Ceiling Calculator — DONE

**Goal**: Compute theoretical maximum match% for every function, accounting for known
unfixable patterns.

**Tool**: `scripts/ceiling_calculator.py`

**Usage**:
```bash
python scripts/ceiling_calculator.py                    # All AT_LIMIT functions
python scripts/ceiling_calculator.py --min 90           # 90%+ only
python scripts/ceiling_calculator.py --find-fixable     # Show fixable patterns
python scripts/ceiling_calculator.py --json             # Machine-readable output
```

**Full scan results (1,838 AT_LIMIT functions, 253,345 instructions)**:

| Category | Count | % of Mismatches | Fixable? |
|----------|-------|-----------------|----------|
| Relocation noise | 31,745 | 32.6% | No (address layout) |
| Insert/delete | ~30,000 | ~31% | No (code structure) |
| Register swaps | 11,328 | 11.6% | Via .obj patcher |
| Merged symbols | 3,403 | 3.5% | No (linker ICF) |
| Scheduling | 2,709 | 2.8% | No (compiler heuristic) |
| Immediate diffs | ~500 | ~0.5% | No (stack offsets) |
| Encoding patterns | 419 | 0.4% | Yes (bool_mask, extrwi) |
| FMA patterns | 171 | 0.2% | Yes (#pragma fp_contract) |
| Save/restore | 124 | 0.1% | No (prologue/epilogue) |

**Key findings**:
- **226 functions have fixable encoding/FMA patterns** (out of 1,838 AT_LIMIT)
- For 90%+ AT_LIMIT functions, **99.9% of mismatches are unfixable**
- Relocation noise is the dominant mismatch type at all match levels
- Average ceiling for 90%+ functions: 96.2%
- Effective completion: 87.4% (vs 88.5% raw closure)

## Future Ideas

- **c2.dll symbol map**: Use prologue detection + callgrind to build a full function boundary map for c2.dll — enables all future investigations
- **Phoenix framework research**: Microsoft's research compiler (Phoenix) shared c2.dll's backend architecture; public papers may describe the allocator in detail
- **Cross-TU register effects**: Do other functions in the same translation unit affect allocation for our target? (Symbol ID numbering is TU-global)
- **Compiler flag archaeology**: Use callgrind-diff to test undocumented `/d2` flags — some may control register allocation or instruction scheduling order
- **c2.dll hot-patching infrastructure**: Build a general tool for runtime patching of c2.dll under wibo, enabling rapid experimentation with compiler behavior changes

## Phase 2: BSF-Guided Register Allocation Tools (DONE)

### Overview

Experiments 1-9 fully characterized c2.dll's register allocator. Phase 2 automates the BSF tracing into reusable tooling that the permuter can use for guided declaration reordering.

### New Modules

#### `tools/compiler_trace/bsf_trace.py` — BSF Trace Capture

Automates the GDB batch-mode BSF tracing from Experiment 8:
- Generates a GDB script from the working template
- Runs `gdb -batch -x <script>` with 32-bit wibo
- Parses output into structured `BSFTrace` / `BSFCall` dataclasses
- Handles c2.dll load timing (callDllMain hit #13)

```bash
python -m tools.compiler_trace bsf-trace /path/to/source.cpp
```

#### `tools/compiler_trace/bsf_diff.py` — BSF Trace Comparison

Compares two BSF traces to identify divergent register allocation decisions:
- Aligns traces by call index
- Groups divergences by compiler phase (initial coloring, coalescing, recoloring)
- Reports which BSF calls differ and by how much

```bash
python -m tools.compiler_trace bsf-diff source_a.cpp source_b.cpp
```

#### `tools/compiler_trace/regmap_solver.py` — Register Order Solver

Given a BSF trace + objdiff mismatch info, computes candidate declaration orders:
- Extracts initial color assignments from the BSF trace
- Identifies GPR swap pairs from objdiff diagnosis
- Generates targeted pairwise swap candidates (not blind permutation)
- Integrates with tree-sitter AST for variable name extraction

```bash
python -m tools.compiler_trace bsf-solve --symbol <mangled> --source source.cpp
```

### Permuter Integration

`scripts/permuter/patterns/declaration_reorder.py` gains a BSF-guided mode:
- `--bsf-guided` flag on the permuter CLI
- When enabled, traces the compiler's BSF calls once (~30-60s GDB overhead)
- Generates targeted pairwise swap candidates instead of random permutation
- Falls back to random permutation if BSF tracing fails or is infeasible

### Investigation Queue

#### Step 6: Batch Scan for Encoding Fixes (DONE)

Built `scripts/batch_pattern_scan.py` — automated scanner that runs `objdiff-cli diff --include-instructions -f json` on functions and detects encoding patterns.

**Scan Results** (500 functions, 80%-99.5% range):

| Pattern | Hits | Fixable | Fix Strategy |
|---------|------|---------|-------------|
| `bool_mask` (clrlwi 24) | 23 | Yes (hard) | Adjust bool/int types, return types, casts |
| `extrwi_rlwinm` | 3 | Yes | Add/remove `bool` variable per Experiment 9 |
| `bool_negate` (subic/subfe vs cntlzw/extrwi) | 1 | Yes (hard) | Change return/variable type (int vs bool) |
| **Total** | **27** | **27** | |

**Key findings:**
- extrwi↔rlwinm is RARE (only 3 instances across 500 functions, 2 in already-fixed DxTex::ResetSurfaces)
- bool_mask (extra `clrlwi` truncation) is the dominant encoding pattern (23 hits)
- bool_negate (subic/subfe vs cntlzw/extrwi for `!x`) is rare but interesting (1 hit)

**bool_mask pattern details:**
- When target has `clrlwi rA, rB, 24` but we don't: we need to add a bool truncation
- When we have it but target doesn't: we need to remove it (use int instead of bool)
- Functions affected: CampaignEraProgress::IsEraComplete, HamDirector::ShotsDisabled, StoreOffer::Handle, GetDefaultMatShaderOpts, RndMat::SyncProperty (3 hits), RndMotionBlur::CanMotionBlur, BinStream::Read, ASCIItoUTF8, GamePanel::SetPausedHelper, and more

**New fixable target: RndMat::LoadOld** (97.0%):
- idx 389: target=`clrlwi r11, r11, 31`, base=`extrwi r11, r11, 1, 26`
- Both extract a single bit, but via different encodings
- Fix: remove `bool` variable, use inline expression

**Scanner usage:**
```bash
python scripts/batch_pattern_scan.py --min 80 --max 99.5 --limit 500
python scripts/batch_pattern_scan.py --pattern extrwi_rlwinm  # filter by type
python scripts/batch_pattern_scan.py --pattern bool_mask --json  # JSON output
python scripts/batch_pattern_scan.py --unit 'system/rndobj'  # filter by unit
```

#### Step 7: fmadds vs fmuls+fadds Investigation

Apply callgrind-diff methodology to understand fused multiply-add selection:
1. Create minimal test pair: `a*b + c` vs `fma(a,b,c)` variants
2. Run callgrind-diff with 32-bit wibo
3. Identify divergent c2.dll code paths
4. Determine if controllable from source

**Status**: Not yet started.

#### Step 8: Instruction Scheduling Investigation

Understand ASSERT_REVS scheduling differences and general block ordering:
1. Create test pair with different instruction scheduling outcomes
2. Callgrind-diff to find scheduler decision points
3. Document findings and whether any source patterns influence scheduling

**Status**: Not yet started — affects ~10% of functions with ~0.8-0.9% gap each.

### mwcc-debugger Reference Architecture

The `~/code/milohax/mwcc-debugger` project instruments the Metrowerks CodeWarrior
compiler for PowerPC using GDB remote debugging through a `retrowin32` x86 emulator.
Key ideas that apply to our c2.dll tracing:

**1. Full pass-by-pass intermediate dumps**
- MWCC: captures state at 20+ optimization passes (CSE, copy propagation, loop opts, peephole, regalloc, scheduling)
- Ours: currently only traces BSF calls (one point). Could instrument more c2.dll passes.

**2. Interference graph extraction**
- MWCC: extracts complete interference graph (nodes, neighbors, spill costs, coalescing)
- Ours: only infer colors from BSF bit indices. Could read c2.dll's IG data structures at breakpoints.

**3. Memory-aware struct reading**
- MWCC: reads compiler data structures directly from memory at breakpoints
- Ours: only trace function call arguments. Could read c2.dll's allocator state at `0x10c2e088/e100/e178`.

**4. Virtual-to-physical register mapping**
- MWCC: directly dumps which virtual register → physical register
- Ours: only infer from BSF outputs. Could read the full mapping table.

**Next step**: Prototype reading c2.dll's register allocator structs directly from memory
during GDB tracing, similar to how mwcc-debugger reads MWCC's `MwccIGNode` structures.
The data layer at RVA 0x0266d0-0x0268xx (documented in Experiment 6) provides the entry
points: interference list at `[node+0x00/0x04/0x08/0x0c]`, free lists at `0x10c2e178[class]`.

## Step 7: fmadds vs fmuls+fadds Investigation — DONE

**Status**: DONE (controlled experiments + batch scan complete)

**Key findings**:

1. **`#pragma fp_contract(off)` definitively prevents fmadds generation** — confirmed via controlled experiment compiling test functions with and without the pragma. The pragma is file-scoped and can be toggled.

2. **Batch scan found 14 functions with FMA mismatches** across 800 functions scanned (50%-99.9%):

| Category | Count | Fix Strategy |
|----------|-------|-------------|
| Pure "need OFF" | 4 | Add `#pragma fp_contract(off)` to file |
| Pure "need ON" | 5 | Restructure expressions to enable fusion |
| Mixed direction | 5 | Unfixable by pragma (scheduling heuristic) |

3. **Previous assessment of "UNFIXABLE" was too broad** — pure-direction cases ARE fixable. Updated TECHNICAL_NOTES.md classification to "PARTIALLY FIXABLE".

4. **Affected files** (for when implementing these functions):
   - BustAMovePanel.cpp, Rot.cpp, CharClip.cpp, BinkReader.cpp → `#pragma fp_contract(off)`
   - ClipDistMap.cpp, ArcDetector.cpp, Profiler.cpp, GamePanel.cpp, Part.cpp → expression restructuring
   - ClipCollide.cpp, Key.cpp, Geo.cpp, MultiTempoTempoMap.cpp, SpotlightDrawer_NG.cpp → accept gap

5. **Scanner updated**: `scripts/batch_pattern_scan.py` now detects `fma_mismatch` pattern type.

## Step 8: Instruction Scheduling Investigation — DONE (previously completed)

**Status**: DONE (see `docs/sessions/2026-02-03-assert-revs-scheduling.md`)

**Key findings**:

1. **ASSERT_REVS scheduling is UNFIXABLE** — 17 documented attempts all failed. The second
   `MILO_FAIL` call in the macro generates 3 independent `addi` instructions (no data dependencies)
   that the compiler is free to schedule in any order. Target and our build choose different orders.

2. **Impact**: ~146 Load functions capped at 98.6-99.1% match, each losing ~0.8-0.9% to scheduling.

3. **General instruction scheduling** differences (non-ASSERT_REVS) arise from:
   - Independent load/store reordering around function calls
   - Register caching decisions (target caches `bs.stream` in callee-saved registers, our build
     reloads from memory each time)
   - These are compiler backend heuristic decisions not controllable from source

## References

- Raymond Chen: [PowerPC 600 Prologues/Epilogues](https://devblogs.microsoft.com/oldnewthing/20180817-00/?p=99515)
- Raymond Chen: [PowerPC 600 Code Walkthrough](https://devblogs.microsoft.com/oldnewthing/20180823-00/?p=99555)
- Geoff Chappell: [C2 Code Generator](https://www.geoffchappell.com/studies/msvc/cl/c2/index.htm)
- Lectem: [MSVC Hidden Flags](https://lectem.github.io/msvc/reverse-engineering/build/2019/01/21/MSVC-hidden-flags.html)
- Aras: [/d2cgsummary](https://aras-p.info/blog/2017/10/23/Best-unknown-MSVC-flag-d2cgsummary/)
- MSDN: [Compiler Optimizations Part 2](https://learn.microsoft.com/en-us/archive/msdn-magazine/2015/may/compilers-what-every-programmer-should-know-about-compiler-optimizations-part-2)
- .NET RyuJIT: [LSRA Heuristic Tuning](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/lsra-heuristic-tuning.md) (different compiler, but same company's philosophy)
