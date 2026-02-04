# Decompilation Work Session Notes

This document tracks progress from active decompilation sessions and provides context for future work.

## Quick Links

- **[Gap Analysis](decomp/GAP_ANALYSIS.md)** - Strategic guide: where to invest effort for maximum progress
- [Low-Hanging Fruit](decomp/LOW_HANGING_FRUIT.md) - Prioritized easy function targets
- [RB3 Reference Guide](decomp/RB3_REFERENCE.md) - Shared code between DC3 and RB3
- [Technical Notes](decomp/TECHNICAL_NOTES.md) - Compiler patterns and lessons learned
- [Subagent Strategy](decomp/SUBAGENT_STRATEGY.md) - How to use parallel AI agents effectively
- [objdiff CLI Usage](OBJDIFF_CLI_USAGE.md) - Instruction-level diff commands for diagnosing near-matches
- [objdiff Learnings](OBJDIFF_LEARNINGS.md) - Deep patterns, fixability analysis, diagnosis workflows
- [Ghidra + MCP](tools/GHIDRA.md) - Binary analysis via pyghidra-mcp
- [Tools Index](tools/INDEX.md) - All tools and quick commands

---

## Work Sessions

Individual session notes are archived below. **Most recent sessions are most relevant** - older sessions become less important as patterns are captured in other docs.

| Date | Focus | Key Wins | Link |
|------|-------|----------|------|
| 2026-01-23 | RndMat::SyncProperty RE | SyncProperty 79→96.7%, binary string extraction, Ghidra | [Session](sessions/2026-01-23-rndmat-syncproperty.md) |
| 2026-01-23 | Research Agents + CharClip | CharClip::Load 28→76%, CSHA1 bug fix, 8 parallel agents | [Session](sessions/2026-01-23-research-agents-charclip.md) |
| 2026-01-23 | Ghidra MCP Workflow | 4 fixes (StrHash, Pool, vector dtor), Ghidra integration | [Session](sessions/2026-01-23-ghidra-mcp-workflow.md) |
| 2026-01-23 | Triage + BustAMove | 2 new 100%s, all 7 near-matches FIXABLE, BAM 33→35% | [Session](sessions/2026-01-23-triage-bustamove.md) |
| 2026-01-23 | Parallel Opus Workflow | PostWaitJump 88→99%, 13 units identified for closeout | [Session](sessions/2026-01-23-parallel-opus-workflow.md) |
| 2026-01-23 | Types & Templates | 9 new 100%s, bitwise formulas, type fixes | [Session](sessions/2026-01-23-types-templates.md) |
| 2026-01-23 | String & Utility | 4 new 100%s, Pool::Alloc bug fix, sizeof pattern | [Session](sessions/2026-01-23-string-pool-fixes.md) |
| 2026-01-23 | Gap Analysis | GAP_ANALYSIS.md created, doc restructure | [Session](sessions/2026-01-23-gap-analysis.md) |
| 2026-01-23 | Character & Math | FindLipSyncForSound 100%, ASSERT_REVS pattern discovered | [Session](sessions/2026-01-23-char-math-focus.md) |
| 2026-01-23 | objdiff deep dive | MatShaderFlagsOK 95→98%, fixability patterns doc | [Session](sessions/2026-01-23-continued.md) |
| 2026-01-23 | Render system (rndobj) | 5 new 100% matches, objdiff CLI diagnosis | [Session](sessions/2026-01-23-render-focus.md) |
| 2026-01-22 | RB3 reference expansion | FastSin 100%, Reset 100%, RB3 overlap research | [Session](sessions/2026-01-22-rb3-reference.md) |
| 2026-01-22 | Parallel subagents | Shuttle 100%, 15+ functions improved | [Session](sessions/2026-01-22-parallel-subagents.md) |
| 2025-01-22 | GameMode fixes | GameModeInit 100%, FillModeArrayWithParentData 100% | [Session](sessions/2025-01-22-earlier.md) |
| 2025-01 | Environment setup | Build system, tools, references | [Session](sessions/2025-01-setup.md) |

---

## Current Game Code Status

From `build/373307D9/report.json`:

