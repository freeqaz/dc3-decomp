# Lane notes: `src/system/midi/MidiReader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/midi/MidiReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

File-level (was lines 16-25; nearest following definition: `MidiRank`):

```text
// w8-e 2026-09-15: ??3FileStream@@SAXPAX@Z (24 B) -- FileStream's class
// `operator delete` -- is a real same-TU gap.  ham_xbox_r.map contributes it
// from `midi:MidiReader.obj` at 8254e100, i.e. the image instantiates
// FileStream's OBJ_MEM_OVERLOAD deallocator in THIS translation unit.
// w8-j 2026-09-15: CLOSED, 0.0 -> 100.0.  It is NOT a `delete` site (see the
// probe at the bottom of this file for why a delete-expression emits nothing
// here): it is a `new FileStream(...)`, whose ctor-throws cleanup path takes
// operator delete's ADDRESS and so forces the COMDAT.  The tell was the map
// asymmetry -- ??3FileStream@@ present, ??2FileStream@@ absent everywhere,
// because MEM_OVERLOAD's operator new is inlined straight into the call site.
```

File-level (was lines 577-601; no definition follows it):

```text
// w8-j 2026-09-15 -- FLOOR at 85.000% (match_percent_normalized) for
// ?pow@@YAMMH@Z, 80 B target vs 76 B ours, 13 of 21 instructions equal.
// The whole residual is FPR naming plus one missing copy.  The image opens with
//     fmr   f13, f1          <- a copy of the base parameter
// and then runs the loop on f13, holding the CSE'd 1.0f literal in f12 and the
// accumulator in f0.  We never emit that copy: we mutate the parameter in f1
// directly, hold 1.0f in f13 and the accumulator in f0 -- so our tail is
// already exact (`fdivs f0, f13, f0` / `fmr f1, f0` / `blr` all pair) and only
// the register NAMES differ, four rows of them.
// Three spellings measured, three full ninja builds:
//   (a) `float b = base;` declared first, loop mutates b   -> 82.000%.  It DOES
//       emit the copy, but frees f1 so MSVC then allocates `result` into f1,
//       which costs the final `fmr f1, f0` (it early-returns with `bgelr`) and
//       loses two rows that (c) already has.
//   (b) same, declared after `float result = 1.0f;`        -> 82.000%, byte
//       identical to (a).  Declaration reorder is inert for register-only
//       swaps, as docs/decomp/patterns/fixable-declarations.md says.
//   (c) `int exp = exponent < 0 ? -exponent : exponent;`   -> 57.750%.  The
//       ternary lowers to the branchless abs idiom (srawi/xor/subf) instead of
//       the image's compare-and-negate, deleting five rows at once.  This also
//       refutes the ternary as a way to fix the one remaining insert/delete
//       pair (the image issues `cmpwi cr6, r4, 0` BEFORE `mr r11, r4`; we issue
//       it after).
// Kept (c)-free original at 85.000%.  What is left wants the permuter, not
// another source spelling.  Do not re-derive.
```

File-level (was lines 604-627; no definition follows it):

```text
// w15-i2 2026-10-02: CLOSED 77.182 -> 100.0 by marking MidiRank `inline`
// (the retail map flags it `f i`, a COMDAT).  The floor note below was the
// right diagnosis -- same-TU register-footprint propagation -- with the wrong
// conclusion: the callee does not have to be invisible, it has to be inline.
// See docs/decomp/patterns/map-comdat-flag-gates-clobber-propagation.md.
// Historical note follows.
// w8-j 2026-09-15 -- FLOOR at 77.182% for
// ?DefaultMidiLess@?A0x7d41cf68@@YA_NABUMidi@MidiReader@@0@Z, 88 B target vs
// 68 B ours, 11 of 22 instructions equal.  This one is a COMPILER POLICY
// difference, not a source difference, and the evidence is unusually clean.
// Our build applies MSVC/Xenon's static-callee register-footprint optimisation
// to the two MidiRank calls: MidiRank is a leaf in this same anonymous
// namespace that touches only r3/r11/cr6 (it is 100.0% matched, 124 B), so the
// compiler keeps the second Midi reference in the VOLATILE r4 and the first
// rank in the VOLATILE r10 straight across both `bl`s and needs no frame at
// all.  The image does not: it saves r30/r31 (`std r30, -0x18(r1)` /
// `std r31, -0x10(r1)`), homes r4 into r31, keeps the first rank in r30, and
// pays 16 more bytes of stack for it.  Every one of the 11 mismatched rows is
// that one decision -- 2 saves, 2 restores, the `mr r31, r4`, the +16 stack
// adjust pair, and four r10<->r30 / r31<->r4 renames.  No logic differs.
// REFUTED: defining MidiRank AFTER DefaultMidiLess with a forward declaration
// (one full ninja) is byte-identical -- MSVC computes the footprint over the
// whole TU, not in source order, so it is not reachable by reordering.  The
// comparison itself already matches exactly (subfc/eqv/srwi/addze/clrlwi).
```


## `Dc3W8jMidiFileStreamProbe()`

`Dc3W8jMidiFileStreamProbe` -- moved verbatim from `src/system/midi/MidiReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Attribution: no function named `Dc3W8jMidiFileStreamProbe` in report.json (unit); filed under the source name

Above the definition (was lines 552-570):

```text
// w8-j: orphan-instantiation probe for ??3FileStream@@SAXPAX@Z (0x8254E100,
// 24 B).  ham_xbox_r.map contributes that COMDAT from midi:MidiReader.obj and
// NOTHING in MidiReader.s calls it -- the listing holds only .fn/.endfn, no bl --
// so the odr-use was compiled into this TU and dropped by /OPT:REF.  Note the
// map attributes no ??2FileStream@@ (operator new) to this object, or to any
// other, so the discarded site deleted a FileStream rather than allocating one.
// The sibling ??3TempoMap@@SAXPAX@Z two functions later in the same listing is
// already 100% here, emitted by the utl\TempoMap.h include above -- same
// MEM_OVERLOAD shape, so the body is not in question, only the odr-use.
// REFUTED: `delete fs` emits NOTHING here -- ~FileStream is virtual, so the
// delete-expression dispatches to the scalar deleting destructor and the
// operator delete call lives inside that, in FileStream's own TU.  The site is
// `new FileStream(...)`: MEM_OVERLOAD's operator new is a small inline static
// member that MSVC inlines straight into the call (which is why the map has no
// ??2FileStream@@ anywhere at all), while the matching operator delete is
// reached by ADDRESS from the constructor-throws cleanup path and therefore has
// to be emitted as a real COMDAT.
// External linkage is required; `static` is discarded before it instantiates
// anything it mentions (measured by w8-b in HamMove.cpp).
```
