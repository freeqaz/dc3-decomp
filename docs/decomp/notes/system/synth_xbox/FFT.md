# Lane notes: `src/system/synth_xbox/FFT.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

File-level (was lines 11-18; nearest following definition: `fft_real_forward_scalar`):

```text
// VMX128 intrinsics used by the AltiVec kernels below.  The shared header
// (xdk/LIBCMT/vectorintrinsics.h) is reached through the PCH, so they are
// declared here rather than there: adding a declaration is all MSVC needs to
// emit the opcode.  (w8-f moved this block from just above
// fft_real_forward_altivec to the top of the file so fft_altivec and
// fft_recursive can use it too; measured inert on every other function in the
// unit -- a declaration reaching a translation unit earlier cannot change the
// code of a function that does not call it.)
```

File-level (was lines 35-76; nearest following definition: `fft_real_forward_scalar`):

```text
// COMDAT-SELECTION LEVER: MEASURED, and the actionable direction is EMPTY for
// this unit (w8-r).  Read the mechanism before reusing this note, because the
// obvious one is WRONG and was retracted: it is NOT "add `inline` so the
// definition becomes a COMDAT".  MSVC/Xenon puts EVERY function we compile into
// its own COMDAT (function-level linking), so being a COMDAT does not
// discriminate at all.  What ham_xbox_r.map's flag column reports is the COMDAT
// *selection type*, carried in the section symbol's aux record at offset 14:
// `f i` <-> IMAGE_COMDAT_SELECT_ANY (inline / template / in-class), bare `f`
// <-> IMAGE_COMDAT_SELECT_NODUPLICATES (ordinary out-of-line).  The dtk-carved
// TARGET objects record no selection byte at all, so the map is the only
// carrier and there is nothing to diff object-to-object -- which is why
// "our source defines it out-of-line, therefore our class matches" is not
// evidence.  Use scripts/analysis/comdat_selection_audit.py instead.
//
// MEASURED over build/373307D9/src/system/synth_xbox/FFT.obj: ZERO selection
// mismatches, in either direction.  Every FFT symbol our build emits already
// carries the class ham_xbox_r.map records for it, so there is nothing here to
// match and the 46.93 / 48.75 stall on fft_altivec / fft_recursive is not a
// selection-class artifact.  (The FFT functions do all call each other, so the
// same-TU precondition IS satisfied -- the lever is not ruled out by
// construction, it is ruled out by the audit finding nothing to fix.)
//
// Two things that are NOT leads for this lever, both measured elsewhere and
// worth not re-deriving: (1) a SAVE-SET difference is not evidence for it --
// a sibling lane's RndSpotlight::BuildBeam is the textbook symptom (image
// `bl __savegprlr_14`, ours `_16`) and stayed BYTE-IDENTICAL at 85.3415 with
// `__savegprlr_16` unchanged after its same-TU callee's selection class was
// matched; (2) all four selection closures verified anywhere so far were
// FIDELITY-ONLY, zero score movement.  Whole-binary surface, for scale:
// 1,898 selection mismatches over 78,352 functions compared, of which 536 are
// the actionable ours-NODUP / image-ANY direction and 1,362 are the reverse.
//
// Target save sets, recorded for whoever picks this up -- as REFERENCE, not as
// a lead, per (1) above: fft_altivec __savegprlr_14; fft_recursive
// __savegprlr_15 + __savefpr_24 + __savevmx_124; fft_scalar __savegprlr_24;
// fft_real_forward_altivec __savegprlr_23 + __savefpr_25 + __savevmx_121;
// fft_matrix_forward_columnwise __savegprlr_20 + __savefpr_24 + __savevmx_120;
// fft_matrix_inverse_columnwise __savegprlr_21 + __savefpr_24 + __savevmx_124;
// fft_real_forward_scalar __savegprlr_29.  The stored
// REGISTER_SAVE_HELPER_MISMATCH scan flags fft_scalar and fft_recursive in this
// unit; its fft_recursive row still reads "STUB: no body emitted", which is
// stale -- the function is implemented and scores 48.754.
```


