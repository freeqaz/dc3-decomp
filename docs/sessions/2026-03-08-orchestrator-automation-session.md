# Session: Orchestrator Automation & Permuter Integration

**Date**: 2026-03-08
**Goal**: Run orchestrator single runs with multiple models, integrate permuter, automate remaining decomp work

## Current State

| Category | Count |
|----------|-------|
| Total functions | 51,009 |
| Complete (100%) | 29,770+ |
| AT_LIMIT | 3,636+ |
| Near-complete (95-99%, fixable) | 185 |
| Medium (80-95%, fixable) | 392 |
| Low (50-80%, fixable) | 57 |
| Stubs (0%) | ~5 |
| Worktrees available | 20 |

## Plan

### Phase 1: Orchestrator Testing (Models)
- **Blocked**: Nested Claude Code sessions cannot make API connections. All agent runs (Haiku, Sonnet, GLM-5) failed with ConnectionRefused.
- **Resolution**: Must run orchestrator externally, not from within Claude Code.

### Phase 2: Permuter Integration [DONE]
- `./bin/orchestrate permute <symbol>` command added
- Invokes hill_climber with BSF-guided pattern selection
- Reports results to orchestrator DB
- Fixed bugs: `milo_log_swap.py` attribute error, `HillClimbResult.best_percent` → `.final_percent`

### Phase 3: Direct Decomp Work [IN PROGRESS]
Manual analysis and fixes using MCP tools:

### Phase 4: Native Port Stubs
- Only 5 stubs remain (down from ~50)
- Key stubs: OnComputeCharWidths (62.9%), WordWrap_CanBreakLineAt (55.8%)

## Functions Fixed to 100% This Session

| Function | Match | Fix Applied |
|----------|-------|-------------|
| `SampleFree` | 99.9→100% | Added 17 blank lines to fix `__LINE__` offset (17→34) |
| `SongMgr::CachedPath` | 99.9→100% | Fixed operator precedence: `data && (IsOnDisc \|\| Version_check)` - also a null deref bug fix |
| `MoggClip::SetVolume` | 99.8→100% | Swapped mControllerVolume/mVolume declaration order in header |
| `ChallengeSortNode::GetChallengeExp` | 99.7→100% | Removed virtual from ChallengeRecord destructor (no vtable in target) |

## Functions Marked AT_LIMIT

| Function | Match | Reason |
|----------|-------|--------|
| `RndTransformable::SetLocalRotIndex` | 99.9% | FPR volatile regswap f9↔f13 |
| `ChallengeSortMgr::GetChallengeScore` | 99.7% | Volatile regswap r10↔r11 |
| `ChunkStream::DecompressChunk` | 99.7% | Volatile regswap r11↔r5 |
| `DetectFrame::LimbPSNR` | 99.7% | Volatile FPR regswap f0↔f31 |
| `SfxInst::IsRunning` | 98.5% | cmplwi vs cmpwi for inline pointer return |
| `ReadError` | 99.6% | && vs nested if branch target |
| `HamVisDir::~HamVisDir` | 99.99% | STLPort vector layout |

## Patterns Discovered

1. **`__LINE__` fix**: Add blank lines to shift `__LINE__` macro values to match target
2. **Operator precedence**: Complex boolean conditions need careful parenthesization
3. **Virtual destructor vtable**: Removing unnecessary `virtual` from destructors removes vtable pointer, shifting all member offsets by -4
4. **Member declaration order**: Header member order directly affects struct layout offsets
5. **cmplwi vs cmpwi**: Inline pointer getter returns generate `cmpwi` (signed). `(unsigned int)cast > 0` generates `cmplwi+bgt` but NOT `cmplwi+bne`. No source-level fix for `cmplwi+bne`.

## Background Agents Running

- MemResizeElem (93.2%) - control flow + regswap analysis
- HDCache::ReadDone (97.0%) - analysis and fix attempt
- FileGetStat (96.5%) - analysis and fix attempt
- LoadingPanel::IsLoaded (96.9%) - permuter run
- Challenges::GetTotalXpEarned (98.2%) - permuter run
- IsValid_AOReceive (91.2%) - permuter run
