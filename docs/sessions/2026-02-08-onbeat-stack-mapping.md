# RhythmBattle::OnBeat Stack Frame Mapping

**Symbol**: `?OnBeat@RhythmBattle@@AAAXXZ`
**Target frame**: 0x750 (1872 bytes)
**Source frame**: 0x760 (1888 bytes) — **+16 bytes**
**Delta summary**: 509 offset differences across 33 distinct deltas

## Stack Frame Composition

The target 0x750-byte frame contains:
- ~15 pointer/scalar locals (4 bytes each): ~60 bytes
- ~2 bool/byte locals: ~2 bytes
- 1 double temp (beat conversion): 8 bytes
- ~40 DataNode temporaries (8 bytes each): ~320 bytes
- 3 Vector3 temps (12 bytes each): 36 bytes
- 1 DancerSkeleton (large, ~0x3B0-based): ~200+ bytes
- String temporaries: ~variable
- Saved registers + linkage: ~80 bytes
- Padding/alignment: remainder

## Confirmed Variable Mapping

### Persistent Locals (stored early, referenced throughout)

| TGT | SRC | Delta | Variable | Evidence |
|-----|-----|-------|----------|----------|
| 0x68 | 0x70 | +8 | `beat` double conversion (stfd) | m2c: stfd to 0x68; delta=+8 at idx 297 |
| 0x6C | 0x74 | +8 | `beat` int part (lwz from double) | m2c: lwz from 0x6C; delta=+8 at idx 298 |
| 0x70 | 0x78 | +8 | `remainingValue` | m2c: unk70 stores Int() result; delta=+8 at idx 147 |
| 0x74 | 0x7C | +8 | temp ptr (multi-use: mCommandLabel, etc.) | m2c: unk74 reused for multiple ptrs; delta=+8 at idx 328 |
| 0x7C | 0x68 | -20 | `b22` (bool byte) | m2c: unk7C set to 0/1; delta=-20 at idx 581,638 |
| 0x84 | ?? | ?? | `i6cc` / temp multi-use | m2c: unk84 = beat%4, reused as temp |
| 0x88 | ?? | ?? | `leader` Symbol / temp | m2c: GetLeader stores to unk88 |
| 0x8C | ?? | ?? | `inMindControl` (byte) | m2c: unk8C = Sym()==mind_control |
| 0x8D | 0x90 | +3 | `goofy` (byte) | m2c: unk8D = GetGoofy(); delta=+3 at idx 48 |
| 0x90 | 0xA8 | +24 | `beat` (int copy) / MILO_ASSERT line | m2c: unk90 reused; delta=+24 at idx 21,305,3110 |
| 0x9C | ?? | ?? | `i6b4` | m2c: unk9C = var_r10_2 (i6b4 value) |
| 0xA0-0xA4 | ?? | ?? | `handled` DataNode (8 bytes) | m2c: HandleType stores to unkA0 |
| 0xA8 | 0x94 | -20 | `focusPanel` (UIPanel*) | m2c: unkA8 = FocusPanel(); delta=-20 at idx 46 |

### Symbol/String Pointer Locals

