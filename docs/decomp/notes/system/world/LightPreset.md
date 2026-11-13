# Lane notes: `src/system/world/LightPreset.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Animate@LightPreset@@IAAXM@Z`

`LightPreset::Animate` -- moved verbatim from `src/system/world/LightPreset.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 1267-1271):

```text
// w20-h / w25-gt: the six address adds in loops 2-4 (`add rD, iv, base`) are
// c2's commutative operand sort (C2RS-BRIDGE 8.7): the vector-begin load is a
// memory leaf keyed by its base temp's sid mod 4, the strength-reduced offset a
// temp leaf (~0x15000).  Loop 1's base temps sit at sid 4k+1/4k+2 (ptr/state);
// loops 2-4 needed theirs moved by 2, which the two extra loop-1 counters do.
```

## `?erase@?$ObjPtrVec@VSpotlight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`

`ObjPtrVec<Spotlight,ObjectDir>::erase`

w-low-world (lane B, 2026-11-13): value-audited against the listing at
0x82848AD0: NOT A BUG.  idx from (it - begin)/0x14 (0 for a null it); swap-last
mode at +0x14 == 1 and idx != size-1 reads back()'s object (end-0x8), pops via
the node's virtual dtor, then the inlined Set: null object with mListMode
(+0x18) == 0 recurses into erase, else SetObjConcrete; otherwise _M_erase at
begin+idx; returns `it`.  Identical to ObjPtr_p.h's template.  Residual stays
the per-TU Set-inlining decision recorded in the source note.

