# Lane notes: `src/system/char/ClipCollide.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetTypeDef@ClipCollide@@MAAXPAVDataArray@@@Z`

`ClipCollide::SetTypeDef` -- moved verbatim from `src/system/char/ClipCollide.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 88-124):

```text
        // MECHANISM (w7-av; w33-c): 94.42 is a temp-slot/scheduling residue.  Rows left:
        // `li r5, 0x1` one slot early (the FindArray `fail` default), and the
        // Sym(0) sret at r1+0x54 where the image reuses r1+0x50 -- the slot the
        // Symbol("modes") temp occupied.  Refuted: one-expression
        // `da->FindArray("modes")->Array(1)->Sym(0)` (94.42, identical rows);
        // spelling `fail` explicitly as `true` (94.42, identical rows); a named
        // `Symbol modes("modes")` in an inner block to end its lifetime early
        // (91.60 -- the named local is stored to its slot instead of using the
        // ctor's return value, and the sret still lands at 0x54).
        // w13-c: `FindArray(Symbol("modes"))` (explicit temporary) is also
        // byte-inert, 94.42.
        // w21-i (stopped at 94.42, same 3 rows): wrapping the FindArray
        // statement in its own `{ }` block (so the "modes" temporary's scope is
        // disjoint from the Sym(0) sret's, per stack-slot-sharing.md) is
        // byte-inert.  og-dc3 and rb3-xenon both spell it as here.
        // w21-aq: naming the Array(1) result (`modeArr`) closes the sret row --
        // Sym(0)'s sret now reuses r1+0x50 like the image (the chained
        // `FindArray("modes")->Array(1)` + `modeArr->Sym(0)` split does not).
        // One row pair left: `li r5, 0x1` scheduled before `mr r3, r31`
        // (image: after).  Not a source-shape property: the same idiom compiles
        // both ways inside ONE matched function in the image (MetagameRank::Init
        // "repeatable" is mr-first, "one_time" li-first; Synth::SetFX likewise),
        // 27 mr-first vs 100 li-first sites binary-wide.  Explicit
        // `FindArray(Symbol("modes"), true)` re-measured inert in this shape.
        // w24-c1 (94.42, unchanged; same li/mr row pair): an early
        // `if (!da) return;` instead of the `if (da) {}` block is byte-inert.
        // Binary-wide census of `bl Symbol ctor; ...; bl FindArray(Symbol,
        // bool)`: 29 mr-first sites in 11 functions, among them matched
        // GeoInit, Synth::SetFX, SynthInit, CheatsInit, DingoSvrXbox::Init.
        // In SetFX the order flips between sites of identical source
        // (coreArr->FindArray("mode"/"volume"/... ), so it is a scheduler
        // tie, not a source shape.
        // w33-c behaviour verdict: EQUIVALENT.  The only delta is the position
        // of `li r5, 0x1` (FindArray's fail argument) relative to `mr r3, r31`:
        // same instruction, same value, same register, same callees, and the
        // relocation-symbol multiset is identical.  Probe: naming the bool
        // (`bool fail = true;`) is inert at 94.44444 -- it folds to the same li.
```

lane C / w-low-gesture-char (2026-11-13):

```text
// Behaviour re-check (94.444 canonical): NOT A BUG.  The image has NO
// `TypeDef() != da` guard (rb3 has one; ours correctly omits it): it calls
// Hmx::Object::SetTypeDef unconditionally, then FindArray("modes", true),
// Node(1).Array(), Sym(0) -> mMode (0x90).  The only rows are `li r5, 1`
// scheduled one slot apart.
```
