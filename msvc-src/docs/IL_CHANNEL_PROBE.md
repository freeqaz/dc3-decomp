# IL-channel feasibility probe (v0) — MSVC c1xx→c2 intermediate language as a dc3 signal

Date: 2026-07-10. Scope: **probe + feasibility readout only.** No training
integration, no exporter/prompt changes, no decomp-synth engine edits. This
report answers three questions: (1) does IL capture work on the current
toolchain, (2) what does the IL expose that the objdiff/asm-observable channel
cannot, and (3) is a leak-safe v0 IL field set worth building. Standing rulings
are respected throughout: compiler + objdiff is the sole judge; the only
eventual A/B endpoint is terminal byte-exact @ budget; no move/pass-family
prediction (R2 / F34 / F35 / F-AXIS-NULL); training integration is owner-gated.

Cross-refs: `IL_FORMAT.md` (opcode map), `PPC_IL_LIFTER.md` (constrained
PPC→IL lift), and decomp-synth
`docs/plans/reverse-compilation/edit-dag-process/04_REVIEW_COMPILER_STATE_CONDITIONING.md`
(the compiler-state channel review; read SS2/SS3/SS4/SS5 — this probe does not
edit it, it recommends amendments below).

---

## 1. Does capture work? YES — one path needed a 2-point de-drift fix

Two independent capture paths exist. Their status on the current wibo
(`build/tools/wibo`, dated 2026-05-28; and the ninja release wibo):

| Path | Tool | Method | Real dc3 TUs | Build-faithful flags | Bundles/manifest | Status |
|---|---|---|---|---|---|---|
| strace unlink-inject | `tools/compiler_trace/il_capture.py` (`capture-il`) | strace `inject=unlink,unlinkat:retval=0`, `TMP`→tmpdir, glob `_CL_*` | yes (has include flags via `CompilerInvoker`) | **NO** (invoker base flags omit `/TP` and per-file flags; no `WIBO_PATH_MAP`) | no | **WORKS** on toys and real TUs |
| /Bd /d2nop fail-early | `msvc-src/tools/il_parser.py` (`capture`) | `/Bd /d2nop` makes c2 fail before it deletes IL; ninja-extracted exact compile cmd | yes (pulls real flags + `WIBO_PATH_MAP` from `ninja -t commands`) | **YES** | yes (`--bundle-name` + `manifest.json`) | **WAS BROKEN by drift → fixed here** |

**The drift (same class that broke `bsf_trace`, different mechanism).** The
current compiler emits the IL temp path as
`-il /home/<user>/tmp\_CL_<hash>` (a real tmp dir + backslash + basename), not
the bare `_CL_<hash>` in CWD the parser assumed. Two failures resulted:
1. the basename regex `-il\s+(_CL_[0-9a-f]+)` never matched (path prefix), so
   capture aborted with "Could not find IL base name"; and
2. the `_CL_*` files landed in the compiler's tmp dir, not `run_cwd`/`output_dir`,
   so even a basename would not have been found.

**Fix (minimal, `il_parser.py` `capture_il` only):** strip any leading
directory before the `_CL_` basename, remember the reported tmp dir, and search
it first when relocating the files into `output_dir`. No refactor; the older
bare-`_CL_` behavior still works. The strace path (`il_capture.py`) needed **no
change** — it already forces `TMP` and globs, so it captures correctly (but is
not build-faithful and produces no bundle/manifest).

> Note: `il_parser.capture_il` is exactly what decomp-synth
> `Scorer.capture_variant_il_hashes` (`decomp_synth/scorer.py:940`) calls. So
> this de-drift fix silently **restores that existing dc3-only consumer**
> without any decomp-synth edit — it was returning `{}` (no IL dedup) under the
> drift.

### Reliability matrix (5 real byte-exact-solved dc3 TUs, collection-frame rule)

All chosen from objdiff `metadata.complete == true` units (known byte-exact
states), captured build-faithfully via the fixed ninja `/d2nop` path. Wall time
is ~0.1–0.2 s because `/d2nop` fails c2 immediately — only the c1xx front-end
runs (this also means **`/d2nop`-captured IL must never be paired with a
same-run `.obj`**; there is none).

