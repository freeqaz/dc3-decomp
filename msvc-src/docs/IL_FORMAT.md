# MSVC PPC Intermediate Language Format

Date: 2026-03-10
Compiler: MSVC 16.00.11886.00 (Xbox 360 PPC cross-compiler)

## Overview

The MSVC compilation pipeline uses a typed intermediate language (IL) between:
- **c1xx.dll** (C++ front-end) — writes IL files
- **c2.dll** (PPC back-end) — reads IL, optimizes, emits PPC code

IL files are normally deleted after compilation. Capture them by making
c2.dll fail early with `/d2nop`.

## File Structure

Each compilation produces 5 files with a common base name `_CL_<hash>`:

| Suffix | Purpose | Typical size |
|--------|---------|-------------|
| `ex` | Expression/IL bytecode (main content) | 2-5KB |
| `gl` | Global symbols + metadata | 200-1000B |
| `sy` | Local symbol/parameter definitions | 50-200B |
| `in` | Type/declaration imports | 200-500B |
| `db` | Debug info (line numbers) | 50-200B |

### Capture Method

```bash
# /Bd shows the IL filename, /d2nop makes c2.dll fail immediately (preserving IL)
wibo cl.exe /Bd /d2nop /Ox /GS- /c /Fo<output.obj> <source.cpp>
# Output shows: -il _CL_xxxxxxxx
# Files preserved in CWD: _CL_xxxxxxxx{ex,gl,sy,in,db}
```

Or use the tool:
```bash
python3 msvc-src/tools/il_parser.py analyze source.cpp
```

For side-by-side source IL vs lifted PPC comparison, use:
```bash
python3 msvc-src/tools/ppc_il_lifter.py compare-source source.cpp \
  --function SomeFunction
```

To preserve a reusable named bundle in the fixture corpus:
```bash
python3 msvc-src/tools/il_parser.py capture source.cpp \
  --output-dir msvc-src/analysis/il-fixtures \
  --bundle-name cast_vs_and_return
```

To inspect a captured bundle:
```bash
python3 msvc-src/tools/il_parser.py list-bundle \
  msvc-src/analysis/il-fixtures/cast_vs_and_return --functions
```

To compare source IL against lifted PPC and derive higher-level shape facts:
```bash
python3 msvc-src/tools/ppc_il_lifter.py compare-source source.cpp \
  --function SomeFunction --json
```

### Bundle Manifest

Named captures write `manifest.json` next to the `_CL_*` files. The manifest is
the stable entry point for fixture-based tooling and should be treated as part
of the captured artifact.

Current manifest fields:

- `bundle_name`
- `captured_at`
- `source_path`
- `bundle_base`
- `il_base`
- `run_cwd`
- `compiler_path`
- `wibo_path`
- `command`
- `files`

The `files` map records presence and size for `.ex`, `.gl`, `.sy`, `.in`, and
`.db`. This is the minimum metadata needed for a persistent IL corpus.

See also: `msvc-src/docs/PPC_IL_LIFTER.md` for the constrained PPC->IL lift used
to compare source IL against emitted PPC shape.

## .ex File (IL Bytecode)

### Header

- Magic: `5B 80 54 0A` (4 bytes)
- Followed by ~2640 bytes of zeros (header/index table)
- IL data starts at offset ~0x0A54

### Function Structure

```
4F 1F 80 05 00 A0 00    -- Function start marker
4F 20 80 FE 00           -- Function descriptor
4F 33 0D 66 12 1C ...    -- Function metadata (flags, attributes)
42 45 0E 06 ...          -- "BE" block entry
0F 4F 02 20 00 4F 01     -- Body marker start

<function_body>           -- Opcodes and data

4F 12                     -- Function separator
47 54 01 54 00            -- "GT" goto/terminate markers
```

Module ends with: `4F 02 20 00 4F 01 NN 4D` where `4D` = 'M' (Module end)

### Function Body Structure

```
NN                        -- Function index (0-based)
53 53                     -- 'SS' = Start Statement
26                        -- Parameter setup marker
XX XX                     -- Result variable token (2 bytes, big-endian)
46                        -- 'F' = Formal parameters marker
2D XX XX [2D YY YY ...]  -- Parameter tokens (separated by 0x2D '-')
4C 4F 11 53               -- 'LO' = Load Operands, 'S' = Start

<operations>              -- See opcodes below

54 02 29 XX XX            -- Terminate + return variable token
```

### Opcodes (Confirmed via Differential Testing)

