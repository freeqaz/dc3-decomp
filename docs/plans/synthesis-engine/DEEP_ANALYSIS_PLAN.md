# Deep Analysis Plan — c2.dll Internals

## Goal

Move from "we know the structure" to "we can predict codegen decisions" by:
1. Mapping the COLOR register allocator's decision logic
2. Identifying the G5_SPECIAL peephole pattern table
3. Building a working differential testing harness **[DONE — see results below]**
4. Extracting inliner cost formulas **[PARTIALLY DONE — threshold found]**
5. Understanding the IL (intermediate language) format

## Completed Results (2026-03-10)

See `msvc-src/results/FINDINGS_SUMMARY.md` for full details. Key findings:

- **Register allocation**: Strictly first-allocated=highest for all tested patterns
  (including virtual calls, loops, conditionals). Linear scan, NOT graph coloring.
  Compiler-generated temporaries (loop counters, this pointers) consume registers
  before user variables, shifting them down.
- **Inlining threshold**: ~30 IR tuples. `inline` keyword = no effect with /Ox.
  `__forceinline` = always inlines (no limit). Header function body size directly
  affects neighboring function codegen.
- **NOR peephole**: u8 XOR 0xFF → `not` (=`nor rA,rS,rS`). Widening to u32 first
  prevents NOR, generates `xori` instead.
- **Boolean materialization**: `(bool)` cast triggers `subfc/eqv/srwi`. Without cast,
  compiler uses branches. Source-controllable.
- **subf. fusion**: `hi - lo >= 0` → `subf.`. `hi >= lo` → `cmpw`. Source-controllable.
- **Branch polarity**: Compiler ALWAYS inverts condition for branches. `== 0` → `bne`,
  `!= 0` → `beq`. Deterministic.
- **Float precision**: DOUBLETOSINGLE aggressively demotes all double literals assigned
  to float. No `lfd` cases found in simple tests.

## Track 1: COLOR Register Allocator Deep Dive

The COLOR pass at `fcn.10bc6487` with 207 helper functions is responsible for
1,218 AT_LIMIT functions (the single largest blocker). Understanding its internals
lets us predict register assignments from source.

### 1.1 Priority Functions to Disassemble

From the COLOR helper list (`msvc-src/analysis/color_functions.txt`), ordered by
size (larger = more logic to extract):

| Function | Size | Likely Role |
|----------|------|-------------|
| `fcn.10bc9550` | 1752 bytes | Core graph coloring loop? (largest non-init) |
| `fcn.10bc9fda` | 725 bytes | Spill cost calculator? |
| `fcn.10bc69f1` | 691 bytes | Register assignment loop? |
| `fcn.10bc7a42` | ~600 bytes | Interference graph builder? |
| `fcn.10bc6487` | 131 bytes | Entry point (already analyzed: memset 1428 bytes, 261/357 regs) |

### 1.2 What to Extract

For each function, identify:
- **Input data structures**: What does the register state buffer (1428 bytes) contain?
  - Register file descriptor (261 GPR+FPR+CR or 357 with VMX)
  - Interference graph adjacency matrix or edge list
  - Live range intervals per virtual register
  - Spill cost per virtual register
- **Decision points**: Where does it choose between:
  - Linear scan vs graph coloring (BSF threshold)
  - Callee-saved vs caller-saved assignment
  - Spill vs split vs rematerialize
- **Output**: The physical register map (virtual -> physical)

### 1.3 Methodology

**Approach A: Ghidra decompilation (x86 project)**
1. Create a new Ghidra project for c2.dll (PE32 x86)
2. Import, auto-analyze, apply function signatures for Win32 APIs
3. Start with `fcn.10bc6487` entry → trace call graph
4. Name functions as we understand them: `color_init`, `color_build_ig`, etc.
5. Export annotated pseudocode for each function

**Approach B: rizin + manual annotation**
1. Use `r2 -A` for auto-analysis
2. `pdf @ fcn.10bc9550` for the large functions
3. Look for characteristic patterns:
   - Loop over register array (counter 0..261 or 0..357)
   - Bit manipulation (interference graph stored as bitsets?)
   - Comparison against constant thresholds
   - Array indexing with stride matching register descriptor size

**Approach C: Dynamic analysis (wibo + DLL hooking)**
1. Build a shim DLL that wraps c2.dll
2. Hook `fcn.10bc6487` (COLOR entry)
3. Dump the 1428-byte state buffer before and after
4. Compile known test cases, observe state transformations
5. Correlate with `/FAcs` output

