# Pattern Analysis: HEAD~3 Decomp Changes

**Date**: 2026-03-05
**Commits**: `f4bd851e6` → `2251355c7` → `35b046f3f`
**Scope**: 71 files, +1284/-404 lines across `src/`

## Executive Summary

Analysis of the last 3 commits reveals **~86 discrete decomp-relevant changes** across 65 source files. The dominant pattern categories by frequency are:

1. **Control flow restructure** (~20 instances) — if/else inversions, `&&` to nested if, ternary rewrites
2. **Missing function calls / logic** (~20 instances) — new code paths, calls to existing methods
3. **Variable declaration reorder** (~11 instances) — callee-saved register allocation tuning
4. **Expression rewrite** (~10 instances) — arithmetic term reordering, subtraction-to-addition forms
5. **Type/cast correction** (~8 instances) — `(char *)`, `(float)`, `(unsigned int)` casts
6. **Null check removal** (~5 instances) — removing redundant pointer checks
7. **Temp variable extraction** (~5 instances) — hoisting subexpressions to named locals
8. **Comparison inversion** (~4 instances) — `a >= b` ↔ `b <= a`, `a < b` ↔ `b > a`
9. **Rename/documentation** (~10 instances) — `unk124` → `mOverrideBlendStartTime`, etc.

---

## Pattern Details (Permuter-Relevant)

### 1. Variable Declaration Reorder
**Frequency**: 11 instances | **Automatability**: HIGH (permuter candidate)

Reordering local variable declarations changes callee-saved register assignment (first decl → r31, second → r30, etc.).

**Examples**:
```cpp
// CharMirror::Poll — static symbol reorder
-    static Symbol mirror_x("mirror_x");
-    static Symbol x("x");
+    static Symbol xy("xy");
+    static Symbol x("x");
+    static Symbol mirror_x("mirror_x");

// HamRegulate::Poll — float before Vector3
-    Vector3 posDelta(0, 0, 0);
-    float rotDelta = 0.0f;
+    float rotDelta = 0.0f;
+    Vector3 posDelta(0, 0, 0);

// CharUpperTwist — member init order (header reorder)
-    CharUpperTwist() : mTwist1(this), mTwist2(this), mUpperArm(this) {}
+    CharUpperTwist() : mUpperArm(this), mTwist1(this), mTwist2(this) {}

// CharLipSync::Generator::RemoveViseme — int/ptr interleave
-    int i = 0;
-    CharLipSync *lipSync = mLipSync;
     int cur = 0;
+    CharLipSync *lipSync = mLipSync;
+    int i = 0;

// CharLipSyncDriver::Poll — ptr before char*
-    char *lsName2;
     CharLipSync *ls2 = mMainPlayback->mLipSync;
+    char *lsName2;

// CharEyes::SetFocusInterest — hoist comparison before assignment
+    bool changed = interest != mFocusInterest;
     mFocusInterest = interest;
```

**Permuter rule**: Enumerate all permutations of local variable declarations within a scope. For N declarations, test N! orderings (with pruning for declarations that have dependencies).

**Caveat**: Only affects callee-saved registers (r13-r31, f14-f31). Volatile register swaps (r0-r12) are NOT fixable this way. The permuter should verify which registers are mismatched before attempting.

---

### 2. Comparison Inversion / Operand Swap
**Frequency**: 10 instances | **Automatability**: HIGH (trivially automatable)

Swapping comparison operands or inverting the sense of a comparison.

**Examples**:
```cpp
// CharLookAt::Poll — operand swap
-    if (srcRad * srcRad < filterSq) {
+    if (filterSq > srcRad * srcRad) {

// CharLipSyncDriver::Poll — >= to <=
-    if (unkc4 >= 1.0f) {
+    if (1.0f <= unkc4) {

// CharLipSyncDriver::Poll — <= to >=
-    if (remainingWeight <= 0.0f)
+    if (0.0f >= remainingWeight)

// SkeletonUpdate — unsigned cast for > 0
-    if (unk5388 > 0) {
+    if ((unsigned int)(unsigned int)unk5388 > 0) {

// AmbientOcclusion — j < 4 to j <= 3
-    for (int j = 0; j < 4; j++) {
+    for (int j = 0; j <= 3; j++) {

// BinkMovieImpl — int to unsigned int comparison
-    for (int j = 0; j < 2; j++) {
+    for (int j = 0; (unsigned int)j < 2; j++) {
```

**Permuter rule**: For each comparison `a op b`, try `b inv(op) a`. For `< N`, try `<= N-1`. For `> 0` with unsigned types, try `!= 0`. For signed loop vars compared against constants, try `(unsigned int)i < N`.

---