#### Arithmetic (0x02-0x0A)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x02 | ADD | `a + b` | Binary |
| 0x03 | SUB | `a - b` | Binary |
| 0x04 | MUL | `a * b` | Binary |
| 0x05 | DIV | `a / b` | Binary |
| 0x06 | MOD | `a % b` | Binary |
| 0x08 | NEG | `-a` | Unary |
| 0x09 | SHL | `a << b` | Binary |
| 0x0A | SHR | `a >> b` | Binary |

#### Bitwise (0x0B-0x0E)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x0B | AND | `a & b` | Binary |
| 0x0C | OR | `a \| b` | Binary |
| 0x0D | XOR | `a ^ b` | Binary |
| 0x0E | NOT | `~a` | Unary |

#### Comparison (0x1F-0x24)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x1F | EQ | `a == b` | Followed by CB (conditional) |
| 0x20 | NE | `a != b` | Followed by CB |
| 0x21 | LE | `a <= b` | Followed by CB |
| 0x22 | LT | `a < b` | Followed by CB |
| 0x23 | GE | `a >= b` | Followed by CB |
| 0x24 | GT | `a > b` | Followed by CB |

Note: Comparison order is EQ, NE, LE, LT, GE, GT (not the standard EQ,NE,LT,LE,GT,GE).

#### Logical (0x1A-0x1C)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x1A | LOGICAL_NOT | `!a` | Logical negation |
| 0x1B | LOGICAL_OR | `a \|\| b` | Short-circuit OR |
| 0x1C | LOGICAL_AND | `a && b` | Short-circuit AND |

#### Type Conversion (0x2C)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x2C | CAST | `(int)a`, promotions | `2C type 00` — converts operand to target type |

Used for integer promotion (short→int before arithmetic) and
explicit casts. The trailing `00` byte appears to be a flag.

#### Pointer / Memory (0x28-0x32)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x28 | PTR_ADD | `p + offset` | Pointer arithmetic, followed by 2 bytes |
| 0x30 | DEREF | `*p`, `p[i]` | Pointer dereference / load indirect |
| 0x32 | STORE | `*p = v`, `s = 0` | Store indirect / variable init |

#### Switch (0x3B-0x3D)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x3B | SWITCH | `switch(x)` | `3B XX XX` — evaluate expression into dispatch var |
| 0x3C | SWITCH_TABLE | dispatch table | `3C type default_tok` — start table with default target |
| 0x3D | CASE | `case N:` | `3D target_tok` — maps preceding literal to case label |

Switch dispatch table structure:
```
SWITCH(expr) → dispatch_var
<case bodies as labeled blocks>
...
GOTO
SWITCH_TABLE:int default=default_label
  literal(0) CASE → case0_label
  literal(1) CASE → case1_label
  literal(2) CASE → case2_label
FALLTHROUGH
```

For values > 127, literals use `0x80` prefix: `33 type 80 NN NN NN NN` (4-byte LE).
Sparse switches (non-contiguous cases) use the same encoding — c2.dll handles jump table generation.

#### Member Access / Virtual Dispatch (0x27, 0x67, 0x9A)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x27 | MEMBER_PTR | `p->member` | `27 type` — pointer + offset → typed member pointer |
| 0x67 | VCALL_SETUP | virtual call | `67 XX XX XX` — prepare virtual dispatch |
| 0x9A | VCALL_BIND | virtual call | `9A type` — resolve vtable method |

Virtual call sequence for `p->VirtualMethod()`:
```
VCALL_SETUP(method_token)
DEREF(p) → vtable_ptr         (class type, prefix 0xA6)
DEREF(vtable_ptr) → func_ptr  (function pointer type)
VCALL_BIND(method_type)
CALL_START() → return_type
[argument expressions]
CALL_EXEC(args) → return_type
```

Direct member access for `p->mField` (offset 4):
```
MEMBER_PTR(p, 4) → int_ptr
DEREF(int_ptr) → int
```

The `0xA6` type prefix is used for class/struct pointer types in vtable contexts,
unlike `0x86` for fundamental 4-byte types.

#### Compound Assignment (0x0F, 0x36)

| Hex | Name | Source | Notes |
|-----|------|--------|-------|
| 0x0F | COMPOUND_ADD | `s += a` | Add-assign to variable |
| 0x36 | COMPOUND_SUB | `a--` | Sub-assign to variable |

Note: Compound ops use `26 XX XX` (variable ref) before the operand load,
and `4B` ('K') as end marker.

#### Function Calls (0xBD, 0x55)

| Hex | Name | Notes |
|-----|------|-------|
| 0xBD | CALL_START | Setup: `26 XX XX BD type 00 80 01 10 00 00` |
| 0x55 | CALL_EXEC | Execute: `55 type 4C` after argument expressions |

