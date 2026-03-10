# IL Type Control — Source Types Drive Instruction Selection

**Date**: 2026-03-10
**Status**: Proven via ByteGrinder (20+ functions fixed)
**Impact**: Any function doing byte-level shift/mask operations

## Executive Summary

The source-level type of intermediate variables directly controls which IL opcodes
c1xx.dll emits, which in turn controls G5P10's PPC instruction selection. This is
**not** a compiler version difference — target and base use the identical MSVC
16.00.11886.00. The differences are caused by source expression choices.

Two IL opcodes behave completely differently in G5P10:

| Source construct | IL opcode | G5P10 behavior | PPC result |
|-----------------|-----------|----------------|------------|
| `u8(expr)` | `CAST(82 12 20)` | Fuses with adjacent shift into `rlwinm` | `extrwi` / `clrlslwi` (fused) |
| `expr & 0xFF` | `AND` + literal | No fusion, mask emitted at point of use | `srwi` + `clrlwi` (separate) |

**Key insight**: `u8()` is a CAST in IL. `& 0xFF` is AND in IL. Same mathematical
result, fundamentally different codegen paths.

## The Discovery

### Problem: ByteGrinder rotation functions at 90%

ByteGrinder has 60+ cryptographic byte operations (`op0`–`op63`). Many were stuck at
~90% match with `extrwi`/`clrlslwi` mismatches — our compiler fused shift+mask into
`rlwinm` variants while the target used separate `srwi`/`slwi`.

### Root cause: u8 type propagation

When a variable is declared as `u8` (or cast to `u8`), c1xx.dll emits a CAST opcode
in the IL. G5P10's instruction selector sees this narrowing type and **fuses** it with
adjacent shift operations:

```
u8 byte = w;           → IL: CAST(82 12 20)  → PPC: clrlwi r, r, 24
u8 hi = byte >> 2;     → IL: SHR (CAST-narrowed operand)
                        → PPC: extrwi r, r, 6, 24  (fused shift+mask)
```

When using `unsigned long` with `& 0xFF`:
```
unsigned long val = w;  → IL: (no CAST, already wide)
unsigned long hi = val >> 2;  → IL: SHR
                              → PPC: srwi r, r, 2  (separate shift)
// later: ... & 0xFF    → IL: AND 0xFF
                        → PPC: clrlwi r, r, 24  (separate mask)
```

### Breakthrough: `& 0xFF` vs `u8()` on return value

The critical finding came from op21. The XOR result truncation behaves differently:

```cpp
// FUSES — u8() CAST propagates backward through XOR operands:
return DataNode(kDataInt, u8(rot ^ l));
// G5P10 backward-propagates the u8 narrowing through XOR,
// masking BOTH operands before XOR → extrwi on both shifts

// DOES NOT FUSE — AND stays at point of use:
return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
// G5P10 sees AND after XOR, emits separate mask at end
// → srwi + slwi for the rotation components (matching target)
```

This backward propagation through XOR/OR/ADD is the key mechanism. `u8()` (CAST) tells
the compiler "this value IS 8-bit", causing it to propagate that knowledge backward.
`& 0xFF` (AND) says "mask this value", keeping the operation local.

## The Fix Pattern

### Before (90% match):
```cpp
DataNode opXX(DataArray *msg) {
    u8 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u8 rot = (w >> N) | (w << (8-N));  // u8 types trigger fusion
    return u8(rot ^ operand);           // u8 CAST propagates backward
}
```

### After (100% match):
```cpp
DataNode opXX(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);                          // single CAST: mask input to 8-bit
    unsigned long rot = (br >> N) | (br << (8-N));     // wide type: no fusion
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF)); // AND: no backward propagation
}
```

### Why `unsigned long`?

`u32` works too, but `unsigned long` is preferred because:
1. It's explicitly unsigned (avoids signed shift issues)
2. On PPC ILP32, `unsigned long` = 32-bit unsigned = same as `u32`
3. The wider type consistently prevents any u8 fusion
4. Matches the probable original source convention for byte manipulation code

## Results

### ByteGrinder functions fixed in this session:

| Function | Before | After | Fix |
|----------|--------|-------|-----|
| op7 | 97.7% | **100%** | Remove early u8 mask, unsigned long + &0xFF |
| op15-op20 | 86-90% | **99.3%** | Explicit rotation + unsigned long |
| op21 | 90% | **100%** | `& 0xFF` instead of `u8()` on return |
| op22-op26 | 90% | **100%** | Same pattern as op21 |
| op28-op31 | 90% | **100%** | Same pattern (add/xor variants) |
| op36 | 90.3% | **100%** | Complement rotation + unsigned long |
| op37-op39 | 90.3% | **99.7%** | Same (1 volatile reg commutative swap) |
| op42-op44 | 99.3% | **100%** | unsigned long + &0xFF |
| op53 | 76.4% | **100%** | Call order fix + unsigned long + &0xFF |

**Total**: 20+ functions improved, 17 to 100%.

### Remaining AT_LIMIT causes in ByteGrinder:

| Pattern | Count | Root cause |
|---------|-------|------------|
| Volatile reg commutative swap (OR/XOR) | 6 | Compiler-internal scheduling |
| extrwi/clrlslwi order swap | 2 | Compiler-internal scheduling |
| Xenon pipeline scheduling (mr+clrlwi split) | 1 | Target scheduler interleaves mask with arg setup |
| Register cascade (callee-saved shift) | 1 | Variable allocation order difference |

## Mechanism Deep Dive

### IL opcode taxonomy for byte operations

