# m2c VMX128 Integration Research

**Date**: 2026-01-26
**Status**: Deep research complete
**Author**: Claude Agent

## Executive Summary

m2c currently has **zero support** for any vector instructions - all AltiVec and VMX128 instructions emit `M2C_ERROR()` in the decompiled output. Meanwhile, the DC3 project has successfully implemented comprehensive VMX128 support in Ghidra via SLEIGH, with 13,836 VMX128 instructions now properly recognized and decompiled in the binary.

This document analyzes four integration approaches to bring VMX128 support to m2c:
1. **Direct implementation in m2c** (Recommended - Medium effort)
2. **Ghidra pcode bridge via pyghidra** (High effort, complex)
3. **pypcode SLEIGH library** (Medium effort, needs custom builds)
4. **Hybrid Ghidra/m2c workflow** (Low effort, manual)

**Recommendation**: Implement VMX128 support directly in m2c, using the existing Ghidra SLEIGH semantics as a reference. This provides the best balance of effort, maintainability, and integration with the existing decomp workflow.

---

## Current State

### m2c's Vector Support

**Current state**: None. The PPC backend in `/home/free/code/milohax/m2c/m2c/arch_ppc.py` handles:
- Scalar integer instructions (add, sub, mul, etc.)
- Scalar floating-point instructions (fadd, fmul, etc.)
- Load/store instructions
- Branch/comparison instructions
- GameCube/Wii paired-singles (`psq_l`) are stubbed with `ErrorExpr("psq_l unimplemented")`

When m2c encounters an unknown instruction, it emits:
```c
M2C_ERROR(/* unknown instruction: vaddfp128 $v0, $v1, $v2 */)
```

**Key files**:
- `/home/free/code/milohax/m2c/m2c/arch_ppc.py` - PPC architecture definition (~1600 lines)
- `/home/free/code/milohax/m2c/m2c/translate.py` - Core translation logic
- `/home/free/code/milohax/m2c/m2c/evaluate.py` - Expression evaluation helpers

### Ghidra VMX128 Implementation

**Location**: `~/code/milohax/vmx128-research/ghidra-vmx128/Ghidra/Processors/PowerPC/data/languages/vmx128.sinc`

**Status**: Complete and validated
- 2,734 lines of SLEIGH definitions
- 77 VMX128 instructions implemented
- Full semantics for core operations (load/store, arithmetic, logical, compare)
- Pcodeop stubs for complex operations (permute, pack/unpack, D3D)

**Key implementation patterns** (from vmx128.sinc):

```sleigh
# Load/store with 16-byte alignment
:lvx128 vregD_21_25, regB_16_20, regC_11_15 is ... {
    tmp:$(REGISTER_SIZE) = (regB_16_20 + regC_11_15) & 0xfffffffffffffff0;
    vregD_21_25 = *[ram]:16 tmp;
}

# Lane-wise FP arithmetic
:vaddfp128 vregD_21_25, vregA_16_20, vregB_11_15 is ... {
    local a_0:4 = vregA_16_20[96,32];  # Extract lane 0
    local b_0:4 = vregB_11_15[96,32];
    local res_0:4 = a_0 f+ b_0;        # FP add
    vregD_21_25[96,32] = res_0;        # Store result
    # ... repeat for lanes 1-3
}

# Logical operations (full 128-bit)
:vand128 vregD_21_25, vregA_16_20, vregB_11_15 is ... {
    vregD_21_25 = vregA_16_20 & vregB_11_15;
}

# Complex operations use pcodeop stubs
define pcodeop vectorDotProduct3128;
:vmsum3fp128 vregD_21_25, vregA_16_20, vregB_11_15 is ... {
    vregD_21_25 = vectorDotProduct3128(vregA_16_20, vregB_11_15);
}
```

### DC3 VMX128 Usage Statistics

From `/home/free/code/milohax/dc3-decomp/docs/vmx128/DC3_VMX128_USAGE.md`:

| Tier | Instructions | Count | % of Total |
|------|-------------|-------|------------|
| Critical (>1000) | 8 | 22,329 | 60% |
| High (500-1000) | 6 | 4,429 | 12% |
| Medium (200-500) | 18 | 6,121 | 17% |
| Lower (50-200) | 25 | 3,365 | 9% |
| Rare (<50) | 20 | 776 | 2% |
| **Total** | **77** | **37,020** | **100%** |

**Top 8 instructions** (60% of usage):
1. `vcmpgtfp128` (8,020) - Compare greater-than FP
2. `lvx128` (3,719) - Load vector indexed
3. `vsldoi128` (2,758) - Shift left by octet
4. `stvx128` (2,709) - Store vector indexed
5. `vperm128` (1,961) - Permutation
6. `vmulfp128` (1,701) - Multiply FP
7. `vor128` (1,423) - Logical OR
8. `vaddfp128` (1,039) - Add FP