## `?fft_real_forward_scalar@@YAHPAMK0@Z`

`fft_real_forward_scalar` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 89-161):

```text
// Three spellings in here are load-bearing (w7-an, 80.9 -> 83.7 canonical):
//  * the loop bound is `size >> 2` written INLINE.  Lifted into a
//    `unsigned int count` local it becomes a provable trip count and MSVC
//    converts the loop to CTR (`mtctr` / `bdnz`); the image keeps an explicit
//    `k` against `size >> 2` in r8 (`addi r9, r9, 0x1` / `cmplw cr6, r9, r8` /
//    `blt`, 0x82E50A4C-0x82E50ABC).  Same lever as CalculateSinCosTable below.
//  * `hi` is `data + size - 2`, indexed `hi[1]` / `hi[0]`, NOT `data + size`
//    with `hi[-1]` / `hi[-2]`.  The image computes `subi r10, r30, 0x2` /
//    `slwi` / `add` and then biases `+0x8` inside the loop preheader
//    (0x82E509D4, 0x82E50A18) -- the bias is MSVC's, so the source value it
//    started from is data+size-2.
//  * the low half IS walked with a `float* lo`, and the walk must be spelled
//    as two separate `++lo` BETWEEN the two stores (w7-bl, 83.70 -> 85.70
//    canonical; this REFUTES the earlier w7-an bullet that said to subscript
//    off `data`).  Subscripting `data[k * 2 + 2]` / `data[k * 2 + 3]` makes
//    MSVC fold the advance into one `stfsu fN, 0x8(r11)` with a -4 base bias;
//    storing through `*lo` and stepping it twice reproduces the image's plain
//    `stfs 0x0(r11)` / `stfs 0x4(r11)` pair plus `addi r7, r11, 0x4` /
//    `addi r11, r7, 0x4` (0x82E50A5C, 0x82E50AB8).  Rows 92-110 are now equal.
//
// RESIDUAL (w7-bl, 85.70 canonical, 43 of 111 rows): one FPR colouring
// decision in the DC/Nyquist preheader, and the schedule it drives.
//  * f0 <-> f13 (15 rows): the image loads `c = 1.0` into f13 and `s = 0.0`
//    into f0 -- i.e. c takes the register `im0` died in and s takes `re0`'s.
//    We colour them the other way round, and the whole twiddle recurrence
//    (idx 66-91, `fmadd`/`fnmsub` chain) inherits the swap.
//  * f30 <-> f31 (4 rows): image holds `inv_n` in f31 and `sin_a` in f30; ours
//    are reversed.
//  * The 12 insert/delete rows are the same cause seen as scheduling: the
//    image computes `fmul f10, f30, f30` (cc) immediately after the second
//    `bl sin` (idx 35) so f10 is occupied before the DC/Nyquist loads, which
//    forces the sum into f8 and stores `data[1]` before `data[0]`.  We compute
//    cc after the loads and store `data[0]` first (the 1 OFFSET_SWAP row).
// Measured-inert spellings, all four re-measured in this worktree at 85.70
// with an identical 26/5/6/6 row split:
//    - swapping the `data[0]` / `data[1]` store statements;
//    - hoisting `double cc = sin_a * sin_a;` above the `sin_2a` call (hoping
//      MSVC would sink the multiply to just after the call, as the image has);
//    - routing both bins through named `float diff0` / `float sum0` temps so
//      the subtraction is computed and stored first;
//    - writing `sum_im` as `lo_im + hi_im` (the image's textual operand order
//      for the idx-73 `fadds`) -- COMMUTATIVE_OP_ORDER stays at 1 either way.
//  Swapping the `c` / `s` declarations was already measured inert by w7-an.
//
// w7-br (85.6 -> 88.0 canonical, 39 of 110 rows): the store-order half of the
// residual is source-addressable after all, but the lever is the POSITION of
// the `ss` / `c` / `s` declarations, not the store statements.  With those
// three between the two bin stores, MSVC emits the second store first and
// defers the first bin's fadds/fsubs to just before its store, whichever bin
// is written first (that is why w7-bl's swap read inert: it swapped both).
// Declaring `ss`, `c`, `s` BEFORE the bins and writing the two stores
// adjacent, sum first, gives the image's `stfs 0x0(r31)`-early shape and the
// 2 OFFSET_SWAP rows go.  Also measured: diff-first adjacent 86.7; named
// diff0/sum0 temps with the declarations before the bins 86.7; `cc` folded
// as `sin_a * sin_a * 2.0` inert; `cc = cc * 2.0` moved above the bins
// inert; `hi` / `lo` moved above the bins inert; `data[0]` re-read in place
// of `re0` 85.6 (load order flips); the bins' stores written after `hi`/`lo`
// 85.6 (separation flips them again).
// What is left (39 rows) is ONE scheduling decision and the colouring it
// drives: the image computes `fmul f10, f30, f30` (cc = sin_a^2) as the very
// first instruction after the second `bl sin` (0x82E509BC), before the
// `lfs f0, 0x0(r31)` / `lfs f13, 0x4(r31)` loads (0x82E509C8 / 0x82E509D0).
// We load first, add, store `data[0]`, and only then multiply -- into the f10
// that the sum just vacated -- so the sum never needs f8, c/s take f0/f13
// instead of f13/f0 (0x82E509EC / 0x82E509F4), and the loop inherits the
// swap (14 rows).  Every position of the `cc` statement from the top of the
// block to just above the loop schedules the multiply after the first store.
// f30 <-> f31 (inv_n / sin_a, 4 rows) is the callee-saved pair's allocation
// order and moves with nothing above.
// Bug-family check (worklist): fft_matrix_inverse_columnwise's harvested
// defect was a loop advancing the malloc'd pointer it later freed.  Nothing
// here is freed; `data` (r31) is never advanced, only `lo` and `hi` walk, and
// the image agrees (`addi r11, r31, 0x8` at 0x82E50A00 rebases `lo` off r31).
```

