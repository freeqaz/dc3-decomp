# Forearm Twist Fix — 2026-03-24

## Problem

Characters in the native port had rigid/straight forearms during gameplay. Forearm
vertices deformed as if attached to the upper arm bone, with no twist or bend distinction.
The bug affected all characters (players and backups) on every song.

## Root Cause

**Pollable ordering + SetWorldXfm dirty cascade.**

`CharForeTwist::Poll()` and `CharUpperTwist::Poll()` have no dependency edge in the
`CharPollGroup` topological sort — they declare unrelated `changedBy`/`change` sets in
`PollDeps()`. Their relative order is arbitrary, determined by container iteration order
and memory layout, which differs between Xbox 360 and the native port.

On Xbox 360, CharUpperTwist happened to run first. On native, it ran second:

1. **CharForeTwist::Poll()** writes foreTwist1/2 world transforms via `SetWorldXfm()`.
   This sets `mWorldXfm` and clears `mDirty`, but does NOT update `mLocalXfm`.

2. **CharUpperTwist::Poll()** writes upperArm via `SetWorldXfm()`. This cascades
   `SetDirty()` to all children of upperArm, including foreTwist1/2.

3. At **render time**, `FillBoneUniforms()` calls `WorldXfm()` on each bone. For
   foreTwist1/2, `mDirty=true`, so `WorldXfm_Force()` recomputes from the stale
   `mLocalXfm` (clip-derived bind pose). CharForeTwist's output is discarded.

4. ForeTwist skin matrices become identical to the upper arm's — rigid forearms.

## Fix

After `SetWorldXfm()` in `CharForeTwist::Poll()` and `CharUpperTwist::Poll()`,
back-compute and store `mLocalXfm` so dirty-cascade recomputation reproduces the
correct world transform:

```cpp
#ifdef HX_NATIVE
{
    Transform invParent;
    Invert(bone->TransParent()->WorldXfm(), invParent);
    Multiply(worldXfm, invParent, bone->mLocalXfm);
}
#endif
```

This is order-independent: even if a later pollable dirties the bone, `WorldXfm_Force()`
will recompute the correct world from the updated local.

## Files Changed

- `src/system/char/CharForeTwist.cpp` — local-update after both `SetWorldXfm()` calls
- `src/system/char/CharUpperTwist.cpp` — local-update after both `SetWorldXfm()` calls
- `src/system/rndobj/Trans.h` — added `friend class CharUpperTwist` for `mLocalXfm` access

## Why Not a Systemic Fix

A systemic fix (back-computing `mLocalXfm` inside `SetWorldXfm()` itself) was attempted
but caused stretched geometry artifacts. `SetWorldXfm()` is called in many contexts beyond
pollables — during loading, scene setup, constraint evaluation — where parent transforms
may not be valid yet, bones may have non-default constraints, or rotation matrices may
include scale. The back-computation produces garbage in those contexts.

The per-file approach is safe because pollable `Poll()` methods run during gameplay on
fully-initialized bones with valid parent hierarchies and `kConstraintNone`.

## Other Vulnerable Pollables

15 additional CharPollable subclasses use `SetWorldXfm()` in their `Poll()` methods and
are theoretically vulnerable to the same ordering bug:

| Class | SetWorldXfm calls | Risk |
|-------|-------------------|------|
| CharBlendBone | 1 (targets) | Medium — shoulder blendbones write to twist bones |
| CharIKHand | 7 (hand, shoulder) | Low — foot IK only in DC3 gameplay |
| CharHair | 2 (physics bones) | Low — hair bones rarely share ancestors with twist |
| CharEyes | 3 (lids, target) | Low — eye bones are isolated from arm hierarchy |
| CharIKHead | 2 (spine, offset) | Low — spine chain is above arm branches |
| CharIKFingers | 2 (output trans) | Low — no finger IK on backup dancers |
| CharSleeve | 2 (sleeve bones) | Low — sleeve bones are leaf nodes |
| CharBoneTwist | 1 (average bone) | Low — rarely used in DC3 |
| CharBoneOffset | 1 (dest bone) | Low — simple offset, leaf nodes |
| CharPosConstraint | 1 (constrained) | Low — position-only, isolated |
| CharLookAt | 1 (pivot) | Low — head/eye area |
| CharIKRod | 1 (dest) | Low — not used in DC3 gameplay |
| CharGuitarString | 1 (bend) | None — guitar, not character |
| CharIKMidi | 1 (bone) | None — MIDI animation |
| CharIKScale | 2 (dest, targets) | None — not used in DC3 gameplay |

These can be fixed on-demand using the same pattern if visual issues are observed. The
diagnostic approach is established:

1. Log `SetDirty_Force()` for the suspect bone name (check `wasDirty=0` after a write)
2. Look for `WRITE` → `DIRTY` sequences confirming ordering conflict
3. Apply the `Invert(parent) + Multiply` local-update fix in the pollable's `Poll()`

## Diagnostic Evidence

Before fix — skin matrices at draw time (6 decimal places):
```
bone_L-upperArm:    skin[0..2] = 0.942566  0.089409  0.321832
bone_L-foreTwist1:  skin[0..2] = 0.942566  0.089409  0.321832  (IDENTICAL)
bone_L-foreTwist2:  skin[0..2] = 0.942566  0.089409  0.321832  (IDENTICAL)
```

After fix:
```
bone_L-upperArm:    skin[0..2] = 0.948758  0.081575  0.305294
bone_L-foreTwist1:  skin[0..2] = 0.470068 -0.396509  0.788472  (twist applied)
bone_L-foreTwist2:  skin[0..2] = 0.456815 -0.496763  0.737782  (more twist)
```

Dirty-cascade trace confirming the ordering:
```
FORETWIST-WRITE foreTwist_L.ik (backup0)          # CharForeTwist writes
FORETWIST-WRITE foreTwist_R.ik (backup0)
UPPERARM-SETWORLDXFM bone_L-upperArm (backup0)   # CharUpperTwist overwrites ancestor
FORETWIST-DIRTY bone_L-foreTwist1 wasDirty=0      # cascade destroys twist output
FORETWIST-DIRTY bone_L-foreTwist2 wasDirty=0
```

## What Was Ruled Out

| Suspect | Result |
|---------|--------|
| `RemoveInvalidBones()` stale indices | No bones removed during gameplay |
| Render-time dirty bones | All `dirty=0` — already recomputed from stale locals |
| Compressed vertex decode | Exhaustive byte-level verification, correct |
| `CharForeTwist::Poll()` math | Matches Ghidra decompilation and RB3 reference |
| CharBones decompiler bugs | All had existing `#ifdef HX_NATIVE` fixes |
| GPU skinning / matrix conventions | Verified correct end-to-end |
| `CharForeTwist` offset values (0/180) | Matches original game data |
| Backup outfit merge dropping pollables | Counts preserved after merge |

## Key Takeaway

`SetWorldXfm()` is a write-world-only operation — it does not back-compute the local
transform. Any later `SetDirty()` cascade (from an ancestor's `SetWorldXfm()`) causes
`WorldXfm_Force()` to recompute from the stale `mLocalXfm`, silently discarding the
procedural animation output. The dirty flag being cleared by the recomputation makes
this invisible at render time (`dirty=0`), making it a particularly sneaky class of bug.
