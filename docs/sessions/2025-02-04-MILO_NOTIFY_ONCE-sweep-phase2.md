# MILO_NOTIFY_ONCE Pattern Sweep - Phase 2

**Date**: 2025-02-04
**Goal**: Identify additional functions that need MILO_NOTIFY -> MILO_NOTIFY_ONCE conversion
**Result**: No additional candidates found beyond Phase 1 fix

## Background

Phase 1 successfully fixed `NavListSortMgr::DataIndex` (+20.2% improvement) by converting MILO_NOTIFY to MILO_NOTIFY_ONCE. Phase 2 aimed to systematically find other candidates using database queries and function analysis.

## Key Discovery: merged_AddToStrings Signature

The definitive way to identify MILO_NOTIFY_ONCE candidates is to look for `merged_AddToStrings` in the analyze function output. This pattern appears when:

1. The target binary has a static guard check (`if ((DAT_xxx & 1) == 0)`)
2. Sets the guard bit and initializes a static `std::list<String>`
3. Calls `atexit()` to register the destructor
4. Calls `AddToStrings()` to check if the message was already shown
5. If AddToStrings returns true, calls `Debug::Notify()`

### Example from NavListSortMgr::DataIndex (98.6% match):
```
### Patterns Detected
- **LINKER_MERGED**: 1 instruction(s) - unfixable
  - `merged_AddToStrings`: 1 call(s)
```

### Example from BinStream::Read (95.4% AT_LIMIT):
```
### Patterns Detected
- **LINKER_MERGED**: 2 instruction(s) - unfixable
  - `merged_823314D8`: 1 call(s)
  - `merged_AddToStrings`: 1 call(s)
```

## Functions Analyzed

### Functions With merged_AddToStrings (Already Correct)

| Function | Match% | Status | Notes |
|----------|--------|--------|-------|
| NavListSortMgr::DataIndex | 98.6% | Fixed in Phase 1 | +20.2% improvement |
| BinStream::Read | 95.4% | AT_LIMIT | Already uses MILO_NOTIFY_ONCE |

### Functions Without merged_AddToStrings (Source is Correct)

These functions have MILO_NOTIFY in source and other merged patterns in target, confirming the source is correct:

| Function | Match% | Merged Patterns |
|----------|--------|-----------------|
| SaveLoadManager::SetState | 70.7% | merged_82E07DA0, merged_824D1870 |
| MetaPerformer::OnRecallMovePassed | 81.0% | merged_82840198, merged_824D1870 |
| LockedContentPanel::SetUpDifficultyLocked | 66.8% | merged_824D1870 |
| AccomplishmentManager::ConfigureAccomplishmentData | 66.0% | merged_82919EE0, merged_823314D8, merged_824D1870 |
| Leaderboards::GetScores | 80.2% | merged_82378970 |
| CharLipSync::PlayBack::Set | 85.9% | merged_SetObjConcrete, merged_823314D8 |
| Instarank::Str | 86.2% | merged_8288B880 |
| CampaignPerformer::AwardCrazeAccomplishments | 84.2% | merged_82919EE0, merged_824D1870 |
| CharClip::Load | 80.3% | merged_823B64C8, merged_823314D8, merged_824D1870 |
| Character::SetFocusInterest | 74.1% | No merged patterns |
| FileMerger::FilterSubdir | 89.0% | No merged patterns |
| ObjectDir::ResetViewports | 82.4% | No merged patterns |
| ObjectDir::PostLoadInlined | 81.7% | merged_8234A858, merged_824D1870 |

## Common Merged Symbols Identified

| Address | Symbol(s) | Description |
|---------|-----------|-------------|
| merged_824D1870 | MakeString variants | Assert string formatting |
| merged_823314D8 | MakeString<PAD>, MakeString<PBD> | String formatting |
| merged_82919EE0 | GetName (Accomplishment, Award, etc.) | Name accessors |
| merged_82E07DA0 | GetLastResult, Tell, etc. | Simple getters |
| merged_SetObjConcrete | ObjPtr::Set variants | Object pointer operations |
| merged_DataArrayNode | DataArray::Node | Data array access |

## Methodology

1. **Database Query**: Searched for functions with `has_linker_merged = 1` and 70-98% match
2. **Source Analysis**: Identified files using MILO_NOTIFY but not MILO_NOTIFY_ONCE
3. **Function Analysis**: Used `mcp__orchestrator__run_analyze_function` to check for `merged_AddToStrings`
4. **Merged Symbol Lookup**: Used `mcp__orchestrator__lookup_merged_symbol` to identify what each merged address represents

## Conclusions

1. **MILO_NOTIFY_ONCE is rare**: The pattern is used sparingly in the codebase
2. **Phase 1 was comprehensive**: The NavListSortMgr::DataIndex fix appears to have been the primary candidate
3. **Database flag limitation**: The `has_linker_merged` flag captures ALL merged calls, not specifically AddToStrings

## Recommendations for Future Work

1. **Pattern recognition**: When analyzing functions, specifically look for `merged_AddToStrings` in patterns
2. **Database enhancement**: Consider adding a specific `has_addtostrings` flag for faster candidate identification
3. **Focus areas**: Shift decomp efforts to other patterns:
   - REGISTER_SWAP (variable reordering)
   - OFFSET_SWAP (field access order)
   - CONTROL_FLOW (branch restructuring)
   - BOOL_MASK (bool handling quirks)

## Files Verified as Already Correct

From Phase 1 documentation:
- TexBlender.cpp - Already uses MILO_NOTIFY_ONCE
- CharDriver.cpp - Uses plain MILO_NOTIFY correctly (target also uses plain)
- CharBonesSamples.cpp - Already uses MILO_NOTIFY_ONCE
- Trig.cpp IsNaN - 100% match, correct pattern