Inside the body (was lines 174-188):

```text
            // ss, c and s are declared BEFORE the DC/Nyquist bins, and the two
            // bin stores are ADJACENT, sum first (w7-br, 85.6 -> 88.0
            // canonical).  With the three declarations between the stores,
            // MSVC emits the SECOND store first and defers the first bin's
            // arithmetic to just before its store; adjacent, the stores keep
            // source order.  Diff-first adjacent is 86.7; pre-computing both
            // bins into named temps changes nothing either way.
            // w8-r: moving ONLY `c`/`s` between the two bin stores (leaving
            // `ss` above them), to reproduce the image's `lfd c` / `lfd s` pair
            // sitting BETWEEN `stfs f9, 0x4(r31)` and `stfs f8, 0x0(r31)`, is
            // byte-identical at 88.048.  The image's residual shape is: diff
            // computed, sum computed, store DIFF, c/s loaded, store SUM -- it
            // stores the second bin first WITHOUT deferring the first bin's
            // arithmetic, which is the half w7-br's declarations-between
            // variant could not get.
```


## `?CalculateSinCosTable@@YAHJPAM@Z`

`CalculateSinCosTable` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 252-264):

```text
// Two spellings carry this to 100 and neither is cosmetic (w7-an):
//  * `n / 2` must be written INLINE in the subscript, not lifted into a
//    `long half` local.  As a local it is an ordinary loop-invariant and MSVC
//    strength-reduces `table[j + half]` into a second walking pointer
//    (`stfs 0x4(rN)` / `stfsu 0x8(rN)`); written inline it is hoisted by the
//    invariant pass instead and the address is rebuilt every iteration --
//    `add r11, r29, r27` / `slwi` / `add r11, r11, r28`, which is what the
//    image does.  75.0 -> 96.9 canonical on that change alone.
//  * the `j` counter must be spelled `i * 2`, not carried as its own `long j`
//    with `j += 2`.  A source-level `j` gets its `li 0` next to `i`'s, before
//    the zero-trip guard; as a compiler-created induction variable its init
//    lands in the loop PREHEADER after the invariant hoists, which is where
//    the image's `li r29, 0x0` sits.  96.9 -> 100.0.
```


