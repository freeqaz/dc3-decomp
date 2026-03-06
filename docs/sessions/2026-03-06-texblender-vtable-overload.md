# RndTexBlender Implementation & MSVC Vtable Overload Discovery

**Date:** 2026-03-06
**Functions:** `RndTexBlender::DrawShowing()` (88.6%), `RndTexBlender::DrawBlendList()` (91.9%)
**Systemic Discovery:** MSVC PPC reverses vtable order of overloaded virtual functions

## What We Accomplished

### 1. RndTexBlender::DrawShowing() — 0% → 88.6%

Full render-to-texture pipeline implementing texture blending for character detail maps (skin stretch, wrinkles, etc.). 675 instructions.

**Implementation structure:**
1. Early-out checks: draw mode, process commands, output texture validity
2. Build sorted near/far/custom blend lists from controller iteration
3. Render pipeline: SetTargetTex → draw base map quad → sort+draw near/far inline → DrawBlendList for custom → restore camera

**Key patterns used:**
- `BlendSorter` functor for `std::sort` on `pair<RndTexBlendController*, float>` by float
- `switch` statement for `BlendState` enum (generates correct cascade compare vs if/else if)
- `MILO_NOTIFY_ONCE` macro for runtime warnings
- Inline near/far loops (not delegated to DrawBlendList) — target doesn't null-check mesh before `IsSkinned()`

### 2. RndTexBlender::DrawBlendList() — 0% → 91.9%

Custom blend list rendering with per-controller texture swapping. 187 instructions.

### 3. MSVC PPC Vtable Overload Reversal (Systemic Fix)

**Discovery:** MSVC for Xbox 360 (cl.exe PPC) **reverses the vtable order** of overloaded virtual functions that share the same name. The last-declared overload gets the **lowest** vtable slot.

**Evidence:** objdiff showed `lwz r11, 0x2C(r11)` (our code) vs `lwz r11, 0x18(r11)` (target) for `SetVConstant(kVS_ViewProjMatrix, viewProjMtx)`. The Matrix4 overload was at vtable offset 0x2C in our build but 0x18 in the target.

**Verified with 7 test compilations** — non-overloaded virtuals are unaffected, only same-name overloads are reversed.

**Fix applied to:**
- `src/system/rndobj/ShaderMgr.h` — RndShaderMgr base class
- `src/system/rnddx9/ShaderMgr.h` — DxShaderMgr derived class

```cpp
// NOTE: MSVC PPC reverses overloaded virtual function order in vtable.
// Declare in reverse of desired vtable order.
virtual void SetVConstant(VShaderConstant, bool) = 0;        // highest slot
virtual void SetVConstant(VShaderConstant, int) = 0;
virtual void SetVConstant(VShaderConstant, const float *, unsigned int) = 0;
virtual void SetVConstant(VShaderConstant, const Vector4 &) = 0;  // 0x24
virtual void SetVConstant(VShaderConstant, RndTex *) = 0;
virtual void SetVConstant4x3(VShaderConstant, const Hmx::Matrix4 &) = 0;
virtual void SetVConstant(VShaderConstant, const Hmx::Matrix4 &) = 0; // 0x18 lowest slot
```

## Remaining Gaps (At Limit)

### DrawShowing — 88.6%
| Pattern | Instructions | Fixable? |
|---------|-------------|----------|
| Register swaps | 150 (32 pairs) | No — mixed volatile+callee-saved |
| Stack frame +8 | 61 | No — compiler stack layout |
| Address relocations | 42 | No — linker-level |
| Static guard counters | 2 | No — TU definition order |
| TheShaderMgr vtable caching | ~10 | No — compiler pre-loads vtable ptr into callee-saved reg |

### DrawBlendList — 91.9%
| Pattern | Instructions | Fixable? |
|---------|-------------|----------|
| r25↔r26 regswap | 11 | Maybe — callee-saved |
| beq↔bne ternary | 1 | No — swapping cascades to worse (84.7%) |
| TheShaderMgr vtable caching | 5 | No |
| Address relocations | 4 | No |

## Patterns for Permuter Rules

### Pattern 1: Ternary Condition Swap (existing `ternary_swap`)

```cpp
// Original (generates bne):
RndTex *texmap = (state != 2) ? mNearMap : mFarMap;
// Swapped (generates beq):
RndTex *texmap = (state == 2) ? mFarMap : mNearMap;
```