### 3. Temp Variable Extraction / Inlining
**Frequency**: 8 instances | **Automatability**: MEDIUM

Extracting a subexpression into a named temporary, or inlining a temporary back.

**Examples**:
```cpp
// CharMirror::Poll — hoist method call
+    auto _tmp0 = mBones.TotalSize();
     float w = Weight();
-    if (w == 0.0f || mBones.TotalSize() == 0)
+    if (w == 0.0f || _tmp0 == 0)

// HamRegulate::Poll — hoist virtual call
+    auto _tmp0 = mCharacter->Teleported();
-    if (!TheLoadMgr.EditMode() || mCharacter->Teleported() || absDt != 0.0f) {
+    if (!TheLoadMgr.EditMode() || _tmp0 || absDt != 0.0f) {

// CharBonesSamples::Save — split bool init
-    bool cached = bs.Cached() && (...);
+    auto _tmp1 = bs.Cached();
     int delta = 0;
+    bool cached = _tmp1 && (...);

// DisplayEvents — extract Rect to temp
+    auto _tmp0 = Hmx::Rect(start14c, f1 + 2.0f, ...);
     TheRnd.DrawRect(
-        Hmx::Rect(start14c, f1 + 2.0f, ...),
+        _tmp0,

// CharLookAt::Poll — extract acos to temp
+    float acosDeg = (float)std::acos(clamped) * RAD2DEG;
     float autoWeight = Clamp<float>(
         0.0f, 1.0f,
-        mMaxWeightYaw - (std::acos(clamped) / (mMaxWeightYaw - mMinWeightYaw))
+        (mMaxWeightYaw - acosDeg) / (mMaxWeightYaw - mMinWeightYaw)
     );

// StubCameraInput — extract array subscript
+    auto& _sub1 = unk11d4.mSkeletonDatas[i];
-    StubSkeletonData(unk11d4.mSkeletonDatas[i], ...);
+    StubSkeletonData(_sub1, ...);

// CharLipSync::RemoveViseme — extract vector ref
+    std::vector<unsigned char> &data = lipSync->mData;
-    int count = lipSync->mData[cur++];
+    int count = data[cur++];
```

**Permuter rule**: For each function call or member access `obj->Method()` used in a compound expression (especially `&&`/`||` or function args), try extracting to a temp before the expression. Also try inlining existing temps. The key insight: **extracting changes when the call is evaluated relative to other operations**, which affects register pressure and spill decisions.

---

### 4. Null Check Removal
**Frequency**: 5 instances | **Automatability**: HIGH

Removing null pointer checks that the target doesn't have.

**Examples**:
```cpp
// MetaPanel — 5 removals of TheMetaMusic checks
-    if (!TheMetaMusic && sHamMaster) {
+    if (!TheMetaMusic) {

-    if (TheMetaMusic)
-        TheMetaMusic->AddFader(_tmp0);
+    TheMetaMusic->AddFader(_tmp0);

// FlowAnimate — remove redundant check before delete
-    if (mAnimTask) {
-        AnimTask *task = mAnimTask;
-        if (task) {
-            task->mAnimTarget = NULL;
-        }
+    AnimTask *task = mAnimTask;
+    task->mAnimTarget = NULL;
```

**Permuter rule**: For each `if (ptr)` guard where `ptr` is immediately dereferenced unconditionally in the else branch or after the block, try removing the guard. Also try removing the second condition in `if (a && b)` → `if (a)`.

---

### 5. `&&` to Nested If / Control Flow Restructure
**Frequency**: ~15 instances | **Automatability**: MEDIUM

Converting `&&`/`||` chains to nested if statements, or restructuring early-return patterns.

**Examples**:
```cpp
// CharLipSyncDriver::Poll — early return to nested if
-    if (!mClips) return;
-    if (!mBones) return;
-    if (mTestClip && TheLoadMgr.EditMode()) {
+    if (mClips) {
+        if (mBones) {
+            if (mTestClip) {
+                if (TheLoadMgr.EditMode()) {
                     ...
+                }
+            }
+        } else return;
+    } else return;

// CharLipSyncDriver::Poll — && to nested if
-    if (!mIsOverrideActive && mMainPlayback) {
+    if (!mIsOverrideActive) {
+        if (mMainPlayback) {

// CharLipSyncDriver::Poll — bool to unsigned char
-    bool skipOverride = false;
-    if (mMainPlayback && mMainPlayback->mLipSync && cam) {
+    unsigned char skipOverride;
+    if (!mMainPlayback || !mMainPlayback->mLipSync || !cam) {
+        skipOverride = 0;
+    } else {
         ...
+        skipOverride = 0;
         if (name && strncmp(name, "battle_", 7) == 0) {
-            skipOverride = true;
+            skipOverride = 1;
         }
     }

// SkeletonViz::Visualize — if/else to ternary
-    if (unk218) {
-        worldXfm = WorldXfm();
-    } else {
-        worldXfm = unk1d4;
-    }
+    worldXfm = unk218 ? WorldXfm() : unk1d4;
```

