# Wrong arithmetic hides in register-only rows — and the canonical ruler forgives them

**Written 2026-09-30** (lane det-arith). Detector: `scripts/analysis/arith_semantics_scan.py`.
Taxonomy class 4 (`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`,
68 historical bugs, no detector until this one). Companion to
[rounded-100-hides-real-bugs.md](rounded-100-hides-real-bugs.md) and
[wrong-field-at-100-percent.md](wrong-field-at-100-percent.md).

## The mechanism

The brief for this lane assumed class-4 bugs *cost match points* and were merely
drowned in noise. That is half true. An opcode substitution (`srawi`/`srwi`,
`fmadds`/`fmsubs`, `lha`/`lhz`) does cost points. But the commonest shape found
on this pass costs **nothing on the canonical ruler**, because the only
difference is **which register an otherwise identical instruction reads**:

| function | image | ours | canonical before |
|---|---|---|---|
| `ObjectDir::ResetViewports` | `stfs f13, 0xd8(r31)` (f13 = -1.0) | `stfs f0, 0xd8(r31)` (f0 = +1.0) | 98.646614 (row not charged) |
| `SkeletonClip::LoadFrame` | `stfs f13, 0xc(r30)` (1.0) | `stfs f0, 0xc(r30)` (0.0) | **100.0** |
| `NgSpotlightDrawer::SetupForPostProcess` | `stfs f13, 0x64(r1)` (far plane) | `stfs f31, 0x64(r1)` (0.0) | **100.0** |
| `PoseFatalities::EndFatal` | `subfe r10, r10, r10` | `subfe r10, r8, r10` | **100.0** |
| `DataNode::Equal` | `subfe r11, r11, r11` | `subfe r10, r8, r10` | 99.19192 (row not charged) |
| `RndMorph::SetFrame` | `subf r9, r30, r31` | `subf r9, r31, r30` | 99.09091 (row not charged) |

Canonical forgives register permutation by design, and a *different register*
is indistinguishable from a *permuted* one at the row level. Only `fuzzy`
charges these rows, and a function whose residue is "register noise" is exactly
the one nobody reads. The value difference lives one step back: **where the
register was defined**.

- `subfe rD, rX, rX` is `CA - 1` (a 0/-1 mask of NOT-carry); `subfe rD, rA, rB`
  after `subic rA, rB, 1` is `CA` itself. Same carry, **opposite truth value**,
  register-only diff. `b &= x != 0` vs the image's `if (x) b = false;` is this.
  **Lever:** a conditional flag *clear* (`if (c) flag = false;`) is what MSVC
  if-converts into the image's `subic/subfe rX,rX` mask; `flag &= !c` / `== 0`
  emit `cntlzw/extrwi` instead (EndFatal: 99.35 → 100.0).
- A reversed `Scale(src, f, dst)` / `subf a, b` shows up as a swapped
  `subf` whose two operands are the *same two values* crosswise.

## What the tool does

One sharded `objdiff-cli diff --batch --include-instructions` pass over every
function in `report.json` (universe **48,365**; the not-defined **16,072** are
dropped by name). Each aligned row is canonicalised (`subi`→`addi -imm`, every
`rlwinm` alias → (rotate, mask32)) and classified; register-only rows are traced
back to their **defining instruction on each side** and compared by a bounded
value-numbering (`val_eq`) that follows reloads from stack slots to the last
store. See the docstring for the full bucket list; the reported ones are
`signedness`, `int-width`, `float-width`, `float-sign`, `int-op`,
`operand-order`, `operand-source`, `const-operand`, `op-substitution`,
`net-term`, `cond-mask`.

**Measured 2026-09-30, whole binary** (branch `det-arith`, after its fixes):
universe **48,365** report functions; examined **32,220** (66.62%); dropped
16,072 not-defined-in-our-build, 58 objdiff errors, 14 duplicate names that
batch mode resolves to another unit, 1 objdiff hang (`?Terminate@VirtualKeyboard@@QAAXXZ`,
named in the coverage block). **1,104** examined functions have any mismatch
row; **190** carry a reported row. Reported rows: signedness 3, int-width 5,
float-width 1, float-sign 16, int-op 12, operand-order 1, operand-source 363,
const-operand 63, op-substitution 65, net-term 48, cond-mask 24. Counted:
register-only 12,384, operand-exchanged 821, displacement 2,538, stack 2,418, ...
~2 min wall with 12 workers.

