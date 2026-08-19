# Struct Layout Mismatches

Struct layout issues cause **systematic offset mismatches** across all functions that access the wrong-sized members. Unlike stack-frame offset diffs (compiler artifacts), struct layout bugs affect every function touching the struct.

## How to Identify

### 1. Check offset comments against sizeof

For any struct with offset comments, verify the math:

```
Expected next offset = current offset + sizeof(member type)
```

Common type sizes (ILP32 / Xbox 360):
- `bool`: 1 byte (aligned to 1)
- `int`, `float`, pointer: 4 bytes
- `Vector3`: 12 bytes (3 floats)
- `Vector4` / `XMVECTOR`: 16 bytes (4 floats)
- `Transform`: 64 bytes (4x Vector3 + padding varies)
- `ObjPtr<T>`: 20 bytes (0x14)
- `String`: 8 bytes (0x8)
- `Symbol`: 4 bytes
- `std::vector<T>`: 12 bytes (0xC) — start, end, capacity

### 2. The gap test

If the gap between two consecutive offset comments is **larger** than the expected sizeof for the member between them, the member type has the wrong size or needs padding.

**Example — 16-byte stride Vector3 arrays:**
```cpp
Vector3 mPositions[20]; // 0x4
int mFlags[20];         // 0x144  ← GAP: 0x144 - 0x4 = 0x140 = 320
                        //   But 20 * sizeof(Vector3) = 20 * 12 = 240 = 0xF0
                        //   320 ≠ 240 → STRIDE MISMATCH (need 16-byte stride)
```

### 3. objdiff offset patterns

When struct layout is wrong, `run_diff_inspect mode=offsets` shows:
- **Consistent deltas on member access** (e.g., all `+4` or `+0x50` on the same base register)
- Deltas that are multiples of the per-element size difference
- Applies to `lwz`, `stw`, `lfs`, `stfs` with struct base registers (not r1/stack)

**Contrast with stack layout diffs:** Stack offsets are r1-relative and indicate local variable placement (unfixable compiler artifact). Struct offsets use other registers (r3, r4, etc.) and indicate incorrect type sizes.

### 4. Cross-reference with RB2 DWARF

Use `mcp__orchestrator__get_rb2_class_info` to check actual member offsets from RB2 debug info (shared Milo engine classes often have the same layout).

## Common Cases

### Padded Vector3 arrays (16-byte stride)

The most common struct layout issue. NUI/Kinect skeleton data uses 16-byte-aligned joint position arrays (matching `XMVECTOR` stride), but the decomp headers may use `Vector3` (12 bytes).

**Symptom:** Array of `Vector3[kNumJoints]` where offset comments show 16-byte-per-element stride (gap of `kNumJoints * 16`, not `kNumJoints * 12`).

**Fix:** Use a padded wrapper struct:
```cpp
struct PaddedJointPos {
    float x, y, z, _pad;
    PaddedJointPos() {}
    PaddedJointPos(const Vector3 &v) : x(v.x), y(v.y), z(v.z), _pad(0) {}
    operator Vector3 &() { return *(Vector3 *)&x; }
    operator const Vector3 &() const { return *(const Vector3 *)&x; }
    PaddedJointPos &operator=(const Vector3 &v) {
        x = v.x; y = v.y; z = v.z; return *this;
    }
};
```

The reference conversion operators (`operator Vector3&`) allow seamless use with existing APIs that take `Vector3&` parameters or return `const Vector3&`.

