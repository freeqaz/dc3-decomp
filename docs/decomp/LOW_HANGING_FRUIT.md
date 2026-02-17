# Low-Hanging Fruit - Easy Function Targets

Prioritized list of functions that are good candidates for quick wins. Updated after each session.

**Last Updated:** 2026-02-17

---

## Selection Criteria

Functions are prioritized based on:
1. **Small size** (< 100 bytes) - Easier to match exactly
2. **Simple logic** - Getters, setters, constructors
3. **Already implemented** - Just needs tweaking
4. **Has RB3 reference** - Can compare implementations

---

## Tier 1: Tiny Functions (< 50 bytes)

These are the easiest wins - very small, simple functions.

| Function | Size | File | Status | Notes |
|----------|------|------|--------|-------|
| `SongDB::PostLoad(DataEventList*)` | 4 bytes | SongDB.cpp | **100%** | Trivial wrapper |
| `Shuttle::SetActive(bool)` | 8 bytes | Shuttle.cpp | **100%** | Simple setter |
| `LiveInput::GetSongToTaskMgrMs()` | 16 bytes | LiveInput.cpp | **100%** | Getter |
| `PresenceMgr::SetNotInGame()` | 32 bytes | PresenceMgr.cpp | **100%** | Sets 2 fields |
| `PresenceMgr::SetInGame(int)` | 32 bytes | PresenceMgr.cpp | **100%** | Sets 2 fields |
| `Shuttle::Shuttle()` | 32 bytes | Shuttle.cpp | **100%** | Constructor |

---

## Tier 2: Small Functions (50-100 bytes)

Slightly larger but still straightforward.

| Function | Size | File | Status | Notes |
|----------|------|------|--------|-------|
| `HamUser::GetPadNum()` | 40 bytes | HamUser.cpp | **100%** | Already implemented |
| `HamUser::CanSaveData()` | 68 bytes | HamUser.cpp | **100%** | Already implemented |
| `PresenceMgr::OnPlayerPresentChange()` | 72 bytes | PresenceMgr.cpp | **100%** | Already implemented |
| `HamUserMgr::GetRemoteUser()` | 80 bytes | HamUserMgr.cpp | **100%** | MILO_FAIL + return |
| `Trig::FastSin()` | 100 bytes | Trig.cpp | 0% | **NEXT TARGET** - only 1 missing from 90% file |

---

## Tier 3: system/math Opportunities

Based on research, these are the best math opportunities:

| File | Match % | Missing | Priority | Notes |
|------|---------|---------|----------|-------|
| Trig.cpp | 90% | 1 func | **HIGH** | FastSin only |
| Decibels.cpp | 50% | 1 func | **HIGH** | Quick win |
| Key.cpp | 40% | 3 funcs | MEDIUM | InterpVector overloads, RB3 has reference |
| Interp.cpp | 42.9% | 4 funcs | MEDIUM | Interpolation functions, RB3 reference |
| Rand.cpp | 72.7% | 3 funcs | LOW | Random number functions |
| SHA1.cpp | 50% | 5 funcs | LOW | Hash functions |
| Rot.cpp | 64.5% | 11 funcs | LOW | Quaternion operations |

**Quick wins to prioritize:**
1. `Trig::FastSin()` - 100 bytes, completes Trig.cpp
2. `Decibels` missing function - completes Decibels.cpp
3. `Key::InterpVector` overload - 84 bytes, RB3 reference at line 113

---

## Completed Functions

Functions that have been matched to 100%.

| Function | Size | File | Date | Notes |
|----------|------|------|------|-------|
| `GameModeInit` | 176b | GameMode.cpp | Jan 2025 | Uncommented callback |
| `FillModeArrayWithParentData` | 324b | GameMode.cpp | Jan 2025 | while→for + local variable |
| `Shuttle::Shuttle()` | 32b | Shuttle.cpp | Jan 2026 | `0` instead of `0.0f`/`false` |
| `Shuttle::SetActive()` | 8b | Shuttle.cpp | Jan 2026 | Already correct |
| `Shuttle::Poll()` | 140b | Shuttle.cpp | Jan 2026 | Already correct |
| `PresenceMgr::SetInGame()` | 32b | PresenceMgr.cpp | Jan 2026 | Already correct |
| `PresenceMgr::SetNotInGame()` | 32b | PresenceMgr.cpp | Jan 2026 | Already correct |
| `PresenceMgr::OnPlayerPresentChange()` | 72b | PresenceMgr.cpp | Jan 2026 | Already correct |
| `RndWind::GetWind(float)` | ~80b | Wind.cpp | Feb 2026 | Linear interp, `Mod(x,1.0f)*1024` indexing |
| `RndWind::GetWhiteNoise(float)` | ~80b | Wind.cpp | Feb 2026 | Linear interp, `Mod(x,1023.0f)` indexing |
| `StartDecompressionThread` | ~100b | ChunkStream.cpp | Feb 2026 | Inverted control flow fix |