**Two-sided sabotage control** (re-introducing the historical `Rand::Seed` bug,
`((unsigned int)j >> 16)` → `(j >> 16)`, full `ninja` each way): clean
signedness **3 rows / 2 functions** → sabotaged **4 / 3** with
`?Seed@Rand@@QAAXH@Z` as `srawi 16 vs srwi 16` → reverted **3 / 2**, stdout+stderr
byte-identical to the clean run (135,109 B, exit 0 all three). ⚠ **The first
attempt at this control FAILED**: objdiff rendered the one-opcode change as a
delete + insert, the row landed in `multi-atom`, and signedness stayed 3. The
fix — pair one-sided instructions through the substitution table when both
operate on the same input value — is what made the control pass. Determinism:
agreed with itself across `PYTHONHASHSEED` 1/7 on 135,109 B.

`cond-mask` is also the **blind spot of det-cond's** `cond_semantics_scan.py`:
a condition compiled *without a branch* (the carry-chain idioms) is arithmetic.
`subfe` masks and `cntlzw/extrwi` zero tests are evaluated to a truth predicate
on both sides; equal truth is `cond-mask-equivalent` (counted), which is what
the five objdiff `BOOLEAN_NEGATION` rows det-cond checked turned out to be.

## Measured noise (each became a counted bucket — do not re-raise)

- **exchanged components**: x- and y-component computations aligned crosswise
  (`fadds f13,f10,f12` / `fadds f0,f11,f0` vs the other way round). Operand
  values are cancelled as a multiset (`operand-exchanged`).
- **operand loads in the opposite order** (GlitchPoker::Dump): the alignment
  pairs each load with the other one. Value-numbering, not alignment, decides.
- **a swapped subtraction consumed only by a zero test** (`sSelected == this`).
- **sign folded into a literal**: `x2*3 + x3*(-2)` vs `x2*3 - x3*2`
  (RndTexBlendController::GetBlendState, ResetViewports' `±768`).
- **static-guard / destructor-flag bit numbers** (`?$S9@` guard bit 2 vs 1,
  `li r28,2` vs `1`): a per-function counter.
- **`extrwi. 1,b` vs `rlwinm. 0,b,b`**, `addi 4; addi 4` vs `addi 8`,
  `clrlwi 24` insertion (BOOL_MASK), `clrrwi rD,rS,0` (a move).
- **0/-1 vs 0/1 truth masks** with the same predicate (IsGameScreenActive's
  current spelling, HDCache::Init): equivalent when ANDed into a bool.
- `/fp:fast` reassociation, FMA contraction and reciprocal folding: balanced
  by splitting fused ops into atoms; an `fdiv` imbalance is `reciprocal-fold`.

## What the tool CANNOT see

- **Functions we do not define** (16,072): arithmetic arrives with the body.
- **A dropped STORE** (`DxRnd::SavePreBuffer`'s `w = 0` — fixed on this branch,
  found by hand): not an arithmetic atom. That is class 10.
- **Definitions across a join.** The def walk is linear; in branchy functions it
  can pick another arm's definition (`SaveLoadManager::SetState`'s `MemFree`
  file-name register). Symmetric, so it adds noise rather than bias — but
  `operand-source` precision falls with the function's mismatch ratio. The
  printout is sorted cleanest-first; **every true positive on this pass came
  from a function with ≤ 20% mismatch rows** (StreamBufferData, 10/50, is the
  loosest).
- A wrong value produced by the *same* instruction sequence (a wrong field
  feeding the op — class 1; a wrong `.data` constant — class 5).

## Manual recognizer (for the part the tool cannot reach)

1. In a sub-100 function, read every `diff_arg` row whose opcode is a **store**
   or a **carry op** (`subfe`, `adde`, `addze`) even when objdiff calls it
   register-only. For a store, find what each side's source register was last
   loaded from; for `subfe`, check whether rA == rB on each side.
2. For a swapped non-commutative op, check whether the two operand *values*
   are the same pair (then it is a real swap) or merely renamed.
3. Before calling any row a bug, state the wrong runtime value. `add`/`or` on
   disjoint bits, a 0/-1 vs 0/1 mask ANDed into a bool, and a reassociated float
   sum are all equal at runtime.
