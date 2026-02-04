# Session: Gap Analysis & Documentation Restructure

**Date:** 2026-01-23
**Focus:** Strategic analysis of where to invest decompilation effort

---

## Summary

Conducted comprehensive gap analysis of the codebase to identify high-impact investment areas. Restructured documentation to separate strategic planning from tactical execution.

---

## Research Performed

### 1. Current Progress Check

Regenerated report and gathered baseline stats:
- **46,958** total functions
- **21,232** (45.2%) at 100% match
- **22,655** (48.2%) at 90%+ match
- **1,388** near-match functions (90-99%)
- **39.0%** overall fuzzy match (size-weighted)

### 2. Gap Identification

Queried report for largest gaps by unit and function:

**Largest unmatched system functions:**
- `RhythmBattle::OnBeat` - 16.5KB at 0%
- `DepthBuffer3D::DrawShowing` - 5.2KB at 0%
- `MoveDir::UpdateOverlay` - 5KB at 0%
- `PlatformMgr::Poll` - 4.8KB at 0%

**Largest unmatched game functions:**
- `BustAMovePanel::OnBeat` - 12KB at 0%
- `MetagameRank::UpdateScore` - 8.6KB at 0%
- `SaveLoadManager::SetState` - 5.2KB at 0%

**Unit-level gaps (game code):**
- MetagameRank: 19.9%
- BustAMovePanel: 33.0%
- HamStorePanel: 42.4%

**Unit-level gaps (system code):**
- Utl: 27.6%
- Text: 43.3%
- HamNavList: 55.2%

### 3. Near-Match Analysis

Identified largest 90%+ functions that could be quick wins:
- `RndParticleSys::SyncProperty` - 99.7%, 7.3KB
- `Spotlight::SyncProperty` - 99.7%, 4.8KB
- `HamNavList::Handle` - 99.0%, 4.1KB

**Caveat:** Many 97%+ functions are at linker limit per OBJDIFF_LEARNINGS.md patterns.

### 4. Documentation Review

Used Explore agents to audit existing docs structure:
- WORKSESSION.md serves as central hub
- LOW_HANGING_FRUIT.md covers tactical function targets
- No strategic "where to invest" document existed

---

## Actions Taken

### Documentation Created

1. **`docs/decomp/GAP_ANALYSIS.md`** - New strategic overview document
   - Current status snapshot
   - What NOT to work on (XDK, external libs)
   - High-impact investment areas (Tier 1/2/3)
   - Category breakdown by subsystem
   - Effort vs Payoff matrix
   - Recommended work order

2. **`docs/sessions/2026-01-23-gap-analysis.md`** - This session document

### Documentation Updated

3. **`docs/sessions/2026-01-worksession-archive.md`** - Added link to GAP_ANALYSIS.md in Quick Links

### Documentation Reorganized

4. **`docs/tools/objdiff/LEARNINGS.md`** - Earlier in session:
   - Moved "Tool Improvement Ideas" to separate OBJDIFF_WISHLIST.md
   - Added concrete code examples to Pattern 2 (Bool Return Mask)
   - Added concrete code examples to Pattern 3 (Register Allocation)
   - Fixed report path inconsistencies across all docs

---

## Key Findings

### Strategic Insights

1. **XDK/external code is ~30% of codebase** - All at 0%, will stay 0%, should be ignored in planning

2. **RB3 reference is valuable** - System code with RB3 equivalents (Text, Part, Mesh, Character) offers better ROI than DC3-specific code

3. **Near-matches need triage** - 1,388 functions at 90-99%, but many are at linker limit. Must diagnose before investing time.

4. **Biggest DC3-specific gaps:**
   - BustAMovePanel (minigame) - 33%, 12KB OnBeat function
   - MetagameRank (scoring) - 20%, 8.6KB UpdateScore function
   - These have no reference and require pure reverse engineering

### Documentation Insights

1. **Needed separation of concerns:**
   - Strategic: "Where should we invest?" → GAP_ANALYSIS.md
   - Tactical: "Which functions to fix?" → LOW_HANGING_FRUIT.md
   - Methodology: "How to work?" → SUBAGENT_STRATEGY.md

2. **OBJDIFF_LEARNINGS.md was mixing** working patterns with wishlist features - now separated

---

## New Document Hierarchy

```
CLAUDE.md (root, minimal context)
    ↓
WORKSESSION.md (central hub, session history)
    ├── decomp/GAP_ANALYSIS.md      ← NEW: Strategic "where to invest"
    ├── decomp/LOW_HANGING_FRUIT.md  (Tactical function targets)
    ├── decomp/SUBAGENT_STRATEGY.md  (Execution methodology)
    ├── decomp/TECHNICAL_NOTES.md    (Compiler patterns)
    ├── decomp/RB3_REFERENCE.md      (Shared code reference)
    ├── OBJDIFF_LEARNINGS.md         (Diagnosis patterns)
    ├── OBJDIFF_WISHLIST.md          ← NEW: Tool improvement ideas
    └── sessions/*.md                (Session notes)
```

---

## Next Actions

1. Verify 99%+ near-matches aren't at linker limit before investing time
2. Continue system/math work (high ROI)
3. Consider Text.cpp or Part.cpp as next medium-size targets (have RB3 reference)
4. Update GAP_ANALYSIS.md stats periodically as progress is made
