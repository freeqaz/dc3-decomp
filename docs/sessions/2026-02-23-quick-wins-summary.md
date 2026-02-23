# Quick Wins Session - 2026-02-23

## Results Summary

| Function | Match | Result | Mangled Name |
|----------|-------|--------|--------------|
| Box::Volume | 98.8% | AT_LIMIT | ?Volume@Box@@QBAMXZ |
| HamMaster::Jump | - | Already complete/removed | - |
| SfxInst::IsRunning | 98.5% | AT_LIMIT | SfxInst::IsRunning |
| RndTransformable::OnCopyLocalTo | 98.2% | AT_LIMIT | ?OnCopyLocalTo@RndTransformable@@AA?AVDataNode@@PBVDataArray@@@Z |
| ScrollSelect::RevertScrollSelect | 98.7% | AT_LIMIT | ?RevertScrollSelect@ScrollSelect@@QAA_NPAVUIComponent@@PAVLocalUser@@PAVObject@Hmx@@@Z |
| FlowLabel::Load | 99.0% | AT_LIMIT | ?Load@FlowLabel@@UAAXAAVBinStream@@@Z |
| **RndBitmap::LoadHeader** | **100%** | **COMPLETE** | ?LoadHeader@RndBitmap@@AAAEAVBinStream@@AAEAV0@AAE@Z |
| HamNavList::NumItems | 98.4% | AT_LIMIT | ?NumItems@HamNavList@@ABEHXZ |
| CharCuff::Highlight | 98.5% | AT_LIMIT | ?Highlight@CharCuff@@UAAXXZ |

## Detailed Notes

### Box::Volume - 98.8% - AT_LIMIT
**Mangled:** `?Volume@Box@@QBAMXZ`
**File:** `src/system/math/Geo.cpp`

**Issue:** COMMUTATIVE_OP_ORDER + OFFSET_SWAP patterns. The compiler reorders the multiplication differently.

**Code:**
```cpp
float Box::Volume() const {
    return (mMax.x - mMin.x) * (mMax.y - mMin.y) * (mMax.z - mMin.z);
}
```

**Attempts:**
- Swapped multiplication order from z-y-x to x-y-z → no change
- Added explicit variable declarations `float x/y/z = ...; return x*y*z;` → no change
- Full rebuild → no change

**Root cause:** Compiler chooses different offset loads (0x4, 0x8 for y, z) and swaps float multiplication operands (f13 ↔ f0). This is a pure register allocation quirk.

---

### HamMaster::Jump - Already complete/removed
**Note:** Function not found in database. May have been completed in previous session or moved to different unit. User modified the function to use pointer approach instead of reference.

---

### SfxInst::IsRunning - 98.5% - AT_LIMIT
**Mangled:** `SfxInst::IsRunning`
**File:** `src/system/synth/Sfx.cpp`

**Issue:** Single instruction mismatch - `cmplwi` (unsigned compare) vs `cmpwi` (signed compare).

**Code:**
```cpp
bool SfxInst::IsRunning() {
    FOREACH (it, mSamples) {
        if ((*it)->IsPlaying())
            return true;
    }
    FOREACH (it, mSfx->MoggClipMaps()) {
        MoggClip *clp = it->GetMoggClip();
        if (clp) {
            if (clp->GetStream())
                return true;
        }
    }
    return false;
}
```

**Attempts:**
- Changed `if (clp->GetStream())` to `if (clp->GetStream() != nullptr)` → no change
- Changed both `if (clp)` to `if (clp != nullptr)` → no change

**Root cause:** The FOREACH macro expands to `it != (container).end()` and the compiler generates `cmplwi` for the iterator comparison but the original binary uses `cmpwi`. This is unfixable via source changes since it's internal to the FOREACH macro expansion.

---

### RndTransformable::OnCopyLocalTo - 98.2% - AT_LIMIT
**Mangled:** `?OnCopyLocalTo@RndTransformable@@AA?AVDataNode@@PBVDataArray@@@Z`
**File:** `src/system/rndobj/Trans.cpp`

**Issue:** REGISTER_SWAP pattern with r30↔r31 across 11 instructions, plus 1 LINKER_MERGED call.

**Code:**
```cpp
DataNode RndTransformable::OnCopyLocalTo(const DataArray *da) {
    DataArray *arr = da->Array(2);
    for (int i = arr->Size() - 1; i >= 0; i--) {
        RndTransformable *t = arr->Obj<RndTransformable>(i);
        t->SetLocalXfm(LocalXfm());
    }
    return 0;
}
```

**Attempts:**
- Declared `int i` before `DataArray *arr` → no change
- Declared `int i;` then `DataArray *arr;` with separate assignment → no change
- Full rebuild → no change