---

## Nearly Matching (> 95%)

Close but need final tweaks. Many were improved this session.

| Function | Match | Size | File | Status |
|----------|-------|------|------|--------|
| `GamePanel::ReloadData` | 99.87% | 628b | GamePanel.cpp | Compiler codegen limit |
| `PartyModeMgr::PartyModeMgr` | 99.81% | 2060b | PartyModeMgr.cpp | Static Symbol order |
| `LoadingPanel::GetLoadingScreen` | 99.8% | 208b | LoadingPanel.cpp | Near-perfect |
| `NavListSortMgr::NavListSortMgr` | 99.9% | 312b | NavListSortMgr.cpp | Prologue artifact |
| `SetMode` | 99.6% | 2312b | GameMode.cpp | Needs objdiff GUI |
| `GetPresenceMode` | 99.49% | 856b | PresenceMgr.cpp | Needs objdiff GUI |

---

## Improved This Session (Feb 2026)

Functions implemented from stub (0%) or significantly improved:

| Function | Before | After | Fix Applied |
|----------|--------|-------|-------------|
| `SetWind` | 0.8% | 91% | Midpoint displacement with `sqrtf(2.0f)` decay |
| `RndWind::Init` | 0% | 82% | REGISTER_OBJ_FACTORY + Rand alloc + field init |
| `RndWind::GetWind(float)` | 0% | 98.8% | Completed stub |
| `RndWind::GetWhiteNoise(float)` | 0% | 99.1% | Completed stub |
| `RndWind::SelfGetWind` | 0% | 71.4% | Wind vector + transform + speed clamp |
| `StartDecompressionThread` | 40% | 98% | Inverted if/else control flow |

---

## Classes to Focus On

### system/math (Highest ROI)
- **Trig.cpp** - 90% complete, 1 function to go
- **Decibels.cpp** - 50% complete, 1 function to go
- **Key.cpp** - 40% complete, 3 functions, has RB3 reference
- **Interp.cpp** - 43% complete, 4 functions, has RB3 reference

### lazer/game (Core Gameplay)
- **Shuttle** - **100% COMPLETE**
- **PresenceMgr** - 99%+ on all functions
- **GameMode** - 2 functions at 99%+, need objdiff

### lazer/meta_ham (UI/Meta)
- **AccomplishmentManager** - Several improved
- **CampaignProgress** - Several improved
- **MetaPanel** - Improved

---

## How to Work on These

### 1. Pick a function from Tier 1-2 or system/math

```bash
# Read the source
cat src/system/math/Trig.cpp

# Check the target assembly
cat build/373307D9/asm/system/math/Trig.s
```

### 2. Build and check

```bash
ninja build/373307D9/src/system/math/Trig.obj
ninja build/373307D9/report.json
```

### 3. Check match percentage

```bash
cat build/373307D9/report.json | python3 -c "
import json, sys
data = json.load(sys.stdin)
for unit in data.get('units', []):
    if 'Trig' in unit.get('name', ''):
        for fn in unit.get('functions', []):
            pct = fn.get('fuzzy_match_percent', 0)
            name = fn.get('name', '')[:60]
            print(f'{pct:5.1f}% - {name}')
"
```

### 4. Compare with RB3 if needed

```bash
grep -rn "FastSin" ~/code/milohax/rb3/src/
```

---

## See Also

- [RB3_REFERENCE.md](RB3_REFERENCE.md) - Using RB3 as reference
- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns
- [SUBAGENT_STRATEGY.md](SUBAGENT_STRATEGY.md) - Parallel agent workflow
- [../WORKSESSION.md](../WORKSESSION.md) - Main session notes
