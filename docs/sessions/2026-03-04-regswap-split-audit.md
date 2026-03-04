# Session: Regswap Split + Permuter Gap Audit (2026-03-04)

## Goal

Validate the regswap split plan, audit pattern-free workable functions, identify permuter gaps, and implement the volatile/callee-saved register swap classification.

## Step 1: Regswap Split Validation

Sampled 16 functions across regswap+addr_reloc and regswap-only categories. Ran `run_diff_inspect mode:regswaps` on each.

### Key Finding: addr_reloc does NOT predict register type

The plan assumed regswap+addr_reloc functions would have volatile swaps and regswap-only functions would have callee-saved swaps. **This was wrong.**

| Function | Group | Swap Registers | Classification |
|----------|-------|---------------|----------------|
| WorldCrowd::Load | +addr_reloc | r10↔r9, r10↔r11 | Pure volatile GPR |
| UIList::GetDistanceToPlane | +addr_reloc | r11↔r30 | MIXED |
| SfxInst::SfxInst | +addr_reloc | f0↔f13 | Pure volatile FPR |
| UIManager::Poll | +addr_reloc | r10↔r9, r8↔r9, r10↔r8 | Pure volatile GPR |
| CharLipSyncDriver::Load | +addr_reloc | r26↔r27 | Pure callee-saved GPR |
| RndPropAnim::Load | +addr_reloc | r10↔r11, r10↔r9 | Pure volatile GPR |
| HamNavProvider::Text | +addr_reloc | r27↔r28 | Pure callee-saved GPR |
| UIFontImporter::Load | +addr_reloc | r24↔r25 | Pure callee-saved GPR |
| RndMesh::MakeWorldSphere | no addr_reloc | r10↔r11 | Pure volatile GPR |
| RndMesh::GetDistanceToPlane | no addr_reloc | f12↔f13 | Pure volatile FPR |
| CharBonesMeshes::AcquirePose | no addr_reloc | r10↔r11, r11↔r9 | Pure volatile GPR |
| FastInvert | no addr_reloc | f30↔f31 | Pure callee-saved FPR |
| Spotlight::SetColor | no addr_reloc | r10↔r11 + f0↔f12 | MIXED |
| MakeRotMatrix | no addr_reloc | f30↔f31 + f12↔f13 + many | MIXED |
| RndOverlay::Print | no addr_reloc | r29↔r30 | Pure callee-saved GPR |
| RndShaderMgr::SetTransform | no addr_reloc | r30↔r31 | Pure callee-saved GPR |

**Conclusion:** Must classify at the register level, not by co-occurring patterns. Plan's Option A (single pattern, split fixability) is correct.

## Step 2: Pattern-Free Function Audit

### Near-perfect (99.5-100%): 12 promoted to COMPLETE

All had 100% normalized match with only `diff_arg` (address relocations). Reported as COMPLETE:
- LightPreset::AnimateLightFromPreset, PropSync (Character), DrawPtrVec::Draw, Trie::delete_node, RndParticleSys::UpdateRelativeXfm, ChallengeSortMgr::GetChallengeRecordSongType, CharEyes::ForceBlink, CharEyes::SetEnableBlinks, Splash::Draw, XboxSessionJob::IsFinished, Character::SetInterestFilterFlags, HandRaisedGestureFilter::NewObject, Curl_resolv_unlock

### 90-99% pattern-free: mismatch taxonomy

| Function | Match | Root Cause | Existing Pattern? |
|----------|-------|------------|-------------------|
| Box::Volume | 58.3% | Offset swap + FPR volatile swap | OFFSET_SWAP + regswap |
| SfxInst::IsRunning | 97.6% | `cmplwi` vs `cmpwi` | signed_unsigned |
| Vector3Keys::SetFrame | 97.3% | Code reordering (replace+delete) | None — scheduling |
| TexMovie::Enter | 97.1% | `cmplwi` vs `cmpwi` + CR field | signed_unsigned + CR |
| UISlider::OnMsg | 92.4% | Offset shifts ±8 + insert/delete | declaration_reorder |
| RndShaderMgr::UpdateCache | 21.4% | 22 diff_arg, offset+regswap | Massive regswap |
| DataMod | 98.4% | 1 extra instruction insert | Dead store / extra move |
| InterpTangent | 95.7% | `fnmsubs`↔`fmsubs`, `fadds`↔`fsubs` | **NEW: FP sign equivalence** |

## Step 3: Permuter Gap Analysis

### 0-win patterns: Keep all three

| Pattern | Trials | Verdict | Rationale |
|---------|--------|---------|-----------|
| commutative_swap | 0/143 | Keep | Well-implemented, MSVC PPC insensitive to associativity |
| empty_size_swap | 0/38 | Keep | Correctly targets divw signal, just rare |
| ternary_swap | 0/10 | Keep | Too few trials to judge, pattern is correct |

### New pattern: FP sign equivalence

InterpTangent shows `fnmsubs` (negate-multiply-subtract) vs `fmsubs` (multiply-subtract). The compiler absorbs sign differently when subtraction operands are swapped. This IS a source-level fix but complex to automate in the permuter. Noted for manual fixes.

## Step 4: Implementation

### Change: `analysis.rs` — volatile/callee-saved register swap classification

Added `is_callee_saved_register()` helper. Updated `detect_register_swap()` to set fixability:
- Pure volatile swaps → `Fixability::Unfixable`
- Pure callee-saved swaps → `Fixability::MaybeFixable`
- Mixed → `Fixability::MaybeFixable`

Updated summary display to show `[volatile, unfixable]`, `[callee-saved, maybe fixable]`, or `[mixed volatile+callee-saved]`.

All 82 tests pass.

### Sync results

Full `--all -j16` sync after implementation:

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| COMPLETE | 8,380 | 9,927 | **+1,547** |
| AT_LIMIT | 21,565 | 22,253 | +688 |
| Workable | 2,071 | 2,035 | -36 |

- 5,634 functions promoted to COMPLETE (mostly from updated analysis detecting 100% matches)
- 36 functions auto-AT_LIMIT'd from volatile regswap + unfixable patterns
- Lower than projected 400-500 because most functions have some unattributed mismatches

### Why only 36 auto-AT_LIMIT?

The auto-AT_LIMIT path requires `unattributed_mismatches == 0` — ALL diff instructions must be explained by detected patterns. Most regswap+addr_reloc functions have 1-2 unexplained diff_args (shift semantics, branch targets) that prevent auto-AT_LIMIT. The conservative approach is correct; the classification is now in place as infrastructure for future improvements.

## Files Changed

| File | Change |
|------|--------|
| `../objdiff/objdiff-cli/src/cmd/analysis.rs` | Added `is_callee_saved_register()`, updated `detect_register_swap()` fixability, updated summary display |
