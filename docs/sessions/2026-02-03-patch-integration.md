# Patch Integration Session — 2026-02-03

## Goal

Validate the patch triage/apply pipeline, fix bugs in `patch_apply_safe.py`, and batch-apply ready patches with subagent debugging.

## Bugs Fixed in `patch_apply_safe.py`

Three bugs found and fixed:

1. **Wrong ninja target path** — `build_unit()` produced `build/373307D9/src/system/utl/MemMgr.cpp.obj` but ninja expects `MemMgr.obj` (no `.cpp` extension before `.obj`). Fix: strip `.cpp`/`.c` before appending `.obj`.

2. **Wrong JSON key for match percent** — `check_match()` looked for `match_percent` / `percent` but objdiff returns `fuzzy_match_percent`. Fix: use `fuzzy_match_percent` as primary key with fallbacks.

3. **Unparseable stdout** — objdiff's `--build` flag causes `ninja: no work to do.` to appear on stdout before the JSON. `json.loads()` choked on this. Fix: find first `{` in stdout and parse from there.

## Manual Patch Validation

Tested 7 patches from `scratch/patches/ready/`. Results:

| Patch | Result | Notes |
|-------|--------|-------|
| MemPushTemp (MemMgr) | **Stale** | Duplicate function definitions — already in tree |
| CharEyes InterestState | **Stale** | Duplicate operator definitions — already in tree |
| BinStream::Read | **Regressed** | 64.8% -> 63.6%, reverted |
| Spotlight::Generate | **Regressed** | 99.5% -> 99.1%, reverted |
| RndTransAnim::Copy | **Kept** | 96.5% -> 99.1% |
| FlowNode::Deactivate | **Kept** | 96.8% -> 99.1% |
| MemStream::Eof | **Kept** | 98.8% -> 100% (perfect match) |

**Key finding**: Many "ready" patches are stale because the codebase has evolved since they were generated. The triage categories from `patch_triage.py` reflect the state at triage time, not current state.

## Script Validation

After fixing bugs, ran `patch_apply_safe.py` on the 3 manually verified patches:
- MemStream::Eof: correctly applied and verified at 100%
- RndTransAnim and FlowNode: correctly detected as below their 100% target and auto-reverted

The script's conservative regression check works correctly. The 0.5% tolerance isn't enough for patches claiming 100% that achieve 99.1% due to merged symbols. For future batch runs, consider `--tolerance` flag or accepting "at_limit" verdicts.

## Subagent Batch Run (5 Functions)

Applied 5 more patches and launched parallel Sonnet subagents to debug each to 100%:

| Function | File | Start | Final | Blocker |
|----------|------|-------|-------|---------|
| CharClip::BeatToSample | CharClip.cpp | 99.7% | 99.7% | Constant pool address mismatch |
| FlowDistance::Load | FlowDistance.cpp | 98.3% | 98.3% | ASSERT_REVS instruction scheduling |
| ThreadTask::Poll | Task.cpp | 99.3% | 99.3% | Merged symbols + address relocs |
| FileMergerOrganizer::CheckDone | FileMergerOrganizer.cpp | 98.8% | 98.8% | Merged symbols + address relocs |
| MidiReader::ReadMidiEvent | MidiReader.cpp | 99.7% | 99.7% | Address relocations only |

All 5 are at their practical limits. The remaining diffs are unfixable compiler/linker artifacts:

- **Merged symbols (ICF)**: Linker folds identical function bodies (e.g., MakeString template instantiations) to a single address
- **Address relocations**: Symbol addresses differ between object files; resolved identically at link time
- **Constant pool addresses**: Compiler assigns different slots for the same float constant (0.0f) across compilation units
- **ASSERT_REVS scheduling**: Known issue affecting ~146 `::Load` functions — compiler reorders `addi` instructions differently than the original binary

## Scripts Added

- `scripts/patches/apply_safe.py` — Apply patches with build + objdiff verification (3 bugs fixed)
- `scripts/patches/candidates.py` — Find patches that apply cleanly, show delta/target/size info

## Takeaways

1. **Patches get functions to their ceiling, not necessarily 100%.** The "ready" patches are useful — they close the gap from current% to practical-limit%. The remaining diffs are linker artifacts.

2. **Staleness is a real problem.** Patches generated against an older tree state may fail to apply, introduce duplicates, or regress. The `--check` dry-run in git apply catches some of this, but build errors catch the rest.

3. **The apply script works but needs tolerance tuning.** A patch targeting 100% that achieves 99.3% (due to merged symbols) shouldn't be classified as "regressed". Future improvement: accept the result if objdiff verdict is `AT_LIMIT` or `COMPLETE`, regardless of the numeric target.

4. **Subagent debugging of at-limit functions is low ROI.** All 5 agents concluded "at limit" without improving match%. Better to use agents on functions with actual code-level mismatches (opcode diffs, control flow diffs) rather than address-only diffs.

5. **Better candidate selection criteria for future batches:**
   - Prefer patches where current match has `diff_op > 0` (actual opcode differences)
   - Skip functions where all diffs are `diff_arg` only (address-only = at limit)
   - Skip functions with high merged symbol ratios
   - The `run_analyze_function` tool can pre-screen candidates before committing subagent time
