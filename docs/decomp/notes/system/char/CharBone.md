# Lane notes: `src/system/char/CharBone.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?StuffBones@CharBone@@QBAXAAV?$list@UBone@CharBones@@V?$StlNodeAlloc@UBone@CharBones@@@stlpmtx_std@@@stlpmtx_std@@H@Z`

`CharBone::StuffBones` -- moved verbatim from `src/system/char/CharBone.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 20-93):

```text
// RESIDUAL (w9-b, 99.70149 canonical, 268 B, 20 rows).  Every row is an
// offset-only diff on an (r1) stack slot -- no wrong field, no wrong constant:
// it is a stack-SLOT POOLING difference, and the two frames are the same size
// (0x90 both).  The image's scalar frame, read off the target listing, is
//
//   0x50  Symbol temp of the FIRST  ChannelName() call
//   0x54  the end() iterator temp   (shared by all three push_back()s)
//   0x58  Symbol temp of the SECOND ChannelName() call
//   0x5c  Symbol temp of the THIRD  ChannelName() call
//   0x60  the insert() sret slot    (shared)
//   0x68  the Bone (name 0x68, weight 0x6c)   (shared)
//
// i.e. each ChannelName destination gets its OWN slot while the push_back
// internals are pooled.  Ours pools the Symbols WITH the iterator temp and
// alternates them: block 1 puts the Symbol at 0x50 and the iterator at 0x54,
// blocks 2 and 3 put the Symbol at 0x54 and the iterator at 0x50.  Symbol and
// _List_iterator are both 4-byte one-pointer structs, which is why MSVC is
// willing to pool them.
//
// NEGATIVE RESULTS (w9-b), all measured with a full ninja:
//   * unnamed ChannelName temporaries (`bone.name = CharBones::ChannelName(...)`,
//     no `Symbol name` local): 86.239, frame shrinks to 0x80.  This is also the
//     diagnostic that fixes the spelling of the Symbol: with a temporary MSVC
//     reads it back through the sret pointer (`mr r11, r3; lwz r11, 0x0(r11)`),
//     where the image reads the known slot (`lwz r11, 0x50(r1)`).  The image's
//     Symbol is therefore a NAMED local, as written below -- do not "simplify"
//     this to the RB3 one-liner.
//   * `CharBones::Bone bone;` hoisted to function scope, Symbols left per block:
//     90.746.
//   * keeping the named Symbol but making the Bone a temporary
//     (`bones.push_back(CharBones::Bone(name, GetWeight(mask)))`): exactly inert,
//     99.70149 with an identical row set.  Readable either way; left as-is.
// What is still unexplained is why MSVC reuses the named Symbol's slot across
// the three sibling `if` scopes here and does not in the image.  A spelling that
// keeps the declaration-order allocation (sym1, iterator, sym2, sym3) while
// stopping the reuse is the open lead; three-at-function-scope does not, because
// it would allocate the three Symbols contiguously ahead of the iterator temp.
// w21-aq (stopped at 99.70149, same 20 r1-slot rows as w9-b; no new lever).
// Measured, all byte-inert (identical 20-row set): `const Symbol &name =
// ChannelName(...)` (lifetime-extended temporary instead of a named local);
// moving each block into a `static inline` helper (inlinee locals are pooled
// exactly like block locals); declaring the Bone before the Symbol; the
// two-arg `CharBones::Bone bone(name, GetWeight(mask))` ctor.  The RB3
// one-liner `push_back(Bone(ChannelName(...), GetWeight(mask)))` is 58.8: MSVC
// evaluates GetWeight first (f31 spill), the image calls ChannelName first.
// rb3-xenon's CharBone.cpp records the same 20 rows and a 0-for-3 record for
// the "make the slots match" direction.  The image keeps three DISTINCT Symbol
// slots (0x50/0x58/0x5c) while pooling the push_back temps (0x54/0x60/0x68),
// which reads like the Symbols interfering with each other -- a spelling with
// function-lifetime Symbols that does not default-construct them is the only
// lead left, and none is known.
// w24-slot (99.70149, unchanged): read with the measured slot rule (locals
// sorted by size, then reference count desc, then last reference asc; each
// one placed in the most recently created slot whose occupants' scopes do not
// overlap it), our block-scoped Symbols give exactly our layout.  The image's
// layout [sym1, iterator, sym2, sym3, insert-result, Bone] is exactly what the
// same rule gives if the three Symbols' scopes OVERLAP (function lifetime)
// while the push_back temps stay per block.  Function-scope `Symbol name1,
// name2, name3;` plus assignment is 86.2 (the default ctor and copy-assign add
// code), so the open question is how the original gave the Symbols
// overlapping lifetimes without a default construction.
// w26-f (99.70149, byte-inert): confirmed the slot split directly with
// scripts/analysis/slotprobe/slot_table.py -- ours is `0x50 name[4]`,
// `0x54 name[4] x2`, `0x60 bone[8] x3`, i.e. block 2 and 3 pool their Symbol
// while block 1 does not, and the image has all three distinct.  Tested the
// stack-slot-sharing pin rule (docs/decomp/patterns/stack-slot-sharing.md: "a
// local is pinned when its address reaches an INLINED callee"): an empty
// `static inline` taking `const Symbol &` is inert, and so is the real
// `Symbol::operator=` that `bone.name = name` lowers to -- Symbol is trivially
// copyable, so c1 emits the `lwz`/`stw` pair directly and no inline call exists
// for the rule to fire on.  Distinct names per block (posName/scaleName/rotName)
// are inert too, so the pooling is not name-identity based.  The pin route is
// therefore closed, which leaves w24-slot's "function-lifetime Symbol without a
// default construction" as the only lead -- unchanged.
```
