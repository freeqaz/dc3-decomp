# Lane notes: `src/system/char/Waypoint.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ShapeDeltaBox@Waypoint@@AAAXABVVector3@@MMAAV2@@Z`

`Waypoint::ShapeDeltaBox` -- moved verbatim from `src/system/char/Waypoint.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 219-246):

```text
// w17-c (99.968, 20 rows in the f2 > 0 arm): all 49 explicit Dot spellings
// are inert; spelling the Subtract per component through a named WorldXfm().v
// (reference or copy) and spelling Scale/ScaleAdd per component are both far
// worse (910+ vs 114 probe diff).
// w21-as: dotx and doty are written in the image's associations, (z + y) + x
// and (z + x) + y (doty: target
// 0x823CBE8C fmuls 0x18*dz, 0x823CBE94 fmadds 0x10*res.x, 0x823CBE9C fmadds dy*0x14) --
// a native-fidelity fix; Dot() sums (x + y) + z.  (dotx alone is probe-inert.)  Normalized unchanged at
// 99.96774, fuzzy 99.081 -> 99.121, 20 -> 15 rows; it also restores the
// image's x, y, z store order after the subtract.  REMAINING: inside the
// {z, x} pair MSVC emits the res.x product first (image: z first), which
// drags the load order and f8/f9 allocation of the clamp block.  Probed
// (standalone cl.exe, exact repro of the old listing): both pair operand
// orders, every association of dotx and doty, Dot arg orders, the subtract as
// z/y/x-declared locals through a named WorldXfm().v (all 6 orders), and the
// dots over those locals instead of res: none emits z first in that pair.
// w26-c: 13 further spellings, all inert at 109/124 -- the pair is a c2
// post-lowering commutative-sort order (C2RS-BRIDGE 8.7), not clean-upgrade
// noise.  Both products of that pair are subexpressions, so their keys are
// (SU<<24)|(nodes<<16)|hash16 and equal node counts leave hash16 deciding; the
// lever has to change one operand's *shape*.  Measured inert: reference
// aliases of `world` and of `world.m` (single and double bindings), reference
// aliases of the m.x / m.y rows (single and double, each alone and both),
// reference aliases of res (a double binding adds an instruction, 44/126),
// float references to the normalDir components, and both pair source orders.
// The lever that DID flip an equivalent row this wave (kdTree::FindSplit_Mean,
// CharCollide::GetRadius) is rebinding a memory operand's base reference; here
// both operands are already bound and re-binding them moves nothing.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. The delta dot products pair (d.x,m+0x10), (d.y,m+0x14) and (d.z,m+0x18) identically on both sides; only the association differs. The f3-f8 at WorldXfm_Force are dead leftovers.


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/Waypoint.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 274-278; no definition follows it):

```text
// w8-c: same shape as CharCuff.cpp's std::list<RndMesh *> note -- Waypoint.obj
// carries `std::vector<Waypoint *>` COMDATs in the image (push_back 0x823CCDA0,
// _M_insert_overflow 0x823CCCA8, allocate 0x823CBEF0, deallocate 0x823CBF30)
// whose only caller in the whole binary is CharacterTest.s:0x823DC7F8.
// Waypoint.obj won the fold, so retail's Waypoint.cpp odr-used one too.
```

File-level (was lines 281-287; no definition follows it):

```text
// w8-c: orphan COMDAT, and like NormalizeTo in CharBonesSamples.cpp it is not a
// template -- `Rand::Int(int, int)` is defined in the body of class Rand in
// math/Rand.h, so it is implicitly inline.  The image's Waypoint.obj carries it
// out-of-line at 0x823CC090 (Waypoint.s) with no caller in the unit; the `bl`
// sites are in math/Rand.s, synth/MicNull.s, world/Crowd.s, world/CameraManager.s
// and lazer/meta_ham/ContextChecker.s.  Waypoint.obj won the fold.  Taking the
// address forces emission; see the linkage warning in that other note.
```
