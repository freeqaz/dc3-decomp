# Lane notes: `src/system/rndobj/Ribbon.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ConstructMesh@RndRibbon@@QAAXXZ`

`RndRibbon::ConstructMesh` -- moved verbatim from `src/system/rndobj/Ribbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 176-198):

```text
            // w30-c (2026-11-02): the opcode+operand multiset differs from the
            // image by exactly ONE `clrlwi rN, rN, 16` (image 4, ours 3), and
            // every other mismatch row is a register or a branch displacement
            // (target 408 B / 102 instructions, ours 404 / 101).  The image
            // re-truncates a value that we keep already-narrowed across the two
            // Set calls, and it re-reads mMesh->Faces() between them.  Measured
            // and refuted this lane: v2/v3 inlined back into the two Set calls
            // (77.235, two extra computes), v3 declared before v1/v2 (80.784,
            // inert), and the named `faces` reference in place of
            // mMesh->Faces() (76.794).  What is left is a register-pressure
            // decision, not a spelling.
            // w34-b (2026-11-04): behaviour verdict first -- no divergence.
            // Per-side multisets (alignment-free, so a misaligned row view
            // cannot invent a field swap): store and load multisets are
            // IDENTICAL, the callee set is IDENTICAL, and the only mnemonic
            // difference in 102 instructions is that one clrlwi.  It is
            // REDUNDANT on the image's side: the value it truncates is
            // consumed by `sth` (which truncates anyway) and as a face-array
            // byte offset bounded far below 65536, so both sides store the
            // same bytes at the same addresses, verified by hand against the
            // listing at 0xE8/0x120-0x160.  Named the face index
            // (`int faceIdx = base + side * 2`) this lane: byte-identical,
            // 80.78432 unchanged.
```


## `?UpdateChase@RndRibbon@@QAAXXZ`

`RndRibbon::UpdateChase` -- moved verbatim from `src/system/rndobj/Ribbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 285-302):

```text
// w20-t (99.8, branch-scan row 87 adjudicated ARTIFACT): the back-edge lands on
// the image's in-loop `lwz r8, 0x0(r31)` reload of mBegin (see w7-j below);
// the search loop stores nothing, so the hoisted value is the same.
// w26-f VERDICT: all 17 rows are equivalent, re-checked against the target
// listing (name_check, full).  (1) the mBegin CSE above; (2) row 94/95 is the
// two `memcpy` argument addresses emitted in the opposite ORDER, same values
// (`add r4,r30,r11` / `add r3,r29,r11` vs ours `add r3,r29,r11` /
// `add r4,r30,r11`) -- right-to-left argument evaluation, not a different
// source or destination; (3) rows 334/346/348 are commutative operand pairs
// (`fmuls f11,f25,f11` vs `fmuls f11,f11,f25`, `fmadds f0,f26,f7,f13` vs
// `fmadds f0,f7,f26,f13`); (4) everything else is the r8/r9/r10 cascade that
// follows from (1).  MECHANISM for the cascade, per c2rs §8.7: the permuted
// values are single-use fold temps whose sort key is the temp's own sid, so the
// whole tail tracks which temp is minted when -- the same one extra live value
// the w7-j note names.  w7-j's container-pointer spelling already reproduces
// the image's addressing and is refused only because MSVC re-hoists the load
// out of the loop (99.757 -> 99.032); defeating that hoist is the open question,
// and the loop body has no store that could make the reload non-invariant.
```

Inside the body (was lines 327-364):

```text
            // 15-row residual (lane w7-j, 2026-09-14).  The one structural row is
            // a target-only `lwz r8, 0x0(r31)` at the top of this loop body: the
            // shipped build RE-READS mTransforms.mBegin through r31 (= &mTransforms)
            // on every iteration, where we CSE it with the mBegin load that
            // `mTransforms.size()` already did two instructions earlier.  Everything
            // else in rows 67..95 is the register cascade that follows from having
            // one extra value live across the loop.
            //
            // MEASURED AND REVERTED: indexing through a container pointer --
            //     Keys<Transform, Transform> *xf = &mTransforms;  ... (*xf)[i].frame
            // -- does produce the target's `lwz r8, 0x0(r31)` / `add r8, r10, r8`
            // addressing AND closes the entire cascade (rows 67..78 all become
            // equal, 15 rows -> 10).  But MSVC still LICM-hoists the load into the
            // loop preheader instead of leaving it in the body, so the one row
            // becomes an insert/delete PAIR, which the canonical ruler charges more
            // than the diff_arg rows it removed: 99.757 -> 99.032.  The remaining
            // gap is loop-invariant code motion, not the addressing form, and no
            // behaviour-preserving spelling found here defeats it.
            // w16-a: the plain loop `for (; i < numKeys; i++) { if (...) break;
            // removeCount++; }` inside the guard is WORSE (99.757 -> 99.5): it
            // keeps a separate size test ahead of the loop and the mBegin load
            // still CSEs with size()'s.
            // w27-m (census STRUCTURAL:opcode, surplus addi:1,lis:2,lwz:1,stw:1
            // / deficit lwz:2): the census's one-instruction deficit IS this
            // documented target-only `lwz r8, 0x0(r31)`, i.e. the row is the
            // known loop-invariant CSE and not a new structural difference.
            // Re-adjudicated behaviour-equivalent.  The three remaining
            // commutative rows (334, 346, 348) are 8.7 operand-order keys.
            // Row 334 is the interesting one: `fmuls f25, [0x108(r1)]` here vs
            // `fmuls [x], f25` in the image, while its NEIGHBOURS 331 and 337
            // multiply the same f25 by loads at 0x100/0x104 and agree on both
            // sides.  Same base, same type, so the three memory keys differ only
            // by disp (0x100, 0x104, 0x108) while f25's leaf key is
            // 0x10000|sid<<5 -- a value that falls BETWEEN the 0x104 and 0x108
            // keys.  That is why exactly one of the nine products is ordered the
            // other way; re-seeding f25's sid would flip 331/337 too and cost
            // more rows than it buys.  346/348 multiply two elements of the same
            // Matrix3, so their order follows the same disp-only key.
```

Inside the body (was lines 445-452):

```text
                // w22-a30: the image's association, written out with explicit
                // parentheses so native sums in the same order (0x82715B10..18:
                // `fmuls f0, f0, f12` = z*z, `fmadds f0, f28, f11, f0` += x*x,
                // `fmadds f0, f13, f10, f0` += y*y) -- the sibling of w22-a21's
                // HamRibbon fix.  Dot() sums x, y, z.  Normalized unchanged
                // (99.75728); the Xbox build's in-place Multiply(smoothDir, inv)
                // now swaps two commutative fmadds operand pairs (0x82715C98,
                // 0x82715CA0), which native does not see.
```
