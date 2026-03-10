# Header-Level Clustering Analysis

Investigation methodology, historical catalog, and findings for identifying
header bugs and template inlining issues that affect many functions
simultaneously.

Established: 2026-03-10. Historically, header-level fixes account for
**1,500+ function improvements** — more than all .cpp-level fixes combined.

## Motivation

Most AT_LIMIT functions are blocked by header-level template codegen, not
.cpp-level source issues. The permuter cannot fix these because:

1. Patterns only modify the function body, not headers
2. Template instantiations produce identical mismatches across all TUs
3. The same match% cluster across unrelated TUs is the signature of a
   shared header root cause

The right approach is to identify match% clusters, trace them to the
header template responsible, and fix the template body or class layout.

Header-level fixes have the highest ROI in this project by a wide margin.
A single header change can fix dozens to thousands of functions instantly.

## Methodology

### Step 1: Extract match% clusters from report.json

```python
import json

with open("build/373307D9/report.json") as f:
    report = json.load(f)

# Build {match%: [functions...]} map
clusters = {}
for unit in report["units"]:
    for func in unit.get("functions", []):
        pct = func.get("fuzzy_match_percent", 0)
        if 70 < pct < 100:
            key = round(pct, 1)
            clusters.setdefault(key, []).append({
                "name": func["name"],
                "unit": unit["name"],
                "size": func.get("size", 0),
            })

# Find clusters spanning 3+ TUs
for pct in sorted(clusters.keys(), reverse=True):
    funcs = clusters[pct]
    units = set(f["unit"] for f in funcs)
    if len(funcs) >= 3 and len(units) >= 2:
        print(f"{pct}%: {len(funcs)} functions across {len(units)} TUs")
```

### Step 2: Identify the shared template

For each cluster, check if the affected functions:
- Call the same template method (e.g., `ObjPtrVec::operator=`)
- Use the same header inline (e.g., `ObjectDir::Find<T>`)
- Share a common type parameter pattern

Use `mcp__orchestrator__run_diff_inspect` with `mode=mismatches` to see
instruction-level patterns. Identical mismatch shapes across TUs = shared
template.

Cross-reference with demangled names to find template patterns:

```python
templates = {}
for unit in report["units"]:
    for func in unit.get("functions", []):
        pct = func.get("fuzzy_match_percent", 0)
        name = func.get("name", "")
        if 70 < pct < 100:
            for pattern in ["ObjPtrVec", "ObjPtrList", "ObjectDir::Find",
                            "DataNode", "ObjRef", "vector", "_M_",
                            "operator=", "operator+", "iterator"]:
                if pattern in name:
                    templates.setdefault(pattern, []).append({
                        "name": name, "unit": unit["name"], "pct": pct,
                    })
                    break

for pattern, funcs in sorted(templates.items(), key=lambda x: -len(x[1])):
    units = set(f["unit"] for f in funcs)
    pcts = set(round(f["pct"], 1) for f in funcs)
    print(f"{pattern}: {len(funcs)} functions across {len(units)} TUs")
    print(f"  Percentages: {sorted(pcts)}")
```

### Step 3: Root cause and fix

Once the template is identified:
1. Read the header definition
2. Compare against target assembly (what does the target inline?)
3. Identify the semantic difference (wrong function called, wrong inlining,
   wrong operator used)
4. Fix the header, rebuild, verify zero regressions

### Step 4: Regression verification

Header changes are extremely sensitive. Always:
- Run `ninja` and check full report.json before and after
- Compare regression count: `python3 scripts/analysis/compare_progress.py`
- A single header change can cause 600+ regressions (proven: commit `12ab0e6fd`)

---

## Historical Catalog of Header-Level Fixes

All header-level fixes applied to this project, ordered by impact.

### Tier 1: Massive Impact (hundreds+ functions)

#### 1. Non-const DataArray accessor overloads (`Data.h`)

