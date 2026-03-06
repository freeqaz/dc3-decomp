# ByteGrinder Decomp Patterns (2026-03-06)

Session fixing ByteGrinder ops in `src/system/synth/ByteGrinder.cpp`. Documents three reusable patterns.

## Pattern 1: rlwimi Mismatch (Rotate Left Word Immediate then Mask Insert)

### Problem
MSVC PPC generates `rlwimi` (a combined rotate-mask-insert instruction) for expressions like:
```cpp
unsigned long ret = u8(w) | ((w << 8) & 0xFF00);
```
The compiler recognizes this as "insert low byte into bits 16-23 of a shifted copy" and emits a single `rlwimi` instruction. If the target uses separate `clrlwi + slwi + or`, a mismatch occurs.

### Fix
Separate the byte mask into its own variable to break the pattern the compiler recognizes:
```cpp
// BEFORE (generates rlwimi)
unsigned long ret = u8(w) | ((w << 8) & 0xFF00);

// AFTER (generates clrlwi + slwi + or — matches target)
unsigned long bw = u8(w);
unsigned long ret = bw | (bw << 8);
```

By computing `u8(w)` into a named variable first, the compiler no longer sees the combined rotate-mask-insert pattern and falls back to individual shift/mask/or instructions.

### Affected Functions
op2, op3, op10, op11, op12, op13, op14 — all use the byte-duplicate `bw | (bw << 8)` pattern.

---

## Pattern 2: XOR Pre-Masking

### Problem
The compiler optimizes `u8(a ^ b)` by distributing the byte mask to both operands: `u8(a) ^ u8(b)`. This generates extra `clrlwi` instructions masking each operand before the XOR, instead of a single mask after.

### Fix
Use `(int)(expr & 0xFF)` instead of `u8(expr)` when masking an XOR result:
```cpp
// BEFORE (compiler pre-masks both XOR operands)
return DataNode(kDataInt, u8(ret ^ operand));

// AFTER (single post-XOR mask — matches target)
return DataNode(kDataInt, (int)(ret & 0xFF));

// Alternative: cast to (int) with & 0xFF
return DataNode(kDataInt, (int)((ret ^ operand) & 0xFF));
```

The `& 0xFF` is treated as a post-operation mask that the compiler doesn't distribute.

### Affected Functions
op10, op11 — both XOR the result with the operand and mask to byte.

---

## Pattern 3: Bool Intermediate for Size() Comparison

### Problem
`da->Size() >= 2` compiles to a simple `cmpwi + blt` (signed compare-and-branch). The target uses a complex boolean computation: `subfc/eqv/srwi/addze` which materializes the comparison result into a register, then branches on the bool.

### Fix
Use a `bool` intermediate variable with `> 1` instead of `>= 2`:
```cpp
// BEFORE (generates cmpwi + blt)
if (da->Size() >= 2) { ... }

// AFTER (generates subfc/eqv/srwi/addze + clrlwi. + beq — matches target)
bool hasArgs = da->Size() > 1;
if (hasArgs) { ... }
```

Key details:
- The `bool` intermediate forces the compiler to materialize the comparison result
- `> 1` generates the correct constant (1) in the `subfc` instruction; `>= 2` generates 2
- `(unsigned int)` cast changes load from `lha` to `lhz` — don't use it

### Affected Functions
getRandomSequence32A, getRandomSequence32B

---

## Pattern 4: NOR Peephole Prevention (u32 Widening)

### Problem
The compiler converts `u8_value ^ all_ones_mask` into a bitwise NOT (`nor` instruction) when the XOR mask covers all bits of the type. For example, `(u8)(w >> 3) ^ 0x1F` on a 5-bit result, or `u8_w ^ 0xFF` on an 8-bit value.

### Fix
Widen the value to `u32` before the XOR. Since the mask no longer covers all 32 bits, the compiler can't use the NOR shortcut:
```cpp
// BEFORE (generates nor)
u8 w = msg->Int(2);
u32 tmp = (u8)(w >> 3) ^ 0x1F;

// AFTER (generates extrwi + xori — matches target)
u8 w = msg->Int(2);
u32 w32 = w;
u32 tmp = (w32 >> 3) ^ 0x1F;
```

### Affected Functions
op32 (95.9% → 100%), op60 (86.7% → 100%), op63 (86.7% → 100%)

---

## Pattern 5: u8 Intermediate Variables for Instruction Scheduling