Call sequence: `26 func_tok BD ret_type metadata [arg_exprs] 55 ret_type 4C`
- `26 XX XX` — function reference token
- `BD` — call start marker
- `86 41 74` — return type
- `00 80 01 10 00 00` — 6-byte call metadata (calling convention/flags)
- Arguments computed as normal IL expressions between CALL_START and CALL_EXEC
- `55` — execute call, followed by result type + `4C` ('L') marker

#### Control Flow (0x38, 0x54)

| Hex | Name | Notes |
|-----|------|-------|
| 0x38 | COND_BRANCH | `if (cond)` — conditional branch to label token |
| 0x4F 0x01 NN | LABEL_DEF | Block label definition (label number NN) |
| 0x54 0x02 | RETURN | `54 02 29 XX XX` — return value |
| 0x54 0x03 | FALLTHROUGH | Unconditional continue to next block |
| 0x54 0x04 | GOTO | `54 04 29 XX XX` — goto label |
| 0x4B | BLOCK_END | 'K' — end of compound operation |

Control flow structure:
```
4F 01 NN              -- Label definition (block NN)
<comparison>          -- e.g., GT(a, 0)
38 XX XX              -- Conditional branch to label XX XX
...
4F 01 NN+1            -- Next block label
<then-body>
54 04 29 XX XX        -- Goto label (loop back-edge or branch target)
54 03                  -- Fallthrough
<else-body>
4F 01 NN+2            -- Exit label
54 02 29 XX XX        -- Return value
```

#### Structural

| Hex | Name | Notes |
|-----|------|-------|
| 0x4F | 'O' | Object/block prefix |
| 0x53 0x53 | 'SS' | Start Statement |
| 0x4C 0x4F | 'LO' | Load Operands |
| 0x43 0x42 | 'CB' | Conditional Branch (ternary select) |
| 0x47 0x54 | 'GT' | Goto/Terminate (function-level) |
| 0x4D | 'M' | Module end |
| 0x4B | 'K' | Block/operation end |

#### Data Encoding

| Pattern | Meaning |
|---------|---------|
| `B9 XX XX type` | Load variable (token XX XX), with type |
| `33 type NN` | Literal value NN, with type |
| `3A XX XX` | Assign to variable (token XX XX) |
| `26 XX XX` | Variable/function reference (in compound ops and calls) |
| `41 type` | Result type annotation |
| `29 XX XX` | Value reference (in return/goto) |
| `54 02 29 XX XX` | Return variable XX XX |

### Type Encoding

Types are encoded as 3-4 byte sequences. The first byte encodes **size class**:

| Prefix | Size | Types |
|--------|------|-------|
| `82` | 1 byte | char, unsigned char, bool |
| `84` | 2 bytes | short, unsigned short |
| `86` | 4 bytes | int, unsigned int, float, pointers |
| `88` | 8 bytes | double |

The second byte encodes signedness/class within each size group:

| Encoding | C Type | Sign class | Notes |
|----------|--------|------------|-------|
| `82 11 70` | `char` | signed | |
| `82 12 20` | `unsigned char` | unsigned | |
| `82 12 30` | `bool` | unsigned | |
| `84 21 11` | `short` | signed | |
| `84 22 21` | `unsigned short` | unsigned | |
| `86 41 74` | `int` | signed | Default type in most contexts |
| `86 42 75` | `unsigned int` | unsigned | |
| `86 45 40` | `float` | IEEE float | |
| `86 43 f4 08` | `int*` | pointer | 4-byte pointer (has extra byte) |
| `88 85 41` | `double` | IEEE double | |

### Type Semantics

The same IL opcodes (ADD, SUB, MUL, etc.) work for all types — the type
marker on operands determines whether c2.dll emits integer or floating-point
PPC instructions:
- `ADD` on `int` → `add rD,rA,rB`
- `ADD` on `float` → `fadds fD,fA,fB`
- `ADD` on `double` → `fadd fD,fA,fB`

**Signedness flows through to PPC instruction selection.** The same `GT` opcode
with signed vs unsigned operands produces dramatically different PPC code:
- `GT` on `int` (signed) → `subfc/eqv/srwi/addze/clrlwi` (6 insns)
- `GT` on `uint` (unsigned) → `subfic/subfe/clrlwi` (3 insns)

### Integer Promotion in IL

Small types (char, short) are promoted to `int` before arithmetic via `CAST`
(opcode 0x2C), matching C's integer promotion rules:

```
CAST(a:short) → int     -- promote short to int
CAST(b:short) → int     -- promote short to int
ADD                       -- arithmetic on int
CAST → short              -- demote result back to short
```