| Source | IL | G5P10 instruction | Fusion? |
|--------|------|-------------------|---------|
| `u8 x = val` | CAST(82 12 20) | `clrlwi r, r, 24` | Yes — marks value as 8-bit |
| `val & 0xFF` | AND, literal(0xFF) | `clrlwi r, r, 24` | No — standalone mask |
| `u8(x >> N)` | SHR, CAST | `extrwi r, r, 8-N, 24+N` | Yes — fused shift+mask |
| `(x >> N) & 0xFF` | SHR, AND | `srwi r, r, N` then `clrlwi` | No — separate |
| `u8(x << N)` | SHL, CAST | `clrlslwi r, r, 24+N, N` | Yes — fused shift+mask |
| `(x << N) & 0xFF` | SHL, AND | `slwi r, r, N` then `clrlwi` | No — separate |

### Backward type propagation

When G5P10 encounters `CAST u8` after a binary operation (XOR, OR, ADD), it
**propagates the u8 type backward** to the operands of that binary op. This means
both inputs to the XOR get masked to 8-bit, generating `extrwi` on any preceding
shift operations.

`& 0xFF` (AND opcode) does NOT propagate backward. It's emitted at its exact
position in the IL stream, affecting only the final result.

This means:
```
u8(A ^ B)        → CAST(XOR(A, B))     → A and B both get 8-bit treatment
(A ^ B) & 0xFF   → AND(XOR(A, B), 0xFF) → A and B computed at full width, masked at end
```

### The Xenon scheduler complication

Even after fixing the fusion issue, some functions have remaining mismatches caused
by the Xenon pipeline scheduler (a sub-function of G5P10 at `fcn.10b71d8f`).

Example from op9: Target splits `mr r11, r3` + `clrlwi r31, r11, 24` (interleaved
with argument setup), while our compiler combines into `clrlwi r31, r3, 24` (one
instruction). This is a pipeline scheduling optimization that cannot be controlled
from source.

## Implications Beyond ByteGrinder

### Where this pattern applies:
1. **Any byte-level crypto/hash operations** (other ByteGrinder-like functions)
2. **String processing** with character-level manipulation
3. **Network/protocol code** doing byte extraction from multi-byte values
4. **Bitmap/pixel operations** extracting color channels

### Detection heuristic:
If objdiff shows `extrwi`/`clrlslwi` where target has `srwi`/`slwi`, and the
source uses `u8` intermediate types, try `unsigned long` + `& 0xFF`.

### Compiler atlas entries:
- `extrwi` (NEGATIVE): If target uses separate `srwi`, convert u8 intermediates to unsigned long
- `clrlslwi` (NEGATIVE): Same — indicates unwanted rlwinm fusion from u8 type

## Verification via IL Capture

**Done (2026-03-10)**: Captured a persistent fixture bundle at:

`msvc-src/analysis/il-fixtures/il_type_control_cast_vs_and/`

Bundle contents:

- `_CL_4c106781ex`
- `_CL_4c106781gl`
- `_CL_4c106781sy`
- `_CL_4c106781in`
- `_CL_4c106781db`
- `manifest.json`
- `bundle.json`

Source fixture:

`msvc-src/analysis/il-fixtures/sources/il_type_control_cast_vs_and.cpp`

Capture command:

```bash
python3 msvc-src/tools/il_parser.py capture \
  msvc-src/analysis/il-fixtures/sources/il_type_control_cast_vs_and.cpp \
  --output-dir msvc-src/analysis/il-fixtures \
  --bundle-name il_type_control_cast_vs_and
```

Inspection command:

```bash
python3 msvc-src/tools/il_parser.py parse \
  msvc-src/analysis/il-fixtures/il_type_control_cast_vs_and
```

Observed IL differences from the captured fixture:

```text
?cast_shift@@YAII@Z:
  CAST(&byte, w:uint)
  STORE() -> uchar
  CAST(&hi, byte:uchar)
  SHR(2)
  CAST
  STORE() -> uchar

?and_shift@@YAII@Z:
  AND(&val, w:uint, 255)
  CAST
  STORE() -> wide temp
  SHR(&hi, val:wide, 2)
  STORE() -> wide temp
  AND(hi:wide, 255)
  CAST
```

and:

```text
?cast_xor@@YAIII@Z:
  XOR(rot:uint, l:uint)
  CAST
  CAST

?and_xor@@YAIII@Z:
  XOR(rot:uint, l:uint)
  AND(255)
  CAST
```

This confirms the core hypothesis:

- `u8()` narrowing is represented as IL `CAST`
- `& 0xFF` masking is represented as IL `AND`
- the CAST-based path is structurally distinct in IL before G5P10 sees it

The remaining open question is not whether the IL differs; it is how much
backward propagation G5P10 applies in more complex real-world expressions.

Original minimal test idea:

```cpp
// Test A: u8 type → expect CAST(82 12 20) before SHR
u8 byte = w; u8 hi = byte >> 2;

// Test B: u32 + mask → expect SHR without CAST, AND at end
u32 val = w & 0xFF; u32 hi = val >> 2;
```

Compare IL byte sequences to confirm CAST is the trigger.

## Connection to MSVC Compiler Architecture

This finding maps to the two-stage compiler model (see DEEP_ANALYSIS_PLAN.md §2):

1. **c1xx.dll (front-end)**: Source types → IL opcodes. `u8` → CAST. `& 0xFF` → AND.
   The front-end's type system directly encodes the source programmer's intent into
   different IL representations.

2. **c2.dll G5P10 (code generator)**: IL opcodes → PPC instructions. CAST triggers
   rlwinm fusion. AND generates separate instructions. G5P10 doesn't "understand"
   the source — it only sees IL opcodes, making the IL representation the control point.

The source type system is the **primary lever** for controlling G5P10's instruction
selection in byte-level operations. This is a new track of IL-level control beyond
the previously known patterns (subf. fusion, bool materialization, NOR peephole).