| TU (src) | src bytes | capture | .ex | .gl | .sy | .in | .db | wall |
|---|---|---|---|---|---|---|---|---|
| `math/Sort.cpp` | 209 | OK | 2,955 | 315 | 121 | 439 | 97 | 0.1s |
| `math/Rand2.cpp` | 355 | OK | 4,732 | 1,022 | 591 | 503 | 3,319 | 0.2s |
| `utl/Cache.cpp` | 380 | OK | 3,029 | 3,124 | 787 | 560 | 8,566 | 0.1s |
| `rndobj/Poll.cpp` | 360 | OK | 27,326 | 21,543 | 6,233 | 777 | 50,908 | 0.2s |
| `math/Decibels.cpp` | 363 | OK | 114,167 | 57,327 | 32,117 | 5,358 | 121,445 | 0.2s |

Committed sanitized bundles (small three) live under
`msvc-src/analysis/il-fixtures/probe_v0/{probe_Sort,probe_Rand2,probe_Cache}`;
Poll/Decibels are kept out of the repo for size but reproduce from the same
command in each `manifest.json`. **Compile rate on these leaf TUs: 5/5.**
`Decibels.cpp` (363 source bytes → 618 IL functions / 114 KB `.ex`) shows the
header/template blow-up: "small source" says nothing about IL size once STL
(`stlpmtx_std` list/string/allocator) instantiations are pulled in.

Cost per capture: one c1xx front-end run (~0.2 s CPU, no c2, no link). Cheap.

---

## 2. Parse coverage on real TUs

Parsed all five with `il_parser.py` (`ILFile.to_dict`), tallying opcodes
against the `IL_FORMAT.md` map:

| TU | IL funcs | ops decoded | distinct opcodes | opcodes NOT in IL_FORMAT map |
|---|---|---|---|---|
| Sort | 1 | 13 | 8 | 0 |
| Rand2 | 13 | 60 | 17 | 0 |
| Cache | 17 | 66 | 13 | 0 |
| Poll | 68 | 1,073 | 28 | 0 |
| Decibels | 618 | 3,385 | 32 | 0 |

**Opcode-name coverage is complete: 32 distinct opcodes across all five TUs,
every one already documented in `IL_FORMAT.md`** — including the "hard" C++
constructs (`VCALL_SETUP`/`VCALL_BIND` virtuals, `SWITCH_TABLE`, `MEMBER_PTR`,
`CAST` promotions, `LOGICAL_AND`/`LOGICAL_OR` short-circuits). The toy-derived
map generalizes to templated/virtual real C++ without new reverse engineering.

Honest caveats (do not over-read the table):
- **Byte-level coverage is partial.** The parser silently *skips* bytes it does
  not recognize (the 2,640-byte `.ex` header index, inter-op type-table and
  metadata bytes). "0 unknown opcodes" is an opcode-*name* statement, not a
  claim that 100 % of `.ex` bytes are decoded.
- **`.gl` name mapping is positional and mis-attributes.** The parser always
  attaches *a* name, but the documented sequential off-by-one bites: `Rand2`
  parses 13 IL functions but `.gl` lists only 7 real symbols, so the accessor
  body under `?Int@Rand2@@…` is not actually `Rand2::Int`. Treat parsed function
  names as advisory on any TU with compiler-generated/inlined bodies.

### Provenance channels (the doc-04 "source→IR→asm links" question)

- **`.sy` recovers real parameter names** — e.g. Poll: `CyclesToMs(s)`,
  `Str(t2, t1, c)`, plus `this`. 6/68 Poll functions carry named params (rest
  are `…XZ` void-param methods).
- **`.db` carries a line map** (`debug.line_candidates` + source-file strings).
- **`.gl` carries mangled names + the source path** (`e:\lazer_build_gmc1\…\rand2.cpp`).

Together these are a genuine **source→IL provenance link** — but see SS4:
it is **candidate-side only**.

---

## 3. Causal tests — what IL shows that asm cannot

All three use the compiler + a normalized-obj oracle as the sole judge. The
`.obj` is deterministic **except the 4-byte COFF `TimeDateStamp`** (offset 4–7;
no `/Zi` in these compiles, so nothing else varies); zeroing it makes identical
source → byte-identical obj. This is the local, mechanical reason the project
judges with objdiff, not raw file bytes.

### (a) Front-end-equivalence oracle — IL is *stricter* than the obj

Two variants that compile to a byte-identical `.obj`: is the IL identical?

| variant pair | obj identical? | IL `.ex` identical? | `.ex` byte diffs |
|---|---|---|---|
| local rename `x`→`y` (same layout) | **yes** | **yes** | 0 |
| whitespace/comment only | **yes** | **no** | 74 |
| `x = x + 1` vs `x += 1` | **yes** | **no** | 46 |