| File | Match % | Functions | Notes |
|------|---------|-----------|-------|
| HamUserMgr.cpp | 100% | 24/24 | Complete |
| LiveInput.cpp | 100% | 7/7 | Complete |
| Shuttle.cpp | 100% | 3/3 | **Complete (this session)** |
| SongDB.cpp | 86.4% | 4/5 | 1 merged function |
| HamUser.cpp | 86.1% | 23/30 | 7 merged functions |
| PresenceMgr.cpp | ~99% | 13/14 | GetPresenceMode at 99.5% |
| GameMode.cpp | 58.3% | 13/18 | SetMode at 99.6% |
| Game.cpp | 58.1% | 56/81 | Large file, many functions |
| GamePanel.cpp | 50.7% | 60/80 | UI/game integration |
| SongSequence.cpp | 43.6% | 22/27 | `DoNext()` stub (14%), `OnSongLoaded` 93% (quick fix available) |
| BustAMovePanel.cpp | 34.9% | 43/76 | +2 funcs this session |
| PartyModeMgr.cpp | - | -/87 | Largest file (1332 lines) |

---

## Next Goals

### Completed (All Sessions) ✓

| File | Function | Status |
|------|----------|--------|
| Trig.cpp | `FastSin` | **100%** ✓ |
| Interp.cpp | `Reset(DataArray*)` | **100%** ✓ |
| GameMode.cpp | `IsGameplayModePerform` | Implemented ✓ |
| PostProc.cpp | `RndPostProc::UpdateTimeDelta` | **100%** ✓ |
| Rnd.cpp | `Rnd::UpdateOverlay` | **100%** ✓ |
| Rnd.cpp | `Rnd::SetPostProcOverride` | **100%** ✓ |
| Trans.cpp | `RndTransformable::Handle` | **100%** ✓ |
| Font.cpp | `KerningTable::SetKerning` | **100%** ✓ |
| Shader.cpp | `RndShader::MatShaderFlagsOK` | 95.3% → **98.2%** ✓ |
| CharLipSync.cpp | `FindLipSyncForSound` | **100%** ✓ |
| Decibels.cpp | `RatioToDb` | 76.6% → **90.7%** (partial) |
| Str.cpp | `String::operator==(FixedString)` | **100%** ✓ |
| Str.cpp | `String::operator==(Symbol)` | **100%** ✓ |
| Pool.cpp | `Pool::Alloc` | **100%** ✓ (bug fix!) |
| Mesh.cpp | `RndMesh::EstimatedSizeKb` | **100%** ✓ |
| CharClip.cpp | `CharClip::BeatToSample` | 96.5% → **96.7%** (partial) |
| PoolAlloc.cpp | `ReclaimableAlloc::ReclaimableAlloc` | **100%** ✓ (bitwise formula) |
| AppChild.cpp | `AppChild::AppChild` | **100%** ✓ (mPort type fix) |
| BinStream.h | `operator>>(map)` (3 instantiations) | **100%** ✓ (unsigned counter) |
| Char.cpp | `CharUpperTwist::operator new/delete` | **100%** ✓ (line number fix) |
| TexProc.cpp | `TexProc::operator new/delete` | **100%** ✓ (line number fix) |
| BustAMovePanel.cpp | `PollCaptureFlashcard` | **100%** ✓ (360 bytes) |
| BustAMovePanel.cpp | `QueueMovePromptVO` | **100%** ✓ (204 bytes) |
| Shader.cpp | `StrHash` | **100%** ✓ (unsigned char loop) |
| GameMode.cpp | `SetMode` | **100%** ✓ (scoped local var) |
| Pool.cpp | `Pool::Free` | **100%** ✓ (free-list bug fix) |
| DetectFrame.cpp | `vector<DetectFrame>::~vector` | **100%** ✓ (Vector3Pad padding) |
| CharClip.cpp | `CharClip::Load` | 28% → **76%** ✓ (RB3 reference, +48pp) |
| SHA1.cpp | `CSHA1::Transform` | Bug fix ✓ (uninitialized m_block pointer) |
| Mat.cpp | `RndMat::SyncProperty` | 79% → **96.7%** ✓ (binary RE, +17.7pp, at compiler limit) |

### Confirmed at Compiler/Linker Limit (via objdiff CLI)

These were investigated with instruction-level diffs and confirmed unfixable:

| File | Function | Match | Diagnosis |
|------|----------|-------|-----------|
| Trans.cpp | `RndTransformable::Copy` | 99.04% | Merged function calls |
| Dir.cpp | `RndDir::SyncObjects` | 99.89% | Merged templates |
| Dir.cpp | `RndDir::PreLoad` | 99.72% | Instruction scheduling |
| Dir.cpp | `RndDir::OldLoadProxies` | 99.45% | `__FILE__` macro |
| Lit.cpp | `RndLight::Save` | 99.96% | Merged function calls |
| Anim.cpp | `RndAnimatable::Load` | 99.93% | At limit |
| Anim.cpp | `RndAnimatable::OnAnimate` | 99.03% | At limit |
| **system/char Load functions** | Multiple (20+) | 99%+ | **ASSERT_REVS linker limit** |