### 1.4 Key Questions to Answer

1. **BSF threshold**: What is the exact variable count where graph coloring activates?
   - We observe ~7 empirically. Is it 7? 8? Based on register count or live range complexity?
2. **Assignment order**: Is it strictly "first declared = highest register" for linear scan?
   - What about variables declared inside scopes vs outer scope?
   - What about variables whose live ranges don't overlap?
3. **Spill cost formula**: What inputs determine which variable gets spilled?
   - Loop depth? Use count? Live range length? Type (GPR vs FPR)?
4. **FPR allocation**: Is FPR assignment independent of GPR, or do they share the same graph?
   - Our empirical observation: FPR NOT addressable by BSF (base=0 is GPR only)

## Track 2: G5_SPECIAL Peephole Patterns

The G5_SPECIAL pass (index 19) contains PPC/Xenon-specific peephole optimizations.
~200+ AT_LIMIT functions are blocked by peephole mismatches.

### 2.1 Find the Peephole Table

Peephole optimizers typically use a pattern-match-replace table:
```
struct PeepholeRule {
    Pattern match;      // IR pattern to match
    Pattern replace;    // IR pattern to replace with
    Condition guard;    // additional conditions
};
```

**Search strategy:**
1. Find the function that implements G5_SPECIAL (in pass group 2 or 3)
2. Look for a table of structs in .rdata or .data near G5_SPECIAL references
3. Each entry should reference PPC instruction mnemonics or opcode constants
4. Cross-reference with known patterns:
   - NOR: match `xori rD, rA, 0xFF` on u8 → replace with `nor rD, rA, rA`
   - Boolean: match compare+branch → replace with `subfc/eqv/srwi`
   - subf.: match `cmpw + subf` → replace with `subf.`

### 2.2 Map Each Known Empirical Pattern

For each AT_LIMIT pattern we've observed, find the specific code path in G5_SPECIAL:

| Pattern | What to Find |
|---------|-------------|
| NOR peephole | The rule that matches `xor 0xFF` on byte-width operands |
| Boolean materialization | The rule that converts compare+branch to branchless |
| subf. fusion | The rule that fuses subtract with record bit |
| Branch hint insertion | Where `+`/`-` branch hints get added |
| Paired-single fusion | Where adjacent float ops get combined |

### 2.3 Priority

**High**: NOR, boolean materialization, subf. — these have known source-side triggers
**Medium**: Branch hints, paired-single — less controllable from source
**Low**: Instruction scheduling — purely internal, no source influence

## Track 3: Differential Testing Harness

The fastest way to get actionable results WITHOUT decompiling anything.

### 3.1 Build the Harness (Phase 1 implementation)

```python
# msvc-src/tools/diff_test.py

class DiffTestHarness:
    def compile(self, source: str, flags: list[str]) -> str:
        """Compile source with /FAcs, return .asm listing path"""
        # Write source to temp file
        # Run: wibo cl.exe /c /FAcs /Ox /GS- ... source.cpp
        # Return path to generated .asm file

    def extract_functions(self, asm_path: str) -> dict[str, FunctionAsm]:
        """Parse .asm listing into per-function assembly"""
        # Parse ; PROC / ENDP delimiters
        # Extract: prologue helper, callee-saved set, instruction count, body

    def diff_functions(self, a: FunctionAsm, b: FunctionAsm) -> FunctionDiff:
        """Structural diff between two function assemblies"""
        # Normalize register names → slots
        # Diff instruction sequences
        # Identify: register swaps, instruction reorders, added/removed insns

    def run_suite(self, suite: TestSuite) -> list[DecisionRecord]:
        """Run a complete test suite and produce decision records"""
```

### 3.2 Test Suite Priority

| Suite | Questions Answered | Impact |
|-------|-------------------|--------|
| **regalloc_order** | Declaration order → register map | 1,218 functions |
| **bsf_threshold** | When does graph coloring kick in? | 1,218 functions |
| **inline_threshold** | Max function size for inlining | ~unknown, many |
| **peephole_triggers** | Source → peephole pattern activation | ~200 functions |
| **branch_polarity** | beq vs bne selection rules | 751 functions |
| **float_precision** | DOUBLETOSINGLE activation rules | ~100 functions |
| **fpmov_intmov** | When FP values move to GPR | ~50 functions |

### 3.3 Concrete Test Cases

