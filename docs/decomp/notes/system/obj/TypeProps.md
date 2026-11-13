# Lane notes: `src/system/obj/TypeProps.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?KeyValue@TypeProps@@QBAPAVDataNode@@VSymbol@@_N@Z`

`TypeProps::KeyValue` -- moved verbatim from `src/system/obj/TypeProps.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 10-17):

```text
// w8-e 2026-09-15: `merged_ObjPtrListRemove` (100 B, 0%) is the same synthetic
// ICF-group label as merged_ObjPtrVecErase in flow/FlowManager -- not a target
// symbol.  build/373307D9/icf_aliases.map:11114 declares the group at 825C6868
// with five folded members (ObjPtrList<FlowNode|Hmx::Object|RndDrawable|
// RndLight|ThreeDSound, ObjectDir>::remove), all `icf_aliases.synthetic`, and
// config/373307D9/symbols.txt:139568 binds the invented name there.  No source
// spelling produces that name, so the row cannot pair.  Measured 0.0%,
// structurally unscoreable.
```

Inside the body (was lines 362-388):

```text
    // w17-d: BEHAVIOUR FIX. The plain save -- the EditorDir extraction, `bs <<
    // mMap` and the re-insert -- is the BODY of the negated proxy predicate and
    // returns; the proxy save follows it (RB3's shape). The image's proxy-path
    // return destroys only `classnames` (0x825C79E4 -> clear(r31+0x78)), so
    // `keys`/`values` are scoped to the plain path; and a NULL mMap branches from
    // the top (0x825C7458 beq .L_825C752C) into the plain path and writes a null
    // DataArray at 0x825C7A00. Ours nested everything in `if (mMap)` -- writing
    // NOTHING for a null map (TypeProps::Load always reads one) -- and ran the
    // extraction before the proxy test, stripping EditorDir entries from mMap on
    // the proxy path without restoring them.
    // The predicate's order: the image tests `DataDir() == owner` first and only
    // consults gLoadingProxyFromDisk once `Dir() == owner` (0x825C76E4..0x825C76F8).
    // Shape (94.68 -> 98.8): the explicit `if (!mMap) break;` after ClearKeyValue
    // is what lets MSVC thread a cleared map straight into this block (0x825C7520
    // bne / fallthrough), and the `Size() < 1` exit after the two Removes is the
    // image's own test at 0x825C7658..0x825C7664. Residual: the dead home store
    // of `typeDef` (`stw r27, 0x64(r31)`, 0x825C7478), one extra callee-saved GPR
    // (we hold the zero in r22, the image in r30) and the order of the two bool
    // flag stores before GetSaveFlags.
    // w21-ay (98.77 -> 99.3): `if (owner->TypeDef())` then a second
    // `owner->TypeDef()` for the local (written twice, CSE'd) gives the image's
    // cr0 test + dead home store at 0x825C7478; `owner == owner->Dir()` gives the
    // image's cmplw operand order.  Left: register-only (zero constant in r22,
    // image r30, coalesced with the plain-path j -> one extra `mr r30, r22`) and
    // the bool-store order before GetSaveFlags.  Inert: const typeDef, typeDef
    // declared in the if-condition, bool decl orders, j declared before the
    // `if (mMap)` or at function top.
```
