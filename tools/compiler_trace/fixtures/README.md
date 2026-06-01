# Compiler-trace proof fixtures

Tiny, self-contained C++ functions compiled through the **real** Xbox 360
toolchain (cl.exe 16.00.11886.00 / c2.dll via 32-bit wibo, project flags
`/O1 /Oi /GR /EHsc`) to PROVE a specific claim about MSVC's PowerPC code
generation. Each fixture's header comment states a PREDICTION and the OBSERVED
machine bytes; the file is both the empirical proof and a durable regression
artifact.

## Files

- **`fmuls_operand_order.cpp`** — proves the operand-order rule for the
  commutative FP family (`fmuls fD,fA,fC`, `fadds fD,fA,fB`,
  `fmadds fD,fA,fC,fB`). Companion analysis:
  `docs/plans/port-harvest/stream3-ideas/05-fmuls-operand-order.md`.
- **`int_commutative_operand_order.cpp`** — proves the *same* operand-order rule
  for the commutative INTEGER family (`xor`, `add`, `or`, `and`, `mullw`, the
  `subf`-based abs idiom). Source spelling / statement order / operand liveness
  all discarded; the A/B slot tracks the post-regalloc physical register, not the
  source. Companion analysis:
  `docs/plans/port-harvest/stream3-ideas/06-commutative-int-and-levers.md`.

## How to re-run a fixture

These fixtures do **not** need a full `ninja` build — they only use the
checked-in toolchain under `build/tools/wibo` + `build/compilers/X360/...` plus
`config/373307D9/config.json` and `tools/defines_common.py` (a freshly
`setup_worktree.sh`'d tree already has all of these). From the repo root (or any
worktree root):

```bash
python3 -c "from pathlib import Path; \
  from tools.compiler_trace.invoker import CompilerInvoker; \
  r=CompilerInvoker().compile_with_asm( \
    Path('tools/compiler_trace/fixtures/fmuls_operand_order.cpp'), \
    Path('tools/compiler_trace/fixtures/_out'), listing_type='/FAcs'); \
  print('rc', r.returncode)"

# Read just the FP instructions (one PROC NEAR + its ops per fixture):
grep -E 'PROC NEAR|lfs |fmuls|fadds|fmadds|stfs|bl ' \
  tools/compiler_trace/fixtures/_out/fmuls_operand_order.cod

rm -rf tools/compiler_trace/fixtures/_out   # the .cod/.obj are throwaway
```

The `.cod` is an MSVC `/FAcs` listing: each instruction line is
`  <offset>\t<machine-word-hex>\t <mnemonic> <operands>`. The hex word is the
encoded instruction; compare it against the `OBSERVED ... (hhhhhhhhh)` values in
the fixture's header comment.

## Decoding a PPC A-form FP word by hand

For `fmuls`/`fadds`/`fmadds` the 32-bit word packs operands as:

```
D = bits[21:25]   A = bits[16:20]   B = bits[11:15]   C = bits[6:10]   xo = bits[1:5]
fmuls  xo=25 (0x19): operands fD,fA,fC        (B field unused/0)
fadds  xo=21 (0x15): operands fD,fA,fB        (C field unused/0)
fmadds xo=29 (0x1D): operands fD,fA,fC,fB
```

e.g. `0xec200372`: D=fr1, A=fr0, B=fr0(unused), C=fr13, xo=25 → `fmuls fr1,fr0,fr13`.

## Adding a new fixture

1. Add a tiny function with a header comment: PREDICTION (what + why) then leave
   OBSERVED blank.
2. Compile + grep as above; paste the actual mnemonic and hex into OBSERVED.
3. Mark PROVEN / BROKEN. If BROKEN, that is the more important finding — say so
   loudly and update the companion doc.

Keep fixtures minimal (one variable each), keep the predictions falsifiable, and
only ever paste bytes you actually observed.
