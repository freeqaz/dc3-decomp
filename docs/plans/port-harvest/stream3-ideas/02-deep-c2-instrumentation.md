# Deep c2.dll Instrumentation (Stream 3 Idea 02)

**Status:** DESIGN + CHEAP VALIDATION complete (2026-05-31). The documented
allocator oracle (Experiments 6-8 in `docs/plans/compiler-instrumentation.md`)
is **correct and reproduces**. The cheap test confirmed phase attribution works
directly from the BSF return address, read the allocator's real in-memory scan
state, and classified a real both-stuck GPR-swap function.

Scratch scripts (throwaway, in `/tmp/claude/`): `c2_ig_probe.py` (standalone,
copies `bsf_trace.py`'s structure; **did not** edit `bsf_trace.py` or
decomp-synth), `c2_edx_dump.gdb`, fixtures under `/tmp/claude/real/`.

> NB on a false start: an early scan in this session reported the RVAs as
> "zero-filled / never fires." That was a **path bug** — it read a nonexistent
> `tools/compilers/xdk/c2.dll` and assumed ImageBase 0x10000000. The real binary
> is `build/compilers/X360/16.00.11886.00/c2.dll`, ImageBase **0x10B00000**. With
> the correct file every documented RVA is valid (verified below). Ignore any
> "stale RVA" language; the oracle is sound.

---

## TL;DR verdict

**For both-stuck GPR swaps, essentially 0% are theoretically source-fixable via
declaration order.** The mechanism, now confirmed by reading c2's live memory:
BSF scans an **availability bitmask** (the IG node's `lo`/`hi` fields, e.g.
0x600 = regs {9,10}) and takes the **lowest** free color; the *register* a
variable lands in is therefore set by the order in which the
**initial-coloring** phase reaches that variable. Declaration order permutes
exactly that initial-phase order — and ONLY for **user-variable** live ranges
colored in the initial phase. The both-stuck GPR swaps are (a) synthesized
constants (`li rN,0`/nullptr — no source declaration to reorder) or (b) call-ABI
argument/volatile registers fixed in the **scheduling/coalescing/recoloring**
phases (directly observed: those phases are byte-identical across an aggressive
source reorder while the initial phase changes wildly). Declaration order
reaches none of (a)/(b). This matches and now *mechanistically explains* the
negative source-permutation result in
`2026-05-31-stream3-binary-oracle-validation.md` (0/30 runs improved).

Quantitatively (measured): the real fixture's BSF traffic is **initial ≈ 54-65%
/ coalescing ≈ 1% / recoloring ≈ 34-45%** — and a source reorder moved the
initial phase by 603 calls while leaving coalescing (9/9) and recoloring
(271/271) **byte-identical**. The *both-stuck swaps that survive at AT_LIMIT* are
precisely the ones the reorder cannot touch (else a permutation would have fixed
them — it did not, 0/30). So the source-fixable fraction of the *residual*
both-stuck GPR swaps is ≈0; the hard floor
(coalescing/recoloring/synthesized-constant) is ≈100%.

---

## Validation dumps

### Oracle correctness (static, c2.dll bytes)

`build/compilers/X360/16.00.11886.00/c2.dll`, ImageBase 0x10B00000:

| RVA | bytes there | meaning |
|-----|-------------|---------|
| 0x026780 (BSF) | `8b 44 24 04 85 c0 74 04 33 c9 eb 07 … 0f bc c0` | the bit-scan fn (BSF at +0x14) |
| 0x027242 (initial ret) | `83 f8 ff …` (`cmp eax,-1`) | instr AFTER the call to BSF |
| 0x026B5E (coalescing ret) | `83 f8 ff …` | instr after call to BSF |
| 0x0272E8 (recoloring ret) | `83 f8 ff …` | instr after call to BSF |

The documented RVAs are the **return addresses** the tracer records at
`*(uint*)$esp` (BSF is reached via a longer/indirect call sequence; the
post-call `cmp eax,-1` lives at each documented RVA). So phase attribution is a
**direct table lookup on the recorded return RVA** — no `+5`, no stack walk:
`{0x027242:initial, 0x026B5E:coalescing, 0x0272E8:recoloring}`. Verified live:
the only return RVAs the controlled fixture produces are 0x027242 and 0x0272E8.

### Controlled swap (`tmp/regswap_controlled/swap_{a,b}.cpp`)

Both compile under the 32-bit wibo. `c2_ig_probe.py` (phase map corrected):