- **Commit**: `1b10da78a`
- **Header**: `src/system/obj/Data.h` + `src/system/obj/Object.h`
- **Impact**: **+1,458 functions to COMPLETE** (29,929 → 31,387)
- **Root cause**: All 20 accessor methods (Int, Sym, Float, Str, Array, etc.)
  were const-only, dispatching to `Node(int) const` (QBA mangling). The
  original binary uses non-const `Node(int)` (QAA) through non-const
  `DataArray*` pointers at ~3,166 call sites.
- **Fix**: Added non-const overloads for all 20 accessors. Changed
  `BEGIN_HANDLERS` macros in Object.h to use `_msg->Sym(1)` instead of
  `CONST_ARRAY(_msg)->Sym(1)`.
- **Detection**: Demangled symbol audit showed QAA vs QBA mangling mismatch.

#### 2. STL template regressions (`_heap.c`, `StlAlloc.h`)

- **Commit**: `1edd00317`
- **Headers**: `src/system/stlport/stl/_heap.c`, `src/system/utl/StlAlloc.h`
- **Impact**: **~200 functions** fixed (STL container operations across all TUs)
- **Root cause**: Two bugs: (1) Added const qualifier to `__push_heap`
  `_Tp __val` parameter (wrong — const changes copy semantics). (2) Changed
  StlAlloc `allocate()`/`deallocate()` from if/else to ternary (wrong —
  MSVC PPC generates different code for if/else vs ternary).
- **Lesson**: `const` qualifiers on template value parameters change codegen.
  if/else vs ternary is never semantically neutral on MSVC PPC.

#### 3. Batch header regression revert (negative example)

- **Commit**: `12ab0e6fd`
- **Headers**: STL headers (`_vector.h`, `_construct.h`, `_list.h`, `_list.c`,
  `_heap.c`, `_algo.c`), `Object.h`, `Dir.h`, `ObjPtr_p.h`, `StoreOffer.h`,
  `FlowPtr.h`, `CharPollable.h`, `StoreEnumeration.h`
- **Impact**: **624 regressions → 8** (recovery)
- **Root cause**: Batch patches introduced incorrect header changes (hardcoded
  memcpy, null checks, const end() expansion, noinline additions,
  class→struct mangling changes).
- **Lesson**: Header changes from automated batch patches must be verified
  individually. A single wrong change to a core header cascades to hundreds
  of functions. This is the worst regression event in the project's history.

### Tier 2: Significant Impact (10-50 functions)

#### 4. DataNode(float/double) constructor union zeroing (`Data.h`)

- **Commit**: `276ca05dd`
- **Header**: `src/system/obj/Data.h`
- **Impact**: **25+ functions to 100%** across DataFunc, Trig, Rnd, Cam,
  MidiParser, TimeConversion, Loader. +1% overall Milo matched.
- **Root cause**: `mValue.object = nullptr` before `mValue.real = f` was
  redundant. Union members fully overlap on 32-bit PPC, so zeroing `object`
  before setting `real` generated extra instructions (two stores instead of
  one).
- **Fix**: Remove the zero-init line. The `real` assignment overwrites the
  same memory.
- **Detection**: Cluster of DataNode-constructing functions all had
  identical extra `stw r0, 0x8(rN)` instructions.

#### 5. ObjPtrVec::Node CopyRef operator= (`Object.h`, `ObjPtr_p.h`)

- **Commits**: Multiple "progress" commits
- **Headers**: `src/system/obj/Object.h`, `src/system/obj/ObjPtr_p.h`
- **Impact**: **18 `_M_fill_insert_aux` instantiations: 83.8% → 100%**.
  Also: **13 `operator=` instantiations: 82.9% → 84.4%** (Set(end()) bug).
- **Root cause (Bug 1)**: `Set(end(), *it)` dereferences past-the-end
  memory. Target subtracts `sizeof(Node)` from end address.
- **Root cause (Bug 2)**: `Node` inherited `ObjRefConcrete::operator=`
  which calls `SetObjConcrete()` (header body, inlined). Target calls
  `CopyRef()` (separate TU body in `link_glue.cpp`, not inlined).
