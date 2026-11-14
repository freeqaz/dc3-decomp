# Lane notes: `src/system/dsp/EQEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Reset@EQEffect@@QAAXXZ`

`EQEffect::Reset` -- moved verbatim from `src/system/dsp/EQEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 121-128):

```text
// w14-d (95.65, 12 rows): the residual is the crossover inner loop only.  The
// image hoists `xover + 0x97` (the output-delay base) out of the tap loop but
// forms the input index as `(xover + tap) + 0x5b` inside it, and moves xover
// between r4 and r11 around the stage loop; we hoist both bases.  INERT:
// `tap + xover` on the input subscript.  WORSE: input store first (95.64);
// `tap++, xover++` with plain [xover] subscripts (86.2, a pointer walk).
// w18-d (95.65): per-channel indexing `[chan][(stage * 2 + pass) * 5 + tap]`
// with no flat counter is worse (91.2).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: index
bias kept in a different register; identical constants and stores.


## `?Process@EQEffect@@QAAXPAMHH@Z`

`EQEffect::Process` -- moved verbatim from `src/system/dsp/EQEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 176-185):

```text
// w26-j (2026-11-01) behaviour verdict: unicorn records EQUIVALENT for this
// symbol (harness v4, high confidence), and the name_check listing agrees -- no
// wrong field (every offset row resolves to a frame slot or a different base
// register) and no wrong constant (every __real@ that pairs differently is a
// one-row slot shift).  The loud row, target `fneg f12,f12` + `fmadds f12,f10,f11,f12`
// (0x82E592A4 region) against our `fmsubs f12,f10,f11,f12` + `fadds f12,f12,f9`,
// is the SAME value by two instruction selections -- the FMADD forms are
// interchangeable here and the target's own `-(x*b0) + ...` algebra is what
// makes MSVC pick one or the other.  Not a bug; see the inline note at the
// band-1 chain below for the spellings already measured against it.
```

Inside the body (was lines 207-234):

```text
                        // NEGATIVE RESULTS (w7-ba): the image has a second dead
                        // `addi r5, r11, 0x25c` beside xd's (82E58ECC), i.e. a
                        // `yd = &mXoverOutputDelay[chan][0]` local folded onto the
                        // this+chan*0x78 walker; declaring it here makes MSVC keep
                        // xd as the walker and yd in its own spilled register
                        // (88.9 vs 91.2).  Swapping the two middle c1[] terms of
                        // the bandpass chains to chase the image's 3,2,1,0 product
                        // order reorders the whole load block instead (87.5), and
                        // `-x * b0` in band 1 still fuses to fmsubs, not the
                        // image's fmuls/fneg/fmadds at 82E592A4.
                        //
                        // w26-r2: oracle verdict for the whole function is
                        // EQUIVALENT (unicorn harness 4, high confidence), and
                        // every "target-only"/"base-only" displacement delta in
                        // the aligned listing is base-REGISTER-relative -- the
                        // image addresses one delay line off r9 as 0x8/0x4/0x0
                        // while ours addresses it off r10 as -0x4/-0x8/0x0 --
                        // rather than pointing at a different slot.  What is left is FP register
                        // permutation and fma-slot reassociation (the image
                        // fuses the 0x44 term of `*s = mBand2B2*fVar3 +
                        // fVar2*mBand0B2 + fVar4*mBand1A1`, ours the 0x64).
                        // The one row that is not register permutation --
                        // diff 67, `fmuls f8, f8, f7` vs `fmuls f8, f7, f8` --
                        // is a section 8.7 commutative key over two MEMORY
                        // leaves, whose key is hash(base)<<8 | disp.  The
                        // operands are 0x13c(r31=this) and 0x25c(r11=walker),
                        // so the order follows those two base symbols and not
                        // the expression's spelling.
```

Inside the body (was lines 353-363):

```text
                            // Retail negates x*b0 up front (fneg f12 at
                            // 0x82E592A4) and subtracts zn*cosCoeff with
                            // fnmsubs.  Written as one expression the double
                            // negation makes MSVC build the inner sum with the
                            // opposite sign and fix it up at the fmadds;
                            // accumulating into a named local pins the
                            // association across the statements, so the fneg
                            // survives and the chain ends fnmsubs.
                            // w27-m named only the negated product and
                            // re-scheduled the delay-line loads/stores for
                            // -2.1pp; the accumulator form does not.
```

```text
lane E w-low-synth-os (2026-11-13), behaviour audit against the target listing, value by value: NOT A BUG
(91.57).  Crossover path: every xd/yd shift, the /mXoverGain[stage] divisor per
stage, each coefficient index (c0[0..1], c1[0..3], c2[0..1]) against the
matching delay tap, the (x0+x2)+2*x1 vs (x0+x2)-2*x1 sign per stage, the mix
mBand2B2*lp + hp*mBand0B2 + bp*mBand1A1, and the three smoothed gains agree.
Biquad path: bands 0-4 enable bytes 0x38/0x54/0x74/0x90/0xa8, every coefficient
and delay offset, the fnmsubs pole subtractions and the three shelf smoothers
agree.  Retail's smoother store order is 0B2, 2B2, 1A1 (0x44/0x80/0x64), which
is independent stores -- order is not semantic.
```

## `?SetParameter@EQEffect@@QAAXHM@Z`

`EQEffect::SetParameter` -- moved verbatim from `src/system/dsp/EQEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 447-459):

```text
// w27-m adjacency notes (row measured 99.335 canonical / 99.214 fuzzy, 523 of
// 542 instructions equal).  Everything left is allocation, not semantics:
//  - the `default:` MakeString is named W4_D3DFORMAT in the image and H (int)
//    here.  Both are passed by reference from the SAME stack home (r1+0x10fc,
//    the switch subject) and both format as an integer, so this is the
//    /OPT:ICF instantiation-naming artifact, not a wrong argument type.
//  - rows 368-376 and 426-434 are the pair of constants -0.05f and 1/24000
//    reaching r10/r11 in the opposite order in the two bandpass blocks, which
//    only swaps which FPR holds mBandNQ vs mBandNGain.  MEASURED AND REFUTED:
//    writing `double invGain = pow(...)` before `float wcF = ...` in band 3
//    only, in both bands, and with band 4's `k + half` named, each re-schedules
//    the whole block (98.0 / 96.2 vs 99.335).  The image's order is produced by
//    the scheduler, not by the statement order.
```