**Affected structs in DC3:**
| Struct | Header | Members fixed |
|--------|--------|---------------|
| SkeletonData | gesture/Skeleton.h | mRawPositions, mJointPositions |
| ArchiveSkeleton | gesture/ArchiveSkeleton.h | mJointPoses |
| DetectFrame | hamobj/DetectFrame.h | mBestNodeErrors, mNodeComponentWeights |
| RndVelocityBuffer | rndobj/VelocityBuffer.h | mFrustumCorners |
| RhythmDetector | hamobj/RhythmDetector.h | unkaac |
| DancerSkeleton | hamobj/DancerSkeleton.h | mCamJointPositions, mCamJointDisplacements |
| HamSkeletonConverter | hamobj/HamSkeletonConverter.h | mJointPositions |
| ErrorFrameInput | hamobj/ErrorNode.h | mJointDisps, mBaseJointDisps, mJointPositions, mBaseJointPositions |
| RecordedFrame | gesture/SkeletonClip.h | unk2c |

### Missing or extra padding between members

**Symptom:** Gap between two members is 1-3 bytes larger than expected, suggesting alignment padding the compiler inserts but the header doesn't account for.

**Fix:** Add explicit padding bytes or reorder members to match alignment.

### Loose scalar fields that are really an aggregate member

