# Session: GetOfficialChallengesJob::GetRows (ChallengeSystemJobs.cpp)

Date: 2026-02-04
Function: `GetOfficialChallengesJob::GetRows`
Symbol: `?GetRows@GetOfficialChallengesJob@@QAAXAAV?$vector@VChallengeRow@@V?$StlNodeAlloc@VChallengeRow@@@stlpmtx_std@@@stlpmtx_std@@AANAA_N@Z`
File: `src/lazer/net_ham/ChallengeSystemJobs.cpp`

## Goal
Improve match for `GetOfficialChallengesJob::GetRows`, focusing only on this function.

## Baseline
- Match: 98.8%
- Patterns: LINKER_MERGED (`merged_823314D8` x3) verified as ICF/unfixable.
- No register swaps, no offset swaps, no control-flow diffs at baseline.

## Actions Taken
1. **Verified ICF**
   - `merged_823314D8` confirmed via merged symbol lookup.

2. **Adjusted DateTime string formatting**
   - Switched `DateTime::ToDateString` to `DateTime::ToString` for `startTime` and `nextStartTime`.
   - Match remained 98.8%.

3. **Tried small codegen nudges (all reverted)**
   - Reordered `GetByName` calls and/or local temporaries.
   - Introduced local `const char *` for keys.
   - Swapped `ChallengeRow& row` to pointer and used `row->`.
   - Manual string compare loop for `"0000-00-00"` (introduced control flow diffs; reverted).
   - Reused a single `String dateStr` instead of multiple locals.
   - Moved static `challenge_*` symbols outside/inside `if (response)`.
   - Added `Locale &loc = TheLocale` alias to affect arg ordering.
   - Added local `const char *emptyStr` for `row.unk2c`.
   - Swapped local declaration order for `DateTime startTime` and `std::vector<ChallengeRow> calcedRows`.

4. **Result of nudges**
   - Most changes either worsened match (down to ~96–97%) or had no effect.
   - All were reverted to restore the baseline state.

## Current State
- Match is back at **98.8%**.
- Remaining deltas appear limited to:
  - LINKER_MERGED calls (`merged_823314D8`) and prolog/epilog `__savegprlr_14` / `__restgprlr_14` differences.
  - No clear fixable pattern detected by objdiff at baseline.

## Recommendation
- Treat this function as **at-limit** unless new structural hints appear.
- If revisiting, focus on more invasive lifetime/structure changes that could affect prolog selection and symbol ordering.

## How To Test / Reproduce (MCP First)
- Primary diff tool: `mcp__Decomp_Orchestrator__run_objdiff`
- Optional analysis: `mcp__Decomp_Orchestrator__run_analyze_function`
- Verify ICF: `mcp__Decomp_Orchestrator__lookup_merged_symbol`
- Docs: `docs/decomp/TECHNICAL_NOTES.md`, `docs/decomp/patterns/verifiable-icf.md`

### MCP Calls Used
```
run_objdiff:
  symbol: ?GetRows@GetOfficialChallengesJob@@QAAXAAV?$vector@VChallengeRow@@V?$StlNodeAlloc@VChallengeRow@@@stlpmtx_std@@@stlpmtx_std@@AANAA_N@Z
  project_dir: .
  context: 3

run_analyze_function:
  symbol: ?GetRows@GetOfficialChallengesJob@@QAAXAAV?$vector@VChallengeRow@@V?$StlNodeAlloc@VChallengeRow@@@stlpmtx_std@@@stlpmtx_std@@AANAA_N@Z
  project_dir: .
  resolve_offsets: true

lookup_merged_symbol:
  address: merged_823314D8
```

### Optional Local Build (if needed)
```bash
ninja build/373307D9/src/lazer/net_ham/ChallengeSystemJobs.obj
```
