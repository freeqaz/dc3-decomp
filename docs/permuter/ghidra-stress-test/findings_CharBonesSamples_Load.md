# Ghidra Type Seeding Stress Test - CharBonesSamples::Load

**Date**: 2026-02-13
**Tester**: Claude Opus 4.6
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: CharBonesSamples::Load

**Symbol**: `?Load@CharBonesSamples@@QAAXAAVBinStream@@@Z`
**File**: `src/system/char/CharBonesSamples.cpp:15`
**Subsystem**: char
**Size**: 73 instructions (292 bytes)

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | 96.7% |
| **Primary Mismatch Type** | REGISTER_SWAP (20 instr), symbol relocs (7), LINKER_MERGED (1) |
| **Mismatch Count** | 35 diff_arg, 38 equal |
| **Verdict** | MAYBE_FIXABLE |

**Objdiff Summary**:
```
Register swaps: 20 instructions across 6 pairs
  r29 <-> r31 : 4 (idx 12-62)
  r27 <-> r31 : 4 (idx 19-49)
  r28 <-> r30 : 3 (idx 15-41)
  r27 <-> r29 : 3 (idx 16-40)
  r26 <-> r28 : 3 (idx 17-39)
  r26 <-> r30 : 3 (idx 20-36)
Symbol relocations: 7 (LINKER_MERGED at 824D1870)
```

---

### Ghidra Analysis (With Type Seeding)

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | 2 | `this` typed as `CharBonesSamples*` but members not accessed by name |
| **Struct members named** | 1 | All offsets shown as `local_*` stack vars, no field names |
| **Function signatures** | 2 | `BinStream` parameter visible, but calling convention warning |
| **Cross-references** | 2 | Called functions shown but mangled, not human-readable |

#### Key Observations

**What was immediately clear from types?**
- The `CharBonesSamples*` this pointer was typed correctly, confirming class identity
- The function structure (version check + LoadHeader + LoadData) matched our source

**What remained unclear?**
- Ghidra showed all locals as generic `uint`/`undefined4` — no member names resolved
- The `BinStreamRev d(bs, revs)` local object was not visible as a typed struct
- Register allocation is the primary issue, and types provide zero insight into PPC register assignment
- The `INIT_REVS` macro expansion wasn't visible — had to know the pattern

**Ghidra Decompilation Snippet** (key section):
```c
this_00 = (CharBonesSamples *)__savegprlr_25(this);
_ReadEndian_BinStream__QAAXPAXH_Z(in_r4);
uVar1 = local_60 & 0xffff;
uVar2 = local_60 >> 0x10;
// ^ These correspond to getHmxRev/getAltRev but types don't reveal that
```

---

### Matching Attempt

**Changes Made**: None attempted.

**Reasoning**: The mismatch is entirely register swaps across 6 interleaved pairs involving r26-r31. With 6 variables (revs, rev, altRev, d, bs, this) all needing callee-saved registers, the compiler's allocation order depends on subtle declaration/use ordering. 6 swap pairs means the allocation order is almost completely inverted — this would require either:
1. A lucky reordering of variable declarations (very unlikely with 6 pairs)
2. The issue is structural and unfixable

**Status**: Blocked (register allocation)

---

### Learnings

**Type Seeding Helpfulness**: 1/5

**Specific Value Add**:
- ✅ Confirmed function is CharBonesSamples::Load (class typing)
- ✅ Confirmed BinStream parameter type

**Gaps Identified**:
- ❌ Stack locals not mapped to struct fields — Ghidra decompilation shows `local_60` not `revs`
- ❌ No insight into register allocation, which is the only mismatch
- ❌ Mangled names in decompilation reduce readability vs m2c output
- ❌ `BinStreamRev` local object not typed — critical for understanding the function

**Pattern Recognized**:
- Register swap dominated functions get zero benefit from type seeding
- The INIT_REVS/BinStreamRev pattern is already well-understood in our codebase
- For this function class, reading our own source + objdiff is strictly more useful than Ghidra

---

### Verdict

**Should pursue 100% match?** No

**Reasoning**: 6 interleaved register swap pairs across the entire function body. The source logic is already correct (96.7% match with correct control flow). Register allocation differences are compiler-internal and don't indicate a code issue.

**Recommended Next Steps**:
- Mark as AT_LIMIT or accept current match
- The 1 LINKER_MERGED call (kAssertStr MakeString) is also unfixable
