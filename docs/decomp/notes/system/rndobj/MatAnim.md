# Lane notes: `src/system/rndobj/MatAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Copy@RndMatAnim@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

`RndMatAnim::Copy` -- moved verbatim from `src/system/rndobj/MatAnim.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 86-122):

```text
// w21-y (was 97.89, 2 rows: the `stw sOwner` sits one slot later in the
// image, after `subi r4, r31, 0x88` for mKeysOwner = this).  Behaviour checked
// against the image: ty==1 / ty==2 && owner!=m take the CopyRef branch, the deep
// path sets sOwner, mKeysOwner and the six key vectors -- identical; the
// remaining call-name rows are ICF folds.  MEASURED NEGATIVE: SetObjConcrete
// directly (inert), static_cast<Hmx::Object *>(this) (inert), swapping the two
// assignments (93.67, store after the call), `RndMatAnim *self = this;` for
// both = 98.87 (shipped): the store pair then matches, but MSVC CSEs the
// image's second `this` (`subi r4, r31, 0x88`) into the conversion's register,
// so 5 rows remain (the conversion reads via r4 instead of r31).
// w26-s: the residual is a c1 CSE, and 14 spellings were MEASURED INERT at
// 98.87: both stores through `this` (97.89), self/this mixed either way, two
// distinct named copies, `*const self`, `RndMatAnim &self = *this`, `&*self`,
// a cast at either use, a named `Hmx::Object *obj = self`, and a nested scope
// around the second store.  Shape read off the image: the conversion base is
// `r11` and `subi r4, r31, 0x88` is re-materialized for the ObjOwnerPtr call,
// i.e. the two RHSs are not CSE'd there at all; ours keeps ONE value in r4 and
// copy-prefers it into the call.  Hypothesis (not measured): the image's r11 is
// regasg first-fit, so the split follows from the conversion not being
// copy-preferred to r4 -- the open question is which source makes the second
// RHS a distinct value.
// w-this-adjust (2026-11-13, objdiff 4.2.9 name_check): POINTER AUDIT, NOT A
// BUG.  The `subi r11,r11,0x84` vs `addi r11,r11,0x4` row is the same
// RndMatAnim* -> Hmx::Object* virtual-base conversion: r31 is this+0x88, the
// image computes (r31 + vbtbl[1]) - 0x84, ours (r31-0x88) + vbtbl[1] + 4 --
// equal.  mKeysOwner gets r31-0x88 on both sides.  Re-measured: shipped `self`
// = 98.9 canonical, equal 90 / diff_arg 3 / diff_op 1 / del 1, 376 B (4 short);
// the RB3/rb3-xenon spelling `sOwner = this; mKeysOwner = this;` = 97.9, equal
// 94 / ins 1 / del 1, 380 B = target size, the ONLY residual being the
// `stw sOwner` emitted one slot before `subi r4, r31, 0x88` instead of after.
// That form reproduces more of the image and scores lower (insert/delete
// costs more than diff_arg); kept `self` for the score.  Also inert at 97.9:
// `mKeysOwner = (sOwner = this, this);`.
// w-five-clean (2026-11-13): READABILITY OVER SCORE -- the `self` local is
// gone; `sOwner = this; mKeysOwner = this;` is the RB3/rb3-xenon spelling, is
// exactly the image's 380 B and reproduces more of it (equal 94 vs 90).  The
// 98.87 `self` form is a score trick that banks nothing below 100.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED, value level, not just
// pointers.  Listing: ty==1 -> SetObjConcrete(&mKeysOwner, m->mKeysOwner);
// ty==2 && m->mKeysOwner (0x30(r30)) != m -> same; else sOwner = this,
// SetObjConcrete(&mKeysOwner, this), then six vector assignments, each
// RE-READING m->mKeysOwner (`lwz r11, 0x30(r30)`) after mKeysOwner = this, from
// 0x38/0x44/0x50/0x5c/0x68/0x74 into the same offsets of this -- the source
// order exactly.  Unicorn h4 (signal 3), fresh: DIVERGENT data_layout,
// stable_divergent -- first mismatch is __RTDynamicCast's RTTI arguments
// (per-side addresses); the comparator stops there, so it is silent on the
// rest.  Native: CopyPollSemanticsTest.MatAnim* (3 tests; 3 of 3 sabotages --
// copy from m's own vectors, invert the FromMax owner test, test kCopyDeep for
// the shallow branch -- fail them).  Readability change in the same lane:
// `self` dropped, 98.874 -> 97.895 canonical, equal 90 -> 94, 380 B.
```
