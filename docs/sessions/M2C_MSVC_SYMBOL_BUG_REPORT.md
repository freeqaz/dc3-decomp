# m2c Bug Report: Failed to parse MSVC-mangled symbol operands in PPC instructions

**Date**: 2026-01-26
**Status**: ✅ FIXED (local fork at ~/code/milohax/m2c)
**Upstream**: https://github.com/matt-kempster/m2c

## Summary

m2c fails to decompile PPC functions that reference MSVC-mangled C++ symbols as instruction operands. The parser doesn't handle symbol names containing `?` and `@` characters when used with address modifiers like `@ha` and `@l`.

## Error Message

```
Decompilation failure:

Failed to parse instruction at <stdin> line 12: lis r11, ?TheDebug@@3VDebug@@A@ha
```

## Root Cause

Xbox 360 (PowerPC) code compiled with MSVC uses mangled C++ symbol names directly in assembly. When loading the address of a global variable, the compiler generates:

```asm
lis r11, ?TheDebug@@3VDebug@@A@ha    ; Load high 16 bits of symbol address
addi r29, r11, ?TheDebug@@3VDebug@@A@l  ; Add low 16 bits
```

The symbol `?TheDebug@@3VDebug@@A` demangles to `class Debug TheDebug` (a global Debug instance). The `@ha` and `@l` suffixes are standard PPC relocations for high-adjusted and low parts of a 32-bit address.

m2c's parser appears to not handle the `?` and `@` characters in symbol names, or conflicts with the `@ha`/`@l` relocation suffix parsing.

## Steps to Reproduce

1. Create a file `test.s` with the following content:

```asm
.global CharMirror_Load
CharMirror_Load:
	mflr r12
	bl __savegprlr_27
	stwu r1, -0xa0(r1)
	mr r30, r4
	mr r31, r3
	li r5, 0x4
	addi r4, r1, 0x50
	mr r3, r30
	bl "?ReadEndian@BinStream@@QAAXPAXH@Z"
	lis r11, ?TheDebug@@3VDebug@@A@ha
	lis r10, lbl_82017228@ha
	stw r30, 0x68(r1)
	addi r29, r11, ?TheDebug@@3VDebug@@A@l
	addi r28, r10, lbl_82017228@l
	lwz r11, 0x50(r1)
	clrlwi r10, r11, 16
	srwi r27, r11, 16
	stw r10, 0x60(r1)
```

2. Run m2c:
```bash
python3 m2c.py -t ppc --valid-syntax test.s
```

3. Observe the parse failure.

## Expected Behavior

m2c should parse the MSVC-mangled symbol and treat it as an external/global variable reference, producing something like:

```c
extern Debug TheDebug;
// ...
r11 = &TheDebug;  // or similar
```

## Source Files

| File | Description |
|------|-------------|
| `build/373307D9/src/system/char/CharMirror.obj` | Compiled object file |
| `orig/373307D9/default.xex` | Original Xbox 360 executable |
| `build/373307D9/asm/system/char/CharMirror.s` | Disassembled source |

## Function Details

- **Symbol**: `?Load@CharMirror@@UAAXAAVBinStream@@@Z`
- **Demangled**: `public: virtual void __cdecl CharMirror::Load(class BinStream &)`
- **Address in XEX**: `0x823A5AC8`
- **Size**: `0x1A8` bytes (424 bytes)

## Context

This is from a Dance Central 3 (Xbox 360) decompilation project. The game was compiled with MSVC for Xbox 360, which uses MSVC name mangling. Many functions reference global objects like `TheDebug`, `TheTaskMgr`, etc., making this a common failure pattern.

## Workaround

Currently we skip m2c for affected functions and rely on Ghidra decompilation instead. The orchestrator context collector shows an informative error message when this occurs.

## Impact

This affects a significant portion of functions in the codebase - any function that references a global C++ object will fail m2c decompilation. Common globals include:

- `?TheDebug@@3VDebug@@A` - Debug singleton
- `?TheTaskMgr@@3VTaskMgr@@A` - Task manager
- `?TheContentMgr@@3VContentMgr@@A` - Content manager
- Many others

## Possible Fix Areas

The issue likely resides in m2c's instruction parser, specifically in how it tokenizes operands. The parser needs to:

1. Recognize that `?Symbol@@...@ha` is a single symbol reference with a relocation suffix
2. Not confuse the `@` in MSVC mangling with the `@ha`/`@l` relocation markers
3. Possibly require the symbol to be quoted or use a different delimiter strategy

## Fix Verification (2026-01-26)

The fix was implemented in `~/code/milohax/m2c/` and verified with the following test cases:

### Test Results

| Pattern | Example | Status |
|---------|---------|--------|
| Global object (unquoted) | `?TheDebug@@3VDebug@@A@ha` | ✅ PASS |
| Global object (quoted) | `"?TheTaskMgr@@3VTaskMgr@@A"@ha` | ✅ PASS |
| Static member | `?sm_cs@CXbcImpl@@0U_RTL_CRITICAL_SECTION@@A@ha` | ✅ PASS |
| Function pointer | `?sm_pCallback@CXbcImpl@@0P6AXJPAU_XBC_EVENT_PARAMS@@PAX@ZA@ha` | ✅ PASS |
| Float constant | `"__real@3f800000"@ha` | ✅ PASS |
| String literal | `"??_C@_0CF@EJLMPHBI@string@"@ha` | ✅ PASS |
| Template type (RTTI) | `"??_R0PAV?$_List_node@_N@stlpmtx_std@@@8"@ha` | ✅ PASS |

### Sample Output (CharMirror::Load)

Before fix:
```
Decompilation failure:
Failed to parse instruction at <stdin> line 12: lis r11, ?TheDebug@@3VDebug@@A@ha
```

After fix:
```c
extern M2C_UNK ?TheDebug@@3VDebug@@A;

void CharMirror_Load(...) {
    ...
    ?Fail@Debug@@QAAXPBDPAX@Z(&?TheDebug@@3VDebug@@A, ...);
    ...
}
```

### Comprehensive Test

A batch test with 7 diverse MSVC symbol patterns all decompiled successfully, generating proper `extern` declarations and symbol references in the output C code.

## Related

- MSVC name mangling: https://docs.microsoft.com/en-us/cpp/build/reference/decorated-names
- PPC relocation types: `@ha` (high adjusted), `@l` (low), `@h` (high)