| TGT | SRC | Delta | Variable | Notes |
|-----|-----|-------|----------|-------|
| 0x78 | 0xD0 | +88 | `winner`/`i6d8` (reused) | delta=+88 at idx 743 |
| 0x80 | ?? | ?? | `play_vo` DataArray ptr | m2c: unk80 = &play_vo static |
| 0x94 | 0x88 | -12 | `intro` sym ptr (reused for stole_congrats) | delta=-12 at idx 405,1657 |
| 0xAC | 0xB0? | +4 | `both` sym ptr | m2c: unkAC = &both_symbol |
| 0xB0 | ?? | ?? | `left` sym ptr | m2c: unkB0 = &left_symbol |
| 0xB4 | 0xC0 | +12 | `none` sym ptr | delta=+12 at idx 216,2781,2879 |
| 0xB8 | 0xEC | +52 | `&"RhythmBattle.cpp"` filename | delta=+52 at idx 15 |
| 0xBC | 0xB4 | -8 | `right` sym ptr | delta=-8 at idx 265,1823,2744,3028 |
| 0xC0 | 0xE4 | +36 | `jack_swag` sym ptr | delta=+36 at idx 1413,1755,2894 |
| 0xC4 | 0xE8 | +36 | `play_finale_vo` DataArray ptr | delta=+36 at idx 189 |
| 0xC8 | ?? | ?? | `new_groove_working` sym ptr | m2c: unkC8 |
| 0xCC | 0xC4 | -8 | `max_multiplier` sym ptr | delta=-8 at idx 1412,1764,2903 |
| 0xD0 | ?? | ?? | `inzone_warning` sym ptr | m2c: unkD0 |
| 0xD4 | 0xE0 | +12 | `almost_over` sym ptr | delta=+12 at idx 457 |
| 0xD8 | ?? | ?? | `inzone` sym ptr | m2c: unkD8 |
| 0xDC | ?? | ?? | `halftime` sym ptr | m2c: unkDC |
| 0x218 | 0x98 | -384 | `&TheDebug` (for MILO_ASSERT) | delta=-384 at idx 13 |

### DataNode Temporaries (8 bytes each: value + type)

These are compiler-generated for each `operator[]`, `HandleType()`, `Handle()` call.

| TGT | SRC (est.) | Use |
|-----|-----------|-----|
| 0xF8-0xFC | 0x108-0x10C? | play_vo[0] = none |
| 0x100-0x104 | ?? | play_vo[1] = both |
| 0x108-0x10C | ?? | play_vo init temp |
| 0x110-0x114 | ?? | play_vo[1] = left |
| 0x118-0x11C | ?? | play_finale_vo init temp |
| 0x120-0x124 | ?? | case 5/6: play_vo[0] |
| 0x128-0x12C | ?? | play_vo[1] DataNode |
| 0x130-0x134 | ?? | case 5: intro DataNode |
| 0x138-0x13C | ?? | countInMsg init temp |
| 0x148-0x14C | ?? | countInMsg[1] |
| 0x158-0x15C | ?? | halftime play_vo[0] |
| 0x168-0x16C | ?? | almost_over play_vo[0] |
| 0x178-0x17C | ?? | winner play_vo[0] |
| 0x198-0x19C | ?? | play_finale_vo[0] = none |
| 0x1A8-0x1AC | ?? | countInMsg[0] |
| 0x1B8-0x1BC | ?? | halftime play_vo[1] |
| 0x1C8-0x1CC | ?? | winner play_vo[1] |
| 0x1D8-0x1DC | ?? | countInMsg init temp 2 |
| 0x1E8-0x1EC | ?? | almost_over play_vo[1] |
| 0x1F8-0x1FC | 0xF8? | intro play_vo[0] |
| 0x208-0x20C | ?? | play_vo init temp 2 |
| 0x220 | 0xAC? | MILO_ASSERT / String temp? |
| 0x230-0x234 | 0x2B8 | some Message/HandleType temp |
| 0x250-0x254 | ?? | HandleType return temp |
| 0x260-0x264 | ?? | HandleType return temp |
| 0x270-0x274 | ?? | HandleType return temp |
| 0x280-0x284 | ?? | Handle return temp |
| 0x2B0-0x2B4 | 0x308 | Handle/HandleType temp |
| 0x2D0-0x2D4 | ?? | HandleType temp |
| 0x2E0-0x2E4 | ?? | HandleType temp |
| 0x300-0x304 | 0x238 | Handle temp |
| 0x310-0x314 | ?? | HandleType temp (finished_intro) |
| 0x340-0x344 | ?? | Handle temp (countInMsg) |
| 0x360 | ?? | String temp? |
| 0x3A0 | 0x370? | ?? |

### Large Stack Objects

| TGT | Size | Variable |
|-----|------|----------|
| 0x350-0x35C | 12 | Vector3 v (current joint pos) |
| 0x370-0x37C | 12 | Vector3 v2 (previous joint pos) |
| 0x390-0x39C | 12 | Vector3 disp |
| 0x3B0-~0x740 | ~400+ | DancerSkeleton (on-stack) |

## Delta Group Analysis