Reading: `.ex` is deterministic and **name-independent** (rename → identical
IL), but it preserves front-end *syntactic form* that c2 later normalizes away —
source line layout (line numbers live inline in `.ex`, not only in `.db`) and
compound-vs-expanded assignment (`COMPOUND_ADD 0x0F` vs `ADD`+`STORE`). Since
`.ex` is c2's **only functional input** (`.sy`/`.gl`/`.db` are
names/linkage/debug), the logic is one-directional and provable:

> **IL-identity ⟹ obj-identity** (sound; a conservative, zero-false-positive
> equivalence key). **obj-identity ⟹ IL-identity is FALSE** (whitespace,
> compound form). So an IL hash is a *safe* dedup/skip key that *misses* some
> true equivalences — never merges non-equivalent candidates, but will not
> collapse whitespace/compound-form obj-twins.

This validates the *soundness* of `Scorer.capture_variant_il_hashes` (dedup
only, "every candidate still goes through build + objdiff") and bounds its
recall.

### (b) IL-delta sensitivity — IL localizes the *cause*; asm shows the *effect*

One controlled non-matching edit: `int` vs `unsigned` in `a > b ? 1 : 0`.

- **IL delta = 4 bytes**, and they are exactly the two operand type markers on
  the `GT`: `86 41 74` (int) → `86 42 75` (uint). The signedness cause is
  pinpointed to 4 typed bytes.
- **ASM delta = a whole instruction-selection rewrite**: signed
  `subfc / eqv / srwi / addze / clrlwi / blr` (6) → unsigned
  `subfc / subfe / clrlwi / blr` (4). Differs at 3 of the middle instructions
  and changes length.

An objdiff reader sees only the diffuse 5→3 sequence swap and must reverse-infer
"this is a sign flip." The IL states the cause directly (one type bit on two
operands).

### (c) Pre-optimization structure inventory (cross-checked with the lifter)

`ppc_il_lifter.py compare-source … --function fn` on the signed variant:

```
Source IL:      GT(a, b) ; CB(1, 0) ; ASSIGN ; RETURN
Lifted PPC:     SUB_CARRY ; EQV ; SHR[31] ; ADD_ZERO_EXTEND ; BYTE_MASK ; RETURN
Derived facts:  bool_materialization: signed_ordered (conf=0.85)
```

Concretely, IL exposes — pre-optimization, before register allocation — facts
the objdiff asm rows do not carry as such:
- **typed signedness on every operator** (drives signed vs unsigned PPC
  sequences; SS3 (b) above);
- **integer promotions** as explicit `CAST` nodes (`short`→`int` before
  arithmetic, `uint` re-cast after a `CB`);
- **ternary / boolean-materialization origin** (`CB(1,0)` → the
  `signed_ordered` pattern the lifter tags at 0.85);
- **switch-table shape** (`SWITCH_TABLE` + `CASE` case→label map, before c2
  decides jump-table vs if-chain);
- **virtual-call shape** (`VCALL_SETUP`/`VCALL_BIND` + vtable `DEREF` chain);
- **short-circuit `&&`/`||` structure** (`LOGICAL_AND`/`LOGICAL_OR` nodes).

---

## 4. Leak-safety — the channel is CANDIDATE-side only (this is decisive)

There is **no target IL and none can exist**: the target is an original `.obj`
with no accompanying source, so nothing produces its `_CL_*` files. Every IL
fact above describes **the model's own current candidate source** — which the
model already wrote and already has in the prompt as text. The only target-side
IL bridge is `ppc_il_lifter`'s constrained PPC→IL lift, which is a
**confidence-tagged heuristic, not a judge** (SS3 (c): `conf=0.75/0.85`).

This is the same asymmetry decomp-synth's `stack_slot_oracle.py` already has
("we can name our locals, never the target's"), now at the IL layer. Any readout
language implying a *target-vs-candidate IL diff* would be false.