**Symptom:** A constructor is well below 100% with insert/delete clusters, and a
[dead temp-slot store census](fixable-inline-boundary.md#empirical-yield-2026-08-06--census-tool-and-measured-results)
shows the *target* has more of those stores than we do (`delta > 0`). The target
computes `&this-><something>` where our header declares several adjacent
same-typed scalars.

**Fix:** Group the adjacent scalars into the aggregate they actually are, and
initialise it as one member in the initialiser list. `HamAudio::HamAudio`
(8 loose crossfade fields → two `HamCrossfade` structs) went 92.8% → 100%, and
`NgPostProc::NgPostProc` (4 loose floats → two `Vector2`s) 79.3% → 100%.

**Corroborate before editing:** look for a sibling function that touches all the
fields together in offset order — that is a longhand struct assignment, and it
is independent evidence. A placement-new temp that merely *reproduces* the
sub-object address does **not** work; the field has to be a real member.

### Inheritance offset shift

**Symptom:** All member offsets are shifted by a fixed amount from the base class size.

**Fix:** Check parent class size. Virtual inheritance adds vbptr (4 bytes on PPC). Multiple inheritance adds vtable pointers.

> **Do not extend this to base *declaration order*.** Under multiple inheritance MSVC
> hoists the polymorphic base to offset 0 regardless of the order the bases are written,
> so a base-adjustment tell in the target constrains a base's **offset** and says nothing
> about the declaration list. See the next section before acting on one.

## Base Declaration Order Does Not Set Base Offsets (MSVC Hoists the Polymorphic Base to 0)

**This is a negative result — a triage rule, not a lever.** It exists to stop a
plausible-looking lead that costs a full-rebuild cycle to chase.

### The measurement

Run the toolchain's own layout dumper rather than reasoning about it:

```bash
cl.exe /d1reportAllClassLayout ...   # X360/16.00.11886.00, the project toolchain
```

For `class String` (`src/system/utl/Str.h`), **both** base orders emit a byte-identical
dump:

```
class String	size(8):
	| +--- (base class TextStream)
 0	| | {vfptr}
	| +--- (base class FixedString)
 4	| | mStr
```

`TextStream` carries the vfptr, so MSVC puts it at 0 whether the declaration reads
`String : public FixedString, public TextStream` or `String : public TextStream, public
FixedString`. `FixedString::mStr` lands at +4 either way.

### What that means for a base-adjustment tell

An MI base adjustment in the target — the null-guarded `p ? p + delta : 0` shape — proves
the base's **offset**. It cannot discriminate declaration order, because both orders
produce the same offset.

`CharLipSync::Print` (`src/system/char/CharLipSync.cpp`) is the case that looked like
proof and was not. Target, idx 62-71 (verified by `run_objdiff full_listing=true`):

```
62  add.  r10, r27, r11        ; element address, sets CR0
64  addi  r25, r10, 0x4        ; -> FixedString sub-object (+4)
65  bne   ...                  ; non-null path
66  mr    r25, r21             ; null -> 0
71  lwz   r4, 0x0, r25         ; load mStr through the adjusted pointer
```

That reads "`FixedString` is at +4", which is exactly where our build already puts it —
our own base side loads `mStr` from `0x5c` off a `String` temp based at `0x58`, i.e. +4.
The tell is **satisfied**, not violated. Reading it as an argument for swapping the base
list is reading a constraint that is already met.

### What *does* discriminate: base construction order

Declaration order does control one observable thing — the order base subobjects are
constructed. Read that out of a constructor instead:

In the target's `String::String(const String &)`, `FixedString`'s `gEmpty` setup is
inlined **first**, then the empty `TextStream` ctor is called, then `??_7String@@6B@` is
stored. That is FixedString-then-TextStream, i.e. the order the tree already ships.

Two gotchas when reading a constructor this way:

- **Read the target's own listing — no diff ruler will show you this.** The target's
  call there disassembles as `bl ??1?$StackString@$0IA@@@UAA@XZ` in the slot where we
  emit `bl ??0TextStream@@QAA@XZ`; ICF merged the two empty functions, so the
  constructor scores 100% and the evidence is masked.

  > **Corrected 2026-08-19.** This bullet used to say *"use raw diff mode —
  > `run_diff_inspect diff_mode=raw`"*. That does not work, and did not work then.
  > Re-measured on `??0String@@QAA@ABV0@@Z` (non-equal instruction rows):
  > `none` **0**, `name_check` **0**, `all` **7** — and all 7 of the `all` rows are the
  > *same symbol on both sides* (`?gEmpty@@3PADA`, `??_7String@@6B@`,
  > `??4String@@QAAAAV0@PBD@Z`), i.e. pure addend noise. **The
  > `??1?$StackString@…` row appears under no ruler at all**, because
  > `build/373307D9/icf_aliases.map` proves the fold and objdiff masks it as
  > `reloc_ignored` regardless. On top of that, `diff_mode` never reached
  > `run_diff_inspect`'s analysis modes in the first place (see
  > [docs/tools/REFERENCE.md](../../tools/REFERENCE.md#the-relocation-ruler-three-rulers-and-which-one-to-reach-for)).

  What actually works is diffing the target object **against itself** and reading the
  target column — one of the named legitimate direct-CLI uses:

  ```sh
  T=build/373307D9/obj/system/utl/Str.obj
  bin/objdiff-cli diff -1 $T -2 $T '??0String@@QAA@ABV0@@Z' --include-instructions
  #   ... bl ??1?$StackString@$0IA@@@UAA@XZ      <- the merged name, visible
  ```

  If you want a *diff* that charges wrong callees generally, the ruler is
  `name_check`, not `raw` — but for an ICF fold the alias map has already
  forgiven it, which is the whole reason you must go to the listing.
- **You cannot repair a base-order regression from the mem-init list.** MSVC reorders a
  mem-init list back to declaration order, so rewriting it changes nothing.

### The whole-build signature of a construction-order-only edit

Worth recognising on sight, because it lets you stop after one measurement instead of
bisecting. Applying the base swap and rebuilding gave:

| Metric | Result |
|--------|--------|
| Overall fuzzy | 53.83% → 53.83% (**−0.00%**) |
| Regressions | exactly **4**, all four `String` constructors (100% → 34.6 / 42.0 / 55.0 / 63.7) |
| Other movers anywhere in the binary | **zero** |

**One subsystem, constructors only, no other movers** is the fingerprint of a
layout-neutral, construction-order-only edit. If a layout had actually moved, every
function that touches the class would have moved with it. Seeing this shape means the
edit did not do what you thought it did — stop and re-read the tell.

### Ground-truth sources that do *not* work here

- **RB2 DWARF is unusable for `String`.** RB2's `String` predates `FixedString` entirely:
  `class String : public TextStream`, size `0xC`, `mCap`@4 / `mStr`@8. The
  [`rb2-class`](../../tools/INDEX.md) skill will answer confidently and wrongly. Check
  the *member set* matches before trusting a DWARF layout for a class that evolved
  between titles.
- **A stale source comment is not evidence.** The `// TODO(hugh)` this lead came from had
  the two base offsets stated backwards and claimed the copy ctor was at 96.7% with an
  extra `TextStream` ctor call. All four ctors were at 100% at baseline, and the edit is
  an ordering swap, not an extra call. `../og-dc3-decomp/src/system/utl/Str.h` uses the
  same declaration order and its comment was the accurate one.

### Cost note

`src/system/utl/Str.h` is in the PCH. Any edit there forces a **~830-step full rebuild**,
so budget accordingly before testing a hypothesis on it — and prefer settling the
question from `/d1reportAllClassLayout` plus a constructor diff, which costs nothing.

Settled in `9065a8f6` (comment-only; the source order was already correct). The full
reasoning is preserved in the class comment at `src/system/utl/Str.h:62-98`.

## Verification Checklist

After fixing a struct:
1. **Build succeeds** — fix type conversion errors with casts where needed
2. **Existing COMPLETE functions stay COMPLETE** — run `batch_check` on affected units
3. **Partial functions improve** — check if offset mismatches decrease
4. **Header changes need touch** — `touch src/path/file.cpp && ninja` since ninja doesn't track header deps

## Distinguishing Struct vs Stack Offset Diffs

| Characteristic | Struct layout bug | Stack layout (unfixable) |
|---------------|-------------------|--------------------------|
| Base register | r3, r4, etc. (param/member) | r1 (stack pointer) |
| Consistency | Same delta across ALL functions using the struct | Varies per function |
| Fix | Change struct member types/padding | Cannot fix (compiler artifact) |
| Diagnosis tool | `run_diff_inspect mode=offsets` | Same tool, but check register |

## Tooling

### Integrated validator (`struct_db.py validate`)

The primary tool. Built into the struct database, with RB2 DWARF cross-validation:

```bash
# Show only stride mismatches (most actionable)
python3 tools/struct_db.py validate --stride-only

# Show only confirmed 16-byte stride issues
python3 tools/struct_db.py validate -t stride_16

# Store results in layout_issues table for querying
python3 tools/struct_db.py validate --stride-only --store

# All issues (noisy — many false positives from hidden members)
python3 tools/struct_db.py validate
```

When `--store` is used, issues go into `struct_db.sqlite`'s `layout_issues` table:
```sql
SELECT c.name, li.member_name, li.issue_type, li.details
FROM layout_issues li
JOIN classes c ON c.id = li.class_id
WHERE li.issue_type = 'stride_16';
```

### Legacy scanner (`tools/find_struct_gaps.py`)

Standalone script, more verbose output but not integrated with the DB.

### MCP tools

```bash
# Check a class layout from RB2 DWARF
mcp__orchestrator__get_rb2_class_info class_name="ClassName"

# Look up what field is at a specific offset
mcp__orchestrator__lookup_struct_offset class_name="ClassName" offset="0x48"

# Check offset mismatches in a function
mcp__orchestrator__run_diff_inspect symbol="..." mode=offsets project_dir="."
```

## Common Pitfalls

### Name collisions
`PaddedJointPos` was originally named `JointPos`, which collided with `BaseSkeleton::JointPos()` (a virtual method). MSVC resolves the method name over the struct type in derived class scope. Always check for method name collisions when naming structs.

### `operator[]` not inherited through conversion
MSVC doesn't apply implicit `operator Vector3&()` for `operator[]` calls. If code uses `padded[i][j]`, you need an explicit `operator[]` on PaddedJointPos.

### Pointer conversion needs explicit cast
`operator Vector3&()` handles reference conversion but NOT pointer conversion. When passing `PaddedJointPos*` to `Vector3*`, use `(Vector3 *)array` or `(const Vector3 *)array`.
