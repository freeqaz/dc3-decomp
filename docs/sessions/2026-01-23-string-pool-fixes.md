## Session: January 23, 2026 (String & Utility Fixes)

### Summary

Focused session targeting **small utility functions** (< 150 bytes) in the 90-99% match range. Used objdiff CLI for diagnosis. Achieved **4 new 100% matches** including a real bug fix in `Pool::Alloc`.

### Functions Fixed This Session (100% Match)

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| Str.cpp | `String::operator==(FixedString)` | 99.1% | **100%** | Swapped strcmp args: `strcmp(str.c_str(), mStr)` |
| Str.cpp | `String::operator==(Symbol)` | 99.3% | **100%** | Swapped strcmp args: `strcmp(s.Str(), mStr)` |
| Pool.cpp | `Pool::Alloc` | 91.4% | **100%** | Fixed bug: `mFree = *(char **)ptr` instead of `nullptr` |
| Mesh.cpp | `RndMesh::EstimatedSizeKb` | 93.8% | **100%** | Cast `sizeof(Face)` to `int` for signed division |

### Functions Improved

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| CharClip.cpp | `CharClip::BeatToSample` | 96.5% | **96.7%** | Fixed RB3-style: assign to `f1` then `*fp = f1` |

### Functions at Compiler/Linker Limit (Diagnosed)

| File | Function | Match | Diagnosis |
|------|----------|-------|-----------|
| Geo.cpp | `Box::Clamp` | 99.5% | OR chain register allocation (r10/r11 in different order) |
| complex.cpp | `complex::operator*` | 99.3% | fmadd operand commutativity (f0,f13 vs f13,f0) |
| DataArray.cpp | `DataArray::~DataArray` | 99.7% | Merged linker symbols for function calls |
| Part.cpp | `ParticleCommonPool::AllocateParticle` | 92.9% | cr0 vs cr6 condition register usage |
| FlowSwitch.cpp | `FlowSwitch::ActivateTransitionCases` | 99.4% | r10/r11 register swaps throughout |
| Task.cpp | `TaskTimeline::ClearTasks` | 95.7% | cr0 vs cr6 condition register usage |

### Key Patterns Discovered

#### 1. strcmp Argument Evaluation Order

The compiler evaluates function arguments **right-to-left**. For `strcmp(a, b)`, it evaluates `b` first, then `a`.

```cpp
// Before (99.1%) - evaluates mStr second, loads second
return strcmp(mStr, str.c_str()) == 0;

// After (100%) - evaluates mStr first, loads first (matches target)
return strcmp(str.c_str(), mStr) == 0;
```

**How to identify:** Look at load instruction order in objdiff. If target loads `this->member` before `param->member`, swap the arguments.

#### 2. sizeof() Returns Unsigned - Affects Division

`sizeof()` returns `size_t` (unsigned), which makes the entire expression unsigned and generates `srwi` (unsigned shift). Cast to `int` to get `srawi + addze` (signed).

```cpp
// Before (93.8%) - unsigned division via srwi
return (NumVerts() * 0x50 + NumFaces() * sizeof(Face)) / 1024;

// After (100%) - signed division via srawi + addze
return (NumVerts() * 0x50 + NumFaces() * (int)sizeof(Face)) / 1024;
```

**How to identify:** Look for `srwi` vs `srawi`/`addze` in objdiff. If return type is signed but division uses unsigned shift, cast the `sizeof()`.

#### 3. Free-List Allocator Pattern

Pool allocators must follow the next-pointer stored at the beginning of each free block:

```cpp
// Before (91.4%) - BUG: loses entire free list!
mFree = nullptr;

// After (100%) - correct: follow next pointer
mFree = *(char **)ptr;
```

**Note:** This was a real bug, not just a matching issue. The original code would break memory allocation.

#### 4. Condition Register Differences (Unfixable)

Some functions use `cr0` while others use `cr6` for comparisons. This is compiler-controlled and not fixable from source:

```asm
; Our code (uses cr6 explicitly)
cmplwi cr6, r3, 0x0
beq cr6, 0x4c

; Target (uses cr0 implicitly)
cmplwi r3, 0x0
beq 0xd8c
```

### Attempted Fixes That Didn't Work

| Function | Attempt | Result | Why It Failed |
|----------|---------|--------|---------------|
| `Box::Clamp` | Added parentheses `(A \| B) \| C` | No change | Compiler ignores redundant parens |
| `Box::Clamp` | Used temp variable | 91.5% (worse) | Added extra instructions |
| `Box::Clamp` | Reversed order `C \| B \| A` | 99.3% (worse) | Wrong evaluation order |
| `BeatToSample` | if/else instead of assign-then-set | 93.5% (worse) | Different branch structure |
| `GetIndexFile` | Swapped Symbol declaration order | 67.6% (worse) | Stack layout changed entirely |

### objdiff Diagnosis Commands Used

```bash
# Quick function status check
objdiff-cli report function build/373307D9/report.json "String::operator=="

# Full instruction diff
objdiff-cli diff -p . '??8String@@QBA_NABVFixedString@@@Z' -f json --include-instructions \
  | jq -r '.instructions[] | "\(.index): \(.match_type) | base: \(.base.opcode) \(.base.args) | target: \(.target.opcode) \(.target.args)"'

# Find non-matching instructions only
objdiff-cli diff -p . 'MANGLED_NAME' -f json --include-instructions \
  | jq -r '.instructions[] | "\(.index): \(.match_type)"' | grep -v "equal"
```

### Project Progress

**Milo Engine Code: 53.83% → 53.84%** (+4 functions, +244 bytes)

### Recommended Next Targets

Based on this session's analysis:

1. **Look for more sizeof() casts** - Search for division by powers of 2 in functions at 90-95%
2. **Check strcmp/strncmp uses** - Argument order might be swapped in other comparison functions
3. **Pool/allocator functions** - Other memory pools may have similar patterns
4. **Avoid cr0/cr6 functions** - These are at compiler limit, don't waste time

### Files Modified

- `src/system/utl/Str.cpp` - operator== fixes
- `src/system/utl/Pool.cpp` - Alloc bug fix
- `src/system/rndobj/Mesh.cpp` - EstimatedSizeKb signed cast
- `src/system/char/CharClip.cpp` - BeatToSample improvement