Path hygiene: `.ex/.gl/.sy` embed `e:\lazer_build_gmc1\…` — the **intentional
original build paths** (WIBO_PATH_MAP maps local→`e:\`, so the compiler only
ever sees them; do not scrub, per project rules). Only `manifest.json` carries
absolute machine paths; the committed fixtures have these rewritten to
repo-relative / `~`.

### Proposed v0 field set (IF ever built — see GO/NO-GO first)

Candidate-side, BEFORE-state-derived, fixed key order, integers/enums only,
`il_avail=0` on capture failure (PCH/flag TUs), ~60 tokens (well under the 200
cap and the 8192 `--max-seq`):

```text
## Candidate IL facts (your current source; NO target IL exists)
il_avail: 1
il_functions: 13
il_ops: 60
signed_int_ops: 8
unsigned_int_ops: 2
float_ops: 0
cast_promotions: 3
vcall_sites: 1
switch_tables: 0
short_circuit_nodes: 0
il_hash: 3f9c2a…      # sound obj-equivalence key: equal il_hash ⟹ equal .obj
```

Binding constraints carried from doc-04: the mandated `## Residual diagnosis`
channel is not replaced (rule 4); endpoint is terminal byte-exact @ budget only;
n≥335 discordant-informative floor before any verdict; no move/pass-family
target (R2/F34/F35); training integration owner-gated.

---

## 5. GO / NO-GO

**Proposer-conditioning IL channel: NO-GO for v0.** Reasons, in order of weight:
1. **Candidate-side redundancy.** The fields describe the model's own source,
   already present verbatim in the prompt. Signedness, casts, vcalls, switch
   shape are recoverable from the source text the model wrote; re-encoding them
   from IL is largely redundant. The genuinely non-obvious items (implicit
   promotions, ternary materialization) are marginal.
2. **No target IL ⇒ no diff use.** The one decisive thing — how the *target's*
   compiler-state differs from the candidate's — is exactly what the asm-delta
   channel (doc-04 section A) already provides at the asm level and what IL
   *cannot* provide (SS4).
3. **NULL gravity.** R2/F34/F35 already show objdiff-derived channels carry no
   move-family MI; an IL channel pitched as "helps pick moves" is dead on
   arrival, and the licensed endpoint (whole-body proposal quality @ terminal)
   is where the asm-delta A/B in doc-04 SS4 should be spent first.

**IL as a search-side equivalence/dedup key: narrow GO (already seated).** The
sound `IL-identity ⟹ obj-identity` result (SS3a) makes the IL hash a safe
recompile-skip / candidate-dedup key. This is **not a training channel** and
needs no new prompt surface — it is precisely `capture_variant_il_hashes`, which
the SS1 de-drift fix restores. Recommended next steps, in order:
1. Land the capture de-drift fix (done in this probe) so
   `capture_variant_il_hashes` works again; optionally widen it from
   ranking/dedup to a recompile-skip gate (still objdiff-judged on every kept
   candidate).
2. Do **not** build an IL prompt channel now.
3. If IL conditioning is ever revisited, it is doc-04 rung-3 territory
   (asm-observables/IL-lifter graph), gated on BOTH a C1 asm-delta win AND a
   demonstrated text-saturation ceiling, run as the SS4 paired A/B (byte-exact @
   budget, n≥335). Owner-gated.

---

## 6. Recommended amendments to decomp-synth doc-04 SS2 (input for the owner — do NOT edit doc-04 here)

- Row **"source/IR/asm provenance links | MOSTLY UNOBTAINABLE"** → add a
  dc3-only qualifier: *candidate-side* partially obtainable via captured IL
  (`.db` line map + `.sy` param names + typed `.ex` ops), with the **same
  target-side asymmetry as `stack_slot_oracle`** (names our locals, never the
  target's). No target IL exists.
- **New bit not in the SS2 table:** *front-end↔back-end divergence
  localization* — IL-identity is a sound, zero-false-positive pre-c2
  obj-equivalence key, and a typed IL delta localizes causes (e.g. a 4-byte
  signedness marker) that asm shows only as diffuse instruction-selection
  changes. dc3-only, candidate-side, search-time (not a proposer channel).
- Reaffirm **"first divergent compiler pass | UNOBTAINABLE"**: IL does **not**
  bridge it. IL is c2's *input*, not per-pass introspection; capturing it gives
  zero pass-level visibility. Explicit non-claim.

## 7. Ruling-compliance checklist

- [x] No move/pass-family prediction endpoint proposed (R2/F34/F35/F-AXIS-NULL).
- [x] Only eventual A/B endpoint named is terminal **byte-exact @ budget**;
      n≥335 discordant floor cited.
- [x] No neutrality framing used.
- [x] Training integration explicitly deferred to owner.
- [x] Residual-diagnosis channel (rule 4) and 8192 `--max-seq` cited as binding
      on any proposed field set.
- [x] `capture_variant_il_hashes` (scorer.py:940) and the `ppc_shape_facts` hook
      (scorer.py:1725) characterized as **consumers**; no decomp-synth edits.
- [x] No target-IL comparison claimed anywhere (SS4).