## `?fft_recursive@@YAHPAMKJ0@Z`

### Lane F (w-low-xbox), 2026-11-13 -- NOT A BUG (behaviour checked)

Whole FFTComplex run in a Python PPC interpreter over the TARGET listing vs our source built on the host (VMX intrinsics emulated), same input, same N-sized sin/cos table: N=65536 (sign -1 and +1, matrix path) and N=131072 (sign -1, recursive path) agree exactly (max rel err 0.0). Swap test (target function swapped into our build) also OK for fft_matrix_forward_columnwise. Neither side is a DFT with an N-sized table at these sizes (rel err ~1.5 on both) because the sub-FFTs want a sqrt(N) table -- that is the image's behaviour, reproduced. Residual is codegen (scheduling/regalloc), not logic. Harness: /home/free/tmp/fftharness (tbig.py, swap.py).

`fft_recursive` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 348-418):

```text
// w8-f (2026-09-15).  RECONSTRUCTED FROM THE TARGET LISTING -- there was no
// body here at all before (case 1: declared at the top of this file, defined
// nowhere in src/; objdiff read 532 insert / 0 base = 0.0%).  rb3-xenon's
// src/system/synth_xbox/FFT.cpp declares the same two symbols and also never
// defines them, so there is no port source for this pair in any sibling tree.
//
// One decimation-in-frequency radix-2 step, vectorised, followed by two
// half-length recursive transforms and a de-interleave:
//
//   * grow the shared ping-pong scratch to size/2 complex (the SAME block
//     fft_pingpong runs, but sized `size >> 1`; 0x82E4FCFC-0x82E4FD90);
//   * one butterfly pass over the whole array, four vectors at a time from
//     BOTH ends of BOTH halves at once -- lo ascending from data, lo
//     descending from the midpoint, hi ascending from the midpoint, hi
//     descending from the end.  Sums go back in place; differences are
//     twiddled.  The body is written as two identical steps because the image
//     is (loop count `size >> 4`, pointers advancing 0x20 per iteration:
//     0x82E4FFE4/0x82E50004 for the low read pointer);
//   * FFTComplex on the upper half then the lower half (0x82E503F0,
//     0x82E5040C) -- note the UPPER half first;
//   * copy the lower half's result into the scratch buffer (0x82E50430);
//   * interleave scratch (even bins) with the upper half (odd bins) back into
//     data with the two merge permutes (0x82E5049C-0x82E504F4).
//
// The descending half's twiddle is the negated conjugate of the ascending
// one: for bin N/2-1-k, cos(pi - theta) = -cos(theta) and sin(pi - theta) =
// +sin(theta), which is why cHi is negated WHOLESALE (`vsubfp128 v13, v127,
// v63` at 0x82E4FF88) while the sign pattern inside sLo/sHi is the usual
// {+s, -s} complex-multiply-by-conjugate pair.
//
// `dir` is +1.0 for sign == -1 and -1.0 otherwise (0x82E4FDF4-0x82E4FE10),
// and it also selects which of the two word-interleave permutes is copied to
// the `perm_sel` slot -- the forward one builds {s, -s} and the inverse one
// {-s, s}.
//
// RESIDUAL (w8-f, 48.75376 canonical / 45.417294 fuzzy, 648 emitted against
// 532 in the image).  PARTIAL by construction: the skeleton, the constants,
// the trig recurrence, the recursion and both tail loops are recovered; the
// per-iteration register colouring and the stack-slot assignment are not.
//
// ⚠ READ THIS BEFORE TUNING.  At this structural distance the canonical
// number is dominated by how objdiff ALIGNS two ~600-instruction loop
// bodies, and a one-line source reorder flips the alignment wholesale.  Four
// changes that are individually justified BY THE LISTING all measured WORSE,
// every one of them re-measured from a full `ninja` in this worktree:
//   * moving the eight walking pointers above the sin/cos block, where the
//     image computes them (0x82E4FE40-0x82E4FE94, between the `lfs 1.0` and
//     the `fdivs`): 48.30 -> 27.3 alone.
//   * computing `angle2` AFTER the first sin() call, which is what the image
//     does (`fmuls f24, f31, f0` at 0x82E4FEB4, between `frsp f13, f1` and
//     `fmul f13, f13, f13`) and what fft_real_forward_altivec already spells:
//     48.30 -> 25.56 ALONE.  That is the single most surprising number here.
//   * all four of {v_zero declared above the scratch block so it is loaded
//     into the callee-saved v127 before free/malloc, as at 0x82E4FD20; the
//     body wrapped in `if (ret == 0)` with one return, matching
//     `mr r3, r31` / `cmpwi cr6, r31, 0` / `bne` at 0x82E4FD94; merge_lo /
//     merge_hi declared below the sign branch so MSVC reuses perm_sel_inv's
//     0x90 slot the way the image does at 0x82E4FE50; plus the pointer
//     hoist} TOGETHER: 48.30 -> 31.3 -- even though that variant is strictly
//     CLOSER on every structural measure objdiff reports (frame delta -0x10
//     instead of -0x30, 1 DIFFER / 17 PERMUTED stack slots instead of
//     6 DIFFER / 18 PERMUTED / 3 base-only, and 115 REGISTER_SWAP
//     instructions instead of 192).
// The lesson is that percentage and structural agreement have DECOUPLED
// here.  Do not read a drop as "that reading of the listing was wrong" --
// three of those four changes are demonstrably what the image does.  They
// will pay once the butterfly body itself pairs; until then they only move
// the alignment.  Judge the next round of work on the structural counters,
// and re-check the percentage at the end.
// The one change that paid was the CTR lever, and it paid because it made a
// BRANCH match rather than moving code: 48.30 -> 48.75.
```


## `?fft_altivec@@YAHPAM0KJ0@Z`

### Lane F (w-low-xbox), 2026-11-13 -- BEHAVIOUR FIX

The w8-f reconstruction below was **not a correct FFT**.  Measured with a
target-listing PPC/VMX128 interpreter (it runs the image's own `FFT.s` and
reproduces a correct DFT for N = 2..2^17: forward e^-i, inverse e^+i scaled
1/N) and an x86 build of our source with emulated intrinsics: every complex
transform with N >= 16 (all of `fft_pingpong`'s AltiVec path, hence every
`FFTComplex` / `FFTRealForward` of 32+ real samples) returned garbage
(rel. error 0.9 - 1e3) and wrote past the ping-pong scratch.  Three defects
in the stage loop, all read off the listing (0x82E4F058-0x82E4F740):
  * butterfly sign: the image computes `x0 - x2` / `x1 - x3`
    (`vsubfp128 v11, v63, v62`), we had `x2 - x0` / `x3 - x1`; the last
    radix-4 stage also paired x0 with x1 instead of x0 with x2;
  * every inner loop is unrolled TWO vectors per iteration with trip count
    `blk / 4`; ours did one vector, so half of each group was never computed;
  * group twiddles start at `twiddle + blk*4` (final) and `twiddle + blk*2`
    (inner) -- `add r21, r27, r7` / `add r22, r26, r7` at 0x82E4F23C -- not
    `blk*2` / `blk`; and the last stage writes f1 to `+blk*2`, twiddled e0 to
    `+blk*4`, which we had swapped.
The first pass and both radix-2 tails were already right.  After the fix our
MSVC object, run in the same interpreter with only this function swapped in,
equals the image bit-for-bit-close (rel. err < 1e-5) on N = 16..4096, both
signs.  46.93 -> 62.5 canonical.


`fft_altivec` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 800-862):

```text
// w8-f (2026-09-15).  RECONSTRUCTED FROM THE TARGET LISTING -- like
// fft_recursive above, there was no body here at all (case 1: declared at the
// top of this file, defined nowhere in src/; objdiff read 762 insert / 0 base
// = 0.0%).  No sibling tree has a definition either; see the fft_recursive
// header comment for the search that established that.
//
// The VECTOR sibling of fft_scalar below, but RADIX-4, not radix-2: each
// pass consumes four inputs a distance size/4 complex apart and emits four
// outputs, and the stage count is therefore (power - 2) / 2 (0x82E4EF08:
// `subi r11, r20, 0x2` / `srawi r11, r11, 1` / `addze. r22`).  Layout:
//
//  * setup + the same power-of-two test fft_scalar runs, plus TWO alignment
//    tests the scalar kernel does not have -- `clrlwi. r11, r24, 28` on `a`
//    (0x82E4ED80) and the same on `b` (0x82E4ED88), each returning 0x16.
//    A VMX kernel needs both buffers 16-byte aligned;
//  * a peeled FIRST pass (0x82E4EDFC) reading `a` and writing `b`, which is
//    where the two `stw r6/r5, -0x128/-0x120` spills of the size*2 and size*6
//    byte strides come from -- the stage loop reloads them (0x82E4F058);
//  * the stage loop, ping-ponging b<->a (0x82E4F408-0x82E4F420 swaps the two
//    pointers and does `slwi r29, r29, 2`, i.e. blk *= 4).  Each stage peels
//    group 0, whose twiddle is (1,0) (0x82E4F084), then walks the remaining
//    groups (0x82E4F250);
//  * four different tails.  When stages == 1 and power is even the last
//    radix-4 stage is specialised (0x82E4F428) into a scaled variant for
//    sign > 0 (0x82E4F4B4, `1/size` splat built at 0x82E4F474-0x82E4F4A8)
//    and an unscaled one (0x82E4F61C).  When power is ODD there is one extra
//    RADIX-2 stage instead, again scaled (0x82E4EF98) or not (0x82E4F754),
//    and which buffer it reads is chosen by bit 1 of power
//    (`rlwinm. r11, r20, 0, 30, 30` at 0x82E4EF30).
//
// The twiddle plumbing: the first pass reads THREE tables per iteration --
// twiddle[j] (r31, +0x10 per step), twiddle[half + j] (r3, 0x82E4EE4C) and a
// double-rate twiddle[2j], twiddle[2j+1] (r8, +0x20 per step).  The first two
// are full vectors holding two complex twiddles and are used with the
// cos-duplicate permute; the last two are consumed one lane at a time with
// `vspltw` because after the merge_lo / merge_hi split each output vector's
// two complex slots share a twiddle index.
//
// The sign of the transform picks three things at once (0x82E4ED90-0x82E4EDB0):
// the {1,-1,1,-1} vs {-1,1,-1,1} multiplier, the {0,~0,0,~0} vs {~0,0,~0,0}
// select mask, and which of the two sin-gather permutes is used.
//
// RESIDUAL (w8-f, 46.934383 canonical / 42.465878 fuzzy, 866 emitted against
// 762 in the image, 282 delete / 104 insert).  PARTIAL: the radix-4
// butterfly, the pass/stage skeleton, the four tails and the constant set are
// recovered; the group-loop pointer algebra is not, and that is where most of
// the 282 deletes live.
//
// ⚠ The same alignment fragility documented on fft_recursive above applies
// here, and it bit the one large structural fix this function obviously
// needs.  The biggest single cluster is 79 instructions, ALL delete
// (idx 213-297): it is the scaled radix-2 tail at 0x82E4EF98, which the image
// lays out immediately after the stage-loop guard, whereas ours sits at the
// end of the function.  The image's own branch says so -- `cmpwi cr6, r22,
// 0x1` / `bne cr6, .L_82E4F084` then `clrlwi. r11, r20, 31` /
// `beq .L_82E4F428` at 0x82E4F074-0x82E4F080 jumps OUT past the stage body to
// a block placed after it, so the source leaves the loop by a forward jump,
// not by falling through.  Reproducing exactly that -- `goto last_radix4`
// from inside the stage loop, the odd-power radix-2 tail immediately after
// the loop, and the last-radix-4 block behind a label after `return 0` --
// measured 46.93 -> 28.72.  Not kept, and NOT refuted as a reading: it moves
// ~136 instructions at once and the diff realigns badly while the butterfly
// bodies themselves still disagree.  Revisit it once a stage body pairs.
```


## `?fft_scalar@@YAHPAM0KJ0@Z`

`fft_scalar` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 1347-1360):

```text
// RESIDUAL (w7-ay, 82.5 canonical, floor held): every butterfly loop below
// differs from the image in ONE address decision.  Two operands are read
// twice per iteration -- src[1] and the high imaginary at src+stride4+4 --
// and MSVC materialises exactly one of the two addresses for the reload.
// The image materialises the high one (`add r4, r3, r10` at 0x82E4F8C0,
// reload `lfs f9, 0x0(r4)`) and keeps src[1] as `lfs 0x4(r10)` both times;
// we materialise src+4 (`addi r8, r10, 0x4`), reload src[1] through it, and
// then re-express the high REAL load off it with a -4 bias (`subi r30,
// r27, 0x4` / `lfsx f9, r30, r8`), which also flips the load order and the
// FPR numbering behind every fadds/fsubs.  Tried: loading the high imaginary
// into a named local before t_im (inert -- the load order is a consequence of
// the materialisation, not of source order); reading it twice through one
// `const float* hp` (76.1, MSVC then walks hp as a second induction variable).
// The `hi` pointer form was already worse (b54e87b6d).
```


## `?fft_matrix_forward_columnwise@@YAHPAMJ0@Z`

### Lane F (w-low-xbox), 2026-11-13 -- NOT A BUG (behaviour checked)

Whole FFTComplex run in a Python PPC interpreter over the TARGET listing vs our source built on the host (VMX intrinsics emulated), same input, same N-sized sin/cos table: N=65536 (sign -1 and +1, matrix path) and N=131072 (sign -1, recursive path) agree exactly (max rel err 0.0). Swap test (target function swapped into our build) also OK for fft_matrix_forward_columnwise. Neither side is a DFT with an N-sized table at these sizes (rel err ~1.5 on both) because the sub-FFTs want a sqrt(N) table -- that is the image's behaviour, reproduced. Residual is codegen (scheduling/regalloc), not logic. Harness: /home/free/tmp/fftharness (tbig.py, swap.py).

`fft_matrix_forward_columnwise` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 1694-1731):

```text
                // NEGATIVE RESULT on the twiddle loop's two source pointers.
                // The image walks THREE pointers into this loop -- `mr r10, r29`
                // (temp), `mr r9, r26` (temp2), `mr r8, r30` (data_ptr) -- and
                // loads both halves with a zero index: `lvx128 v63, r0, r10` /
                // `lvx128 v62, r0, r9`, advancing each with its own
                // `addi rN, rN, 0x10` (FFT.s, the block at 0x45c-0x558).  We
                // emit only TWO `mr`, plus `subf r7, r29, r26`, and load the
                // second half indexed off the first: `lvx128 v62, r7, r11`.
                // That is MSVC folding src2 into src1 + (temp2 - temp) --
                // induction-variable elimination, and it cascades into the
                // whole loop's vector-register assignment (61 of this
                // function's 102 mismatch rows are in this one loop).
                //
                // Two variants tried, both BYTE-FOR-BYTE INERT (86.8 canonical /
                // 84.9 raw, 319 instructions, 60/6/19/17 row split, identical
                // before and after):
                //   1. `float*` walked with `+= 4` instead of `char*` walked
                //      with `+= 0x10` -- i.e. spelled exactly like the gather
                //      loop 30 lines above, which does NOT get merged even
                //      though its dst1/dst2 stand in the same temp/temp2
                //      relationship.
                //   2. the two increments separated in source order to match
                //      the image's schedule (src1 right after `k += 1`, src2
                //      down inside the recurrence after the last use of `b`).
                // The gather loop's pointers survive because they are STORE
                // destinations; the merge here is a load-side decision the
                // pointer's spelling and its increment's position do not reach.
                //   3. (w7-ay) indexing both loads off temp/temp2 by k
                //      (`__lvx(temp + k * 4, 0)`, no source pointers at all):
                //      WORSE, 86.6 -- strength reduction rebuilds the same
                //      merged pair.
                //   4. (w7-ay) outside the loop: assigning sv.f[] / w_re1 /
                //      w_re2 / w_im2 straight from the sin()/__vspltw/__vmrglw
                //      expressions instead of via the sin2_1/v_cos_splat/
                //      v_cos_merged/v_sin_merged locals: byte-for-byte inert
                //      (the `vor128 v62, v63, v63` copy of v_cos_vec and the
                //      `fmul f0, f1, f1` square-before-copy at the first sin
                //      return are scheduling, not spelling).