**regalloc_order** (first to implement):
```cpp
// test_regalloc_01.cpp — vary declaration count
extern int get(int);
extern void use(int, int, int, int, int);

void test_2vars() { int a = get(0); int b = get(1); use(a, b, 0, 0, 0); }
void test_3vars() { int a = get(0); int b = get(1); int c = get(2); use(a, b, c, 0, 0); }
void test_4vars() { int a = get(0); int b = get(1); int c = get(2); int d = get(3); use(a, b, c, d, 0); }
// ... up to 15 vars

// test_regalloc_02.cpp — vary declaration ORDER
void test_order_ab() { int a = get(0); int b = get(1); use(a, b, 0, 0, 0); }
void test_order_ba() { int b = get(1); int a = get(0); use(a, b, 0, 0, 0); }

// test_regalloc_03.cpp — mix GPR and FPR
extern float getf(int);
extern void usef(int, float, int, float);
void test_mixed() { int a = get(0); float b = getf(1); int c = get(2); float d = getf(3); usef(a, b, c, d); }
```

**inline_threshold** (high value):
```cpp
// test_inline_N.cpp — generated for N = 1..100
// Template: callee has N simple statements, caller calls it once
// Check if callee body appears inlined in caller's assembly
```

### 3.4 Expected Output

```
msvc-src/results/
├── regalloc_order/
│   ├── raw/                  # .asm listings
│   ├── parsed/               # per-function JSON
│   └── decision_map.json     # source pattern → asm pattern
├── bsf_threshold/
│   └── ...
└── summary.json              # cross-suite findings
```

## Track 4: Inliner Analysis

The inliner affects ALL other patterns indirectly — if a function is inlined,
its body is subject to the caller's optimization context.

### 4.1 Find the Inliner

The inliner runs BEFORE the per-function optimization loop (it decides what gets
inlined during IL load or function preparation). Look for:
- References to the `INL:` diagnostic strings
- `%s won't be inlined (too big)` string → follow xref to the decision function
- `Inlining %s (%d instrs)` → the instruction count comparison

### 4.2 Extract the Cost Model

From diagnostic strings, we know the inliner uses:
- **Instruction count** (`%d instrs`) — primary size metric
- **Bad candidate flag** (`InlBadCandidate`) — explicit rejection
- **Force inline** (`__forceinline`) — override
- **Dangerous asm** — assembly block rejection

What we need to find:
1. The threshold constant for "too big" (compare against `%d instrs`)
2. How `__forceinline` overrides the threshold
3. How call context affects the budget (caller size? nesting depth?)
4. Whether the budget is per-function or per-TU

### 4.3 Differential Test

```cpp
// Generate: inline int callee_N() { ... N statements ... return x; }
// For N = 1, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50
// Compile with /FAcs, check if callee body appears in caller
```

Find the exact threshold where inlining stops.

## Track 5: IL Format Discovery

Understanding the intermediate language format lets us:
- Build a c2.dll test harness that feeds specific IR patterns
- Understand what the optimizer "sees" before making decisions
- Eventually build an IR → source reverse translator

### 5.1 Capture IL Files

c2.dll reads IL from c1xx.dll (the front-end). The IL file is a temp file
passed between compiler phases.

**Capture method:**
```python
# Modify capture_il.py to:
# 1. Set TEMP to a known directory
# 2. Compile with /E (preprocess only) to verify front-end works
# 3. Compile normally, intercept the temp IL file before c2.dll deletes it
# 4. Use filesystem monitoring (inotifywait) to catch the temp file
```

Alternative: Hook `CreateFileW` in c2.dll to log the file path, then copy before delete.

### 5.2 Analyze IL Structure

Once captured:
1. Hex dump the IL file
2. Look for magic bytes / header
3. Cross-reference with c2.dll's `fcn.10b75957` (IL LOAD function)
4. The IL load function's data structure reads reveal the format

### 5.3 Use `/d1` Flags on c1xx.dll

While `/d2` flags (c2.dll) don't work in our version, `/d1` flags (c1xx.dll)
might. Worth testing:
```
/d1reportAllClassLayout  — dump class layouts
/d1trimfile              — minimize IL output
```

## Track 6: Runtime Instrumentation (wibo hooks)

Most aggressive approach — observe c2.dll's actual decisions at runtime.

### 6.1 wibo Interposition

wibo translates Win32 → Linux. We can add hooks at the wibo level:

```cpp
// In wibo source, add hooks for:
// 1. CreateFileW — capture IL file paths
// 2. WriteFile — capture IL content before close
// 3. VirtualAlloc/HeapAlloc — track c2.dll's memory allocations
```

### 6.2 c2.dll Function Hooking