**ASSERT_REVS Pattern:** All Load functions using the `ASSERT_REVS` macro are blocked at 99%+ due to:
1. Static `gRevs[4]` array gets generic linker label instead of mangled symbol
2. Argument evaluation order differs in MILO_FAIL calls
3. Shows as `diff_arg` in objdiff (unfixable at source level)
| Line.cpp | `RndLine::Load` | 99.82% | At limit |
| Line.cpp | `RndLine::Handle` | 99.07% | At limit |
| AmbientOcclusion.cpp | `GatherObjectsFromDir` | 99.88% | At limit |
| AmbientOcclusion.cpp | `RndAmbientOcclusion::Load` | 99.78% | At limit |
| Mat.cpp | `RndMat::SyncProperty` | 96.7% | Tail merging, instruction scheduling (was 79%) |
| Mat.cpp | `RndMat::LoadOld` | 97.0% | Linker-merged Read*Struct functions |
| Mat.cpp | `RndMat::GetRefractEnabled` | 97.1% | Compiler bool return masking |
| Group.cpp | `RndGroup::Load` | 98.4% | Register allocation (r26↔r27) |
| Geo.cpp | `Box::Clamp` | 99.5% | OR chain register allocation |
| complex.cpp | `complex::operator*` | 99.3% | fmadd operand commutativity |
| FlowSwitch.cpp | `FlowSwitch::ActivateTransitionCases` | 99.4% | Register allocation (r10↔r11) |
| PresenceMgr.cpp | `GetPresenceMode` | 99.44% | Symbol labels + linker-merged calls |
| Geo.cpp | `Box::Volume` | 98.83% | Load instruction scheduling (y,z,x order) |
| Utl.cpp | `PageDirection` | 98.75% | Register allocation (r10↔r11) |
| DoubleExponentialSmoother.cpp | `Vector2DESmoother::ForceValue` | 99% | Store instruction scheduling |
| MemStream.cpp | `MemStream::ReadImpl` | 96.56% | Register allocation (r7 vs r10) |
| socks.c | `Curl_SOCKS5` | 99.15% | Constant caching in callee-saved register |
| Task.cpp | `TaskTimeline::ClearTasks` | 95.7% | cr0 vs cr6 condition register |

### Render Functions - Recent Analysis

| File | Function | Match | Status | Diagnosis |
|------|----------|-------|--------|-----------|
| Shader.cpp | `RndShader::MatShaderFlagsOK` | **98.2%** | Improved | Combined if conditions |
| Mat.cpp | `RndMat::SyncProperty` | **96.7%** | Improved | 79→96.7%, binary RE (+17.7pp) |
| Mat.cpp | `RndMat::LoadOld` | 97.0% | At limit | Linker-merged functions |
| Mat.cpp | `RndMat::GetRefractEnabled` | 97.1% | At limit | Compiler bool handling |
| Group.cpp | `RndGroup::Load` | 98.4% | At limit | Register allocation |

See [OBJDIFF_LEARNINGS.md](OBJDIFF_LEARNINGS.md) for detailed diagnosis patterns and fixability analysis.

### Triaged as FIXABLE (not at linker limit)

These were analyzed with `objdiff-cli --verdict` and confirmed fixable:

| Function | Match | File | Verdict | Status |
|----------|-------|------|---------|--------|
| `GameMode::SetMode` | **100%** | GameMode.cpp | MAYBE_FIXABLE | **FIXED** ✓ |
| `RndMesh::Handle` | **98.77%** | Mesh.cpp | LIKELY_FIXABLE | Improved (+1.4%) |
| `ShaderOptions::GenerateMacros` | 97.28% | Shader.cpp | LIKELY_FIXABLE | Blocked (STL inlining) |
| `Spotlight::SyncProperty` | 98.7% | Lit.cpp | MAYBE_FIXABLE | Pending |
| `InterpTangent` | 82.9% | Key.cpp | LIKELY_FIXABLE | Pending |
| `QuatSpline` | 71.4% | Key.cpp | LIKELY_FIXABLE | Partial fix |
| `RndParticleSys::SyncProperty` | 98.6% | Part.cpp | NEEDS_INVESTIGATION | Pending |

### Recently Fixed

