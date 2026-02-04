# Session: NavListSortMgr::HeadersSelectable ICF Analysis
**Date:** 2026-01-24

## Overview
Investigated and fixed `NavListSortMgr::HeadersSelectable()` based on reviewer feedback that the function should return `true`, not `false`. Used symbol map analysis to confirm the correct implementation.

## Problem
Reviewer comment on PR #154:
> `virtual bool HeadersSelectable() { return false; }` - can you tell the bot this one returns true
> `0x82e2ab00: public: virtual bool __cdecl NavListSortMgr::HeadersSelectable(void)` - that one you can tell because of the map, the address it's mapped to returns 1

## Analysis

### Identical Code Folding (ICF)
Address `0x82e2ab00` in the original binary has **150+ functions** mapped to it. This is Identical Code Folding - the linker merges functions with identical machine code.

A simple `return true` in PowerPC is just:
```asm
li r3, 1    ; load immediate 1 into r3 (return value)
blr         ; branch to link register (return)
```

### Evidence from Symbol Map
Grepping `docs/dc_symbols.txt` for `0x82e2ab00` shows many functions at this address:
- `CustomPlaylist::IsCustom(void)` - returns bool
- `UIButton::CanHaveFocus(void)` - returns bool
- `NavListNode::LocalizeToken(void) const` - returns bool
- `NavListSortMgr::HeadersSelectable(void)` - returns bool
- `RndPollable::PollEnabled(void) const` - returns bool
- ... and ~145 more

All these are simple `return true` implementations that got folded together.

### Class Hierarchy
```
NavListSortMgr (base)
    └── SongSortMgr (derived, overrides HeadersSelectable)
```

`SongSortMgr::HeadersSelectable()` at `0x829600A0` (size 0x10C) has complex logic checking game mode properties. The base class `NavListSortMgr::HeadersSelectable()` just returns `true`.

## Fix Applied

Changed in `src/lazer/meta_ham/NavListSortMgr.h`:
```cpp
// Before
virtual bool HeadersSelectable() { return false; } // 0x6c

// After
virtual bool HeadersSelectable() { return true; } // 0x6c
```

Also restored other virtual function declarations at correct vtable offsets and renamed the member accessor from `HeadersSelectable()` to `GetHeadersSelectable()` to avoid conflict.

## Commit
```
a88e869 Fix NavListSortMgr::HeadersSelectable to return true
```

Pushed to `freeqaz/wip-lazer-meta-ham` branch (PR #154).

## Key Learnings

### Using Symbol Maps for ICF Analysis
When many functions share the same address in the symbol map, they have identical implementations. Common patterns:
- `return true` / `return false` / `return 0` / `return 1`
- Empty functions `{}`
- Simple getters returning the same offset

### Verification Method
1. Look up the function's address in `docs/dc_symbols.txt`
2. If many functions share that address, they're ICF'd
3. Examine what the other functions do to infer the implementation
4. For bool returns: if all other functions at that address return true, yours does too

## Files Modified
- `src/lazer/meta_ham/NavListSortMgr.h`

## Tools Used
- `grep` on `docs/dc_symbols.txt` for address lookup
- `./tools/analyze_function.py` (function not in tracked symbols)
- `./bin/objdiff-cli` (function not in tracked symbols)
- Git worktree at `/tmp/claude/wip-lazer-meta-ham`