### Group 1: +16 (228 instructions, 44.8%)
The "frame size offset." Variables at the SAME relative position in both builds,
but uniformly shifted by the 16-byte frame size difference. This group represents
the "correctly placed" locals — they're in the right order, just offset by the
frame delta.

**Fixable by**: Eliminating the extra 16 bytes from our frame (see below).

### Group 2: +8 (165 instructions, 32.4%)
Second-largest group. These variables are 8 bytes closer to the +16 group than
expected. Hypothesis: there's a single 8-byte variable (likely a DataNode temp
or double) that exists in one build but not the other, causing everything above
it to shift by 8.

Variables in this group: `beat` double, `remainingValue`, the temp ptr at 0x74.

**Fixable by**: Identifying and removing/adding the extra 8-byte slot.

### Group 3: +4 (22 instructions)
The `goofy` byte at TGT 0x8D → SRC 0x90 has delta=+3, not +4. But the +4 group
exists elsewhere (e.g., DataNode at TGT [0x4,0x8] vs SRC [0x8,0x4] = OFFSET_SWAP).

### Scattered Groups (±20, ±36, ±88, ±200, ±384, etc.)
These represent variables placed at COMPLETELY different positions:
- `focusPanel`: TGT 0xA8 → SRC 0x94 (delta=-20)
- `b22`: TGT 0x7C → SRC 0x68 (delta=-20)
- `play_finale_vo ptr`: TGT 0xC4 → SRC 0xE8 (delta=+36)
- `winner`/`i6d8`: TGT 0x78 → SRC 0xD0 (delta=+88)
- `TheDebug`: TGT 0x218 → SRC 0x98 (delta=-384)

These are evidence of fundamentally different stack ordering — the compiler
placed these variables in entirely different regions of the frame.

## Root Cause Analysis

### Why +16 extra bytes?
The most likely explanation: **one extra DataNode temporary** (8 bytes) + **alignment padding** (8 bytes). With ~40 DataNode temps in this function, a single extra Handle/HandleType codepath generating an extra temporary would add 8 bytes, and alignment could round up to 16.

Possible candidates:
- `bars_between_vo_suggestion` HandleType call at line 898 — the result is used for `handled` but then discarded; an extra temp might be generated
- The `MakeString` call at line 1399 generates String temporaries with different sizes than target
- Different inlining of DataNode `operator=` could create/eliminate temps

### Why scrambled ordering?
MSVC for Xbox 360 uses a **reverse-declaration-order** heuristic for locals: the last-declared variable gets the lowest stack offset. However, temporaries (DataNode return values, String conversions) are placed by the compiler's internal temp allocator which follows a **first-use** pattern.

The combination of ~15 explicit locals (reverse-decl-order) + ~40 compiler temps (first-use order) + large objects (aligned) creates a layout that's extremely sensitive to any codegen difference.

### The DataNode Temporary Problem
Each of these C++ patterns generates an 8-byte DataNode temp on the stack:
```cpp
play_vo[0] = none;          // DataNode temp for operator[]
handled = focusPanel->HandleType(msg);  // DataNode return
worked_it_progress[0] = min84;  // DataNode temp
```

With ~40 such temps, the compiler allocates them at whatever offset it pleases.
Re-ordering these operations in source could shift them, but the cascading
effects make it nearly impossible to match all 40 simultaneously.

## Actionability Assessment

| Category | Instructions | Fixable? | Approach |
|----------|-------------|----------|----------|
| +16 frame delta | 228 | Maybe | Remove one local/temp to shrink frame by 8+8 |
| +8 variable group | 165 | Unlikely | Would need to find the specific 8-byte variable causing the shift |
| Scattered locals | ~80 | Very unlikely | Would need to reorder ~15 declarations, each affecting register alloc |
| DataNode temps | ~36 | No | Compiler-internal temp allocation, cannot control from source |

**Bottom line**: The stack layout mismatch accounts for ~509 instructions of the
~1757 non-equal instructions (29%). Fixing even the +16 group would require
removing exactly 16 bytes from the frame, which is extremely hard to target
precisely. The scattered groups and DataNode temps are essentially unfixable.

This confirms the "at_limit" assessment for this function.
