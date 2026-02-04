# ValidateOffers Deep Dive Session

**Date**: 2026-01-27
**Function**: `StorePanel::ValidateOffers`
**Initial Match**: 35.2%
**Final Match**: 55.3%
**Status**: AT_LIMIT (static initialization pattern mismatch)

---

## Executive Summary

This session attempted to improve `ValidateOffers` from 35% to a higher match. Through systematic analysis of the instruction diff, we identified several code structure differences and fixed many of them. However, we hit a fundamental limitation: the **static variable initialization pattern** generates fundamentally different code between lazy `init_flags` and `static Symbol("...")` approaches.

---

## What Worked

### 1. Adding `song_offers` Vector
The original code collects song-type offers into a separate vector during the first loop, then iterates that vector in the second loop.

```cpp
// First loop
if (offer_type == song_sym) {
    song_offers.push_back(offer);
}

// Second loop
for (nit = song_offers.begin(); nit != song_offers.end(); ++nit) {
    StoreOffer *song_offer = *nit;
    // ...
}
```

**Evidence**: Diff showed `deallocate@StlNodeAlloc@PAVStoreOffer@@` in TARGET, confirming a `vector<StoreOffer*>` exists.

### 2. Using `std::find` for Duplicate Check
Replaced manual iterator loop with `std::find`:

```cpp
// Before (manual loop - generates inline comparisons)
for (sit = song_names.begin(); sit != song_names.end(); ++sit) {
    if (*sit == short_name) break;
}

// After (std::find - generates merged function call)
std::vector<Symbol>::iterator sit =
    std::find(song_names.begin(), song_names.end(), short_name);
```

**Evidence**: TARGET had `bl merged_826121C0` which is the linker-merged std::find.

### 3. Lazy `Sym(0)` Call in Second Loop
Moved the `song_name = Sym(0)` call inside the `if (count > 1)` block:

```cpp
// Before (always called)
Symbol song_name = song_offer->StoreOfferData()->Sym(0);
// ... counting loop ...
if (count > 1) {
    TheDebug.Notify(...song_name...);
}

// After (only when needed)
// ... counting loop ...
if (count > 1) {
    Symbol song_name = song_offer->StoreOfferData()->Sym(0);
    TheDebug.Notify(...song_name...);
}
```

**Evidence**: TARGET had `Sym@DataArray` call at indices 191-195 (inside error path), not earlier.

### 4. Preserving Buggy `HasSong(offer_ptr)` Pattern
The original code has what appears to be a bug - it passes the same pointer to HasSong:

```cpp
if (offer_ptr->OfferType() == cur_type && offer_ptr->HasSong(offer_ptr)) {
```

This checks if an offer "has song" with itself, which is logically strange. But preserving this improved match slightly (55.2% → 55.3%).

---

## What Didn't Work

### 1. Lazy `init_flags` Pattern
Tried the explicit lazy initialization pattern:

```cpp
static Symbol song_sym;
static int init_flags = 0;

if (!(init_flags & 1)) {
    init_flags |= 1;
    song_sym = Symbol("song");
}
```

**Result**: Match dropped to 42.5%, size increased to 872 bytes. The compiler generates significantly different code structure with this pattern.

### 2. Changing Comparison Order (short_name vs offer_type)
Tried comparing `short_name != dummy_upsell_sym` instead of `offer_type != dummy_upsell_sym`.

**Result**: Match dropped from 51.6% to 50.7%. The original logic uses offer_type.

### 3. Aggregate Array Initialization
Tried `Symbol offer_types[2] = { album_sym, pack_sym };`

**Result**: Generated smaller code (744 bytes vs 756 target) - we became TOO small.

---

## Root Cause Analysis

### The Static Initialization Problem

The fundamental issue is how static local variables are initialized:

**TARGET (Original Binary)**:
```
li r17, 0x0           ; Initialize register to 0
mr r18, r17           ; Copy to other registers
stw r17, 0x88, r31    ; Store to stack
...
lwz r11, lbl_8316B58C ; Load init_flags
clrlwi. r10, r11, 31  ; Check bit 0
bne skip_init         ; Branch if already initialized
ori r11, r11, 0x1     ; Set bit 0
stw r11, lbl_8316B58C ; Store init_flags
; ... Symbol constructor call ...
```

**BASE (Our Code with `static Symbol("...")`)**:
```
lis r30, init_guard   ; Load guard variable address
lwz r11, init_guard   ; Load guard
clrlwi. r10, r11, 31  ; Check if initialized
bne skip_init
ori r11, r11, 0x1
stw r11, init_guard   ; Store guard
lis r11, "song"       ; Load string literal
bl Symbol::Symbol     ; Call constructor
; ... more symbols ...
```

The compiler generates guard variables and constructor calls in a different order/pattern than the original.

### Why Can't We Match It?

1. **Compiler behavior**: MSVC generates different code for `static Symbol("...")` vs explicit `init_flags` checks
2. **Symbol ordering**: The original may have been compiled with different optimization settings
3. **Register allocation**: Different variable declaration order affects register assignment
4. **Stack frame size**: TARGET uses 0x110, BASE uses 0x120 (16 bytes difference)

---

## Tooling Gaps Identified

### 1. No Ghidra Decompilation Available
The `analyze-function` tool failed to get Ghidra decompilation:
```
Error: Binary /default.xex not found. Available binaries: ['default.xex-997567']
```

**Impact**: We couldn't see the original C-like pseudocode, which would have shown the exact control flow and variable usage.

**Recommendation**: Fix the Ghidra binary name mismatch or add a fallback.

### 2. No m2c Decompiler Integration
The plan mentioned using `tools/decompile.sh` for m2c output, but this wasn't readily available.

**Impact**: We had to manually interpret raw assembly diffs.

**Recommendation**: Integrate m2c into the workflow:
```bash
./bin/objdiff-cli diff ... --m2c-output /tmp/func.c
```

### 3. Missing Static Init Pattern Detection
The analysis tools detected LINKER_MERGED and REGISTER_SWAP but didn't flag the static initialization pattern difference.

**Recommendation**: Add pattern detection for:
- `STATIC_INIT_MISMATCH`: When init guard patterns differ
- `SYMBOL_CONSTRUCTOR_ORDER`: When Symbol() calls appear in different order

### 4. No "What Would Match" Suggestions
The tooling tells us what's wrong but not what code changes might fix it.

**Recommendation**: Add heuristic suggestions like:
```
Pattern: STATIC_INIT with lazy guards
Suggestion: Try explicit init_flags pattern:
  static Symbol foo;
  static int init_flags = 0;
  if (!(init_flags & 1)) { init_flags |= 1; foo = Symbol("..."); }
```

### 5. No Incremental Diff Comparison
When we made changes, we had to mentally track what improved.

**Recommendation**: Add `--compare-to-previous` flag:
```bash
./bin/objdiff-cli diff ... --compare-to-previous
# Output: Match improved 51.6% → 55.3% (+3.7%)
# Changed: -3 REGISTER_SWAP, +2 EQUAL
```

### 6. Missing Symbol Access Pattern Analysis
We had to manually identify that TARGET accesses `Sym(0)` differently than `OfferType()`.

**Recommendation**: Add function call analysis:
```
TARGET calls: DataArray::Sym(0) at indices 79, 87, 195
BASE calls: StoreOffer::OfferType() at indices 79, 108
Mismatch: Different member access patterns
```

---

## Suggested New Tools

### 1. `objdiff-cli pattern-suggest`
Analyzes diff and suggests code patterns to try:
```bash
./bin/objdiff-cli pattern-suggest "ValidateOffers"
# Output:
# 1. Static init pattern mismatch - try lazy init_flags
# 2. std::find might generate merged call - try std::find()
# 3. Variable declaration order affects registers - try reordering
```