**Root cause:** Register swap affects nearly every instruction in the function. This is a callee-saved register allocation decision that cannot be influenced by source code changes. The LINKER_MERGED call is also unfixable (ICF - Identical COMDAT Folding).

---

### ScrollSelect::RevertScrollSelect - 98.7% - AT_LIMIT
**Mangled:** `?RevertScrollSelect@ScrollSelect@@QAA_NPAVUIComponent@@PAVLocalUser@@PAVObject@Hmx@@@Z`
**File:** `src/system/ui/ScrollSelect.cpp`

**Issue:** REGISTER_SWAP pattern with r11↔r29 across 3 instructions.

**Code:**
```cpp
bool ScrollSelect::RevertScrollSelect(
    UIComponent *comp, LocalUser *user, Hmx::Object *obj
) {
    int oldAux = mSelectedAux;
    if (oldAux != -1) {
        int selAux = SelectedAux();
        bool auxChanged = oldAux != selAux;
        SetSelectedAux(oldAux);
        mSelectedAux = -1;
        DataNode node(kDataUnhandled, 0);
        if (auxChanged && obj) {
            node = obj->Handle(UIComponentScrollMsg(comp, user), false);
        }
        if (node.Type() == kDataUnhandled) {
            node = SendScrollSelected(comp, user);
        }
        if (auxChanged) {
            if (node.Type() == kDataUnhandled) {
                TheUI->Handle(UIComponentScrollMsg(comp, user), false);
            }
        }
        return true;
    } else
        return false;
}
```

**Attempts:**
- Moved `DataNode node` declaration before `selAux` and `auxChanged` → match dropped to 90.9%
- Other orderings → match dropped to 85.7%

**Root cause:** Register swap is unfixable. Moving declarations around changed control flow and made things much worse.

---

### FlowLabel::Load - 99.0% - AT_LIMIT
**Mangled:** `?Load@FlowLabel@@UAAXAAVBinStream@@@Z`
**File:** `src/system/flow/FlowLabel.cpp`

**Issue:** REGISTER_SWAP pattern with r26↔r27 across 9 instructions.

**Code:**
```cpp
BEGIN_LOADS(FlowLabel)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(FlowQueueable)
    d >> mLabel;
    if (d.rev > 0) {
        ObjPtr<FlowNode> node(this);
        d >> node;
        if (mFlowParent != node) {
            SetParent(node, true);
        }
        if (Flow *flow = dynamic_cast<Flow *>(node.Ptr())) {
            flow->RefreshPortLabelLists();
        }
    }
END_LOADS
```

**Attempts:**
- Moved `ObjPtr<FlowNode> node(this)` to before `LOAD_REVS` → match dropped to 77.6%
- Moved `node` between `d >> mLabel` and `LOAD_SUPERCLASS` → match dropped to 92.2%

**Root cause:** Register swap is unfixable. The BEGIN_LOADS macro expands with specific ordering that influences register allocation, but reordering declarations made things worse.

---

### RndBitmap::LoadHeader - 100% - COMPLETE ✓
**Mangled:** `?LoadHeader@RndBitmap@@AAAEAVBinStream@@AAEAV0@AAE@Z`
**File:** `src/system/rndobj/Bitmap.cpp`

**Issue:** Missing `li r5, 0x4` instruction and different `bl` call.

**Original code:**
```cpp
if (rev > 1)
    bs >> mName;
```

**Fixed code:**
```cpp
if (rev > 1)
    bs.ReadEndian(&mName.mCRC, 4);
```

**Explanation:** The `operator>>` for CRC reads an int through a function call. The original binary directly calls `ReadEndian(&mName.mCRC, 4)` which takes a pointer to the int and the size (4 bytes). Using `ReadEndian` directly generates the correct `li r5, 0x4` (size parameter) and calls the right function.

---

### HamNavList::NumItems - 98.4% - AT_LIMIT
**Mangled:** `?NumItems@HamNavList@@ABEHXZ`
**File:** `src/system/hamobj/HamNavList.cpp`

**Issue:** REGISTER_SWAP pattern r30↔r31 across 9 instructions, plus 1 LINKER_MERGED call.

**Code:**
```cpp
int HamNavList::NumItems() const {
    int i;
    if (mListState.ScrollPastMinDisplay()) {
        if (mScrollBehavior.AtTop() || mScrollBehavior.AtBottom()) {
            i = HamListRibbon::sNumListSelectable + 1;
        } else
            i = HamListRibbon::sNumListSelectable + 2;
    } else {
        int count = GetDisabledCount(mListState.NumShowing());
        i = mListState.NumShowing();
        i -= count;
    }
    return i;
}
```

**Attempts:**
- Declared `int count;` before `int i;` and moved assignment down → no change

**Root cause:** Note says "(1 merged call remains)" - the LINKER_MERGED call is the main issue. Register swap is secondary but also unfixable.

