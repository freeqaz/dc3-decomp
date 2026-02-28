# Unimplemented Functions Analysis (2026-02-28)

## Context

After a full `sync_objdiff.py --all` scan of 32,208 non-SDK functions, the sync reported **4,369 "unimplemented"** functions — symbols where `base_size == 0` in objdiff (the decomp .obj has no code for the symbol, but the target .obj does).

This session investigated whether these are config/build issues or genuinely missing implementations.

## What "Unimplemented" Means

In `sync_objdiff.py`, a function is "unimplemented" when objdiff returns `base_size == 0` — the target .obj (original binary) contains the function, but the decomp .obj (our compiled source) does not. This does NOT mean the function is broken; it means our .cpp file for that unit doesn't produce code for that symbol.

## Findings: Breakdown of 4,369

| Category | Est. Count | % | Root Cause |
|---|---|---|---|
| **STL template instantiations** | ~1,290 | 30% | COMDAT emitted in original TU, our source triggers them in different TUs |
| **Truly unimplemented methods** | ~736 | 17% | .cpp files exist but function bodies never written |
| **Vector/scalar deleting destructors** | ~690 | 16% | Compiler-generated when `delete` is used; appears when parent code is implemented |
| **Boilerplate (atexit/dyninit)** | ~580 | 13% | Already all COMPLETE in DB; not actually contributing to unimpl count |
| **ObjPtr/ObjRef template instantiations** | ~170 | 4% | Same COMDAT issue as STL |
| **Inline functions from headers** | small | ~1% | Original TU called them, ours doesn't |
| **Other (MakeString, thunks)** | ~240+ | ~5% | Mixed template/compiler artifacts |

### Key Results

1. **Zero functions are compiled into the wrong .obj.** The build system mapping is correct.
2. **736 game methods are genuinely undecomped.** The .cpp files exist but the method bodies were never written.
3. **~3,633 are COMDAT/template/compiler artifacts** that resolve naturally as real methods get implemented.
4. **link_glue contributes ~0 to the count.** All 1,338 link_glue functions are COMPLETE.
5. **All 2,581 boilerplate functions are already COMPLETE** (2,570 at 100%).

## Categories Explained

### Truly Unimplemented (~736)
These are the actionable items. Examples:
- `CharEyes::Poll`, `CharEyes::Load`, `CharEyes::NextLook`
- `HamDirector::Poll`
- `VorbisReader::DoFileRead`, `VorbisReader::DecodeThreadPoll`
- `DateTime::DayOfWeek`, `DateTime::FromDayNumber`
- `HolmesClient` anonymous namespace functions
- `FlowSetProperty` easing functions (inline in headers but not called by decomp)

### STL/Template COMDATs (~1,460)
MSVC emits COMDAT template instantiations in whichever TU first uses them. The original and decomp builds may instantiate the same templates in different TUs. When objdiff checks a specific unit's target .obj, some templates exist there but not in our decomp .obj for that unit. These resolve when the real methods using those templates are implemented.

### Vector/Scalar Deleting Destructors (~690)
Compiler-generated virtual destructor wrappers (`??_G`, `??_E`). Emitted when a `delete` expression exists in the TU. Appear automatically when the parent code that calls `delete` is implemented.

## Minor Bug Found

`skip_boilerplate` in `scripts/orchestrator/database.py` uses SQL `LIKE` with prefixes like `??__F`, but `_` is a SQL single-character wildcard. This falsely filters 618 non-boilerplate functions (e.g., `Message::Message` constructors). No current impact — all affected functions are already COMPLETE.

Fix: use `ESCAPE` clause or switch to `GLOB`.

## DB Updates Performed

### Full Scan Results
Ran objdiff on all 31,309 non-boilerplate functions. Found **3,885 unimplemented** (base_size=0):

| Category | Count |
|---|---|
| game_method | 2,344 |
| stl_template | 1,157 |
| obj_template | 188 |
| makestring | 91 |
| vec_del_dtor | 70 |
| scalar_del_dtor | 33 |
| vbase_dtor | 2 |

### DB Changes
1. **Flagged 1,690 newly discovered stubs** as `is_stub=1` (654 were already flagged)
2. **Cleared 292 resolved stubs** — functions that previously had `is_stub=1` but now have compiled code
3. **Reset 2,429 falsely-COMPLETE verdicts** — stubs that were marked COMPLETE but have no source implementation

### Updated Progress
- Non-excluded: 35,196
- COMPLETE: 30,915
- AT_LIMIT: 873
- **Remaining workable: 3,408** (2,429 stubs + 979 non-stub)
- Done: 90.3% (previously inflated to ~97.5%)

### Workable Stubs by Area

| Area | Count | Bytes |
|---|---|---|
| Rendering | 535 | 268,880 |
| Audio/Synth | 390 | 105,628 |
| OS/Net/Utility | 315 | 75,620 |
| Gameplay (Ham) | 236 | 112,748 |
| Game-specific | 208 | 70,980 |
| Third-party libs | 187 | 78,680 |
| Character | 149 | 74,908 |
| UI/Flow | 137 | 41,296 |
| Kinect/Gesture | 115 | 47,872 |
| Other | 84 | 18,556 |
| Core/Object/Movie | 73 | 24,476 |

### Querying Stubs
```sql
-- All workable stubs
SELECT demangled, unit, size FROM functions
WHERE is_stub = 1 AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
ORDER BY unit;

-- Stubs by unit
SELECT REPLACE(unit, 'default/', '') as u, COUNT(*) as cnt
FROM functions WHERE is_stub = 1
AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
GROUP BY unit ORDER BY cnt DESC;
```
