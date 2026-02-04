# Parallel Decomp Agents Session - 2026-01-23

## Overview

Ran 10 parallel Opus agents to work on high-impact decompilation targets. Used custom objdiff-cli and m2c decompiler workflow.

## Tools Used

- **objdiff-cli**: `~/code/milohax/objdiff/target/release/objdiff-cli` (custom build with `--verdict`, `--analyze`)
- **m2c workflow**: `tools/asm_to_m2c.py` → `m2c.py -t ppc`

## Progress Summary

| Category | Before | After | Change |
|----------|--------|-------|--------|
| Overall | 30.79% | 30.82% | +0.03% |
| Game Code | 62.17% | 62.28% | +0.11% |
| Milo Engine | 53.98% | 54.01% | +0.03% |

## Agent Results

### Completed (6/10)

| Function | Unit | Size | Before | After | Notes |
|----------|------|------|--------|-------|-------|
| HamStorePanel::Handle | lazer/meta_ham | 3,348 | 78.7% | **97.6%** | +19% - Added 6 missing handlers |
| RndFont::Load | system/rndobj | 2,748 | 10.4% | **78.2%** | +68% - Full version-branched Load |
| MetagameRank::UpdateScore | lazer/meta_ham | 8,596 | 0% | **48.9%** | New impl - 63 static symbols, debug paths |
| CSHA1::Transform | system/math | 5,856 | 54.3% | 55.0% | +0.7% - Register alloc issues |
| RhythmBattle::OnBeat | system/hamobj | 16,508 | 0% | 17.4% | AtLimit - 15 merged calls, massive function |
| BustAMovePanel::OnBeat | lazer/game | 12,056 | 0% | 11.4% | Partial - state machine needs more work |

### Still Running (4/10)

| Function | Unit | Size | Before |
|----------|------|------|--------|
| NavListSortMgr::Handle | lazer/meta_ham | 4,796 | 80.3% |
| CamShot::Load | system/world | 3,296 | 81.2% |
| ChallengeResultPanel::UpdateList | lazer/meta_ham | 2,456 | 43.2% |
| SaveLoadManager::SetState | lazer/meta_ham | 5,152 | 0% |

## Key Findings

### Best Improvements
1. **RndFont::Load** (+68%): Complex version-branched Load function with MatChar support
2. **HamStorePanel::Handle** (+19% to 97.6%): Near-perfect match after adding missing handlers

### AtLimit Functions
These hit compiler/linker limits and can't improve further:
- **RhythmBattle::OnBeat**: 15 LINKER_MERGED calls, 7 BOOL_MASK patterns
- **CSHA1::Transform**: 1736 REGISTER_SWAP patterns due to compiler scheduling

### Partially Implemented (Need More Work)
- **BustAMovePanel::OnBeat**: State machine cases mostly empty, framework in place
- **MetagameRank::UpdateScore**: Debug paths done, normal scoring logic remains

## Lessons Learned

1. **Quick wins** (80%+ functions) often just need missing handlers added
2. **Large functions** (>10KB) often hit AtLimit due to linker-merged calls
3. **m2c** helps understand structure but MSVC symbols need cleanup
4. **Parallel agents** effective for batch progress but create merge conflicts

## Files Modified

- `src/lazer/meta_ham/HamStorePanel.cpp`
- `src/lazer/meta_ham/HamStoreProvider.h`
- `src/lazer/meta_ham/MetagameRank.cpp`
- `src/lazer/meta_ham/MetagameRank.h`
- `src/system/rndobj/Font.cpp`
- `src/system/math/SHA1.cpp`
- `src/system/hamobj/RhythmBattle.cpp`
- `src/system/hamobj/RhythmBattle.h`
- `src/lazer/game/BustAMovePanel.cpp`
- `src/lazer/game/BustAMovePanel.h`

## Commands Reference

```bash
# Find largest unimplemented functions
~/code/milohax/objdiff/target/release/objdiff-cli report query build/373307D9/report.json \
  --functions --unimplemented --sort-by size --sort-order desc --limit 20

# Analyze a function with verdict
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "FunctionName" \
  -f markdown --verdict --build

# m2c decompile workflow
python3 tools/asm_to_m2c.py build/373307D9/asm/path/to/file.s -f FuncName | \
  python3 ~/code/milohax/m2c/m2c.py -t ppc -
```
