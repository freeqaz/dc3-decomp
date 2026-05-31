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
the available-register set handed to BSF is already a **single-bit mask** — the
color is decided upstream. Declaration order only permutes the *order* in which
the **initial-coloring** phase consumes colors, which only relabels
**user-variable** live ranges. The both-stuck GPR swaps are (a) synthesized
constants (`li rN,0`/nullptr — no source declaration to reorder) or (b) decided
in the **coalescing/recoloring** phases, neither of which declaration order
reaches. This matches and now *mechanistically explains* the negative
source-permutation result in `2026-05-31-stream3-binary-oracle-validation.md`
(0/30 runs improved).

Quantitatively, across both controlled and the real fixture, BSF traffic splits
roughly **initial ≈ 56% / coalescing ≈ 32% / recoloring ≈ 11%** of calls — but
the *both-stuck swaps that survive at AT_LIMIT* are precisely the ones NOT in the
decl-order-controllable initial subset (else a permutation would have fixed
them). So the source-fixable fraction of the *residual* both-stuck GPR swaps is
≈0; the hard floor (coalescing/recoloring/constant) is ≈100%.

---

## Validation dumps

### Oracle correctness (static, c2.dll bytes)

`build/compilers/X360/16.00.11886.00/c2.dll`, ImageBase 0x10B00000:

| RVA | bytes | meaning |
|-----|-------|---------|
| 0x026780 (BSF) | `0f bc 44 24 08 0f 45 …` | `bsf eax,[esp+8]; cmovne …` |
| 0x027242 (initial) | `e8 …` → target **0x026780** | `call BSF` |
| 0x026B5E (coalescing) | `e8 …` → target **0x026780** | `call BSF` |
| 0x0272E8 (recoloring) | `e8 …` → target **0x026780** | `call BSF` |

All three phase call sites are 5-byte `call rel32` into the BSF function. So a
BSF call's **return address = call_site + 5** uniquely identifies the phase:
0x027247=initial, 0x026B63=coalescing, 0x0272ED=recoloring. The production
tracer already captures this return address (`$caller = *(uint*)$esp`); the
probe just maps it to a phase name.

### Controlled swap (`tmp/regswap_controlled/swap_{a,b}.cpp`)

Both compile under the 32-bit wibo. `c2_ig_probe.py`:

```
swap_a: 170 BSF  {initial:96, coalescing:60, recoloring:14}
swap_b: 170 BSF  {initial:96, coalescing:60, recoloring:14}
initial-coloring colors:  swap_a #1=bit2 #2=bit1   swap_b #1=bit1 #2=bit2  (MIRROR)
```

The declaration swap flips exactly the first two **initial-coloring** colors —
reproducing Experiment 8 byte-for-byte. This is the *only* phase a source
reorder touches.

### IG / scan-state read (the actual in-memory structure)

At each initial-coloring BSF breakpoint, raw memory at the registers:

```
INIT #1 ret=0x10b27247 edx=0x0013f9a4 ecx=0x4 esi=0x0013fc28 edi=0x2
  [edx]  = 0x00000004 0x00000000 0x00000020 0x00000000 0x00000001 0x0000001f 0x0013fc28 0x00000004
  [esi]  = 0x00000000 0x00000004 0x00000008 0x0000000c ...
```