- **Fix**: Added `void operator=(const Node &o) { CopyRef(o); mOwner = o.mOwner; }`
  to Node struct. Changed `Set(end(), *it)` to
  `Set(begin() + (mNodes.size() - 1), *it)`.
- **Key constraint**: CopyRef body must NOT be in the header. External call
  is the mechanism. Adding template body defeats the fix.
- **Scope isolation**: Changing the base `ObjRefConcrete::operator=` caused
  19 severe regressions. The fix must be Node-specific.

#### 5b. ObjPtrVec::insert argument scheduling (`ObjPtr_p.h`)

- **Date**: 2026-03-10
- **Header**: `src/system/obj/ObjPtr_p.h`
- **Impact**: **18 `insert` instantiations: 96.6% → 100%** across 14 TUs.
  Zero regressions.
- **Root cause**: `mNodes.insert(mNodes.begin() + idx, 1, newNode)` computes
  the position argument (`begin() + idx`) as part of the function call
  expression. The compiler schedules its evaluation LAST among the 4
  arguments (r3, r4, r5, r6), hiding the load latency of `_M_start`. The
  target evaluates `pos` FIRST, using `r11` immediately after loading it.
- **Fix**: Separate the position computation into a local variable:
  ```cpp
  typename std::vector<Node>::iterator pos = mNodes.begin() + idx;
  mNodes.insert(pos, 1, newNode);
  ```
  This forces the compiler to evaluate `pos` before materializing the other
  arguments, matching the target's argument scheduling order.
- **Detection**: 18 functions at exact same match% (96.6%) across 14 TUs.
  All 4 mismatched instructions are the same set, just reordered — pure
  argument scheduling permutation before `_M_fill_insert`.
- **Lesson**: Separating subexpressions into local variables can influence
  the compiler's instruction scheduler, even for volatile register
  arguments. This is a new fix category — not register allocation, not
  inlining, but **argument materialization scheduling**.

#### 6. AmbientOcclusion VectorSort sort comparator

- **Impact**: **14 VectorSort sort templates → 100%**
- **Root cause**: `return it1 < it2` generates iterator comparison.
  `return (it1 - vector.begin()) < (it2 - vector.begin())` generates
  index-based comparison matching target's `subf`+`clrrwi`+signed compare.
- **Detection**: All 14 sort templates had identical mismatch pattern at
  the comparator call site.

#### 7. ShaderMgr.h vtable method reorder

- **Commit**: `d7d7dcd3a`
- **Header**: `src/system/rndobj/ShaderMgr.h`
- **Impact**: **11 functions to 100%** (NgPostProc, NgSpotlightDrawer, NgMat)
- **Root cause**: MSVC PPC reverses overloaded virtual method ordering in the
  vtable. `SetVConstant(Vector4)` was after `SetVConstant(float*,uint)` but
  needed to be before. Same for `SetPConstant` overloads.
- **Detection**: vtable offset mismatch (0x20↔0x24, 0x3c↔0x40) visible in
  all functions calling these methods across different TUs.

### Tier 3: Moderate Impact (3-10 functions)

#### 8. FlowPtr `__forceinline` operator= (`FlowPtr.h`)

- **Commit**: `fa459e693`
- **Header**: `src/system/flow/FlowPtr.h`
- **Impact**: **7 Copy methods: ~60% → ~99%** (FlowCommand, FlowDistance,
  FlowAnimate, FlowSound, FlowSetProperty, FlowRun, FlowTrigger)
- **Root cause**: `FlowPtr::operator=` generated an out-of-line call. Target
  inlines it with manual save-call-restore pattern.
- **Fix**: `__forceinline` on `operator=` with matching codegen pattern.

#### 9. UIList struct layout fix (`UIList.h`)

- **Commit**: `a257a52ac`
- **Header**: `src/system/ui/UIList.h`
- **Impact**: PostLoad 76.7→100%, DrawShowing 84.3→90.1%,
  BuildDrawState 85.6→86.8%
