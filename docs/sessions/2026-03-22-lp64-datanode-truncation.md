# LP64 DataNode Symbol Truncation — DTA Handler Dispatch Broken on 64-bit

**Date**: 2026-03-22
**Status**: Systemic audit complete, all identified truncation sites fixed
**Files changed**: `Data.h`, `DataArray.cpp`, `DataNode.cpp`, `DataFunc.cpp`, `DataUtl.cpp`, `DirLoader.cpp`, `HamNavList.cpp`

## Problem

The native port's DTA handler system was silently broken on 64-bit. Pressing confirm on `song_select_screen` selected a tier header (`song_tier_0`) but the DTA `NAV_SELECT_MSG` handler never executed — no `{print}` output, no `{switch}` dispatch, no screen transition.

## Root Cause: Symbol→int Pointer Truncation

`Symbol::operator int()` in `Symbol.h`:
```cpp
#ifdef HX_NATIVE
    operator int() { return (int)(intptr_t)mStr; }  // truncates 8-byte ptr to 4 bytes!
#else
    operator int() { return (int)mStr; }             // 32-bit: no truncation
#endif
```

On Xbox 360 (ILP32), `const char*` and `int` are both 4 bytes — the cast is lossless. On Linux x86_64 (LP64), `const char*` is 8 bytes but `int` is 4 bytes — the upper 32 bits are lost.

The `DataNode` union has pointer members (8 bytes on LP64) overlapping with `int integer` (4 bytes). Any code path reading `mValue.integer` from a pointer-typed node silently truncates, and two different interned string pointers sharing the same lower 32 bits collide (~1% probability at 10K interned strings via birthday bound).

## Systemic Audit

### The DataNode union on LP64

```cpp
union {
    const char *symbol;   // 8 bytes on LP64
    int integer;          // 4 bytes on LP64  ← only covers lower half
    float real;           // 4 bytes
    DataArray *array;     // 8 bytes on LP64
    Hmx::Object *object;  // 8 bytes on LP64
    DataNode *var;        // 8 bytes on LP64
    DataFunc *func;       // 8 bytes on LP64
} mValue;
```

Every code path that calls `UncheckedInt()` on a node containing a pointer-typed value (Symbol, Object, Array, etc.) is vulnerable. The audit found **8 affected sites** across 6 files.

### Why not the comprehensive `intptr_t` fix?

Making `mValue.integer` be `intptr_t` would fix all truncation at once, but:
- Changes `UncheckedInt()` return type from `int` to `intptr_t`, requiring cascading changes at every caller
- `FindArray(int)` signature change propagates to dozens of call sites
- Binary DTA serialization (`DataNode::Save/Load`) writes 4-byte ints — needs size translation layer
- Risk of subtle behavior changes in arithmetic on DataNode int values

The surgical `#ifdef HX_NATIVE` approach is safer: each fix is isolated, PPC decomp is untouched, and we can enumerate all affected sites.

### Save/Load — NOT affected

`DataNode::Save` dispatches on `mType` and uses type-appropriate accessors (`mValue.symbol` for kDataSymbol, `mValue.real` for kDataFloat, etc.) — never reads `mValue.integer` for pointer types. `DataNode::Load` similarly writes through the correct union member. Serialization is clean.

### operator>() — NOT affected

Only compares kDataInt/kDataFloat via `LiteralFloat()`, never touches pointer-typed nodes.

### operator!=() — already fixed

Routes through `Equal()` which has the kDataSymbol guard.

## All Fixes

### Phase 1 — Already applied (prior session)

#### `Data.h` — DataNode constructors zero upper bytes
Int/long constructors already had `mValue.object = nullptr` before writing `mValue.integer`. This ensures the upper 4 bytes of the 8-byte union are zero on LP64, so `UncheckedStr()` (pointer compare) returns a consistent value for int-typed nodes.

#### `DataArray.cpp` — FindArray(Symbol)
Replaced `FindArray((int)tag, false)` with direct Symbol comparison via `arr->Node(0).LiteralSym() == tag` on LP64.