**Decisive structural finding:** `edx` does **not** point at a heap IG node
with `[+0]=base / +4=lo / +8=hi / +0xc=next`. It points into the **wibo guest
stack** (0x0013xxxx) at the **availability bitmask itself**: word[0]=`0x4`=lo
(== `ecx`), word[1]=`0x0`=hi, then scan bookkeeping. `esi` points at a small
ascending `{0,4,8,12,…}` table (color/register-index map). The documented
`[node+0x00/0x04/0x08/0x0c]` IG layout (built by the RVA 0x026d39/0x026d68
helpers) is constructed/consumed **earlier**, not live at the BSF call — by BSF
time the interference graph has already been reduced to a one-bit available set
(consistent with Experiment 6's "exactly ONE bit set per BSF call; the decision
is made BEFORE BSF"). So `c2_ig_probe.py`'s edx-chain walk correctly dumps 0
linked-list records: there is no list to walk *here*. To read the real IG you
must breakpoint the **graph-build helpers (0x026d39 insert / 0x026d68 lookup)**,
not the BSF site.

### Real both-stuck GPR-swap function

`UIScreen::GetTitleSafeArea` (`src/system/ui/UIScreen.cpp:345`) —
`run_diff_inspect`: 92.1%, **AT_LIMIT**, divergence_class **regalloc**, root
cause = GPR swaps **r4↔r5** (6 instrs) + **r9↔r10** (3 instrs), "no logic
difference." The swaps are the `int` results of repeated `TheRnd->Width()/
Height()` calls held across the float conversions.

Faithful standalone fixtures (`/tmp/claude/real/tsa_a.cpp` = original shape,
`tsa_b.cpp` = Width/Height hoisted into named `int` locals — an
extraction+reorder transform) both trace to **identical** phase counts
(121 BSF: initial 70 / coalescing 38 / recoloring 13). The GPR swap here is
driven by the **scheduling/coalescing** of the call-return values into r4/r5/
r9/r10, not by a user-variable declaration the initial phase colors — which is
why even the aggressive "hoist to named int" transform does not change the
allocation, and why this function sits at AT_LIMIT.

---

## Instrumentation design (validated, corrected)

### Harness
- 32-bit wibo: `/home/free/code/milohax/wibo/build/debug/wibo` (ELF i386). The
  in-repo `build/tools/wibo` is 64-bit and unusable for GDB single-stepping.
- `gdb -batch`: `break callDllMain; run;` then **12× continue** → c2 loaded
  (hit #13). Verify with `*(u8*)0x10b26780 == 0x0f`.
- Compile argv via `CompilerInvoker.base_command()` (matches project cflags).
- `WIBO_PATH_MAP` maps `e:/lazer_build_gmc1/...` → local `src/`.

### Phase attribution protocol
At the BSF breakpoint, read `ret = *(uint*)$esp`. `phase = {0x10b27247:initial,
0x10b26b63:coalescing, 0x10b272ed:recoloring}[ret]`. Done — no stack walk
needed; the call sites are leaf calls into BSF.

### Reading the real interference graph (TODO if ever needed)
The IG nodes (`[+0]=base aligned-64, +4=lo, +8=hi, +0xc=next`) are *built* and
*queried* by 0x026d39 (`insert_interference`) / 0x026d68 (`lookup_interference`).
Breakpoint **those** and walk the list head they receive — not the BSF callsite,
where the graph is already collapsed to a one-bit mask. The probe's edx-walk is
the wrong vantage point (proven: 0 records, because edx → stack bitmask).

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
- Probe: `/tmp/claude/c2_ig_probe.py` (standalone; phase tags + edx/IG dump).
- Fixtures: `tmp/regswap_controlled/swap_{a,b}.cpp` (controlled, in repo);
  `/tmp/claude/real/tsa_{a,b}.cpp` (faithful `GetTitleSafeArea` repro).
- Oracle constants (all verified): ImageBase 0x10B00000, BSF 0x026780,
  phase call sites 0x027242 / 0x026B5E / 0x0272E8, return tags +5.
- IG-build helpers to breakpoint for a real graph read: 0x026d39 / 0x026d68.

## Note for `compiler-instrumentation.md`
The Experiment 6-8 RVAs are correct against
`build/compilers/X360/16.00.11886.00/c2.dll` at ImageBase 0x10B00000. Worth
adding two clarifications: (1) phase = BSF return-address (`call_site+5`), no
stack walk; (2) at the BSF callsite `edx` points at a stack-resident
availability bitmask, not a heap IG node — read the real IG at the
0x026d39/0x026d68 helpers instead.