---

## Integration Options Analysis

### Option A: Direct Implementation in m2c (RECOMMENDED)

**Approach**: Add VMX128 instruction handlers to `arch_ppc.py`, following the existing patterns for scalar instructions.

**Effort**: Medium (40-80 hours)

**Implementation Plan**:

#### Phase 1: Infrastructure (8-16 hours)
1. Add vector register definitions (`vr0`-`vr127`) to `PpcArch.all_regs`
2. Create `Type.v128()` or similar for 128-bit vector type
3. Add helper functions for lane-wise operations

```python
# In arch_ppc.py, add to all_regs:
vector_regs = [Register(f"vr{i}") for i in range(128)]

# Add vector type support in types.py:
@staticmethod
def v128() -> Type:
    return Type(kind=TypeKind.PRIMITIVE, size=16, sign=Signedness.UNSIGNED)
```

#### Phase 2: Core Instructions (16-24 hours)
Implement the top 8 instructions (60% coverage):

```python
# In instrs_destination_first or a new instrs_vmx128 dict:
"lvx128": lambda a: handle_vmx_load(a, type=Type.v128()),
"stvx128": lambda a: make_vmx_store(a, type=Type.v128()),
"vaddfp128": lambda a: fn_op("__vaddfp128", [a.vreg(1), a.vreg(2)], Type.v128()),
"vmulfp128": lambda a: fn_op("__vmulfp128", [a.vreg(1), a.vreg(2)], Type.v128()),
"vor128": lambda a: BinaryOp.int(a.vreg(1), "|", a.vreg(2)),
"vand128": lambda a: BinaryOp.int(a.vreg(1), "&", a.vreg(2)),
"vcmpgtfp128": lambda a: fn_op("__vcmpgtfp128", [a.vreg(1), a.vreg(2)], Type.v128()),
"vsldoi128": lambda a: fn_op("__vsldoi128", [a.vreg(1), a.vreg(2), a.imm(3)], Type.v128()),
```

#### Phase 3: Extended Instructions (16-24 hours)
Implement remaining high/medium priority instructions.

#### Phase 4: Intrinsic Mapping (8-16 hours)
Map m2c output to XDK intrinsics where possible:

| m2c Output | XDK Intrinsic |
|------------|---------------|
| `__vaddfp128(a, b)` | `__vaddfp(a, b)` |
| `__vmulfp128(a, b)` | `__vmulfp(a, b)` |
| `__vmaddfp128(a, b, c)` | `__vmaddfp(a, b, c)` |

**Advantages**:
- Cleanest integration with existing workflow
- No external dependencies
- Matches existing m2c patterns
- Easy to maintain and extend
- Can generate actual C code (not just function calls)

**Disadvantages**:
- Requires understanding m2c's architecture
- Cannot reuse Ghidra's semantic definitions directly
- Need to implement register encoding manually

**Code Example** - Adding `vaddfp128`:

```python
# In arch_ppc.py

# Add to INSTRS_R0_AS_ZERO if needed for base register handling
# (VMX128 instructions use different addressing)

# Add vector register handling
def vreg(self, index: int) -> Expression:
    """Get vector register argument."""
    arg = self.raw_arg(index)
    if isinstance(arg, Register):
        return self.regs[arg]
    raise DecompFailure(f"Expected vector register at index {index}")

# Add to instrs_destination_first:
"vaddfp128": lambda a: fn_op(
    "__vaddfp128",
    [a.vreg(1), a.vreg(2)],
    Type.v128()
),

# Or for better output, implement lane-wise semantics:
"vaddfp128": lambda a: VectorOp(
    op="+",
    left=a.vreg(1),
    right=a.vreg(2),
    lanes=4,
    lane_type=Type.f32(),
    result_type=Type.v128()
),
```

---

### Option B: Ghidra Pcode Bridge via pyghidra

**Approach**: Have m2c query Ghidra (via pyghidra-mcp) to get pcode or decompiled snippets for VMX128 instructions.

**Effort**: High (80-120 hours)

**Architecture**:
```
Assembly Input
     |
     v
  m2c parser
     |
     +--[VMX128 instruction?]---> pyghidra query
     |                                  |
     v                                  v
  Normal m2c                      Ghidra pcode
  translation                          |
     |                                  v
     +<---------[C snippet]<------  pcode->C
     |
     v
  Combined C output
```

**Implementation**:

