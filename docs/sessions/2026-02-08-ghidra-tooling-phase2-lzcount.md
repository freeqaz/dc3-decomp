# Ghidra Tooling Phase 2: LZCOUNT Pattern Annotation

**Date**: 2026-02-08

## Overview

Implemented Phase 2 of the Ghidra tooling improvements: post-processing of Ghidra decompilation output to handle PowerPC `LZCOUNT()` patterns. The initial approach (replacing patterns with simplified equivalents) was implemented, questioned, and revised to an annotation-based approach that preserves original output while adding explanatory comments. Also fixed a pre-existing bug in `direct_client.py`.

## Background: LZCOUNT Patterns

PowerPC's `cntlzw` (Count Leading Zeros Word) instruction appears in Ghidra decompilation as `LZCOUNT()`. The compiler uses it as a branchless boolean negation:

- `LZCOUNT(x) >> 5` -- returns 1 if x == 0, returns 0 if x != 0
- `(ulonglong)(LZCOUNT(x) << 0x20) >> 0x25` -- 64-bit variant, same semantics
- `(uint)LZCOUNT(x) >> 5` -- cast variant

These patterns appear frequently in DC3 decompilation output, including array indexing like `this->unk9a4[LZCOUNT(this->unk64) >> 5]`.

## Initial Implementation: Transformation

The plan called for a `simplify_ppc_decompilation()` function that would replace LZCOUNT patterns with their boolean equivalents:

```
LZCOUNT(x) >> 5  -->  ((x) == 0)
```

This was implemented in `/home/free/code/milohax/pyghidra-mcp/src/pyghidra_mcp/tools.py`, integrated into `_decompile_function_impl()` (applied before caching), and tested with 13 unit tests. All tests passed, and the full pyghidra-mcp test suite (55 tests) remained green.

## The Pivot: Annotation Over Transformation

After the initial implementation, the question was raised: "Are we 100% confident this is the right move? Is it definitely helpful for decomp workflows?"

Critical evaluation revealed significant downsides for decomp work:

1. **Hides instruction-level information** -- Knowing the original uses `cntlzw` could be important for matching. If your C++ generates `cmpwi r3, 0` instead of `cntlzw`, you will not match even though the semantics are identical.
2. **Decomp is about matching assembly, not semantics** -- Ghidra's `LZCOUNT(x) >> 5` tells you "the compiler chose this specific instruction sequence." That is actionable information when trying to replicate it.
3. **False confidence** -- You might write `if (x == 0)` thinking it matches, when the compiler actually needs a different construct to emit `cntlzw`.
4. **Cache contamination** -- Simplified output gets cached, so raw Ghidra output becomes unavailable without a cache clear.

The function was revised from `simplify_ppc_decompilation()` to `annotate_ppc_decompilation()`, which preserves the original pattern and appends a comment:

```
LZCOUNT(x) >> 5  -->  LZCOUNT(x) >> 5 /* == !x */
```

### Implementation Details

The annotation function handles three pattern variants:

- 64-bit: `(ulonglong)(LZCOUNT(x) << 0x20) >> 0x25` gets `/* == !x */` appended
- Cast: `(uint)LZCOUNT(x) >> 5` gets `/* == !x */` appended
- Simple: `LZCOUNT(x) >> 5` gets `/* == !x */` appended (with negative lookahead to avoid matching when a cast prefix is present)

A regex bug was caught during testing: `(uint)LZCOUNT(val) >> 5` was matching both the uint-cast pattern AND the simple pattern, causing double annotation. Fixed by adding a negative lookbehind to the simple pattern so it does not fire when preceded by a closing parenthesis from a cast.

### Files Modified

- `/home/free/code/milohax/pyghidra-mcp/src/pyghidra_mcp/tools.py` -- Added `annotate_ppc_decompilation()`, integrated into `_decompile_function_impl()`
- `/home/free/code/milohax/pyghidra-mcp/tests/unit/test_ppc_simplify.py` -- 13 unit tests covering all pattern variants, multiple occurrences, no-op cases, and the double-annotation regression

## Bug Fix: direct_client.py

An error surfaced during a batch decomp run:

```
'str' object has no attribute 'getEntryPoint'
```

This was a pre-existing bug in `tools/ghidra/direct_client.py` at line 239. The `decompile_function()` method was calling `self.tools.decompile_function(symbol)` where `symbol` is a string, but `GhidraTools.decompile_function()` expects a Ghidra `Function` object, not a string.

**Fix**: Changed to `self.tools.decompile_function_by_name_or_addr(symbol)`, which accepts a string and performs multi-strategy lookup to find the function before decompiling.

## Pre-Implementation Checkpoint Process

Based on the Phase 2 experience, a pre-implementation checkpoint was added to the Ghidra tooling improvement plan. Before implementing any future phase, these questions must be answered:

1. Does this preserve instruction-level information needed for matching?
2. Does this hide information that could be crucial for writing matching C++?
3. Would annotation (adding context) be better than transformation (changing output)?
4. Is this actually useful, or just "nice to have"?

## Key Lessons

- **Annotation beats transformation for decomp work.** In decompilation, the binary is the source of truth. Ghidra output exists to show what the compiler did, not just what the code means. Hiding instruction-level patterns removes information that could be crucial for matching assembly. Adding comments preserves everything while making the code more readable.
- **Question features before committing.** The initial implementation was clean, tested, and working -- but it was the wrong approach. The pivot happened because the fundamental question "does this help the actual workflow?" was asked after implementation rather than before.
- **Separate concerns in API surfaces.** The `direct_client.py` bug arose because `decompile_function()` (takes a Function object) and `decompile_function_by_name_or_addr()` (takes a string) have similar names but different contracts. The caller used the wrong one and got a confusing error at runtime.

## Results

- `annotate_ppc_decompilation()` function deployed with 13 passing unit tests
- Bug fix in `direct_client.py` for string-vs-Function-object mismatch
- Pre-implementation checkpoint added to the multi-phase plan
- All 55 pyghidra-mcp unit tests passing