#### `DataNode.cpp` — Equal()
Added `kDataSymbol` branch comparing full pointers via `UncheckedStr()` instead of truncated `UncheckedInt()`.

#### `HamNavList.cpp` — IsAnimating() bypasses
Two `#ifdef HX_NATIVE` bypasses for `!RndAnimatable::IsAnimating()` (ribbon animations never settle without Kinect).

### Phase 2 — New fixes from audit

#### `Data.h` — Float/double constructors zero upper bytes
```cpp
DataNode(float f) {
#ifdef HX_NATIVE
    mValue.object = nullptr; // zero all 8 bytes on LP64
#endif
    mValue.real = f;
    mType = kDataFloat;
}
```
Without this, two float nodes with the same value could have different garbage in the upper 4 bytes, breaking any raw pointer comparison (e.g. in Remove/Contains).

#### `DataArray.cpp` — Remove(const DataNode&) and Contains(const DataNode&)
Original code compared all nodes via `UncheckedInt()` — a 4-byte bitwise comparison that works on ILP32 where the union is 4 bytes. On LP64, pointer-typed nodes need full 8-byte comparison via `UncheckedStr()`:
```cpp
#ifdef HX_NATIVE
bool isPtr = (dn.Type() == kDataSymbol || dn.Type() == kDataObject
              || dn.Type() >= kDataArray);
// ...
bool match = isPtr ? (mNodes[i].UncheckedStr() == dn.UncheckedStr())
                   : (mNodes[i].UncheckedInt() == dn.UncheckedInt());
#endif
```

#### `DataFunc.cpp` — DataContains DTA function
Changed from `w->Contains(n.UncheckedInt())` (truncates then reconstructs DataNode(int)) to `w->Contains(n)` (passes DataNode directly, preserving type and full pointer).

#### `DataFunc.cpp` — DataFindExists / DataFind
These call `FindArray(n.UncheckedInt(), false)` which truncates kDataSymbol nodes. Fixed to route kDataSymbol through the Symbol overload:
```cpp
#ifdef HX_NATIVE
if (n.Type() == kDataSymbol) {
    arr = arr->FindArray(n.LiteralSym(), false);
} else {
    arr = arr->FindArray(n.UncheckedInt(), false);
}
#endif
```

#### `DataUtl.cpp` — DataMergeTags and DataReplaceTags
Both call `FindArray(arr->UncheckedInt(0), false)` to look up sub-arrays by their first element (typically a Symbol tag). Fixed to check `arr->Node(0).Type()` and route kDataSymbol through `FindArray(Symbol)`.

#### `DirLoader.cpp` — ClassAndNameSort::ClassIndex
Compared `(unsigned int)n.UncheckedInt() == name` where `name` is a Symbol. On LP64, both sides truncated to 32 bits. Fixed to compare Symbols directly:
```cpp
#ifdef HX_NATIVE
if (n.Type() == kDataSymbol && n.LiteralSym() == name) {
#else
if ((unsigned int)n.UncheckedInt() == name) {
#endif
```

## Debugging Trace (original session)

1. **Symptom**: Game reaches `song_select_screen` but pressing confirm doesn't transition.
2. **Initial hypothesis**: Message routing broken. Added traces to UIManager::Handle, UIScreen::Handle, SongSelectPanel::Handle.
3. **Discovery**: Message DOES reach the panel. `TypeDef()->FindArray("nav_select")` returns non-null. But `ExecuteScript` runs the wrong handler body.
4. **Key trace**: Found array's `[0] = Symbol('load')` instead of `Symbol('nav_select')` — FindArray matched the wrong sub-array.
5. **Root cause**: `FindArray(Symbol)` → `FindArray((int)tag)` truncates. `"load"` and `"nav_select"` collide in lower 32 bits.
6. **Scope expansion**: Audit found 7 additional truncation sites beyond the original 2.

## Verification

PPC decomp build: no regressions (same progress numbers before and after).
Native port build: compiles cleanly.

