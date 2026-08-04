# Systemic AT_LIMIT Patterns

These are project-wide issues that cause regressions across many functions. Fixing them would require macro/header changes affecting many translation units.

## 1. `__FILE__` Path Difference (FIXED)
- **Was**: Decomp `__FILE__` expanded to `"src/system/obj/DirLoader.cpp"`, target had original build paths
- **Fix**: `WIBO_PATH_MAP` in `configure.py` remaps source paths to original Windows paths (`e:/lazer_build_gmc1/...`)
- **Result**: `__FILE__` strings now match original, `??_C@_` string literal hashes match (121/121)

## 2. LINKER_MERGED / ICF (Identical COMDAT Folding)
- **What**: Linker merges functions with identical machine code to a single address
- **Impact**: Call targets differ (`bl merged_XXXXXXXX` vs `bl ActualFunction`)
- **Common merged functions**: `MakeString` templates, `String` ctor, `_M_find`, template instantiations
- **Fix**: Unfixable - this is a linker optimization we cannot replicate
- **Lookup**: Use `mcp__orchestrator__lookup_merged_symbol` to identify what's at a merged address

## 3. `DoneLoading` → `OnlyReturns` ICF
- **What**: `DoneLoading` virtual function (returns void, does nothing) gets merged with `OnlyReturns` in target
- **Impact**: Functions calling `DoneLoading` show `bl OnlyReturns` in target vs `bl DoneLoading` in ours
- **Affects**: DirLoader destructor, LoadObjs, and any function calling DoneLoading on a Loader

## 4. FormatString Wrapper in MILO_NOTIFY (UNFIXABLE)
- **Target's MILO_NOTIFY**: Has a `FormatString` construction wrapping MakeString result (~4KB stack buffer)
- **Our MILO_NOTIFY**: Passes string directly via `MakeString` → `DebugNotifier::operator<<`
- **Impact**: Stack frame differences (e.g., SetupDir: 0x1190 vs 0x170)
- **Attempted fixes**:
  - `__forceinline` on MakeString templates: REGRESSED (87.2% from 96.7%) — inlines MakeString differently than target
  - FormatString wrapper in `DebugNotifier::operator<<`: REGRESSED (91.6%) — adds SECOND FormatString, doubling stack usage
- **Root cause**: Target compiler appears to optimize FormatString reuse across control flow paths in a way we can't reproduce
- **Status**: UNFIXABLE with current understanding

## 5. `_MemAllocTemp` vs `MemAlloc` in ChunkStream (FIXED)
- **Target**: ChunkStream's `operator new` uses `_MemAllocTemp`
- **Our build**: Used `MemAlloc` via `MEM_OVERLOAD` macro
- **Fix applied**: Replaced `MEM_OVERLOAD(ChunkStream, 0x31)` with explicit `operator new` using `_MemAllocTemp`
- **Result**: OpenFile improved 93.8% → 93.9%

## 6. `_MemAllocTemp` vs `MemAlloc` in ArkFile (FIXED)
- **Target**: ArkFile's `operator new` uses `_MemAllocTemp`
- **Fix applied**: Changed `MemAlloc` → `_MemAllocTemp` in ArkFile_p.h
- **Note**: No current `new ArkFile` call sites (allocation done via placement new in File.cpp), so no functional impact yet

## 7. Block Sinking / Cold Code Relocation (UNFIXABLE)
- **What**: 361 functions (1.5%) in the target have basic blocks placed AFTER the function return, with backward branches to the join point. Typically null-check patterns where the non-null block (vbase conversion, member loads) is "sunk" past the epilogue.
- **Pattern**: `bne cr6, [past_return]` → null-path falls through → epilogue → return → [non-null block] → `b [back_to_join]`
- **Affects**: FlowPtr<T>::operator= (all specializations with vbase: RndAnimatable, Sound, Flow, ObjectDir), STL algorithms, D3D shader code, error-return paths
- **Root cause investigation (c2.dll RE)**:
  - **PEEP branch pair reorder** (`FUN_10bacf2b` in c2.dll) reorders conditional/unconditional branch pairs but is gated behind `DAT_10c3de20 == 2` (PGO optimize mode)
  - `DAT_10c3de20` is set from `DAT_10c6f1c8` which is only set to 2 when PGO options (`-PogoSafeMode` etc.) are active
  - **Xenon scheduler** (`0x10b71d8f`) also has PGO-gated block layout code paths
  - Binary patching: forced PGO mode 2 (patched all 10 loads of DAT_10c6f1c8) → **no effect** — PGO code paths need actual profiling data (branch weights) to make different block layout decisions
  - Tested with /O1, /O2, /Ox, various source patterns (if/else, defaults+if, ternary, different complexity levels) — **identical output** in all cases
  - No PGO symbols (`__PogoProbeVector`, `__PogoRuntimeVector`) in target's `ham_xbox_r.map` — target was NOT compiled with standard PGO
  - No BBT section splitting in target binary (single `.text` section)