The existing `ternary_swap` pattern should handle this but had 0 wins. For DrawBlendList, swapping the ternary caused a cascade failure (91.9% → 84.7%) because it changed the fall-through path, shifting register allocation for subsequent code. **Lesson:** ternary swaps are high-risk when the branches have different register pressure (ObjPtr dereference vs direct member).

### Pattern 2: Null Guard Elimination (existing `null_guard_elimination`)

```cpp
// With guard (adds cmplwi+beq before the body):
RndMesh *mesh = controller->Mesh();
if (mesh) {
    if (mesh->IsSkinned()) { ... }
    mesh->DrawFacesInRange(0, -1);
}

// Without guard (target trusts mesh is non-null):
RndMesh *mesh = controller->Mesh();
if (mesh->IsSkinned()) { ... }
mesh->DrawFacesInRange(0, -1);
```

The `null_guard_elimination` pattern already exists. In DrawShowing's inline near/far loops, removing the null check matches the target. In DrawBlendList, keeping it matches better (different register allocation context). **Lesson:** same logical pattern can go either way depending on surrounding code pressure.

### Pattern 3: Vtable Pre-Load Caching (NEW — not currently a permuter pattern)

The target caches `TheShaderMgr`'s vtable pointer in a callee-saved register before a call that clobbers volatiles (like Matrix4 constructor):

```asm
; Target: pre-loads vtable before Matrix4 ctor
lwz r25, 0x0, r26      ; r25 = TheShaderMgr.vtable
bl  Matrix4::Matrix4    ; clobbers volatiles, but r25 survives
lwz r11, 0x18, r25      ; use cached vtable
mr  r5, r3              ; matrix result
mr  r3, r26             ; TheShaderMgr
li  r4, 0x4             ; kVS_ViewProjMatrix
mtctr r11
bctrl

; Source: reloads from global after ctor
bl  Matrix4::Matrix4
lwz r3, TheShaderMgr    ; reload global
addi r5, r31, 0x150     ; matrix on stack
li  r4, 0x4
lwz r11, 0x0, r3        ; load vtable
lwz r11, 0x18, r11      ; load method
mtctr r11
bctrl
```

This is a **compiler scheduling optimization** — MSVC PPC hoists the global load + vtable deref before the intervening function call when it detects the result will be needed afterward. No source-level fix exists; it's purely a compiler register allocation decision based on liveness analysis.

**Not viable as a permuter pattern** — would require inserting a dummy variable to force a register live range, which is fragile and unlikely to match.

### Pattern 4: Switch vs If/Else-If for Enums

```cpp
// Switch generates cascading cmpwi + beq pattern:
switch (state) {
case RndTexBlendController::kBlendNear:
    nearList.push_back(...); break;
case RndTexBlendController::kBlendFar:
    farList.push_back(...); break;
case RndTexBlendController::kBlendCustom:
    customList.push_back(...); break;
}

// If/else-if generates different branch structure:
if (state == kBlendNear) nearList.push_back(...);
else if (state == kBlendFar) farList.push_back(...);
else if (state == kBlendCustom) customList.push_back(...);
```

Both generate compare-and-branch, but switch can emit jump tables for dense cases and has different fall-through semantics. For small enums (3 cases), both generate similar code, but the switch version matched the target's cascade pattern here. **Potential permuter pattern:** `switch_to_if` / `if_to_switch` conversion for small enum dispatches.

### Pattern 5: MILO_NOTIFY_ONCE Static Guard Counter Ordering

Each `MILO_NOTIFY_ONCE` macro expands to a static guard variable (`$S3`, `$S4`, etc.) and a `DebugNotifyOncer` object. The guard counter values depend on **TU-wide definition order** of all statics. Moving a function up/down in the file changes which counter index it gets, creating unfixable 1-2 instruction mismatches.

**Not viable as a permuter pattern** — would require reordering entire function definitions within the TU, which is too disruptive.

## Vtable Overload Pattern — Impact Assessment

Classes affected by the MSVC overload reversal pattern (have same-name overloaded virtuals):
- `RndShaderMgr` / `DxShaderMgr` — **FIXED** (SetVConstant ×7, SetPConstant ×7)
- Any other class with overloaded virtual methods sharing the same name

To find more: `grep -r "virtual.*\b(\w+)\b.*;" headers | group by class+method name | filter count > 1`

The fix is always the same: declare overloads in **reverse** of the desired vtable order. Verify with the `/vtable` skill.