For unsigned types, the CB (ternary select) always produces `int`-typed 0/1,
requiring an extra `CAST` to convert back to `unsigned int`:
```
GT(a:uint, b:uint)      -- unsigned comparison
CB(1, 0)                  -- ternary select (int-typed)
CAST → uint               -- convert result to unsigned
```

## .gl File (Globals)

Contains:
- Mangled function names (e.g., `?simple_add@@YAHHH@Z`)
- Source file path (e.g., `z:\tmp\claude-1000\test.cpp`)
- Compiler version markers (`__C1_11886`)
- Section info (`XBLD$W`)
- Token references for function-global mapping

## .sy File (Symbols)

Contains per-function local symbol definitions:
- Parameter names as ASCII strings (e.g., `a`, `b`)
- Token references (2-byte big-endian) preceding each name
- Type codes (`0x74` = int observed)

## .in File (Imports/Types)

Contains type descriptors with size information:
- Width/alignment data for fundamental types
- Import table for external type references

## .db File (Debug)

Contains debug metadata:
- Line number mappings
- Source file references

## Tools

- `msvc-src/tools/il_parser.py` — Parse and analyze IL files
  - `capture` — Compile and capture IL files
  - `parse` — Parse a captured IL file set
  - `analyze` — Capture + parse in one step

## Inlining Threshold

Empirically determined via differential testing (N functions of increasing body size):

| Method | Max inlined | Not inlined | PPC insns at boundary |
|--------|-------------|-------------|----------------------|
| Arithmetic chain | N=39 (40 PPC insns) | N=40 (41 PPC insns) | ~40 |
| If/else chain | N=5 (24 PPC insns) | N=6 (29 PPC insns) | ~26 |

The inliner uses a **weighted cost model**, not raw instruction count:
- Arithmetic operations ≈ weight 1
- Branch operations ≈ weight 8
- Effective threshold ≈ 40 cost units
- COMDAT function bodies are always emitted even when inlined

## Known Gaps

- Compound assignment for MUL/DIV/bitwise not yet tested
- The 2640-byte zero-filled header in .ex files may contain an index table
- Opcodes 0x1D-0x1E unknown (may be additional logical ops or unused)
- Function-to-name mapping is sequential; compiler-generated functions (e.g., derived
  class overrides) may not appear in .gl globals, causing off-by-one name assignments
- `0xA6` class type prefix details (inheritance hierarchy encoding) need more testing
- `0x99` opcode in function preamble (hidden `this` parameter annotation?) unconfirmed

## Methodology

All opcodes discovered by writing minimal test functions that differ by exactly one
operation, compiling to IL (with `/d2nop`), and comparing the hex dumps byte-by-byte.
This differential approach isolates exactly which bytes change for each source construct.

## Relationship to PPC Codegen

The IL is what c2.dll's optimization passes operate on:
- Group 1 (COLOR): Register allocation on IL virtual registers
- Group 2: Algebraic/FP transforms on IL expressions
- Group 3: Main optimization (including record-form fusion — marking `subf.`)
- Group 4: Post-optimization cleanup
- Group 5 Pass 10: IL → PPC instruction selection + Xenon scheduling

Understanding IL opcodes helps predict which PPC patterns will be generated:
- IL `GT` + `CB` with literals → PPC boolean materialization sequence
- IL `SUB` in loop condition → potential `subf.` fusion (if G3P2 marks it)
- IL comparison type (signed vs unsigned) → different carry-based sequences
- IL `SWITCH_TABLE` with case entries → c2.dll generates jump tables or if-chains
- IL `MEMBER_PTR` + `DEREF` → struct member access (`lwz rD, offset(rBase)`)

### Algebraic Optimization (pre-register allocation)

c2.dll performs aggressive algebraic simplification on IL before register allocation.
For example, `s1+s2+s3` where `s1=a+b`, `s2=b+c`, `s3=a+c` gets simplified to
`2*(a+b+c)` → one `add` chain + `slwi` shift. The register allocator then operates
on the simplified form, not the original IL variables.

This means **IL variable count does not directly predict register pressure**. The
compiler eliminates redundant computations and reassociates expressions before
allocating registers.

### Register Save Strategy

MSVC PPC prefers to save raw inputs (parameters, addresses) in callee-saved registers
and recompute derived values after calls, rather than saving pre-computed intermediates.
For example, given `saved = a + b; result = call(a); return saved + result;`, the
compiler saves both `a` and `b` in separate callee-saved registers and recomputes
`a+b` after the call, rather than saving the pre-computed sum.