After phase 1 fixes:
- `FindArray("nav_select")` returns correct handler
- `ExecuteScript` binds `$name`, `$index`, `$component`, `$can_select` correctly
- Screen transitions work through `main_screen → choose_mode_screen → song_select_screen`

## Phase 3 — Explicit (int)Symbol casts eliminated

Three remaining call sites used `(int)Symbol` for comparison — all fixed:

#### `Locale.cpp` — FindDataIndex binary search
Binary search compared `(int)s > (int)mSymTable[mid]`. On LP64 this truncates both sides. The table is sorted at runtime by `Symbol::operator<` (pointer comparison), so the fix compares `s.Str() > mSymTable[mid].Str()` for full-width pointer ordering.

#### `ChooseModeProvider.cpp` — mode comparison
Changed `(int)dataSym == custom_party` → `dataSym == custom_party`. Both are Symbols; `Symbol::operator==` compares full pointers.

#### `PlaylistSongProvider.cpp` — song data check
Changed `(int)dataSym != playlist_addsong` → `dataSym != playlist_addsong`. Same pattern.

Note: The ChooseModeProvider/PlaylistSongProvider changes are unconditional (no `#ifdef`), but on PPC (ILP32) `(int)Symbol` and `Symbol::operator==` produce identical code (both 4-byte pointer comparisons). Zero regressions.

## Phase 4 — Compile-time hardening

#### `Symbol.h` — `[[deprecated]]` on `operator int()` for LP64
```cpp
#ifdef HX_NATIVE
    [[deprecated("Symbol→int truncates on LP64; use Symbol::operator== or Str()")]]
    operator int() { return (int)(intptr_t)mStr; }
#endif
```
Any future code that uses `(int)someSymbol` in native builds gets a compile-time deprecation warning. Current native build has zero warnings — all truncation sites are eliminated.

## Song List Expansion — RESOLVED

### Status: Working correctly. No bug — test script was confirming without navigating.

Full investigation traced the entire path and confirmed everything works:

1. **nav_select** → DTA handler on `song_select_panel` executes (dispatched via UIManager → mSink → HamScreen → UIScreen → FocusPanel)
2. **Header mode toggling** → `SongHeaderNode::OnSelect()` correctly toggles `IsInHeaderMode()`, `EnteringHeaderMode()`, `ExitingHeaderMode()`
3. **nav_select_done** → fires after animation settles, triggers `uncollapse_headers` DTA function
4. **BuildItemList()** → correctly rebuilds: 55 items (normal) ↔ 9 items (header mode)
5. **RealRefresh()** → fires on HamNavList, `numShowing` updates to match new item count

The original symptom ("list doesn't expand") was because the test script confirmed on the same item repeatedly without navigating. Confirming on a tier header COLLAPSES to header-only view (correct behavior). Navigating DOWN after collapse moves between headers. Confirming on a header again EXPANDS. To reach a song, navigate to a song entry after expanding, then confirm.

### Key debugging findings

- **DTA `{print}` doesn't go to stderr**: outputs via `Debug::Print` → `mLog` (file) + `HolmesClient` (network). Temporarily add `fputs(msg, stderr)` to see DTA print output.
- **UIManager mSink routing**: On native, `mSink` is set to the current screen on every transition (line 672 of UI.cpp). Messages route through mSink first, which IS the screen, so the full UIScreen → FocusPanel → panel chain executes.
- **NavListSort::NumData() vs GetDataCount()**: `NumData()` returns `mShortcutNodes.size()` (shortcut/header count, constant). `GetDataCount()` returns `mList.size()` (actual flat list items, varies with header mode). Use `GetDataCount()` for the real item count.

## Test Commands

```bash
# Boot to song_select
MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/to-song-select.txt \
  DC3_DATA=orig-assets timeout 120 native/build/dc3-native 2>&1 | \
  grep -E "DC3 (UI|Input|HamNavList)|unhandled msg"

# Scroll test (navigates through song list)
MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
  DC3_DATA=orig-assets timeout 120 native/build/dc3-native 2>&1 | \
  grep -E "DC3 (UI|Input|HamNavList)"
```