**Permuter rule**: For `if (a && b) { body }`, try `if (a) { if (b) { body } }`. For `if (!a) return; if (!b) return; body`, try `if (a) { if (b) { body } else return; } else return;`. Also try De Morgan's law: `if (a && b)` ↔ `if (!(!a || !b))`.

**Important**: This is one of the highest-impact patterns — early return vs nested if can shift 20+ instructions.

---

### 6. Expression Rewrite (Arithmetic)
**Frequency**: ~8 instances | **Automatability**: MEDIUM

Rewriting arithmetic expressions to equivalent forms.

**Examples**:
```cpp
// Key.cpp InterpTangent — subtraction to addition form
-    float b = 1.0f - (f4 - fsq3);
+    float b = fsq3 - f4 + 1.0f;

// PropKeys.cpp CalcSpline — same pattern
-    float term3 = p3 - (p2 * 3.0f - p1x3m0);
+    float term3 = p1x3m0 - p2 * 3.0f + p3;

// CharIKFingers — operand reorder in subtraction
-    ((toTargetLen - len03) * (toTargetLen - len03) - (len02 * len02 + lenTip * lenTip))
+    ((len02 * len02 + lenTip * lenTip) - (toTargetLen - len03) * (toTargetLen - len03))

// CharLipSync::Generator::NextFrame — add division
-    int count = mLipSync->mData.size() - 1 - mLastCount;
+    int count = (mLipSync->mData.size() - 1 - mLastCount) / 2;
```

**Permuter rule**: For `a - (b - c)`, try `c - b + a`. For `a - b + c`, try all 6 orderings of {a, -b, c}. This is algebraically equivalent but MSVC PPC may generate different instruction sequences for different forms.

**Note**: Unlike x86, PPC `a + b` vs `b + a` generates identical code (MEMORY.md confirms). But `a - b` vs `-(b - a)` can differ because subtraction is not commutative.

---

### 7. Type/Cast Corrections
**Frequency**: ~8 instances | **Automatability**: MEDIUM

Adding explicit casts, changing types, or adjusting string literal types.

**Examples**:
```cpp
// (char *) cast on Symbol/PathName returns — 5 instances
-    MILO_NOTIFY("Keyframes in %s are out of order.", Name());
+    MILO_NOTIFY("Keyframes in %s are out of order.", (char *)Name());

// (float) cast on ceil result
-    int frameIdx = (int)ceil(frame);
+    int frameIdx = (int)(float)ceil(frame);

// (String &) cast on FilePath
-    MILO_NOTIFY("... won't load %s", PathName(this), fp);
+    MILO_NOTIFY("... won't load %s", PathName(this), (String &)fp);

// bool to unsigned char
-    bool skipOverride = false;
+    unsigned char skipOverride;

// (char *) on string literal
-    lsName2 = "";
+    lsName2 = (char *)"";
```

**Permuter rule**: For varargs functions (printf-style), try adding `(char *)` cast on Symbol, `const char *`, and FilePath arguments. For `ceil`/`floor` results, try `(float)ceil(x)` and `(int)(float)ceil(x)`. For `bool` locals used in integer comparisons, try `unsigned char`.

---

### 8. Stub Inlining Prevention
**Frequency**: 2 instances | **Automatability**: HIGH (pattern detection)

When a stub function body is trivial (`return f;`, empty body), the compiler may inline it into callers, eliminating the expected `bl` instruction.

**Examples**:
```cpp
// Rnd::DrawTimers — noinline on trivial stub
-float Rnd::DrawTimers(float f) {
+__declspec(noinline) float Rnd::DrawTimers(float f) {
     return f;
 }
// Result: Rnd::UpdateOverlay went from 77.6% → 100%
```

**Permuter rule**: Not a source-level permutation, but a **detection** pattern. If objdiff shows a missing `bl` to a function that has a trivial body in the same TU, mark it `__declspec(noinline)`.

---

### 9. Statement Reorder Within Block
**Frequency**: ~4 instances | **Automatability**: MEDIUM

Reordering independent statements within a block.