1. **Ghidra Service** (existing pyghidra-mcp): Extend with pcode extraction
```python
# In pyghidra_mcp/tools.py
def get_instruction_pcode(self, address: str) -> list[dict]:
    """Get pcode operations for instruction at address."""
    addr = self.program.getAddressFactory().getAddress(address)
    listing = self.program.getListing()
    instr = listing.getInstructionAt(addr)
    if instr:
        pcode = instr.getPcode()
        return [{"op": op.getMnemonic(), "inputs": [...], "output": ...} for op in pcode]
    return []
```

2. **m2c Integration**: Add IPC to call Ghidra
```python
# In arch_ppc.py
def handle_vmx128_via_ghidra(a: InstrArgs) -> Expression:
    addr = a.instruction.meta.address
    pcode = ghidra_client.get_instruction_pcode(addr)
    return pcode_to_expression(pcode)
```

3. **Pcode to C Translation**: Convert Ghidra pcode to m2c expressions

**Advantages**:
- Leverages existing VMX128 implementation
- Automatically gets updates when Ghidra improves
- No need to reimplement instruction semantics

**Disadvantages**:
- Complex IPC/integration
- Performance overhead (Ghidra startup, JVM)
- Requires Ghidra running alongside m2c
- Pcode->C translation is non-trivial
- Harder to debug and maintain

---

### Option C: pypcode SLEIGH Library

