# Batch Work Targets

Prioritized list of units and functions to tackle in bulk. Generated 2026-02-25.

---

## Tier 1 — 15 Workable Functions, Never Attempted

These are already tracked in `decomp.db`, have a partial match percentage, and have never been worked on. They are the most immediately actionable targets.

Run the following SQL to get the current list:

```sql
SELECT symbol, unit, size, current_percent
FROM functions
WHERE excluded = 0
  AND verdict NOT IN ('COMPLETE', 'AT_LIMIT')
  AND attempt_count = 0
  AND unit NOT LIKE '%/lib/%'
ORDER BY current_percent DESC;
```

Current snapshot (2026-02-25):

| Match | Size | Unit | Function |
|-------|------|------|----------|
| 99.3% | 636B | `lazer/game/Game` | `Game::LoadNewSong` |
| 99.3% | 696B | `system/meta/MoviePanel` | `MoviePanel::Poll` |
| 99.3% | 192B | `system/char/CharIKRod` | `CharIKRod::Copy` |
| 99.3% | 800B | `lazer/game/Game` | `Game::LoadSong` |
| 99.3% | 1460B | `system/rndobj/Gen` | `RndGenerator::Load` |
| 99.2% | 604B | `lazer/meta_ham/AppLabel` | `AppLabel::SetCreditsText` |
| 99.2% | 1980B | `system/ui/UIFontImporter` | `UIFontImporter::Handle` |
| 99.2% | 1388B | `system/char/CharIKHand` | `CharIKHand::Load` |
| 99.2% | 1000B | `lazer/meta_ham/CharacterProvider` | `CharacterProvider::UpdateList` |
| 99.1% | 1036B | `lazer/game/BustAMovePanel` | `BustAMovePanel::AnimateFlashcard` |
| 99.1% | 4100B | `system/hamobj/HamNavList` | `HamNavList::Handle` |
| 99.1% | 1800B | `system/rndobj/Line` | `RndLine::Handle` |
| 99.0% | 3024B | `lazer/meta_ham/VoiceControlPanel` | `VoiceControlPanel::OnMsg` |
| 99.0% | 268B | `system/rndobj/Trans` | `RndTransformable::Copy` |
| 94.2% | 2092B | `system/rndobj/Anim` | `RndAnimatable::OnAnimate` |

---

## Tier 2 — Units with Many Untracked Functions

These units have 150+ functions that have never been checked. Running `batch_check` will auto-mark all 100% matches as COMPLETE and surface partial matches for manual work. Each unit represents a large potential score gain.

**Before batch_checking any unit**, always rebuild its object file to avoid stale results (see Gotcha below):

```bash
touch src/system/rndobj/Rnd.cpp && ninja build/373307D9/src/system/rndobj/Rnd.obj
```

Then batch-check:

```
mcp__orchestrator__batch_check
  unit_pattern: "default/system/rndobj/Rnd"
```

### Priority units (sorted by function count):

| Unit | Untracked Funcs | Avg Size | Notes |
|------|----------------|----------|-------|
| `default/system/world/LightPreset` | 291 | 170B | |
| `default/system/rndobj/Rnd` | 263 | 140B | |
| `default/system/hamobj/HamDirector` | 250 | 233B | Large Handle funcs |
| `default/system/flow/Flow` | 240 | 126B | FlowNode has known offset issue |
| `default/system/rndobj/PropAnim` | 236 | 144B | |
| `default/system/char/Character` | 233 | 131B | Shared with RB3 |
| `default/system/char/Char` | 222 | 96B | Shared with RB3 |
| `default/lazer/meta_ham/MetaPanel` | 205 | 96B | |
| `default/system/synth/Synth` | 194 | 125B | |
| `default/system/obj/DataFunc` | 187 | 196B | DataArray script ops |
| `default/system/rndobj/EventTrigger` | 185 | 173B | |
| `default/system/hamobj/Ham` | 175 | 90B | |
| `default/system/rndobj/Text` | 173 | 149B | |
| `default/system/synth/Sequence` | 170 | 128B | |
| `default/system/obj/Dir` | 169 | 168B | |
| `default/system/hamobj/MoveDir` | 168 | 180B | |
| `default/system/rndobj/Mesh` | 158 | 187B | |
| `default/system/hamobj/HamNavList` | 157 | 157B | |

SQL to refresh this list:

```sql
SELECT unit, COUNT(*) as cnt, CAST(AVG(size) AS INT) as avg_bytes
FROM functions
WHERE attempt_count = 0 AND excluded = 0
  AND unit NOT LIKE '%/lib/%'
  AND unit NOT LIKE '%asm/%'
GROUP BY unit
ORDER BY cnt DESC
LIMIT 30;
```

---

## Tier 3 — Struct Offset Bugs

When multiple functions in the same unit share the same `off:+N` mismatch pattern, it means a struct's field declaration order is wrong in the header. This is fixable at the source level.

### Known issue: `default/system/math/Geo`

Two functions with: `OFFSET_SWAP pattern detected with original assembly accessing offset 24 (4 bytes...)`

Investigate with:
```
mcp__orchestrator__get_rb2_class_info class_name: "BSPNode"
```

### How to detect struct ordering bugs

1. Run `batch_check` on a unit — if multiple functions have the same offset shift, it's likely a field ordering issue
2. Check `attempts` table for matching notes:
   ```sql
   SELECT f.unit, f.symbol, a.notes
   FROM functions f JOIN attempts a ON f.id = a.function_id
   WHERE a.notes LIKE '%off:+%'
   ORDER BY f.unit;
   ```
3. Use Ghidra to verify the actual field order: `piVar2[0xN]` in the decompiled output gives you the byte offset (`N * 4`)
4. Cross-check with `mcp__orchestrator__get_rb2_class_info` for the class layout

### Case study: `AnimTask` (fixed 2026-02-25)

- **Symptom**: `FireFlowLabel` had `off:+20` at instruction 38 — `lwz r11, 0x60(task)` vs expected `0x4c`
- **Diagnosis**: Ghidra showed `piVar2[0x13]` = `0x4c`, meaning `mAnimTarget.mObject` is at `0x4c` → `mAnimTarget` at `0x40`
- **Fix**: Swapped `mAnimTarget` (was `0x54`) and `mListener` (was `0x40`) in `Anim.h`
- **Impact**: Fixed offset mismatch in `FireFlowLabel`; `AnimTask::Poll` and constructor updated automatically

---

## Gotcha: Ninja Doesn't Track Header Dependencies

Ninja's build graph only lists `.cpp` files as inputs — it does **not** detect when a header changes.

After modifying a header:

```bash
touch src/path/to/file.cpp && ninja build/373307D9/src/path/to/file.obj
```

Without this, `run_objdiff` and `batch_check` will silently use stale object files and report wrong match percentages. The `full_build` flag in `run_objdiff` does **not** help — it only forces a rebuild of the specific object, but ninja still won't recompile it if it thinks the source is unchanged.