### Problem
Independent shift-right (`extrwi`) and shift-left (`clrlslwi`) operations are scheduled by the compiler in a fixed internal order. The target uses the opposite order.

### Fix
Use `u8` intermediate variables for each half of the rotation, which creates data dependencies via truncation that force the compiler to schedule in the correct order:
```cpp
// BEFORE (compiler schedules freely — wrong order)
u32 tmp = ((u8)(w >> 3) ^ 6) | ((w & 7) << 5);

// AFTER (u8 intermediates force ordering — matches target)
u32 w32 = w;
u8 tmp1 = u8((w32 >> 3) ^ 6);
u8 tmp2 = u8(((w32 & 7) << 5));
u8 combined = u8(tmp1 | tmp2);
```

Key details:
- Always put shift-right part as tmp1 (first declaration) — compiler schedules `extrwi` first
- The `u8()` cast on each intermediate forces the compiler to complete that operation before moving on
- Some functions also needed XOR constant swaps (see Pattern 6)

### Affected Functions
op45 (99.3% → 100%), op46 (99.3% → 100%), op47 (99.3% → 100%)

---

## Pattern 6: XOR Constant Pairing Correction

### Problem
When Ghidra decompiles bit-rotation-with-XOR operations like `(w >> N) ^ A | (w << M) ^ B`, it sometimes associates the XOR constants with the wrong shift halves. The compiler assigns xori values to match the register holding each shift result, so misattribution produces swapped xori operands.

### Fix
Swap the XOR constants between the shift-right and shift-left parts. Verify by checking which `xori` value the target pairs with `extrwi` (shift-right) vs `clrlslwi` (shift-left).
```cpp
// BEFORE (wrong XOR pairing from Ghidra)
u8 tmp1 = u8((w32 >> 5) ^ 2);   // target actually pairs >>5 with 3
u8 tmp2 = u8((w32 << 3) ^ 3);   // target actually pairs <<3 with 2

// AFTER (correct pairing)
u8 tmp1 = u8(((w32 << 3) & 0xF8) ^ 2);
u8 tmp2 = u8((w32 >> 5) ^ 3);
```

### Affected Functions
op50 (99.9% → 100%), op52 (99.0% → 100%), op55 (99.9% → 100%), op56 (99.9% → 100%)

---

## Remaining Unfixable Patterns

### Instruction Scheduling (no XOR constants)
op14: `srwi`/`slwi` swap for `(bw >> 1) | (bw << 7)` — no XOR constants to manipulate, u8 intermediates don't affect scheduling. 99.3%.

### Instruction Scheduling + XOR Constant Coupling
op61: `extrwi`/`clrlslwi` scheduling coupled to XOR constant values. Swapping XOR constants fixes scheduling (99.9%) but then xori values are wrong. Can't control both independently. 98.9%.

## Results Summary

| Function | Before | After | Fix Applied |
|----------|--------|-------|-------------|
| op2 | ~93% | 100% | rlwimi + arg read order |
| op3 | ~93.6% | 100% | rlwimi + arg read order |
| op10 | 71.3% | 100% | rlwimi + XOR pre-mask |
| op11 | 77.2% | 100% | rlwimi + XOR pre-mask |
| op12 | 93.3% | 100% | rlwimi |
| op13 | 93.2% | 100% | rlwimi |
| op14 | 86.8% | 99.3% | rlwimi (remaining: shift scheduling) |
| op32 | 95.9% | 100% | u32 widening (NOR prevention) |
| op45 | 99.3% | 100% | u8 intermediates |
| op46 | 99.3% | 100% | u8 intermediates |
| op47 | 99.3% | 100% | u8 intermediates |
| op50 | 99.0% | 100% | u8 intermediates + XOR constant swap |
| op52 | 99.0% | 100% | u8 intermediates + XOR constant swap |
| op55 | 99.0% | 100% | u8 intermediates + XOR constant swap |
| op56 | 99.0% | 100% | u8 intermediates + XOR constant swap |
| op60 | 86.7% | 100% | u32 widening (NOR prevention) |
| op61 | 98.9% | 98.9% | unfixable (scheduling + XOR coupling) |
| op63 | 86.7% | 100% | u32 widening (NOR prevention) |
| getRandomSequence32A | 89% | 100% | bool intermediate |
| getRandomSequence32B | 89% | 100% | bool intermediate |
