# Lane notes: `src/system/char/CharClipGroup.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?HasClip@CharClipGroup@@QBA_NPAVCharClip@@@Z`

`CharClipGroup::HasClip` -- moved verbatim from `src/system/char/CharClipGroup.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 88-139):

```text
// RESIDUAL (w8-i, 56.81 canonical / 55.41 fuzzy): the only structural difference is
// WHERE `end() = begin() + size()` is finished.  The image completes it before the
// call and holds the single pointer in r31 across `bl find` (one callee-save,
// __savegprlr_31); we keep `begin` in r31 and `size` in r30 and sink the
// `mulli`/`add` past the call (two callee-saves).  Every one of the 23 charged rows
// is downstream of that one scheduling choice.  Measured here, all at 56.81 canonical:
//   - `const_iterator e = mClips.end(); return e != mClips.find(clip);`  56.81 / 55.41
//   - `return mClips.find(clip) != mClips.end();` (operand swap)          56.81 / 55.19
// Binding end() to a named local does not materialise it -- the value is a bare
// pointer, so MSVC re-sinks the arithmetic regardless of statement boundaries.
// w21-h (56.81, unchanged; same 23 rows): three more spellings measured, all
// 56.81 canonical -- `!(mClips.find(clip) == mClips.end())`, an
// `if (find == end()) return false; return true;`, and a named
// `const_iterator it = mClips.begin() + mClips.size()` compared against find.
// Same residual as ObjPtrVec<RndDrawable>::merge (ObjPtrVec_impl.h): the image
// finishes end() (`mulli`/`add r31`) before `bl find` and holds ONE callee-saved
// register; we keep begin/size in two and sink the add.  Contrast: LightPreset::
// SyncKeyframeTargets' `mSpotlights.find(key) == mSpotlights.end()` (non-const)
// materialises end() before the call in OUR build too and reads 100, so the
// inline end() itself is not wrong.  find<CharClip> is `f i` in the map and
// same-TU: marking both ObjPtrVec::find templates `inline` (ObjPtrVec_impl.h)
// was measured with a full ninja + whole-binary compare -- UP 0 / DOWN 0, inert.
// w21-q (56.81, unchanged; same 23 rows): three more measured, each with the
// built object -- `const ObjPtrVec<CharClip> &clips = mClips;` bound once
// (43.0, worse: the reference splits the mNodes loads), named `e`/`f` iterators
// compared as `f != e` (56.81), and end() taken through the NON-const overload
// via const_cast (56.81 -- so const vs non-const end() is not what separates
// this from LightPreset::SyncKeyframeTargets).  Behaviour matches the image.
// w21-am (56.81, unchanged; same 23 rows): eight more measured with the
// built object, all 56.81 / 55.41 -- `if (end != find) return true; return
// false;`, named e/f iterators compared as `e != f`, a named bool, `!(end ==
// find)`, `? true : false`, `if (end == find) return false;`, and
// `begin() + n` with a named size; a `const &clips` binding is 43.0 again.
// The `find != end` swap changes only the subf operand order (image is end -
// find, so the current `end != find` is the image's order).  Only one const
// find caller exists in the whole image, so there is no matched sibling to
// copy; the non-const sites (LightPreset::SyncNewSpotlights, 100) compare
// inside an `if`, which the `if` spellings above did not reproduce here.
// w24-c1 (56.81, unchanged; same 23 rows): `mClips.find(clip) !=
// mClips.end()` re-measured -- it only flips the subf operands and the end()
// add is still sunk past `bl find`; 56.8 canonical either way.
// w31-d (56.81 -> 100, 128 B = the image's 128 B): the residual was exactly
// the c2 schedule/colour chicken-and-egg documented in the C2RS-BRIDGE:
// `end()` = begin() + size() is not a stored pointer, so c2 sank the
// `mulli`/`add` past `bl find`, which made the COUNT live across the call --
// a nonvolatile COLOR candidate (tap: id 3 sym 4/k04 pri 7 tie 6 allowed
// { r14..r30 } -> r30) -- and the extra saved register is the 8-byte frame.
// Binding `end()` to a const REFERENCE (C2RS-BRIDGE 8.8: bind, do not copy)
// materialises the iterator before the call: the image's shape, one saved
// GPR (r31), the tap's two nonvolatile candidates collapse to one.  A plain
// named copy (or `find != end`, `begin()+size()`, a named count, an if/else)
// is folded back by c1 and stays at 56.81.
```


## `ClampTo()`

`ClampTo` -- moved verbatim from `src/system/char/CharClipGroup.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Attribution: no function named `ClampTo` in report.json (unit); filed under the source name

Above the definition (was lines 153-158):

```text
// w19-c: ClampTo is OUR name -- the image only shows that both clamps below
// went through an inlined by-reference helper: each stores its Min()
// unconditionally (`blt`/`bge` over one `mr`, then the `stw`) and the member is
// RELOADED afterwards instead of forwarded.  Written as plain
// `mWhich = Min(...)` MSVC forwards the value into a callee-saved register and
// renumbers r24-r31 (93.2); through the reference it does not (100).
```


## `?GetClip@CharClipGroup@@QAAPAVCharClip@@H@Z`

`CharClipGroup::GetClip` -- moved verbatim from `src/system/char/CharClipGroup.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 161-167):

```text
// w19-c (94.87 -> 100): three changes, each measured.  (1) the two scans are
// plain `while` loops, not hand-rotated `if (c) do {} while (c)` (-> 96.1, the
// r30/r31 `this`/`pos` swap is a rotation artefact); (2) the clamps go through
// ClampTo above (-> 97.5); (3) each scan advances through a fresh `next`
// local, wrapped and then assigned back, which is the image's xoris-before-subf
// order for the branchless wrap (-> 100); `pos++; pos -= ...` schedules the
// subf first.
```