```


## `?fft_matrix_inverse_columnwise@@YAHPAMJ0@Z`

### Lane F (w-low-xbox), 2026-11-13 -- NOT A BUG (behaviour checked)

Whole FFTComplex run in a Python PPC interpreter over the TARGET listing vs our source built on the host (VMX intrinsics emulated), same input, same N-sized sin/cos table: N=65536 (sign -1 and +1, matrix path) and N=131072 (sign -1, recursive path) agree exactly (max rel err 0.0). Swap test (target function swapped into our build) also OK for fft_matrix_forward_columnwise. Neither side is a DFT with an N-sized table at these sizes (rel err ~1.5 on both) because the sub-FFTs want a sqrt(N) table -- that is the image's behaviour, reproduced. Residual is codegen (scheduling/regalloc), not logic. Harness: /home/free/tmp/fftharness (tbig.py, swap.py).

`fft_matrix_inverse_columnwise` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 1947-1957):

```text
            // BEHAVIOURAL FIX (w7-ay): the gather loop below walks COPIES of
            // temp/temp2 -- the image's `mr r9, r28` / `mr r8, r27` at
            // 0x82E4E970-74 -- and r28 (temp) is what the two FFTComplex
            // calls, the deinterleave and free() then use (0x82E4EA78,
            // 0x82E4EAA8, 0x82E4EB1C).  The permuter harvest fb98fec2e had
            // replaced them with `temp += 4` / `temp2 += 4`, which scored
            // 3pp higher (89.0 vs 86.0) and handed FFTComplex, the second
            // loop and free() a pointer half_cols*16 bytes past the malloc.
            // Keep the copies; the score is not the point.  Declared here,
            // before src_data, like the forward transform; scoping them
            // inside the guard is worse (85.6).