### 2. `objdiff-cli history`
Shows match progression over time:
```bash
./bin/objdiff-cli history "ValidateOffers"
# 2026-01-27 09:00  35.2%  Initial
# 2026-01-27 09:15  51.6%  Added std::find, song_offers
# 2026-01-27 09:30  55.3%  Moved Sym(0) call
```

### 3. `ghidra-compare`
Side-by-side Ghidra decompilation vs our code:
```bash
./bin/ghidra-compare "ValidateOffers" src/system/meta/StorePanel.cpp:316
# Shows: Original pseudocode | Our code | Key differences
```

### 4. `init-pattern-analyzer`
Specifically for static initialization analysis:
```bash
./bin/init-pattern-analyzer "ValidateOffers"
# Detects: 4 static symbols with guard variable pattern
# Original: Uses init_flags with bits 1,2,4
# Our code: Uses per-symbol guards
# Suggestion: Convert to explicit init_flags
```

---

## Lessons Learned

1. **Start with the diff**: The instruction diff reveals exact differences - read it carefully before coding
2. **Size matters**: Watch both match% AND size - being too small is as bad as too big
3. **Preserve bugs**: Original buggy behavior (like `HasSong(offer_ptr)`) should be preserved for matching
4. **Static init is hard**: Different static initialization patterns generate very different code
5. **Merged functions are unfixable**: `merged_826121C0` can't be changed, we must use code that generates the same merge

---

## Follow-up Tasks

- [ ] Fix Ghidra binary name mismatch for decompilation
- [ ] Integrate m2c decompiler into workflow
- [ ] Add static init pattern detection to analyze-function
- [ ] Create pattern suggestion system
- [ ] Add match history tracking
- [ ] Document static Symbol initialization patterns and when to use each

---

## Appendix: Final Code

```cpp
void StorePanel::ValidateOffers(std::vector<StoreOffer *> &offers) {
    static Symbol song_sym("song");
    static Symbol dummy_upsell_sym("dummy_upsell_offer");
    static Symbol album_sym("album");
    static Symbol pack_sym("pack");

    std::vector<Symbol> song_names;
    std::vector<StoreOffer *> song_offers;

    std::vector<StoreOffer *>::iterator it;
    for (it = offers.begin(); it != offers.end(); ++it) {
        StoreOffer *offer = *it;
        Symbol offer_type = offer->OfferType();

        if (offer_type != dummy_upsell_sym) {
            Symbol short_name = offer->StoreOfferData()->Sym(0);

            std::vector<Symbol>::iterator sit =
                std::find(song_names.begin(), song_names.end(), short_name);

            if (sit != song_names.end()) {
                TheDebug.Notify(MakeString("Duplicate offer short name: %s", short_name));
            } else {
                song_names.push_back(short_name);
            }

            if (offer_type == song_sym) {
                song_offers.push_back(offer);
            }
        }
    }

    Symbol offer_types[2];
    offer_types[0] = album_sym;
    offer_types[1] = pack_sym;

    for (int i = 0; i < 2; i++) {
        Symbol cur_type = offer_types[i];
        std::vector<StoreOffer *>::iterator nit;
        for (nit = song_offers.begin(); nit != song_offers.end(); ++nit) {
            StoreOffer *song_offer = *nit;
            int count = 0;
            std::vector<StoreOffer *>::iterator oit;
            for (oit = offers.begin(); oit != offers.end(); ++oit) {
                StoreOffer *offer_ptr = *oit;
                if (offer_ptr->OfferType() == cur_type && offer_ptr->HasSong(offer_ptr)) {
                    count++;
                }
            }
            if (count > 1) {
                Symbol song_name = song_offer->StoreOfferData()->Sym(0);
                TheDebug.Notify(MakeString("Song %s is in more than one %s", song_name, cur_type));
            }
        }
    }
}
```
