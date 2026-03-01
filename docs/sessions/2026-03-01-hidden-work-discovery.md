# Hidden Work Discovery (2026-03-01)

## Problem

`query_functions(status='workable')` returned 0, but ~400 functions need real work. They're hidden behind stale COMPLETE/AT_LIMIT verdicts.

## Root Cause

`sync_match_percent.py` updates match percentages but **never demotes verdicts**. When the `base_size=0` objdiff bug inflated percentages (0/0 = 100%), many functions got marked COMPLETE. After the objdiff fix, percentages dropped but verdicts stayed, hiding them from workable queries.

## Scale

| Category | Count | Description |
|----------|------:|-------------|
| COMPLETE < 10% | 17 | Likely stubs or missing implementations |
| COMPLETE 10-30% | 19 | Mostly wrong or placeholder code |
| COMPLETE 30-50% | 29 | Partially implemented |
| COMPLETE 50-70% | 100 | Significant code issues |
| COMPLETE 70-80% | 152 | Probably real code bugs (like op34) |
| COMPLETE 80-90% | 411 | Mix of fixable and compiler artifacts |
| COMPLETE 90-95% | 1,008 | Mostly compiler artifacts |
| AT_LIMIT < 60% | 25 | Gave up too early |
| **Missing implementations** | **3** | Target-only, no decomp symbol |

## Key Finding: op34 Pattern

During this session, `op34` (ByteGrinder) was at 95.7% with a logic bug: source had `(w ^ 0x3F)` but target does `((w >> 2) ^ 0x3F)`. Fixed to 100%. This proves sub-100% COMPLETE functions can have real fixable bugs.

## Worst Offenders (COMPLETE < 50%)

```
  0.0% | FileGetPath, FileGetDrive, FileGetBase (missing implementations)
  0.2% | SetBloomBlurWeightsStreak (712B)
  0.3% | FileLocalize (564B)
  1.4% | MemAlloc (1172B)
  1.6% | StorePanel::OnMsg (652B)
  7.3% | UIListDir::BuildDrawState (1476B)
 15.6% | ClipCollide::Collide (1036B)
 16.6% | FlowMathOp::Load (568B)
 23.9% | Trie::store (576B)
 25.2% | UIListLabelElement::Draw (744B)
 27.5% | FlowManager::Poll (1820B)
 30.6% | HamStorePanel::CreateCartUIs (1532B)
 30.7% | DrivenPropertyEntry::Load (480B)
 31.9% | RndGroup::DrawShowing (588B)
 47.7% | StorePanel::Poll (2084B)
 48.9% | UIScreen::Enter (744B)
```

## Detection Tool

Created `scripts/find_hidden_work.py`:
```bash
python3 scripts/find_hidden_work.py              # report
python3 scripts/find_hidden_work.py --demote 80   # demote COMPLETE < 80% to workable
python3 scripts/find_hidden_work.py -v             # verbose (show missing implementations)
```

## Pipeline Fix Needed

`sync_match_percent.py` should optionally demote stale verdicts. When `current_percent` drops significantly below the threshold at which a function was marked COMPLETE, the verdict should be cleared.

## How Functions Become Invisible

```
1. base_size=0 bug → objdiff reports 0/0 = 100%
2. batch_check or report_result marks function COMPLETE
3. objdiff fix applied → percentage drops to real value (e.g., 15%)
4. sync_match_percent updates percentage but keeps COMPLETE verdict
5. query_functions(status='workable') excludes COMPLETE → function invisible
```