**Approach**: Use [pypcode](https://github.com/angr/pypcode) to load our custom VMX128 SLEIGH definitions and lift instructions to pcode, then translate to C.

**Effort**: Medium-High (60-100 hours)

**Prerequisites**:
- Install pypcode: `pip install pypcode`
- Build custom SLEIGH spec with VMX128 support
- Or patch pypcode to load our modified Ghidra processor module

**Implementation**:

```python
# Conceptual - pypcode usage
from pypcode import Context, PcodePrettyPrinter

# Load PowerPC with VMX128 (requires custom build)
ctx = Context("PowerPC:BE:64:Xenon")

# Translate instruction
translation = ctx.translate(bytes.fromhex("14001234"), base_address=0x82000000)
for op in translation.ops:
    print(op)  # COPY vr1, vr2, ...
```

**Challenges**:
1. pypcode bundles its own SLEIGH specs from Ghidra - need to patch or rebuild
2. Custom processor variants require rebuilding pypcode
3. Still need pcode->C translation layer

**Advantages**:
- Python-native, no JVM
- Fast instruction lifting
- Can process individual instructions

**Disadvantages**:
- Requires custom pypcode build
- pypcode doesn't include Xbox 360/Xenon variant
- Still need to translate pcode to C
- Maintenance burden (keeping pypcode in sync)

---

### Option D: Hybrid Workflow (Manual)

**Approach**: Use Ghidra for VMX-heavy functions, m2c for scalar code, manually merge.

**Effort**: Low for tooling, High per-function

**Workflow**:
1. Identify VMX-heavy functions in DC3
2. Decompile with modified Ghidra (VMX128 support)
3. Clean up Ghidra output manually
4. For scalar portions, use m2c
5. Manually merge and adjust

**Current State**: This is essentially what the project does now:
- Ghidra with VMX128 for vector code analysis
- m2c for scalar code decompilation
- Manual integration

**Advantages**:
- Works today
- No additional tooling needed
- Human review catches errors

**Disadvantages**:
- Time-consuming per function
- Inconsistent output quality
- Doesn't scale

---

## Technical Deep-Dive: m2c Architecture

### How m2c Processes Instructions

1. **Parsing** (`asm_instruction.py`): Assembly text -> `AsmInstruction`
2. **Normalization** (`arch_ppc.py::normalize_instruction`): Canonicalize forms
3. **Pattern Matching** (`asm_pattern.py`): Detect multi-instruction idioms
4. **Instruction Parsing** (`arch_ppc.py::parse`): Create `Instruction` with I/O info
5. **Evaluation** (`evaluate.py` + `arch_ppc.py`): Execute `eval_fn` to build expression tree
6. **Translation** (`translate.py`): Convert expressions to C code

### Adding New Instructions

For each instruction, define:

```python
# 1. Input/output registers
inputs = [Register("vr1"), Register("vr2")]
outputs = [Register("vr0")]

# 2. Evaluation function
def eval_fn(s: NodeState, a: InstrArgs) -> None:
    result = fn_op("__vaddfp128", [a.vreg(1), a.vreg(2)], Type.v128())
    s.set_reg(a.reg_ref(0), result)

# 3. Add to instruction table
instrs_vmx128: InstrMap = {
    "vaddfp128": lambda a: fn_op("__vaddfp128", [a.vreg(1), a.vreg(2)], Type.v128()),
    ...
}
```

### Register Encoding Challenge

VMX128 uses non-contiguous bits for 7-bit register encoding:
- VD128: bits 21-25 (low 5) + bits 2-3 (high 2)
- VA128: bits 16-20 + bits 5, 10
- VB128: bits 11-15 + bits 0-1

The disassembler (objdiff/spimdisasm) handles this, so m2c receives pre-decoded register names. No special handling needed in m2c if the disassembler outputs `vr47` correctly.

---

## Recommended Implementation Plan

### Phase 1: Foundation (Week 1)

1. **Add vector register support** to `arch_ppc.py`:
   ```python
   vector_regs = [Register(f"vr{i}") for i in range(128)]
   all_regs = saved_regs + temp_regs + vector_regs + ...
   ```

2. **Add vector type** to `types.py`:
   ```python
   @staticmethod
   def v128() -> Type:
       return Type(kind=TypeKind.PRIMITIVE, size=16)
   ```

3. **Create helper functions** for vector operations:
   ```python
   def vreg(a: InstrArgs, index: int) -> Expression:
       """Get vector register at argument index."""
       ...
   ```

### Phase 2: Core Instructions (Week 2)

Implement in priority order:

| Priority | Instructions | Output |
|----------|-------------|--------|
| 1 | `lvx128`, `stvx128` | Load/store with type |
| 2 | `vor128`, `vand128`, `vxor128`, `vnor128` | Bitwise operations |
| 3 | `vaddfp128`, `vsubfp128`, `vmulfp128` | `__vaddfp128(a, b)` |
| 4 | `vcmpgtfp128`, `vcmpeqfp128` | `__vcmpgtfp128(a, b)` |
| 5 | `vsldoi128`, `vperm128` | Intrinsic calls |

### Phase 3: Extended Support (Week 3)

1. Implement remaining tier 2/3 instructions
2. Add intrinsic headers (`vectorintrinsics.h` integration)
3. Test with DC3 functions

### Phase 4: Polish (Week 4)

1. Optimize common patterns (e.g., vector copy via `vor128 vD, vS, vS`)
2. Add documentation
3. Submit PR to m2c upstream (optional)

---

## Effort Estimates

| Option | Setup | Per-Instruction | Total | Maintenance |
|--------|-------|-----------------|-------|-------------|
| A: Direct m2c | 16h | 30min | 40-80h | Low |
| B: Ghidra bridge | 40h | 1h | 80-120h | High |
| C: pypcode | 24h | 45min | 60-100h | Medium |
| D: Hybrid manual | 0h | 2-4h/func | Varies | N/A |

---

## Risks and Challenges

### Technical Risks

1. **Instruction encoding complexity**: VMX128's split register fields could cause disassembly issues. Mitigation: Verify objdiff/spimdisasm handle encoding correctly.

2. **Type system limitations**: m2c's type system may not handle 128-bit vectors well. Mitigation: Use opaque type or array of floats.

3. **Expression complexity**: Vector lane operations may produce unwieldy C code. Mitigation: Use intrinsic function calls instead of inline expressions.

### Project Risks

1. **Upstream acceptance**: m2c maintainers may not want Xbox 360-specific code. Mitigation: Design as optional target variant.

2. **Maintenance burden**: VMX128 support requires ongoing maintenance. Mitigation: Document thoroughly, use reference tests.

---

## Conclusion

**Recommended approach**: Option A (Direct m2c implementation)

This provides the cleanest integration with the existing decomp workflow while remaining maintainable. The Ghidra SLEIGH implementation serves as an authoritative reference for instruction semantics, reducing the research burden.

The implementation can be phased:
- Phase 1 enables basic VMX128 decompilation (M2C_ERROR -> intrinsic calls)
- Phase 2 covers 60%+ of VMX128 usage in DC3
- Phase 3+ can be done incrementally as needed

Total estimated effort: **40-80 hours** for production-ready VMX128 support in m2c.

---

## References

### Project Documentation
- `/home/free/code/milohax/dc3-decomp/docs/vmx128/` - VMX128 Ghidra implementation docs
- `/home/free/code/milohax/dc3-decomp/docs/vmx128/GHIDRA_IMPLEMENTATION.md` - SLEIGH patterns
- `/home/free/code/milohax/dc3-decomp/docs/vmx128/ISA_REFERENCE.md` - Instruction reference

### Code References
- `/home/free/code/milohax/m2c/m2c/arch_ppc.py` - m2c PPC backend
- `~/code/milohax/vmx128-research/ghidra-vmx128/Ghidra/Processors/PowerPC/data/languages/vmx128.sinc` - SLEIGH definitions

### External Resources
- [pypcode](https://github.com/angr/pypcode) - Python SLEIGH bindings
- [m2c](https://github.com/matt-kempster/m2c) - Decompiler project
- [pyghidra](https://github.com/NationalSecurityAgency/ghidra/tree/master/Ghidra/Features/PyGhidra) - Python Ghidra bindings
