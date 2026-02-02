# Fixable Patterns: Operators

Patterns related to operator selection, FMA instructions, and expression structure.

---

## FMA Expression Order

**Impact:** +1-75%
**Success Rate:** 98%
**Time:** 5 minutes

Expression order determines which fused multiply-add variant is generated.

### Symptom

objdiff shows `fmsubs` vs `fnmsubs` mismatch.

### Why It Works

PowerPC has two FMA subtract variants:
- `(x*y - 1.0f)` generates `fmsubs` (fused multiply-subtract)
- `(1.0f - x*y)` generates `fnmsubs` (fused negate multiply-subtract)

### Fix

```cpp
// Before (generates fmsubs - wrong)
float targetAsp = widescreen ? 0.75f : 0.5625f;
float v1x = v1y + (targetAsp * realAspect - 1.0f) * 0.5f;

// After (generates fnmsubs - correct)
float targetAspect;
if (widescreen) {
    targetAspect = 0.75f;
} else {
    targetAspect = 0.5625f;
}
// fnmsubs requires: 1.0f - (x*y)
float v1x = v1y + (1.0f - targetAspect * realAspect) * 0.5f;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| DxRnd::DrawSafeArea | 98.8% | 100% | +1.2% | Removed temp, `1.0f - targetAspect * realAspect` |
| InterpVector | 99.31% | 100% | +0.69% | `tmp0 = fcubed - fsq * 2.0f` for fnmsubs |
| RndLine::GetDistanceToPlane | 96.48% | 100% | +3.52% | Dot product split into t1, t2, t3 temps |

### Rule

- `(1.0f - x*y)` → `fnmsubs`
- `(x*y - 1.0f)` → `fmsubs`

Check which instruction the original uses and restructure accordingly.

---

## Operator Overload Selection

**Impact:** +1-2%
**Success Rate:** 100%
**Time:** 2 minutes

Use the correct operator to invoke the intended overload.

### Symptom

objdiff shows call to wrong overload (e.g., BinStream vs BinStreamRev).

### Fix

```cpp
// Before - calls BinStream::operator>>
d.stream >> mCrowds;

// After - calls BinStreamRev::operator>>
d >> mCrowds;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| TransformArea::Load | 98.6% | 100% | +1.4% | `d >> mCrowds` not `d.stream >> mCrowds` |

---

## Inline Assignment

**Impact:** +1-2%
**Success Rate:** 95%
**Time:** 2 minutes

Inline assignment expression directly into function call.

### Symptom

objdiff shows different register allocation around function calls.

### Why It Works

Eliminates intermediate register assignment, changes allocation sequence.

### Fix

```cpp
// Before - separate assignment
unk300 = mtx;
Invert(unk300, unk340);

// After - inline assignment
Invert(unk300 = mtx, unk340);
```

Also works for function arguments:

```cpp
// Before - separate assignment and call
era = pEra->GetName();
CampaignEraProgress *progress = GetEraProgress(era);

// After - assignment within call (matches stw instruction pattern)
CampaignEraProgress *progress = GetEraProgress(era = pEra->GetName());
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| RndCam::SetViewProj | 98.04% | 100% | +1.96% | `Invert(unk300 = mtx, unk340)` |

---

## Boolean Index

**Impact:** Variable
**Success Rate:** MEDIUM
**Time:** 5 minutes

Use arithmetic instead of comparison for boolean-to-index conversion.

### Symptom

objdiff shows `cntlzw` + `extrwi` instructions for boolean indexing.

### Fix

```cpp
// Before - generates cntlzw + extrwi instructions
label = mBAMColumns[side == 0]->Find<HamLabel>(...);

// After - simpler arithmetic
label = mBAMColumns[1 - side]->Find<HamLabel>(...);
```

---

## Bitwise Alignment

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 5 minutes

Use bitwise formula instead of division for word-aligned calculations.

### Symptom

objdiff shows `clrrwi` (clear right bits) in target vs division in decomp.

### Why It Works

The compiler uses `clrrwi` for certain alignment patterns:
- `clrrwi r4, r11, 2` clears the bottom 2 bits (`& ~3`)

### Fix

```cpp
// Before - standard division formula, generates srawi + addze
FixedSizeAlloc((x + 15) / 4, ...)

// After - bitwise formula, generates srawi + clrrwi
FixedSizeAlloc(((x + 15) >> 2) & ~3, ...)
```

---

## Dot Product Component Order

**Impact:** Variable
**Success Rate:** LOW
**Time:** 10 minutes

The order of arithmetic components can affect register allocation.

### Symptom

objdiff shows `fmuls`/`fadds` in different order.

### Fix

Try reordering components:

```cpp
// Standard order
x*q.x + y*q.y + z*q.z + w*q.w

// May match better
((w*q.w + x*q.x) + z*q.z) + y*q.y
```

### Warning

This pattern is highly context-dependent. Success rate is low, and the "correct" order varies by function.

---

## Comparison Operand Order

**Impact:** +2%
**Success Rate:** HIGH
**Time:** 2 minutes

Operand order affects which register becomes the comparison base.

### Symptom

objdiff shows register operands in different order for comparison.

### Fix

```cpp
// Before
return mBuffer.size() == mTell ? EofType(1) : EofType(0);

// After
return mTell == mSize ? EofType(1) : EofType(0);
```

### Why It Works

PowerPC `cmpwi`/`cmplwi` instruction selection depends on operand ordering.

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| BufStream::Eof | 98.0% | 100% | +2.0% | `mTell == mSize` not `mSize == mTell` |

---

## Commutative Operand Order

**Impact:** +1-5%
**Success Rate:** 80%
**Time:** 5 minutes

Swap operand order in commutative operations to match the original.

### Symptom

objdiff detects `COMMUTATIVE_OP_ORDER` pattern with swapped source operands.

### Why It Works

For commutative operations (`add`, `fadd`, `mul`, `fmul`, `and`, `or`, `xor`), the result is mathematically identical, but the compiler may have chosen a specific operand order.

### Affected Instructions

- **Floating-point:** `fadd`, `fadds`, `fmul`, `fmuls`
- **Integer:** `add`, `addi`, `addis`, `and`, `andi.`, `andis.`, `or`, `ori`, `oris`, `xor`, `xori`, `xoris`

### Fix

```cpp
// Before - operands in wrong order
float result = a + b;

// After - swap operands
float result = b + a;
```

For operations with more than 2 operands:

```cpp
// Before
float result = x + y + z;

// After - try different groupings
float result = (y + x) + z;
float result = x + (z + y);
```

### Detection

objdiff shows `COMMUTATIVE_OP_ORDER` pattern with details like:
```
swapped_operands: [(r3, r4), (r5, r6)]
```

---

## See Also

- [fixable-control-flow.md](fixable-control-flow.md) - Branch structure patterns
- [unfixable-compiler.md](unfixable-compiler.md#fmadds-vs-separate-ops) - When FMA fixes don't work