More targeted: patch c2.dll's code at specific locations:

```python
# Hook COLOR entry (fcn.10bc6487):
# - Before: dump function descriptor (the linked list node)
# - After: dump register assignment result
#
# Hook inliner decision point:
# - Log: function name, instruction count, decision (inline/skip)
#
# Hook G5_SPECIAL:
# - Log: which peephole rules fired
```

Implementation: binary-patch c2.dll to call our logging DLL, or use
LD_PRELOAD with wibo to intercept at the Win32 API level.

### 6.3 Compilation Trace

For each DC3 source file, produce a trace:
```json
{
    "file": "src/system/os/BlockMgr.cpp",
    "functions": [
        {
            "name": "BlockMgr::AllocateBlock",
            "color_result": {"r31": "this", "r30": "size", ...},
            "inlined": ["MemAlloc"],
            "peepholes_fired": ["NOR_byte_xor"],
            "callee_saved": ["r31", "r30", "r29"]
        }
    ]
}
```

This gives us ground truth for the compiler model.

## Execution Plan

### Week 1: Foundation + Quick Wins

| Day | Task | Track | Deliverable |
|-----|------|-------|-------------|
| 1 | Build diff_test.py harness | 3 | Working test runner |
| 1 | Run regalloc_order suite (2-15 vars) | 3 | Register assignment map |
| 2 | Run inline_threshold suite (N=1..100) | 3 | Inline threshold constant |
| 2 | Run peephole trigger suite (NOR, bool, subf.) | 3 | Peephole activation rules |
| 3 | Create Ghidra project for c2.dll (x86) | 1 | Named functions, cross-refs |
| 3 | Disassemble `fcn.10bc9550` (largest COLOR helper) | 1 | Pseudocode + annotations |
| 4 | Disassemble `fcn.10bc69f1` (register assignment?) | 1 | Assignment loop logic |
| 5 | Find G5_SPECIAL entry in pass groups | 2 | G5_SPECIAL function address |
| 5 | Find inliner via `INL:` string xrefs | 4 | Inliner entry point |

### Week 2: Deep Dives

| Day | Task | Track | Deliverable |
|-----|------|-------|-------------|
| 1-2 | Decompile COLOR core (graph coloring algorithm) | 1 | BSF threshold, spill costs |
| 3-4 | Decompile G5_SPECIAL (peephole table extraction) | 2 | Full peephole rule list |
| 5 | Decompile inliner decision function | 4 | Cost formula |

### Week 3: Integration

| Day | Task | Track | Deliverable |
|-----|------|-------|-------------|
| 1-2 | Build register allocation predictor | 1+3 | Python module |
| 3 | Build peephole predictor | 2+3 | Python module |
| 4-5 | Integrate with permuter as constraint oracle | All | Guided permuter prototype |

### Week 4: Validation + IL

| Day | Task | Track | Deliverable |
|-----|------|-------|-------------|
| 1-2 | Capture IL files for 10 test cases | 5 | IL format documentation |
| 3-4 | Validate model against 100 AT_LIMIT functions | All | Accuracy metrics |
| 5 | Runtime instrumentation prototype | 6 | Compilation trace for 1 file |

## Success Criteria

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Register assignment prediction | >80% accuracy | Predict callee-saved map for 100 functions |
| Inline threshold known | Exact constant | Differential test finds precise N |
| Peephole rules extracted | 5+ rules | Each maps source pattern → asm pattern |
| AT_LIMIT functions fixed | 50+ functions | Move from AT_LIMIT to COMPLETE using model |
| BSF threshold known | Exact constant | When graph coloring activates |

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Ghidra decompilation too noisy (no PDB) | High | Supplement with dynamic analysis + diff testing |
| COLOR uses complex heuristics not amenable to simple model | Medium | Focus on common cases (linear scan), acknowledge BSF limits |
| IL format undocumented and opaque | High | Bypass: use `/FAcs` differential testing instead |
| wibo hooks break c2.dll execution | Low | Test incrementally, use non-invasive hooks first |
| Register allocation depends on global state we can't observe | Medium | Differential testing can still map input→output empirically |

## Dependencies

- **Ghidra**: Need x86 PE analysis project (currently loaded with DC3 PPC binary)
- **wibo**: Already working for compilation; may need source mods for hooking
- **rizin**: Already used for initial analysis; continue for targeted disassembly
- **cl.exe + c2.dll**: Already available at `build/compilers/X360/16.00.11886.00/`
- **Test infrastructure**: Need wibo + cl.exe accessible from Python test harness
