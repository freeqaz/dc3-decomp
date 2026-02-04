# Fixable Patterns: Bool Mask

**Prevalence:** 33 functions tagged (database)
**Often fixable** — do not skip this pattern without trying the steps below.

The compiler inserts `clrlwi rN, rN, 24` to mask a value to 8 bits at bool type boundaries.

## Step 1: Detect

Look for `clrlwi` with `, 24` in objdiff output:
```
| delete | clrlwi r29, r29, 24 |    ← target has it, our build doesn't
| insert |                     | clrlwi r3, r11, 24    ← our build has it, target doesn't
```

## Step 2: Determine Direction

- **Target has `clrlwi`, our build doesn't** (`delete`) → This is the fixable direction. Go to Step 3.
- **Our build has `clrlwi`, target doesn't** (`insert`) → Usually unfixable. Go to Step 4.

## Worked Example: `PartyModeMgr::SetSongAndDefaults`

objdiff showed a single `delete` at instruction 121:
```
[118] equal      cmplw  cr6, r25, r11          | cmplw  cr6, r25, r11
[119] equal      bne    cr6, 0x79e4            | bne    cr6, 0x1ec
[120] equal      li     r29, 0x1               | li     r29, 0x1
[121] delete     clrlwi r29, r29, 24           |                        ← target masks r29 to bool
[122] equal      bl     MetaPerformer::Current  | bl     MetaPerformer::Current
...
[131] equal      mr     r5, r29                | mr     r5, r29         ← r29 passed as bool arg
```

**Reading the context:** r29 is set to 1 by a comparison (instructions 107-120 are a `mode == dance_battle || mode == strike_a_pose` check). Then r29 is passed as argument r5 to `CalcCharacters`. The target masks it to bool width; our build doesn't.

**The source had:**
```cpp
MetaPerformer::Current()->CalcCharacters(data, mode == dance_battle || mode == strike_a_pose, ...);
```

**Fix:** Extract to local bool:
```cpp
bool isSpecialMode = mode == dance_battle || mode == strike_a_pose;
MetaPerformer::Current()->CalcCharacters(data, isSpecialMode, ...);
```
Result: `delete` count 1→0, sizes matched, BOOL_MASK pattern gone.

## Step 3: Fix (target has mask, we don't)

**Bool funniness usually means there's an inline.** The `clrlwi` appears at bool type boundaries — inline function returns, local bool assignments, explicit casts. If the original code went through a bool-typed intermediate and our code doesn't, we'll be missing the mask.

Try these in order. Rebuild and check after each one.

**3a. Find the instruction context.** Look at the ~5 instructions before the `clrlwi`. Identify what register holds the bool value and what produced it (comparison? function call? logical or/and?). Then look at what consumes it (passed as argument? stored? returned?).

**3b. Extract to a local `bool` variable.** If a bool expression is passed directly as a function argument or used inline, extract it to a named local. This forces the compiler to treat it as a bool at the assignment boundary.

```cpp
// BEFORE — bool expr passed directly as argument, no mask generated:
Func(data, mode == dance_battle || mode == strike_a_pose, ...);

// AFTER — local bool forces compiler to mask at assignment:
bool isSpecialMode = mode == dance_battle || mode == strike_a_pose;
Func(data, isSpecialMode, ...);
```
*Real fix: `PartyModeMgr::SetSongAndDefaults` — 97.8% → 98.2%, bool mask eliminated.*

**3c. Add explicit `(bool)` cast.** Works for ternary expressions where one branch isn't typed as bool.

```cpp
// BEFORE — no mask:
_msg->Size() > 3 ? _msg->Int(3) != 0 : false
// AFTER — cast forces mask:
_msg->Size() > 3 ? (bool)(_msg->Int(3) != 0) : false
```
*Real fix: `RndTransformable::Handle` — 99.9% → 100%.*

**3d. Check for a missing inline function.** The original code may have called a bool-returning inline that we're writing as a raw expression. To find it:
1. Use `lookup_rb3` to check if the RB3 decomp uses an inline helper at that point
2. Check headers for existing inline bool helpers (`streq()`, `IsAsciiNum()`, `PowerOf2()`, etc.)
3. Check if peer functions in the same translation unit use a common bool helper

## Step 4: When It's Actually Unfixable

If our build generates `clrlwi` that the target doesn't have (`insert` direction), or if all Step 3 approaches fail, accept the gap. These source-level changes have been tried and do not remove an unwanted `clrlwi`:

- `return 1` instead of `return true`
- Direct condition return (`return ptr != NULL`)
- Ternary (`return x ? true : false`)

**Typical unfixable gap:** ~1-3%.

---

## See Also

- [fixable-declarations.md](fixable-declarations.md#variable-extraction) - Variable extraction (related technique)
- [fixable-casting.md](fixable-casting.md) - Other casting fixes
- [unfixable-compiler.md](unfixable-compiler.md) - Unfixable compiler patterns
