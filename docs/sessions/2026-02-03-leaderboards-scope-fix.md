# Session: Leaderboards::ReadScoresComplete Scope Fix

**Date**: 2026-02-03
**Function**: `Leaderboards::ReadScoresComplete(bool, bool)`
**Symbol**: `?ReadScoresComplete@Leaderboards@@QAAX_N0@Z`
**Result**: 96.5% (at_limit) -- scope numbering fixed, only ICF merged call remaining

## Summary

Fixed `?7?` vs `?6?` scope mismatch on static local Messages by removing braces from an `if` statement. All 39 non-equal instructions (33 diff_arg + 6 replace) were caused by either the scope mismatch or cosmetic stripped-vs-unstripped label differences. The sole functional mismatch is 1 ICF merged `GetRows` call.

## The Problem

The three `static Message` variables in `ReadScoresComplete` had scope `?7?` in our build but `?6?` in the target binary. This caused every relocation reference to these statics (and their guard variable and atexit destructors) to mismatch.

## Root Cause: Braced vs Braceless `if`

MSVC counts `{}` blocks as scopes sequentially within a function. An `if (cond) { stmt; }` creates one more scope than `if (cond) stmt;` because the braces introduce an explicit block scope.

```cpp
// WRONG - scope ?7? (one extra scope from braces)
if (b2) {
    unk64.insert(std::make_pair(job->unkb0, unk58));
}

// CORRECT - scope ?6? (no extra scope)
if (b2)
    unk64.insert(std::make_pair(job->unkb0, unk58));
```

## How MSVC `?N?` Scope Numbering Works

The `?N?` in mangled static local names (e.g., `?leaderboardsLoadedMsg@?6??ReadScoresComplete@...`) is a **sequential scope counter** within the function. Every `{}` block opening increments it, regardless of nesting depth. The counter does NOT reset when leaving a scope -- it only increases.

### Verification Method

Empirically tested by removing code blocks and observing scope changes:

| Code removed | Resulting scope | Delta |
|---|---|---|
| Nothing (baseline) | `?7?` | -- |
| Removed `unk58.clear()` | `?7?` | 0 (clear's inlined erase `if` doesn't count) |
| Removed entire `if (b2) { insert(...) }` | `?4?` | -3 |
| Removed braces from `if (b2)` only | `?6?` | -1 |

The `if (b2) { insert(make_pair(...)) }` block contributes 3 scopes:
1. The `if (b2) {}` braces (+1 scope)
2. Two scopes from `make_pair` template expansion and temporaries (+2 scopes)

Removing only the braces eliminates exactly 1 scope, matching the target.

### Cross-validation with other functions

| Function | Scope | Static location | Notes |
|---|---|---|---|
| `PostProcScores` | `?1?` | Function body level | Matches -- minimal code before static |
| `UploadNextScore` | `?1?` | Function body level | Matches -- minimal code before static |
| `Poll` | `?7?` | Inside 2 nested ifs + `Find<T>` template | Matches -- `Find<UIPanel>` inlines ~4 scopes |

## Remaining Diff

After the fix, 96.5% match with:
- **1 ICF merged call** (index 23): `bl merged_82996288` vs `bl GetRows@GetLeaderboardByPlayerJob` -- unfixable
- **33 diff_arg**: All cosmetic -- stripped `lbl_XXXXXXXX` labels in target vs full mangled symbol names in our object file
- **6 replace**: All cosmetic -- extra relocation annotations on `mr`/`lwz` instructions in our build

## Key Takeaway

When static locals have wrong `?N?` scope numbers, count the `{}` blocks before the declaration point. Braceless `if`/`for`/`while` statements do NOT increment the scope counter. This is a quick fix: just remove or add braces to adjust the count by 1.

## Files Modified

- `src/lazer/meta_ham/Leaderboards.cpp` -- removed braces from `if (b2)` block