- **Root cause**: Members were in wrong order. Correct layout:
  `mDrawManuallyControlledWidgets` (0x15c), `mAllowHighlight` (0x15d),
  `mLimitCircularDisplayNumToDataNum` (0x15e), `mUncappedNumDisplay` (0x160),
  `mScrolling` (0x164). Also: `BuildDrawState` last param should be
  `mScrolling` not `false`.
- **Detection**: Offset mismatches in member loads visible across multiple
  UIList methods.

#### 10. UIListElementDrawState field order (`UIListWidget.h`)

- **Commit**: `42adb983a`
- **Header**: `src/system/ui/UIListWidget.h`
- **Impact**: Multiple UIList drawing functions (UIListSlot::Draw, etc.)
- **Root cause**: 5 DC3-specific fields (unk1c–mNavVtablePtr) were after
  mDisplay/mShowing/mData but should be before (mDisplay at offset 0x30,
  not 0x1c).
- **Detection**: Struct offset mismatches in `lwz`/`stw` instructions.

#### 11. Accessor outline fix (`UIListWidget.h` → `.cpp`)

- **Commit**: `0fcb1af9f`
- **Header**: `src/system/ui/UIListWidget.h`
- **Impact**: UIListSlot::Draw 80.8→100%, HamListRibbon Start/EndFrame 93→100%
- **Root cause**: `DisabledAlphaScale()` and `ParentList()` were inline in
  the header but target didn't inline them (emits `bl` calls instead).
- **Fix**: Move bodies from header to `.cpp`.
- **Detection**: `replace` cluster where target has `mr+bl` and base has
  direct `lwz`/`lfs` member load at the same offset.

#### 12. StandingStillGestureFilter spurious `unk44` field

- **Commit**: `0bbf58bd9`
- **Header**: `StandingStillGestureFilter.h`
- **Impact**: **3 functions to 100%**
- **Root cause**: `int unk44` was not a real field. It was the compiler's
  16-byte copy alignment of Vector3 writing into implicit padding. The
  struct had a phantom member that inflated the struct size and shifted
  all subsequent offsets.
- **Lesson**: When a struct has `Vector3` followed by unexplained `int`,
  check if the `int` is just padding from 16-byte aligned copy.

#### 13. RndText::Highlight out-of-line (`Text.h` → `.cpp`)

- **Commit**: `4c54e2b7e`
- **Header**: `src/system/rndobj/Text.h`
- **Impact**: UILabel::Highlight 94.4→100%
- **Root cause**: Trivial virtual was inline in Text.h, getting inlined into
  UILabel::Highlight. Target doesn't inline it.
- **Fix**: Move body to Text.cpp.

#### 14. Header-driven regression recovery (`Easing.h`, `Cam.h`, `UIList.h`, `UIListWidget.h`)

- **Commit**: `bae42314d`
- **Headers**: Multiple
- **Impact**: +1 function to 100%, 0 regressions
- **What**: Easing.h parameter name revert; Cam.h ProjectZ body moved out;
  UIList.h ChildList() body moved to .cpp; UIListWidget.h explicit
  `~UIListWidgetDrawState()` dtor removed.
- **Lesson**: Even parameter names in headers can affect codegen (debug info
  in IL). Explicit empty dtors can differ from implicit ones.

#### 15. NavListSortMgr vtable layout fix

- **Commit**: `da9af30c8`
- **Header**: `NavListSortMgr.h`
- **Impact**: Multiple sort node functions
- **Root cause**: Virtual functions declared in wrong order, producing
  incorrect vtable offsets.

#### 16. FftIpp IppBuf struct introduction

- **Commit**: `8adb35f99`
- **Impact**: FftIpp 60→97.3%, SpectralAnalysis 85.5→99.05%
- **Root cause**: 15 raw `unsigned int` fields needed to be 5 IppBuf
  members (begin/end/cap triple) with non-trivial destructors for proper
  EH state tracking.
- **Lesson**: When functions have large prologue mismatches involving
  exception handling, check if POD fields should be RAII wrappers.

#### 17. Header signature fixes from demangled symbol audit

