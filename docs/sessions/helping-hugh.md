ClosestMoveFrame Decomp Report

The Original Bug

The operator() signature was bool operator()(MoveFrame*, float) — wrong for std::min_element, which dereferences iterators and passes MoveFrame& to the comparator.
The fix: bool operator()(const MoveFrame&, const MoveFrame&).

Changes Made

1. HamMove.h: Added float GetBeat() const accessor to MoveFrame (since mBeat is private)
2. MoveDir.h: Added MoveFrame *ClosestMoveFrame() declaration (private)
3. MoveDir.cpp: Implemented ClosestMoveFrame() with the corrected FilterFrameDist comparator

Match Results
┌──────────────────────────────────────────┬─────────────┬───────────────────────────────────────────────────────────────────┐
│                 Function                 │    Match    │                               Notes                               │
├──────────────────────────────────────────┼─────────────┼───────────────────────────────────────────────────────────────────┤
│ min_element<MoveFrame*, FilterFrameDist> │ 100% (code) │ Identical instructions, but emitted under scope ?4 vs target's ?3 │
├──────────────────────────────────────────┼─────────────┼───────────────────────────────────────────────────────────────────┤
│ MoveDir::ClosestMoveFrame                │ 84.1%       │ See remaining diffs below                                         │
└──────────────────────────────────────────┴─────────────┴───────────────────────────────────────────────────────────────────┘
Remaining Diffs in ClosestMoveFrame (84.1%)

1. MSVC scope numbering (?3 vs ?4) — The FilterFrameDist struct gets scope index 4 in our build, but 3 in the target. The constructor body {} likely counts as an
extra scope. Removing the constructor isn't viable (needed for FilterFrameDist(value) syntax). This only affects the mangled symbol name of the min_element call,
not actual code.

2. Instruction scheduling — Target computes measure * 4 (slwi) BEFORE calling GetMoveFrames, while our code calls GetMoveFrames first. The compiler interleaves
operations differently based on source structure. Multiple variable orderings were tested; none reproduced the target's scheduling.

3. Register allocation — Minor swaps: r10 vs r11 for TheTaskMgr base, r31 vs r11 for the measure shift. Consequence of the scheduling difference.

4. Extra 8 bytes — Our build emits 2 extra instructions (b/li r3, 0x0) for the nullptr return path. The target optimizes these away because r3 is already 0 on the
early-exit path (null move pointer).

Current Code

MoveFrame *MoveDir::ClosestMoveFrame() {
  HamMove *move = mMovePlayerData[0].mCurMove;
  if (move) {
      struct FilterFrameDist {
          float mDist;
          FilterFrameDist(float dist) : mDist(dist) {}
          bool operator()(const MoveFrame &a, const MoveFrame &b) {
              return fabsf(a.GetBeat() - mDist) < fabsf(b.GetBeat() - mDist);
          }
      };
      int measure = TheTaskMgr.CurrentMeasure();
      float beat = TheTaskMgr.TotalBeat();
      std::vector<MoveFrame> &frames = move->GetMoveFrames();
      MoveFrame *result = std::min_element(
          frames.begin(), frames.end(),
          FilterFrameDist(beat - (float)(measure * 4))
      );
      return result != frames.end() ? result : nullptr;
  }
  return nullptr;
}

Notes for Friend's Code (debug_image2.png)

- return &*it without an end-check is wrong — min_element returns begin on empty ranges, and the target clearly has a branchless end-check (subf/subfic/subfe/and
pattern)
- const on operator() is fine — it doesn't affect the generated code for this compiler
- The operator() in their code uses MoveFrame* parameter types — still the original bug
