# Wave 25 Results — FileMerger floor+origin · honest native scoring · Xbox-stub lever empty

**Date:** 2026-06-23 · all-Opus, agentRetry, adversarially verified, native-gated.
Single ultracode workflow (5 agents): FileMerger 100%+origin ‖ scoring plan→impl→review ‖ Xbox-stub scout.

## Lane A — FileMerger::Merger::Clear: backend floor + bug-origin answered
**100% attempt:** NO — genuine MSVC backend floor at **98.4% normalized / 97.8% raw**. The 4
control-flow diffs (idx 102-104, 125) are a CR-field-allocation + loop-guard CSE/scheduling
decision: the target hoists `mLoadedSubdirs.size()==0` into a callee-stable CR field (cr6) that
dominates BOTH the `if(mergerDir)` RemoveSubDir loop and the `else` `clear()` loop (top-test);
ours recomputes per-branch and lowers `clear()` bottom-test. Permuter 0/58; 5 hand-variants all
regressed (clear()→explicit-loop 97.1, if/else invert 90.6, bool-cache 96.0, guard-hoist 97.2,
`size()!=0` identical). Source is **provably faithful** — byte-for-byte identical structure to the
independent og-dc3-decomp. Not source-fixable; nothing committed (worktree restored to baseline).

**Bug-origin (the user's questions), evidence-backed:**
- **Is it a decomp bug? NO.** The PPC `#else` drain `while(!empty) delete front()` is a faithful
  decomp match (98.4%, matches og-dc3).
- **Would the original Xbox game also hang? NO.** The infinite loop originates entirely in
  **native-only** code: `gInReplaceList` (declared inside `#ifdef HX_NATIVE`, Object.h:24-31) makes
  `ObjPtrList::ReplaceNode` *suppress* the node erase during a ring-walk (ObjPtr_p.h:458-470). On
  Xbox/PPC the erase is **unconditional** (ObjPtr_p.h:471-473 `#else`); confirmed independently —
  og-dc3-decomp has `if(!SetObj(obj) && mListMode==kObjListNoNull) erase(node)` with **no
  gInReplaceList** anywhere. The native engine sets `gInReplaceList=true` across its snapshot-based
  `ReplaceRefs` ring-walk (a workaround for native heap "corrupted double-linked list" corruption);
  while true the erase is suppressed, `mSize` never decrements, `front()` never advances → spin.
  The original binary has none of this, so its loop always terminates. The bug is a native-port
  artifact, correctly fixed on main (wave 24, `92b9ae13`) by the `#ifdef HX_NATIVE`
  pop_front-before-delete drain. **No further action needed.**

## Lane B — Native scoring TODOs: honest scoring + crash-safety (LANDED `1a4d0db0`)
Plan (`make_handler_crash_safe`) → impl → adversarial review (**CONFIRMED**). Real move detection
is **not** feasible now: native never instantiates `SkeletonUpdate` (only `LiveCameraInput`/NUI
does), so `MoveDir` is never a skeleton callback, `EnqueueDetectFrames` never runs, and
`DetectFrac` is genuinely ~0. Changes (all `#ifdef HX_NATIVE`, PPC byte-identical — Game::SetHamMove
/ GamePanel::Poll / GameModeInit all 100% normalized):
- **Removed the fake-autoplay-points hack** (was awarding 100-500 pts/beat into the *real* provider
  `score` property AND HUD/screenshots — actively misleading for Xbox/native validation). Native
  score is now honestly **0**.
- **Crash-safety:** default `gameplay_mode` on TheGameMode (`GameMode.cpp:251`) so
  `MetaPerformer::OnMovePassed`'s `TheGameMode->Property(gameplay_mode)` (fail=true) can't
  `MILO_FAIL_DTA`.
- **Real `move_passed` path re-enabled but gated behind `DC3_REAL_MOVE_PASSED`** (off by default):
  the implementer empirically found driving the full DTA handler every beat destabilizes the move
  graph (activeMoveCount 2→0) and intermittently null-derefs `SymbolKeys::SetFrame` (song-anim
  PropAnim). Honest deviation from the plan's "enable unconditionally," documented for follow-up.

Gates: build clean; **418/418** serial (reconfirmed in main's real build env); orchestrator headless
gameplay smoke (sandbox-disabled) ran **2000 frames "engine stable", exit 0, no crash**; score
HTTP-verified == 0. **Next lever** (deferred): root-cause why feeding `move_passed` back collapses
activeMoveCount, under `DC3_REAL_MOVE_PASSED=1`.

## Lane C — High-relevance Xbox stubs: EMPTY (confirmed)
Read-only scout, three converging lines of evidence: (1) a runtime `DC3_STUB_TRACE` of a full
5000-frame boot→gameplay run hits only 5 distinct stubs — all no-op (OutputDebugStringA),
already-implemented (vorbis_synthesis_poll → system libvorbis), or Kinect/devkit one-shots; (2)
static disassembly — all 41 live `engine_stubs_generated.cpp` stubs are bare `xor eax,eax;ret`, all
pure Xbox domain (D3D9/XAudio/XNet/JPEG/Bink/NUI); the listed PropSync<ObjPtrVec> "stubs" are dead
(strong template wins); (3) the prompt's own candidates (MemcardMgr/ContentMgr/DateTime) are already
complete native impls. **The native-relevant stub lever is empty** — matches the wave-24 finding.

## Loop status
The game+engine native-correctness lever is now thoroughly characterized and largely spent:
FileMerger (the one real bug) fixed + proven not-original-affecting + matching-floored; the fake
scoring hack replaced with honest behavior + crash-safety; the Xbox-stub lever empirically empty.
The remaining game+engine work is deep single-investigation: (1) the `DC3_REAL_MOVE_PASSED` move-graph
instability (gateway to real native scoring), (2) IK deep-fidelity (user-deferred). Both are
root-cause investigations, not wave-shaped.