- **Commit**: `d73357437`
- **Headers**: User.h, DancerSkeleton.h, CharCollide.h, Morph.h, Debug.h, etc.
- **Impact**: Multiple functions across UIList, RhythmBattle, AmbientOcclusion,
  CharClip, MemMgr
- **What**: Added non-const `GetRemoteUser()` virtual overload, removed
  incorrect copy ctor declarations, moved `RemoveFromLists` to correct class,
  un-inlined `AddToStrings` in Debug.h.
- **Detection**: Demangled symbol audit comparing expected vs actual mangling.

### Tier 4: Infrastructure / Cross-cutting

#### 18. `.begin()`/`.end()` vs `.data()+.size()` revert

- **Commit**: `e9b59e38d`
- **Headers**: 15+ files
- **Root cause**: `.end()` is one load (`lwz _M_finish`). `.data()+.size()`
  computes extra `subf`/`divw`/`mulli`. Always prefer `.begin()`/`.end()`.
- **Rule**: Never use `.data()+.size()` as a substitute for `.end()`.

#### 19. `/FORCE:MULTIPLE` linker warning reduction

- **Commit**: `ffdce82aa`
- **Impact**: LNK4006 warnings 1,781 → 102
- **What**: Added `inline` to header-defined functions, moved arrays from
  headers to .cpp with extern declarations, removed 26 obsolete
  ALTERNATENAME pragmas.

#### 20. gEaseFuncs array location (`Easing.h`)

- **Commits**: `ffdce82aa` (moved to .cpp), `b7c6ac1d7` (moved back to header)
- **Impact**: FlowSetProperty unit lost 33 Ease function COMDAT instantiations
  when array was in .cpp. Restored when moved back to header.
- **Lesson**: Array location in header vs .cpp affects COMDAT instantiation
  of template functions referenced from that array.

#### 21. Object.h iterator operator+ copy semantic

- **Commit**: `1cfaa5402`
- **Header**: `src/system/obj/Object.h`
- **Impact**: Zero regressions verified, but the copy-semantic `operator+`
  is the root cause of **68 og-baseline regressions** (unfixable).
- **Lesson**: Reverting to og's mutate-and-return `operator+` gains only 1
  regression fix but causes 21 NEW regressions (8 drop from 100%). The copy
  semantic is net positive. Some header-driven regressions are trade-offs,
  not bugs.

---

## Current Cluster Landscape

As of 2026-03-10, from report.json and decomp.db analysis:

### Active Template Clusters (same match% across TUs)

