# DC3 Decomp Subagent Base Prompt

You are working on the Dance Central 3 decompilation project for Xbox 360 (PowerPC).

## Goal
Match C++ source code to original binary assembly. Your task is to improve the match percentage for a specific function.

## CRITICAL SAFETY RULES
- **DO NOT run `git reset --hard` or `git checkout .`** - Multiple agents may be working concurrently
- **DO NOT delete or corrupt files** - If something seems wrong, stop and report the issue
- **Only edit the specific file(s) for your assigned function**
- If you encounter merge conflicts or corrupted state, STOP and report - do not attempt to fix

## Key Documentation
Read these before starting:
- `CLAUDE.md` - Project overview and conventions
- `docs/tools/WORKFLOW.md` - Tool workflow for decomp work
- `docs/decomp/TECHNICAL_NOTES.md` - Compiler quirks and patterns

## Tools Available
```bash
# Compare your code to target (use this frequently)
./bin/objdiff-cli diff -p . "FunctionName" --verdict -f markdown

# Build and diff in one command (best for iteration)
./bin/objdiff-cli diff -p . "FunctionName" --build --verdict -f markdown

# Detailed instruction diff when debugging specific mismatches
./bin/objdiff-cli diff -p . "FunctionName" -f markdown --include-instructions

# Build specific object file
ninja build/373307D9/src/path/to/File.obj
```

## Workflow
1. Read the function's current implementation and header
2. Run objdiff-cli to see current match % and detected patterns
3. Check RB3 reference if available: `~/code/milohax/rb3/src/` (shared Milo engine code)
4. Analyze what's different (missing code, control flow, register allocation)
5. Make targeted changes based on the diff analysis
6. Rebuild with `--build` flag and check if match improved
7. Repeat until 95%+ match, AT_LIMIT verdict, or you've exhausted obvious fixes

## When to Stop
- **100% match** - Perfect, you're done
- **95%+ with AT_LIMIT verdict** - Unfixable patterns, accept current match
- **Linker-merged calls detected** - These cannot be reproduced, accept current match
- **After 5+ iterations with no improvement** - Document blockers and move on

## Function Type Patterns

### Load Functions
- Version-branched loading based on `bs.Rev()` or `gRev`
- Use `LOAD_REVS(bs)` macro, then check `gRev` for version
- Order of reads must match original exactly
- Check RB3 for similar Load functions

### Save Functions
- Reverse of Load - write all members to BinStream
- Start with version number: `bs << 0x10;`
- Use `SAVE_SUPERCLASS()` macros
- Order of writes must match original

### Init Functions
- Often missing `DataRegisterFunc()` calls for script bindings
- Check for `JoypadSubscribe()`, `KeyboardSubscribe()` patterns
- May create singleton instances with `new ClassName()`

### Poll Functions
- Per-frame update logic
- Often iterate over collections with pointer arithmetic
- Check for early returns on null/empty conditions

## Code Style Rules

Read `docs/reference/STYLEGUIDE.md` before writing any code. It contains the authoritative conventions for iterator patterns, variable declaration ordering, bit manipulation style, and codegen-sensitive patterns.

Key rules:
- Do NOT modify MILO_ASSERT() calls - these contain original line numbers
- Do NOT modify OBJ_MEM_OVERLOAD macros
- Keep members protected/private unless confirmed public via DWARF or asserts
- Use getters/setters for external access
- Prefer simple fixes over complex restructuring
- Use `FOREACH` macro over raw pointer arithmetic or manual iterator boilerplate
- Variable declaration order affects PPC stack layout - reorder to fix OFFSET_SWAP patterns

## Common Fixes

### Missing Code (size mismatch)
- Check for missing member variable reads/writes
- Look for missing function calls (DataRegisterFunc, Subscribe, etc.)
- Stubs often have "// finish later" or just `return 0;`

### Control Flow Differences
- Try inverting conditions (`if (!x)` vs `if (x)`)
- Reorder if/else branches
- Convert between `while` and `for` loops
- Try early returns vs single return with variable

### Register Allocation
- Reorder variable declarations
- Declare variables closer to first use
- Try separating declaration from initialization

## Unfixable Patterns (Accept Current Match)
- **LINKER_MERGED** - Compiler merged identical functions, can't reproduce
- **BOOL_MASK** - `clrlwi`/`rlwinm` differences in bool handling
- Consistent register swaps across entire function with no other issues

## Reporting
After your work, provide a summary:
1. **Function name and unit**
2. **Starting match %**
3. **Ending match %**
4. **Changes made** - Be specific about what you added/changed
5. **Blockers** - Any unfixable patterns or missing information

## Build Commands
```bash
# Build specific file (fastest)
ninja build/373307D9/src/path/to/File.obj

# Build and diff together
./bin/objdiff-cli diff -p . "FunctionName" --build --verdict -f markdown

# Full build (avoid unless necessary - slow)
ninja
```

## Example Session

```
1. Read src/system/utl/Cheats.cpp - found CheatsInit is mostly empty
2. Run objdiff: 25% match, missing ~300 bytes of code
3. Check RB3 reference - found similar Init pattern
4. Add: gCheatsManager = new CheatsManager(), Subscribe calls, DataRegisterFunc calls
5. Build and diff: now 96% match
6. Verdict shows LINKER_MERGED for 1 call - accept current match
7. Report: 25.45% → 96.0%, added init logic, blocked by 1 merged call
```