**Examples**:
```cpp
// CharLipSyncDriver::Poll — w=0 moved after warns
-    w = 0.0f;
     if (unkc4 < 0.0f)
-        MILO_WARN("mOverallOverrideWeight = %f", unkc4);
+        MILO_FAIL("mOverallOverrideWeight = %f", unkc4);
     if (mOverrideWeight < 0.0f)
         MILO_FAIL("mOverrideWeight = %f", mOverrideWeight);
     if (pct > 1.0f)
         MILO_FAIL("pct = %f", pct);
+    w = 0.0f;

// DirLoader::LoadHeader — read order change
+    bool &hasEditorDir = mHasEditorDir;
+    hasEditorDir = false;
+    if (mRev > 0x1c) {
+        *mStream >> hasEditorDir;
+    }
     size1 += mDir->HashTableUsedSize() + 0x10;
     size2 += mDir->StrTableUsedSize() + 0x98;
     mDir->Reserve(size1, size2);
-    bool &unk9aRef = unk9a;
-    unk9aRef = false;
-    if (mRev > 0x1c) {
-        *mStream >> unk9aRef;
-    }
```

**Permuter rule**: For consecutive statements with no data dependency, try all orderings. Key: verify independence by checking that no statement reads a variable written by another.

---

## Non-Permuter Patterns (Higher-Level)

### A. Missing Virtual Calls (from TODO comments)
Two functions fixed to 100% by replacing TODO comments with actual calls:
- `HamNavList::SetHighlight`: Added `mDirectionGestureFilter->ResetHoverTimer()` (85.7% → 100%)
- `HamNavList::Poll`: Added `mDirectionGestureFilter->ClearSwipe()` (86.5% → 86.7%)

**Takeaway**: Grep for `TODO(match)` and `TODO(stub)` — these mark known-missing code.

### B. Out-of-Line Virtual Destructors
Two classes got explicit empty destructors in .cpp files:
- `FitnessCalorieSort::~FitnessCalorieSort() {}`
- `PlaylistSort::~PlaylistSort() {}`

**Takeaway**: If a class has virtual methods but no explicit destructor in the .cpp, the compiler may generate it inline in the header, causing wrong-TU issues.

### C. `MsgSinks::Export` Complete Rewrite
The message dispatch system (`Msg.cpp`) got a ~80-line structural rewrite:
- `ExportSink` free function → `Sink::Export` member function
- Null check inversions throughout (`obj == nullptr` → `obj != nullptr` with swapped branches)
- `sCurrentExportEvent` save/restore logic fixed
- `RemoveSink` rewritten with different control flow
- `Replace` changed from assignment to erase

**Takeaway**: Large structural rewrites can't be automated — they require understanding the algorithm.

### D. RndText::Load Major Revision
~100 lines of changes converting hex revision constants to decimal, restructuring the version-branching logic, and fixing the `d.rev >= 22` / `d.rev == 23` / `d.rev >= 24` cascade.

**Takeaway**: Revision-gated loading code often has subtle control flow differences from the original — the nesting depth and branch ordering matter.

### E. Struct Field Reorder
`CharEyes.h`: Swapped `mLastLook` and `mLastCang` at offsets 0xe8/0xec.
`CharUpperTwist.h`: Reordered `mUpperArm`/`mTwist1`/`mTwist2` (changing offsets).
`CharLipSync.h`: Changed `mVisemes` type from `std::vector<String>` to `std::vector<FilePath>`.

**Takeaway**: Header-level type/order changes ripple through all functions accessing those fields.

---

## Permuter Priority Ranking

| Priority | Pattern | Est. Functions Affected | Automation Effort |
|----------|---------|------------------------|-------------------|
| 1 | Variable declaration reorder | ~50-100 (regswap) | Low — enumerate permutations |
| 2 | Comparison inversion | ~30-50 | Trivial — try all variants |
| 3 | Temp variable extraction | ~20-40 | Medium — identify hoist points |
| 4 | Null check removal | ~10-20 | Low — try removing each guard |
| 5 | `&&` to nested if | ~15-30 | Medium — structural transform |
| 6 | Arithmetic rewrite | ~10-20 | Medium — algebraic equivalences |
| 7 | Type cast insertion | ~10-15 | Low — try casts on varargs |
| 8 | Statement reorder | ~5-10 | Medium — dependency analysis |

---

## Key Insight: Composability

Many functions require **multiple patterns applied together**. For example, `CharLipSyncDriver::Poll` needed:
- Variable declaration reorder (4 instances)
- `&&` to nested if (3 instances)
- Comparison inversion (3 instances)
- Statement reorder (2 instances)
- Null check removal (1 instance)
- Type change bool→unsigned char (1 instance)

A permuter that only tries one pattern at a time will miss these. The search space explodes combinatorially, so a good permuter needs:
1. **Incremental application**: Apply one pattern, check if match% improved, keep or revert
2. **Prioritized ordering**: Try comparison inversions first (cheap), then temp extraction, then declaration reorder
3. **Early termination**: Stop when match% stops improving
