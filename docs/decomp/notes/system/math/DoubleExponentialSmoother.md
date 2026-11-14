# Lane notes: `src/system/math/DoubleExponentialSmoother.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Smooth@DoubleExponentialSmoother@@QAAXMM@Z`

`DoubleExponentialSmoother::Smooth` -- moved verbatim from `src/system/math/DoubleExponentialSmoother.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 16-26):

```text
    // w33-d, C2RS-BRIDGE 16, measured and REVERTED: binding the difference by
    // const reference escapes c1's single-use fold, and it does move the inlined
    // expansions -- Vector3DESmoother::Smooth 99.73684 -> 99.82456 with
    // canonical held at 100.  But this body is also the out-of-line
    // DoubleExponentialSmoother::Smooth, which a binding after the level store
    // takes from fuzzy 100.00000 to 99.65517, and a binding before the store
    // takes Vector3DESmoother to 65.41.  Net -1 row at fuzzy 100, so neither
    // form is bankable.  This is the concrete form of the constraint in the
    // Vector3DESmoother::Smooth note: the out-of-line body and all three
    // inlined expansions are ONE body, so no spelling can move expansions 2/3
    // without moving the out-of-line copy that already matches.
```


## `?Smooth@Vector2DESmoother@@QAAXVVector2@@M_N@Z`

`Vector2DESmoother::Smooth` -- moved verbatim from `src/system/math/DoubleExponentialSmoother.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 62-74):

```text
// w14-f: 94.75 -> 97.4.  BEHAVIOUR FIX in the normalize tail (latent: the one
// caller, PartyModeMgr, passes normalize = false).  The image normalises the way
// Vector3DESmoother does: inv = 0 when the length is zero (`beq` to `fmr f13, f0`
// with f0 = 0.0f), so a zero vector stays zero, and BOTH mLevel and mPrevLevel
// receive the normalised value (`stfs f11, 0x4(r3)` / `stfs f11, 0x0(r3)`,
// `stfs f13, 0x4(r11)` / `stfs f13, 0x0(r11)`).  We used to skip the write on a
// zero length and never touched mPrevLevel.  Remaining 6 rows: the mY expansion
// of the inlined DoubleExponentialSmoother::Smooth keeps mLevel in a register
// (`fmr f10, f11`) where the image re-loads it (`lfs f12, 0x0(r11)`) for the
// normalize; a Vector2 val(sx.mLevel, sy.mLevel) temp is worse (9 rows).
// w17-c: `Vector2 val = Value();` (the out-of-line accessor, inlined) gives the
// reload, and the length sums y*y first: non-PCH probe diff 235 -> 10, the one
// row left is the commutative fmadds inside the inlined mY Smooth().
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.870, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `?Smooth@Vector3DESmoother@@QAAXVVector3@@M_N@Z`

`Vector3DESmoother::Smooth` -- moved verbatim from `src/system/math/DoubleExponentialSmoother.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 119-173):

```text
    // Residual (100 normalized, fuzzy 99.74, 3 rows): commutative operand order
    // in the 2nd and 3rd inlined DoubleExponentialSmoother::Smooth (w25-oa,
    // c2's commutative sort key, C2RS-BRIDGE 8.7). Not a register decision.
    //   - mTrend fmadds (mY and mZ): both factors are subexpressions. The
    //     change ((mPrevLevel - oldPrev) - mTrend) keys SU 1 / count 7
    //     (0x0107xxxx) and the clamped newBeta fsel keys count 6 (0x0106xxxx),
    //     so the change sorts first in every expansion. The image has that order
    //     in the 1st expansion and the out-of-line body, newBeta first in the
    //     2nd and 3rd (and in Vector2DESmoother's 2nd). Per-copy differences can
    //     only come from the low 16 bits (a hash of the operand sids), which
    //     decide only when the counts are equal. So the image's trees had equal
    //     counts, and a fix needs a spelling that gives them equal counts while
    //     emitting the same code. 14 code-neutral spellings keep 7/6 (+=, named
    //     change/trend/level/prevDiff/beta, single-expression and Clamp()
    //     clamps, oldPrev and newBeta moved); Min(1, b), a negated change and a
    //     split change alter the code. "Base temp sid mod 4" does not apply
    //     here: no memory leaves are involved.
    //   - mY mLevel fadds: two temp leaves, mTrend V588 (0x19300) before
    //     mPrevLevel V382 (0x15f80); the image needs mPrevLevel first. Temp keys
    //     are (sid << 6) & 0xffff, so this needs a temp layout where mTrend's sid
    //     wraps past a multiple of 1024 and mPrevLevel's does not. That is not a
    //     small sid shift.
    //   - w26-f7 re-measured the three rows (61 / 63 / 85) against the current
    //     tree: all three still reproduce and the note above still holds. What
    //     it rules out, stated explicitly, is any UNIFORM change to the inlined
    //     body: the image wants `change` first in the mX expansion and `newBeta`
    //     first in mY/mZ, and the three expansions are the same inline body, so
    //     a single spelling that flips one flips all three. A per-expansion
    //     difference can only come from the operand sids, which is the 1024-wrap
    //     question in C2RS-BRIDGE 8.9. Do not re-run the 14 named-local and
    //     operator spellings listed above; they are measured.
    // w33-d (2026-11-04): confirmed from c2's own operand dump (c2rs
    // `stage cands --regasg --payload`, the `CQT cqlo3` lines). In every one of
    // the three inlined expansions the mTrend fmadds sorts `change`
    // (SU 1, nodes 7 -> 0107xxxx) ahead of the clamped `newBeta`
    // (SU 1, nodes 6 -> 0106xxxx); the three measured key pairs are
    // 01076e1c/0106499e, 0107291c/01060bbe and 0107995c/010609de, and all three
    // expansions carry the SAME counts. Declination is by node count before any
    // sid hash, so no uniform source spelling can make expansions 2 and 3
    // differ from expansion 1: that needs those counts to tie so the
    // per-expansion hash decides. c1 folds every node-adding spelling tried
    // (`+ 0.0f`, `* 1.0f`, `Max(mBeta * delta, 0.0f)`) back off before c2 sees
    // the tree; 18 spellings measured inert and 2 worse, so nothing was landed.
    // The one lever that DOES escape that fold is C2RS-BRIDGE 16's const-ref
    // binding of the difference. It was measured and rejected: it lifts THIS
    // row to 99.82456 but takes the out-of-line DoubleExponentialSmoother::
    // Smooth from fuzzy 100.00000 to 99.65517, net -1 row at fuzzy 100. See the
    // note on that body.
    // The mLevel fadds row is a different sub-case: both operands are temp
    // leaves keyed (sid << 6) & 0xffff (here 00019780 for mTrend against
    // 000168c0 for mPrevLevel, i.e. mTrend sorts first, the image wants
    // mPrevLevel first). That needs a temp layout where mTrend's sid wraps past
    // a multiple of 1024, the open question in C2RS-BRIDGE 8.9.
    // Also measured (w25-fa): mX in place of the sx ref is inert; dropping all
    // three refs flips the 2nd expansion's mPrevLevel fmadds the wrong way.
```