- **Possible explanations**: Different c2.dll build variant, linker-level BBT with branch trace data, or unknown compiler mechanism
- **Status**: UNFIXABLE — cannot reproduce block sinking with our compiler under any conditions

## 8. Short-Circuit Bool Materialization Rotates Later Anonymous-Temp Slots (AT_LIMIT)

- **Observed in**: `BustAMovePanel::Poll` (dc3-decomp), 99.58% with the residual below.
- **What it looks like**: two source-level fixes that are individually correct
  cannot be held at the same time. Fixing a 4-instruction bool block ~320
  instructions in costs 23 instructions in an unrelated branch ~250 instructions
  later. Net 99.58% -> 97.74%.

### The two clusters

Residual at 99.58% is 5 instructions in two places:

| idx | target | ours |
|----|--------|------|
| 319 | *(absent)* | `li r28, 0x1` |
| 332 | `li r10, 0x1` | *(absent)* |
| 334 | `mr r10, r21` | `mr r28, r21` |
| 336 | `clrlwi r28, r10, 24` | *(absent)* |
| 729 | `add r4, r11, r28` | `add r4, r28, r11` |

The target builds the bool in a scratch register **after** the comparison and
then truncates it into the named local (`clrlwi rD, rS, 24`). That shape is the
signature of a **short-circuit boolean materialization** — a branch converted to
a value. Only `bool x = A && B;` produces it. The `bool x = true; if (!A || !B)
x = false;` form we ship assigns the named local directly and emits no scratch
temp and no truncation.

Idx 729 is a second live range of the same register (`r28` is reused as the
`mSongStructure[i]` byte offset), so it moves with the first cluster.

### What actually moves 250 instructions away

Writing `bool isPlayer0Pink = A && B;` fixes idx 332/334/336 **byte-for-byte**
and simultaneously breaks the `static DebugGraph scoreGraph(...)` construction:

```cpp
static DebugGraph scoreGraph(
    0.1f, 0.1f, 0.8f, 0.2f,
    Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),   // arg 5
    Hmx::Color(0.0f, 0.0f, 0.0f, 0.3f),   // arg 6
    100, 0.0f, 1.0f, String(""));
```

`run_diff_inspect mode=stack-layout` names it exactly. Good build: **50/50
MATCH**. Bad build: **40 MATCH / 10 PERMUTED**, frame size identical (0x1f0
both), callee-saved counts identical, **no TGT_ONLY / BASE_ONLY slot**. The
report reads `target's slot 0xb0 <-> base 0xb8`, `0xc0 <-> 0xc4`, etc.

Decoding the stores, the two 16-byte `Hmx::Color` argument temporaries simply
**swap frame slots**:

- target / good build: arg5 (white) at `0xb0-0xbf`, arg6 (dark) at `0xc0-0xcf`
- bad build:           arg6 (dark)  at `0xb0-0xbf`, arg5 (white) at `0xc0-0xcf`

So this is **not** a slot-count change, **not** a frame-size change, and **not**
a shift. It is a *rotation of assignment within an unchanged set of slots* — two
anonymous temporaries of identical size, alignment and type exchanging places.
The 23 extra instructions are the re-scheduled `stfs`/`ld` pairs that feed the
by-value Color arguments, not extra work.

### Mechanism

MSVC allocates anonymous (unnamed) constructor temporaries from a pool ordered
by an internal creation counter, separate from named locals. A short-circuit
boolean materialization introduces one extra anonymous temp earlier in the
function; that bumps the counter and flips the tie-break between the next pair
of equal-size, equal-alignment temps. Arguments are evaluated right-to-left, so
arg6 is created before arg5; the good ordering gives the lower slot to arg5
(left-to-right), the bad ordering gives it to arg6 (creation order).

### Ruled out (measured, do not re-derive)

- **Lexical scope count** — ruled out by an earlier pass.
- **Surface syntax of the `&&`** — `bool x = !(A' || B');` compiles to
  *byte-identical* output to `bool x = A && B;`: 97.735%, the same 28
  mismatches at the same 28 indices. So it is the short-circuit
  branch-to-value conversion itself, not the operator spelling.
- **Register pressure / frame growth** — frame size, callee-saved GPR count
  (11) and FPR count (7) are identical in all variants.
- **Declaration reorder** — inert here as everywhere else.

### Why it is not repairable from source (currently)

The obvious counter-lever is lever 5, "name the temporaries so their slots are
pinned". It does not apply: both Colors are arguments to a **function-local
`static`**, so they are constructed *inside* the run-once guard (the guard's
`bne` is at idx 561, the Color stores at 568-593). Naming them would hoist
construction outside the guard and run it on every call — a behaviour change,
not a formulation change. There is no source position that is both inside the
guard and able to hold a name.

Retried at four independent baselines; cost identical every time.
**Verdict: accept the 4-instruction bool residual. AT_LIMIT.**
