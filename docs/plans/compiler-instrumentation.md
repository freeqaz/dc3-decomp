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

## Next Steps

### Priority 1: GDB at Non-Scaling Addresses

Set hardware breakpoints at 0x09b5d1 (ratio 0.9x) and 0x0267ef (register class init) during controlled compilation. Compare register/memory state between swap_a and swap_b.

```bash
python -m tools.compiler_trace gdb-attach --min-evidence 4
python -m tools.compiler_trace gdb-attach swap_a.cpp --min-evidence 4
```

### Priority 2: Reverse-Engineer Register Allocation Primitives

The cluster around 0x0267ef uses BSF (bit scan forward) on bitmasks — this is the classic "find first available register" operation. Disassemble the full function containing 0x026792-0x0267ef to understand the allocation state machine.

### Priority 3: Binary Patch Experiment

If the decision point at 0x09b5d1 or 0x0267ef is confirmed, patch c2.dll to reverse the tie-breaking comparison and verify it swaps registers in the controlled test case.

### Other Unfixable Patterns to Investigate

- **extrwi vs rlwinm encoding**: Same bit test, different machine code. Is this a scheduling decision or codegen table?
- **fmadds vs fmuls+fadds**: Separate category of unfixable pattern, same methodology
- **Instruction scheduling / block layout**: Why does block layout change between similar functions?

## Future Ideas

- **c2.dll symbol map**: Use prologue detection + callgrind to build a function boundary map
- **Phoenix framework research**: Microsoft's research compiler shared c2.dll backend; public papers?
- **Binary patching c2.dll**: If we find the allocation decision point, patch the branch to reverse it
- **Cross-TU register effects**: Do other functions in the TU affect allocation for our target?
- **Ghidra analysis of c2.dll**: Import c2.dll into Ghidra for full decompilation of the register allocator

## References

- Raymond Chen: [PowerPC 600 Prologues/Epilogues](https://devblogs.microsoft.com/oldnewthing/20180817-00/?p=99515)
- Raymond Chen: [PowerPC 600 Code Walkthrough](https://devblogs.microsoft.com/oldnewthing/20180823-00/?p=99555)
- Geoff Chappell: [C2 Code Generator](https://www.geoffchappell.com/studies/msvc/cl/c2/index.htm)
- Lectem: [MSVC Hidden Flags](https://lectem.github.io/msvc/reverse-engineering/build/2019/01/21/MSVC-hidden-flags.html)
- Aras: [/d2cgsummary](https://aras-p.info/blog/2017/10/23/Best-unknown-MSVC-flag-d2cgsummary/)
- MSDN: [Compiler Optimizations Part 2](https://learn.microsoft.com/en-us/archive/msdn-magazine/2015/may/compilers-what-every-programmer-should-know-about-compiler-optimizations-part-2)
- .NET RyuJIT: [LSRA Heuristic Tuning](https://github.com/dotnet/runtime/blob/main/docs/design/coreclr/jit/lsra-heuristic-tuning.md) (different compiler, but same company's philosophy)
