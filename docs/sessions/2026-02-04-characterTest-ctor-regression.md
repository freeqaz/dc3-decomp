# Session: CharacterTest Ctor Regression (Zero-Float Init)

**Date**: 2026-02-04  
**Function**: `CharacterTest::CharacterTest(Character *)`  
**Symbol**: `??0CharacterTest@@QAA@PAVCharacter@@@Z`  
**Result**: 99.1% (size matched; remaining diffs are relocations)

## Problem

Regression in `CharacterTest::CharacterTest` dropped from 100% to ~94.5%. Objdiff MCP showed an **extra float zero store** (`lfs/stfs` to `0x90`) in the base that was missing in our build, along with function-local static `Symbol none` guard relocation differences.

## Investigation (Objdiff MCP)

- The base includes a `lis/lfs/stfs` sequence writing `0.0f` to offset `0x90`.
- Any explicit `unk90` initialization in the constructor (`unk90(0)`, `unk90 = 0.0f`, or forced volatile stores) **introduced** the float store and **changed code size**, worsening the match.
- Removing the explicit init removed the float store and restored size parity.

## Fix Applied

**Action:** Removed `unk90(0)` from the constructor initializer list.

`src/system/char/CharacterTest.cpp`
- Constructor now leaves `unk90` uninitialized in the ctor.

## Current Status

- Match **99.1%**, target and base size **536 bytes**.
- Remaining diffs are **relocation symbol name mismatches** for the function-local static `Symbol none` guard/storage (e.g., `lbl_82F5F1DC/E0` vs `?none@?1...`). These appear to be compiler/static-guard layout decisions.

## Takeaway

**Do not add constructor zero-inits unless confirmed in the target.**  
Even “harmless” zeroing can introduce `lfs/stfs` sequences and drop matches.