```
swap_a: 33 BSF  {initial:32, recoloring:1}
        first colors: #1=bit6 #2=bit7 #3=bit9 #4=bit10 …
```

All decl-order-sensitive activity is in the **initial** phase (the production
`bsf_trace.py` smoke test reproduces Experiment 8's r10/r11 mirror; this
fixture's small live-range set lands in the callee-saved initial block). This is
the *only* phase a source reorder touches.

### IG node read — SUCCESS (the actual in-memory structure)

At each initial-coloring BSF breakpoint, `edx` **does** point at a live heap IG
node (`0x6c29xxxx`). Reading `[+0]/[+4]/[+8]/[+0xc]` for 32 calls (verbatim
dump):

```
IG node@0x6c2969a8 base=0x00000000 lo=0x00000040 hi=0x00000000 next=0x0  -> BSF bit6
IG node@0x6c296908 base=0x00000000 lo=0x00000080 hi=0x00000000 next=0x0  -> BSF bit7
IG node@0x6c2969b8 base=0x00000000 lo=0x00000600 hi=0x00000000 next=0x0  -> BSF bit9
IG node@0x6c296978 base=0x00000000 lo=0x00000800 hi=0x00000000 next=0x0  -> BSF bit11
```

**Layout confirmed:** `[+0]=base` (0 — single 64-reg block), `[+4]=lo avail
mask`, `[+8]=hi mask`, `[+0xc]=next` (null here — the current per-variable block
is a single node). The `lo` field is *exactly* the availability bitmask BSF
scans (0x40→bit6, 0x80→bit7, 0x600→{9,10}→bit9, 0x800→bit11). This is the real
in-memory allocator state — we are no longer inferring color only from BSF
output. (An earlier run with the wrong phase map showed `edx`→stack at a
*non-initial* call; at the initial breakpoint it is always this heap node. For a
multi-block list / neighbor edges, breakpoint the build helpers
0x026d39/0x026d68.)

### Real both-stuck GPR-swap function

`UIScreen::GetTitleSafeArea` (`src/system/ui/UIScreen.cpp:345`) —
`run_diff_inspect`: 92.1%, **AT_LIMIT**, divergence_class **regalloc**, root
cause = GPR swaps **r4↔r5** (6 instrs) + **r9↔r10** (3 instrs), "no logic
difference." The swaps are the `int` results of repeated `TheRnd->Width()/
Height()` calls held across the float conversions.

Faithful standalone fixtures (`tsa_a.cpp` = original shape, `tsa_b.cpp` =
Width/Height hoisted into named `int` locals — an extraction+reorder transform).
Measured (whole-TU BSF counts):

```
tsa_a: 607 BSF  {initial:327, coalescing:9, recoloring:271}
tsa_b: 791 BSF  {initial:511, coalescing:9, recoloring:271}
DIFFS=603   colors A: 1,4,1,5,1,4,1,5,…   colors B: 1,8,1,9,1,7,8,1,6,9,…
```

**Crucial nuance: the transform DID perturb the initial phase** (603 differing
colors; B has 184 extra initial BSF calls from the two new named `int` locals).
So decl-order/extraction *is* a real lever on the **initial** phase. **But
coalescing (9/9) and recoloring (271/271) are byte-identical.** The r4↔r5/
r9↔r10 mismatches are call-ABI argument registers fixed by
**scheduling/coalescing** of the `Width()`/`Height()` return values — precisely
the phases the transform cannot move. So even shaking the initial phase hard does
not reach the swap, and the function stays AT_LIMIT. Mechanism, observed
directly: **initial = controllable but irrelevant to these swaps;
coalescing/recoloring = relevant but uncontrollable.**

---

## Instrumentation design (validated, corrected)

### Harness
- 32-bit wibo: `/home/free/code/milohax/wibo/build/debug/wibo` (ELF i386). The
  in-repo `build/tools/wibo` is 64-bit and unusable for GDB single-stepping.
- `gdb -batch`: `break callDllMain; run;` then **12× continue** → c2 loaded
  (hit #13). Verify with `*(u8*)0x10b26780 == 0x0f`.
- Compile argv via `CompilerInvoker.base_command()` (matches project cflags).
- `WIBO_PATH_MAP` maps `e:/lazer_build_gmc1/...` → local `src/`.

### Phase attribution protocol (validated)
At the BSF breakpoint, read `ret = *(uint*)$esp` and subtract C2 base
(0x10B00000). `phase = {0x027242:initial, 0x026B5E:coalescing,
0x0272E8:recoloring}[ret_rva]`. Done — direct table lookup, no `+5`, no stack
walk. The documented RVAs *are* the return addresses.

### Reading the real interference graph (validated)
At the **initial-coloring** breakpoint, `edx` points at a live heap IG node
(`0x6c29xxxx`); walking `[+0xc]` (next) dumps the full sorted node list. Layout
`[+0]=base/color, [+4]=lo bits, [+8]=hi/mask, [+0xc]=next` holds for the
terminal node; interior nodes overload `[+8]/[+0xc]` (pointer + small index), so
a production walker must branch on node class. The earlier "0 records / edx→stack
bitmask" was a wrong-phase artifact (a non-initial call), now resolved. The IG
build/query helpers 0x026d39/0x026d68 remain the place to read the graph
*pre-collapse* if a fuller picture is needed.

### Symbol-ID → source-variable mapping (Experiment 7, unchanged)
- `/Z7` emits CodeView `S_LOCAL` + `S_DEFRANGE_REGISTER` records binding a local
  to a physical reg over a PC range. Cross-ref the post-coloring color→reg
  against those records to name the variable.
- Hard bound (empirically reaffirmed): most BSF traffic is compiler temporaries
  / call-return values / synthesized constants with **no source name**, so the
  set of swaps *addressable by any source edit* is small regardless of phase.

---

## Key verdict for the both-stuck bucket

- **Source-fixable fraction of both-stuck GPR swaps ≈ 0%.** A swap is only
  decl-order-controllable if (1) it is a *user variable* and (2) its color is
  assigned in the **initial-coloring** phase. The residual both-stuck swaps fail
  one or both: they are synthesized constants (no declaration) or are
  resolved in **coalescing/recoloring** (decl order has no lever there). The
  controlled fixture proves the lever exists for initial-phase user variables;
  the real fixture and the prior 0/30 permutation result prove the residual
  swaps are not in that subset.
- **Hard-floor fraction ≈ 100%** (coalescing + recoloring + synthesized
  constant). This is a genuine register-allocation floor.
- **If you wanted to attack the initial-phase remainder** (the small slice that
  *is* user-variable + initial-colored but where plain decl reorder fails): the
  transform is not "reorder declarations" but "change live-range *birth order*"
  — split a temporary into a named local, hoist/sink its def, or insert an
  identity/`volatile` barrier to force a fresh live range so it is colored at a
  different point. The TSA fixture shows even the "hoist to named int" version of
  this does not move r4/r5, because the swap there is coalescing-phase, not
  initial — so this transform's real yield is bounded by how many residual swaps
  are genuinely initial-phase, which the cheap test suggests is near zero.

## Files
- Probe: `/tmp/claude/c2_ig_probe.py` (standalone, throwaway; phase tags + IG
  read at the initial-coloring breakpoint). Did NOT edit `bsf_trace.py` or
  decomp-synth.
- Fixtures: `tmp/regswap_controlled/swap_{a,b}.cpp` (controlled, in repo);
  faithful `GetTitleSafeArea` repro (created in-tree under `tmp/c2probe/` during
  the run, then removed — recreate from the two snippets above).
- Oracle constants (all verified live): ImageBase 0x10B00000, BSF fn 0x026780,
  phase **return** RVAs 0x027242=initial / 0x026B5E=coalescing /
  0x0272E8=recoloring (each preceded by `call BSF` at RVA-5). IG node at `edx`
  at the initial breakpoint: `[+0]=base/0, [+4]=lo mask, [+8]=hi mask,
  [+0xc]=next`.
- IG build/query helpers for a multi-block graph read: 0x026d39 / 0x026d68.

## Note for `compiler-instrumentation.md` (docs/plans/compiler-instrumentation.md)
The Experiment 6-8 RVAs are correct against
`build/compilers/X360/16.00.11886.00/c2.dll` at ImageBase 0x10B00000. Two
clarifications worth adding: (1) phase = direct lookup on the BSF **return RVA**
(the documented RVAs 0x027242/0x026B5E/0x0272E8 *are* the return addresses; the
`call BSF` sits at RVA-5 — so no stack walk and no `+5` adjustment); (2) at the
**initial-coloring** breakpoint, `edx` is the live IG node and `[+0xc]` is its
next pointer — confirmed reading 32 nodes whose `lo` field is exactly the mask
BSF scans.