| Function | Before | After | Fix |
|----------|--------|-------|-----|
| `Pool::Pool` | 72% | **99.5%** | Rewrote free list init loop (do-while, proper chain) |
| `MultiTempoTempoMap::AddTempoInfoPoint` | 87.88% | **100%** | `empty()` → `size() == 0` (different codegen) |
| `FixedSizeAlloc::Alloc` | 81.75% | **100%** | Fixed broken allocator logic, load ordering |
| `FixedSizeAlloc::Free` | 89% | **99.82%** | Fixed free list chain linkage |
| `MoveRatingHistory::AddHistory` | 82.22% | **87.44%** | Preserve old value in history slots |
| `ExternalMic::ExternalMic` | 94.19% | **99.71%** | Pass function ptr (was calling instead) |
| `GameMode::SetMode` | 99.0% | **100%** | Scoped local var for instruction scheduling |
| `RndMesh::Handle` | 97.4% | **98.77%** | `CopyBones(nullptr)` instead of `mBones.clear()` |
| `StrHash` | 84.7% | **100%** | `const unsigned char*` loop (avoids extsb) |
| `Pool::Free` | 83% | **100%** | Missing free-list head update (bug fix!) |
| `Pool::Alloc` | ~85% | **100%** | Follow free-list pointer |
| `vector<DetectFrame>::~vector` | 99.9% | **100%** | Vector3Pad had duplicate padding |
| `QuatSpline` | 70.1% | **71.4%** | Horner's method for polynomial |
| `RatioToDb` | 90.8% | **100%** | Static const float zero pattern |
| `Game::PostWaitJump` | 88.9% | **99.93%** | Added unk28 conditional check |
| `StringTable::Add` | 99.31% | **100%** | Reused str param for return value |
| `EventEntry::Add` | 95% | **100%** | MaxEq() + operation reorder |

### At Compiler Limit (~99.9%)

These are functionally correct; remaining diff is metadata/codegen:

| Function | Match | File |
|----------|-------|------|
| `DataNode::Save` | 99.88% | DataNode.cpp |
| `SpeechMgr::Enable` | 99.90% | SpeechMgr.cpp |
| `PartyModeMgr::PartyModeMgr` | 99.81% | PartyModeMgr.cpp |
| `GamePanel::ReloadData` | 99.87% | GamePanel.cpp |

### Units Ready for Closeout (99.9%+, all remaining AT_LIMIT)

These units have only arg-difference mismatches that cannot be fixed:

| Unit | Match | Remaining Funcs |
|------|-------|-----------------|
| `system/zlib/inflate` | 99.99% | 1 |
| `system/net/curl/lib/dict` | 99.99% | 1 |
| `system/net/curl/lib/gopher` | 99.99% | 1 |
| `system/net/curl/lib/http_digest` | 99.99% | 3 |
| `lazer/meta_ham/AccomplishmentSongConditional` | 99.98% | 1 |
| `system/utl/SongInfoAudioType` | 99.98% | 1 |
| `lazer/meta_ham/Accomplishment` | 99.97% | 1 |
| `system/net/curl/lib/content_encoding` | 99.94% | 4 |
| `system/net/curl/lib/rawstr` | 99.94% | 1 |
| `system/net/curl/lib/fileinfo` | 99.92% | 2 |
| `system/net/curl/lib/base64` | 99.92% | 1 |
| `system/meta/Jukebox` | 99.76% | 1 |
| `system/utl/Option` | 99.64% | 1 |

### Large Undertakings (Future)

| Target | Effort | Notes |
|--------|--------|-------|
| Geo.cpp | Large | 57 missing geometric primitives |
| mtx.cpp | Large | Matrix4::Invert is 2800 bytes |
| system/char/ | Large | 77 files, all 0%, has RB3 reference |
| system/rndobj/ | In Progress | 85 files, 178 near-matches identified |

---

## Useful Commands

```bash
# Build single file
ninja build/373307D9/src/lazer/game/PresenceMgr.obj

# Generate report
ninja build/373307D9/report.json

# Check specific function match %
cat build/373307D9/report.json | python3 -c "
import json, sys
data = json.load(sys.stdin)
for unit in data.get('units', []):
    if 'PresenceMgr' in unit.get('name', ''):
        for fn in unit.get('functions', []):
            pct = fn.get('fuzzy_match_percent', 0)
            name = fn.get('name', '')[:50]
            print(f'{pct:5.1f}% - {name}')
"

# Find function in target assembly
grep -n "FunctionName" build/373307D9/asm/lazer/game/PresenceMgr.s

# Search RB3 for reference
grep -rn "GetPresenceMode" ~/code/milohax/rb3/src/

# Format before commit
clang-format -i src/lazer/game/PresenceMgr.cpp
```

---

## Contributing

After making changes:
1. Run `clang-format -i <file>` on modified files
2. Build and check match percentage
3. Test with `ninja` to ensure no regressions
4. Reference this doc for context on past work