| Cluster | Match% | Count | TUs | Status | Root Cause |
|---------|--------|-------|-----|--------|------------|
| ObjectDir::Find\<T\> | 99.7% | 81 | 40 | AT_LIMIT | Volatile reg scheduling in `__RTDynamicCast` |
| ObjPtrVec\<T\>::insert | 96.6% | 18 | 14 | **FIXED** | Argument scheduling — separated pos local (Tier 2 #5b) |
| ObjPtrVec\<T\>::_M_fill_insert_aux | 83.8% | 18 | 14 | AT_LIMIT | Register swap after CopyRef fix |
| ObjPtrVec\<T\>::operator= | 82.9% | 13 | 10 | AT_LIMIT | Address relocation noise |
| ResourceDirPtr\<T\>::SetName | 88.3% | 7 | 5 | AT_LIMIT | Register swap |
| ObjPtrVec\<T\>::sort | 96.3% | 3 | 3 | Open | Header template |

### Match% Concentration by Cluster Size

| Match% | Functions | TUs | Dominant Content |
|--------|-----------|-----|------------------|
| 99.7% | 119 | 73 | 81 ObjectDir::Find, 38 other |
| 96.6% | 34 | 30 | 18 ObjPtrVec::insert, 16 other |
| 95.6% | 17 | 16 | Mixed Load/keygen functions |
| 94.7% | 16 | 14 | Mixed Polls/Loads |
| 93.3% | 14 | 14 | Mixed handlers |

### AT_LIMIT Pattern Breakdown (from decomp.db)

| Primary Pattern | Count | Avg % | Fixable from Header? |
|-----------------|-------|-------|---------------------|
| ADDRESS_RELOCATION_NOISE | 578 | 89.5% | No — .text offset difference |
| (unclassified) | 566 | 97.1% | **Sometimes** — template clusters |
| CONTROL_FLOW | 262 | 89.9% | Rarely |
| REGISTER_SWAP | 253 | 92.5% | **Sometimes** — declaration order |
| OFFSET_SWAP | 134 | 94.5% | No — struct layout settled |
| COMMUTATIVE_OP_ORDER | 75 | 96.9% | No |
| BOOL_MASK | 34 | 91.8% | **Sometimes** — signedness in header |
| STATIC_GUARD_COUNTER | 3 | 99.6% | No — wibo guard difference |

---

## Fix Classification Taxonomy

Header-level fixes fall into six categories:

### Category A: Struct Layout Fixes

Wrong member order or phantom fields shift all subsequent offsets.

**Detection**: `lwz`/`stw` offset mismatches across multiple methods of
the same class. Use `ghidra-struct` skill or `/struct-info` to compare.

**Examples**: UIList (Tier 3 #9), UIListElementDrawState (#10),
StandingStillGestureFilter (#12), RockCentral, NavListSortMgr (#15).

**Expected gain**: +5-40% per function, affects all methods of the class.

### Category B: Vtable Order Fixes

MSVC PPC reverses overloaded virtual methods in the vtable. Declaring
them in the wrong order puts methods at wrong vtable slots.

**Detection**: `lwz rN, 0xXX(r11)` with wrong offset XX, consistent
across all callers of that virtual method.

**Examples**: ShaderMgr (#7), NavListSortMgr (#15).

**Expected gain**: +5-100% per function, affects all virtual callers.

### Category C: Inlining Control Fixes

Function body in header → compiler inlines it. Body in .cpp → compiler
emits `bl` call. When target doesn't inline, moving body out of header
fixes the match.

**Detection**: `replace` cluster with `mr+bl` (target) vs direct member
load (base) at the same offset.

**Examples**: CopyRef/Node operator= (#5), DisabledAlphaScale/ParentList
(#11), RndText::Highlight (#13), Cam::ProjectZ (#14), Debug::AddToStrings
(#17).

**Expected gain**: +3-20% per function. Can affect many functions if the
inlined function is used widely.

**Key constraint**: The mechanism is TU boundary separation. A function
body in a separate .cpp prevents inlining. Adding `__declspec(noinline)`
is NOT the same — it's a hint, not enforced.

### Category D: Template Semantic Fixes

The template body itself has a bug or uses the wrong API. Every
instantiation inherits the same bug.

**Detection**: All instantiations of the same template share exact match%.

**Examples**: ObjPtrVec Set(end()) bug (#5), DataArray const-only
accessors (#1), STL const parameter (#2).

**Expected gain**: Varies widely. +1-100% per function, across all
instantiations.

### Category E: `__forceinline` / Inlining Directive Fixes

Target inlines a function that our compiler doesn't (or vice versa).
`__forceinline` forces inlining regardless of threshold.

**Detection**: Large `insert` or `delete` cluster corresponding to the
inlined function body.

**Examples**: FlowPtr operator= (#8).

**Expected gain**: +10-40% per function.

### Category F: Type Qualifier / Mangling Fixes

const, volatile, or access specifier mismatches produce different mangled
names, causing wrong overload dispatch or COMDAT linkage.

**Detection**: Demangled symbol audit comparing expected vs actual mangling
(QAA vs QBA, etc.).

**Examples**: DataArray const accessors (#1), header signature audit (#17).

**Expected gain**: +1-100% depending on how many call sites dispatch
through the wrong overload.

### Category G: Argument Scheduling Fixes

Subexpressions computed inline in function call arguments get scheduled
by the compiler in a different order than the target. Separating the
computation into a local variable forces earlier evaluation.

**Detection**: 3-4 `replace` instructions that are the same set of
operations, just permuted. All arguments go to volatile registers
(r3-r10). No register allocation or inlining difference.

**Examples**: ObjPtrVec::insert position computation (#5b).

**Expected gain**: +3-5% per function, across all template instantiations.

---

## Lessons Learned

1. **Match% clustering is the best signal for header bugs**. When 10+
   functions across 5+ TUs share an exact match%, it's almost always a
   header template issue.

2. **Inlining control via TU boundaries is a real technique**. The CopyRef
   fix works because the body is in a separate TU. This is the same
   mechanism the original compiler used — ICF merges identical COMDAT
   specializations but doesn't inline them.

3. **Base class changes cascade dangerously**. Always scope operator
   overrides to the most specific class possible. Node-specific operator=
   was the right granularity; ObjRefConcrete-level caused 19 regressions.

4. **Not all clusters are fixable**. Volatile register scheduling
   (ObjectDir::Find, 81 functions) and compiler-internal scheduling
   decisions are genuinely unfixable from source. Clustering analysis still
   has value — it confirms AT_LIMIT definitively.

5. **Semantic bugs hide in templates**. The Set(end()) bug was a real
   correctness issue (past-the-end dereference) that happened to also cause
   a codegen mismatch. Header investigation can find real bugs.

6. **Never batch-modify headers without per-change verification**. The
   624-regression event (commit `12ab0e6fd`) proves that automated batch
   patches to core headers are catastrophically dangerous.

7. **const qualifiers on template parameters change codegen**. Adding
   `const` to a value parameter in a template function changes copy
   semantics and can affect 200+ instantiations.

8. **if/else vs ternary is never neutral on MSVC PPC**. These generate
   different branch structures. Always check target before switching.

9. **Explicit empty dtors differ from implicit ones**. For multi-
   inheritance classes, `virtual ~Foo() {}` generates different code than
   letting the compiler generate the implicit dtor (extra vtable stores).

10. **Header changes are trade-offs, not just fixes**. The Object.h
    iterator operator+ copy semantic improves 21 functions but regresses
    68 og-baseline functions. Net positive, but not free.

11. **Separating subexpressions into locals influences instruction
    scheduling**. Computing `mNodes.begin() + idx` inline in a call
    expression lets the scheduler defer it. Assigning to a local first
    forces earlier evaluation — matching the target's argument order.
    This is a new fix category: **argument materialization scheduling**.

---

## Integration with Permuter/Synthesis Engine

This analysis methodology is complementary to the permuter:

- **Permuter** works bottom-up: tries source variations on individual
  functions to find matches
- **Header clustering** works top-down: identifies shared root causes
  across many functions, then fixes the header once

The synthesis engine should incorporate header-level clustering as a
pre-pass: before running the permuter on a function, check if it belongs
to a known cluster with a shared header root cause. If so, fix the header
first — no amount of function-body permutation will fix a header bug.

### Remaining Opportunities

Based on the current cluster landscape, these are the highest-ROI
header-level targets:

1. ~~**ObjPtrVec\<T\>::insert** (18 functions at 96.6%)~~ **FIXED** —
   separated position computation into local variable. All 18 → 100%.

2. **ObjPtrVec\<T\>::sort** (3 functions at 96.3%): Same header, may
   have a similar argument scheduling or template body issue.

3. **Unclassified 99.7% functions** (38 non-Find functions): May include
   additional template clusters not yet identified.

4. **32 LIKELY_FIXABLE** functions in decomp.db: Range from 9.8% to 88.6%.
   Some may have header-level root causes (SaveLoadManager::Poll at 34.7%,
   CharHair::SimulateInternal at 21.8%).

### Future Automation

A `header_cluster_detector.py` script could:
1. Parse report.json for match% clusters spanning 3+ TUs
2. For each cluster, extract the shared template via symbol demangling
3. Cross-reference with known-unfixable clusters (ObjectDir::Find, etc.)
4. Flag new clusters for manual investigation
5. Track cluster sizes over time to detect regressions from header changes