```


## `?fft_real_forward_altivec@@YAHPAMJ0@Z`

`fft_real_forward_altivec` -- moved verbatim from `src/system/synth_xbox/FFT.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 2092-2106):

```text
// RESIDUAL (w7-an, 79.1 canonical): the rest is VMX/FPR allocation and the
// schedule it drives.  (a) The image homes perm_d/perm_e on the stack and
// keeps sLo in a register for the whole loop; we keep both perm controls in
// registers and spill sLo instead, which costs three extra vector memory ops
// per iteration.  (b) The image walks the low half with two induction
// variables (`addi r9, r31, 0x10` in the preheader, then `addi r8/r9, .., 0x10`
// separately); MSVC folds ours into one, recomputing loRead as `loWrite + 16`
// and rotating with `mr`.  (c) The double trig recurrence is scheduled
// s-chain-first in our build and c-chain-first in the image.
// NEGATIVE RESULT (w7-an, 2026-09-14): reversing the four `sv.f[n]` stores is
// byte-for-byte inert; so is reversing the uc/us declarations, and so is
// swapping the perm_d/perm_e declarations.  Reversing the four
// `c1 = c1 - uc1` / `s1 = s1 - us1` updates buys +0.5pp but SHRINKS the frame
// by 0x10 and changes the callee-save helpers, so it is a step away from the
// image's prologue -- not kept.
```
