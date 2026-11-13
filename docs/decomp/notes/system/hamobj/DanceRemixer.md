# Lane notes: `src/system/hamobj/DanceRemixer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PostMoveFinished@DanceRemixer@@UAAXXZ`

`DanceRemixer::PostMoveFinished` -- moved verbatim from `src/system/hamobj/DanceRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 216-221):

```text
    // DECOMP-INTRODUCED GUARD, now correctly scoped (w7-ai). This used to be a
    // `TheHamDirector ? ... : nullptr` ternary in the shipping path, which the
    // image does not have: it loads TheHamDirector and dereferences it
    // unconditionally (`lwz r11, TheHamDirector` then `lwz r28, 0x328(r11)`,
    // no cmplwi/beq between them). The null test belongs on the native side
    // only, where TheHamDirector really can be absent.
```


## `?JumpedBeat@DanceRemixer@@QBAMM@Z`

`DanceRemixer::JumpedBeat` -- moved verbatim from `src/system/hamobj/DanceRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 320-370):

```text
    // w7-bb residual at 90.89% (4 rows, 824F02AC-824F02CC).  Everything through
    // 824F02A8 matches; the residual is that the image reuses the SINGLE
    // conversion scratch slot -0x10(r1) for both int->float conversions
    // (std r11,-0x10 / lfd f0,-0x10 / std r10,-0x10 / lfd f13,-0x10) where we
    // spill the second one to -0x8(r1), which also flips the fcfid/frsp
    // scheduling into an f0<->f13 swap.  That is a frame-temp allocation
    // decision, not an expression shape: three spellings were all byte-inert
    // at 90.88636 -- splitting into two statements (float shifted = beat -
    // (float)fromBeat; return shifted + (float)toBeat), reversing the fadds
    // operands ((float)toBeat + (beat - (float)fromBeat)), and hoisting
    // (int)beat into a named local used by all three compares.
    // w11-a: two more spellings inert at 90.88636 -- compound assignment on
    // the parameter (beat -= fromBeat; beat += toBeat) and a float local for
    // the converted toBeat.
    // w21-b (still 90.88636, same 4 rows): measured in a standalone /FAs probe
    // with the project's cl.exe flags.  The second slot is caused by the
    // EARLIER conversions: this tail alone, or with only the `< fromBeat`
    // early-out above it, reuses -0x10 for both; any (float) conversion in a
    // block that comes before it in source order pushes the first of these two
    // onto -0x8.  Writing this case BEFORE the jumpSize block (`if (b >= to)
    // { if (to < from) return ...; return beat; }`) does give one slot for all
    // four conversions -- but MSVC then lays the block out first (the image
    // falls through into the jumpSize block), so it is not the image's source.
    // Also inert: jumpSize hoisted to function scope, nested if/else with one
    // `return beat`, a `result` local, an inline Shift(float,int,int) helper
    // (int or float params), one `float fJump` for both jumpSize arms, and
    // implicit int->float conversions.  Values match the image on every path.
    // w21-al (still 90.88636, same 4 rows): standalone probe, ~25 more
    // spellings, all leave the tail's second conversion on -0x8: every
    // association of the tail sum (explicit parens both ways, flat sums),
    // float locals hoisted to function scope, (float)(__int64) conversions,
    // ternary forms of both blocks, if/else-if chains, a single `beat +=` /
    // `beat -=` exit, inline FromBeat()/ToBeat() accessors, and re-reading
    // mFromMeasure*4 in the tail.  Only a tail-first layout (w21-b) gives one
    // slot, and it reorders the blocks.
    // w29-b (still 90.88636, same 4 rows): c2-rs candidate tap shows no COLOR
    // candidate and no regasg row in this tail at all -- the two scratch slots
    // are c1's frame-temp allocation for the `std/lfd` int->double idiom, whose
    // live range ends at the `lfd`, so the image reuses -0x10 while we hold two
    // slots for the same pair.  Three more spellings inert: two named float
    // locals for the conversions, `float shifted = beat - (float)fromBeat;` in
    // its own statement, and both of those combined.  Opcode multiset is
    // IDENTICAL and the count is 44 = 44, so nothing is missing -- the whole
    // residual is the -0x8 displacement and the f0/f13 flip it induces.
    // w33-c behaviour verdict: EQUIVALENT.  Both sides compute
    // (beat - (float)fromBeat) + (float)toBeat with the same fsubs/fadds order;
    // the relocation-symbol multiset is empty on both sides (no globals or
    // callees in the body) and the instruction counts are 44 = 44.  The only
    // delta is the int->double scratch slot: the image reuses -0x10 for both
    // conversions, we take -0x8 for the second, which is c1 frame-temp
    // allocation rather than an expression shape.
```


## `?BuildSetOfPrevAdjacentMoveParents@@YAXAAV?$set@PBVMoveParent@@U?$less@PBVMoveParent@@@stlpmtx_std@@V?$StlNodeAlloc@PBVMoveParent@@@3@@stlpmtx_std@@ABV12@@Z`

`BuildSetOfPrevAdjacentMoveParents` -- moved verbatim from `src/system/hamobj/DanceRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 452-463):

```text
        // w8-i: the index is a SIGNED int and the size is cast, not `unsigned int
        // i < prevAdjs.size()`.  95.04 -> 97.83 (184 B); all three opcode rows
        // closed together, because they are one decision seen three times: the
        // entry guard `srawi. r10, r10, 2` -> `clrrwi. r11, r11, 2` (a signed
        // count needs no arithmetic shift -- the image tests the BYTE span with
        // its low two bits cleared), its branch `beq` -> `ble`, and the loop-back
        // compare `cmplw` -> `cmpw`.
        // w11-a: 97.83 -> 100 by calling PrevAdjacents() at BOTH use sites
        // instead of binding a reference local.  With the getter re-invoked the
        // image re-reads the vector's start at the top of every iteration
        // (`lwz r11, 0x0(r31)`) and reads finish-then-start in the loop test;
        // the reference local let MSVC carry start from the test into the body.
```