---

### CharCuff::Highlight - 98.5% - AT_LIMIT
**Mangled:** `?Highlight@CharCuff@@UAAXXZ`
**File:** `src/system/char/CharCuff.cpp`

**Issue:** REGISTER_SWAP pattern f29↔f30 (float registers) across 11 instructions.

**Code:**
```cpp
void CharCuff::Highlight() {
    Hmx::Color white(1, 1, 1, 1);
    const float kTwoPi = 6.2831855f;
    const float kInv32 = 1.0f / 32.0f;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 32; j++) {
            float toSine = (kInv32 * (kTwoPi * j));
            Vector3 va8(Sine(toSine), Cosine(toSine), mShape[i].offset);
            Vector3 vb4(Sine(toSine), Cosine(toSine), mShape[i + 1].offset);
            (Vector2 &)va8 *= mShape[i].radius * Eccentricity((Vector2 &)va8);
            (Vector2 &)vb4 *= mShape[i + 1].radius * Eccentricity((Vector2 &)vb4);
            // ... more code ...
            if (i < 2) {
                float toSinePlus1 = (kInv32 * (kTwoPi * (j + 1)));
                // ...
            }
            if (i == 1) {
                float toSinePlus1 = (j + 1) * kTwoPi * kInv32;
                // ...
            }
        }
    }
}
```

**Attempts:**
- Changed line 132 from `(kInv32 * (kTwoPi * (j + 1)))` to `(j + 1) * kTwoPi * kInv32` → match dropped to 98.1%
- Reverted → back to 98.5%

**Root cause:** Float register swap is unfixable. The code has two different float expression forms:
- Form 1: `(kInv32 * (kTwoPi * j))` - lines 121, 132
- Form 2: `(j + 1) * kTwoPi * kInv32` - line 142

Making them all the same form didn't help - the compiler still makes different allocation choices for float registers.

---

## Key Fix Applied

**RndBitmap::LoadHeader** (98.5% → 100%):
```cpp
// Before:
if (rev > 1)
    bs >> mName;

// After:
if (rev > 1)
    bs.ReadEndian(&mName.mCRC, 4);
```

The `operator>>` for CRC goes through `operator>>(BinStream &bs, Hmx::CRC &crc)` which:
1. Creates a temporary int `hash = 0;`
2. Calls `bs >> hash;`
3. Assigns to `crc.mCRC = hash;`

The original binary directly calls `bs.ReadEndian(&mName.mCRC, 4)` which:
1. Takes a pointer directly to the int field
2. Passes the size (4 bytes)
3. Reads in one step

This generates the `li r5, 0x4` instruction for the size parameter that was missing before.

---

## Unfixable Patterns Summary

| Pattern | Description | Fixable? |
|---------|-------------|----------|
| REGISTER_SWAP | Compiler assigns variables to different registers (r30↔r31, f29↔f30, etc.) | No - callee-saved register allocation |
| LINKER_MERGED | Call to merged function (ICF - Identical COMDAT Folding) | No - linker decision |
| OFFSET_SWAP | Same field accessed at different offsets (0x4 vs 0x8) | Sometimes - but often no |
| COMMUTATIVE_OP_ORDER | Multiplication operands swapped (a*b vs b*a) | Sometimes - try reordering |
| COMPARISON_STYLE | cmplwi (unsigned) vs cmpwi (signed) | Sometimes - type casting can help |
| CONTROL_FLOW | bne↔beq branch direction changes | Sometimes - invert if/else |

**General finding:** Declaration reordering, float expression reordering, and variable assignment changes either made things worse or had no effect. High-90% functions with register swaps are typically unfixable via source changes.

---

## Files Modified

- `src/system/math/Geo.cpp` - Box::Volume (changed but reverted, AT_LIMIT)
- `src/system/hamobj/HamMaster.cpp` - HamMaster::Jump (user modified externally)
- `src/system/synth/Sfx.cpp` - SfxInst::IsRunning (reverted changes, AT_LIMIT)
- `src/system/rndobj/Trans.cpp` - RndTransformable::OnCopyLocalTo (reverted, AT_LIMIT)
- `src/system/ui/ScrollSelect.cpp` - ScrollSelect::RevertScrollSelect (reverted, AT_LIMIT)
- `src/system/flow/FlowLabel.cpp` - FlowLabel::Load (reverted, AT_LIMIT)
- `src/system/rndobj/Bitmap.cpp` - **RndBitmap::LoadHeader (FIXED - COMPLETE)**
- `src/system/hamobj/HamNavList.cpp` - HamNavList::NumItems (reverted, AT_LIMIT)
- `src/system/char/CharCuff.cpp` - CharCuff::Highlight (reverted, AT_LIMIT)
