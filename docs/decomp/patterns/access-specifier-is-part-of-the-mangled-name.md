# The access specifier is part of the mangled name — and no ruler charges it

**dc3-decomp (title 373307D9).** A member we declare `public` that the shipped
image declares `protected` is not a differently-annotated symbol. It is a
**different symbol**:

```
target (ham_xbox_r.map)              ours
??_GJsonObject@@MAAPAXI@Z            ??_GJsonObject@@UAAPAXI@Z          M = protected  vs  U = public
??_ERndVelocityBuffer@@EAAPAXI@Z     ??_ERndVelocityBuffer@@UAAPAXI@Z   E = private    vs  U = public
?Poll@GroupSeqInst@@MAAXXZ           ?Poll@GroupSeqInst@@UAAXXZ         M = protected  vs  U = public
?SendDoneImpl@StreamReceiver360@@MAA_NXZ   …@@UAA_NXZ                   M = protected  vs  U = public
```

MSVC encodes member access in the mangled name. The character immediately after
the final `@@` is the access/storage code:

| code | access |
|---|---|
| `A`–`H` | private |
| `I`–`P` | protected |
| `Q`–`X` | public |

## Why every instrument in this repo misses it

This class is invisible three separate ways, which is why it went unrecorded:

1. **objdiff pairs symbols by NAME.** The two spellings never pair at all, so
   the row is `unresolved-target` rather than a mismatch. It contributes no
   instruction diff and costs **zero** match percent under every
   `functionRelocDiffs` setting, `name_check` included.
2. **`run_symbol_sweep(kind="vtable_slots")` filters it out by design.** That
   sweep keeps a slot only when the two sides resolve to *different addresses*;
   equal address is treated as a proven ICF fold and dropped as benign. These
   rows have **identical bodies at identical addresses** — only the name
   differs — so the sweep discards them correctly by its own rule and wrongly
   for this purpose.
3. **It reads as cosmetic.** `docs/analysis/dispatch-data-rescan-20260818.md`
   filed the `JsonObject` / `RndVelocityBuffer` pair as *"`??_E` vs `??_G`
   deleting-destructor thunk naming. Cosmetic ICF naming."* and corrected
   itself the next day. The correction is worth reading: the grep that "found
   nothing" had searched for the **`U`** spelling *our own build emits*, which
   is genuinely absent from the map. The map spells them `M` and `E`.

It is not cosmetic. The access specifier is a fact about the original source we
got wrong; it changes what compiles against the class, and for a virtual it can
change vtable binding — a method whose access we mis-declare still binds, but a
signature we get wrong alongside it does not.

## Finding them

```sh
python3 scripts/analysis/access_specifier_scan.py --selftest   # validate the instrument first
python3 scripts/analysis/access_specifier_scan.py
```

Method: reduce every mangled symbol on both sides to an **access-blind key**
(the name with the access character blanked), then report keys where our set of
access characters and the target's are **disjoint**.

Disjoint, not merely different, is deliberate — a symbol legitimately appears
under more than one spelling across objects, and a key where the two sides share
*any* spelling is not evidence of anything. On the current tree that bucket is
empty (0 partial overlaps), but it is counted and reported rather than assumed.

The access character is the one after the **final** `@@`. Template names embed
`@@` inside themselves, so `rfind` is correct and `find` silently reclassifies
every templated member — that is mutation **M1** in the test suite.

## What it cannot see

- **Only 990 of 2,223 target objects currently have a built counterpart (44.5%).**
  A wrong access specifier in a TU that does not build yet is invisible here.
  The coverage block prints this on every run. **This is not a whole-binary
  census and must not be quoted as one.**
- A member the image never emitted standalone (inlined away, or an unreferenced
  template instantiation) has no target spelling to disagree with. ~22k of our
  keys are in that bucket — overwhelmingly STL and inline (`?Str@Symbol@@QBAPBDXZ`,
  `?end@?$vector@…`). They are counted as `absent-from-target`, not skipped.
- `static` vs non-static and near/far share the same character, so a
  disagreement is reported as an access disagreement even when the real defect
  is storage class. The rendered row prints both raw characters so the reader
  can tell which it is.

## Current findings — 6 rows, 4 distinct declarations, all OPEN

| declaration | ours | target | status |
|---|---|---|---|
| `JsonObject::~JsonObject` (`??_E`/`??_G` thunks) | public | **protected** | documented 2026-08-19, unfixed |
| `RndVelocityBuffer::~RndVelocityBuffer` (`??_E`/`??_G`) | public | **private** | documented 2026-08-19, unfixed |
| `GroupSeqInst::Poll` (`src/system/synth/Sequence_p.h`) | public | **protected** | **found by this scanner, previously unrecorded** |
| `StreamReceiver360::SendDoneImpl` (`src/system/synth_xbox/StreamReceiver360.h`) | public | **protected** | **found by this scanner, previously unrecorded** |

`GroupSeqInst::Poll` appears in `docs/native/DECOMP_GAPS.md` only as
implementation status ("Done, 99.4%"); `StreamReceiver360`'s other methods
appear in several pattern docs for their match percentages. Neither the access
divergence nor its class is described anywhere before this document, and
`git log --grep=SendDoneImpl` is empty.

⚠ **Fixing one is not a one-line header edit.** The in-source comments on the
first two record why: moving the declaration under `protected:`/`private:` has
to happen *together with* corrections to `config/373307D9/symbols.txt` and
`scripts/target_symbol_map.json`, and editing `symbols.txt` re-triggers the
`dtk xex split` — which rewrites the target side of every diff in the project.
Treat each as its own small lane with a before/after report, not a drive-by.

## The recognizer, for classes this scanner cannot reach

When the TU does not build, the same tell is readable by hand: grep the linker
map for the method name, and compare the character after the final `@@` against
what our header's access section implies. A method under `public:` whose map
spelling is `M…` or `E…` is this bug. The inverse also occurs and is equally
real — the map is the truth on both sides.

## Guards

- `scripts/analysis/tests/test_access_specifier_scan.py` — 11 negative controls.
  Each was checked by sabotaging the scanner and confirming the test goes red;
  three mutations are recorded in the suite (`rfind`→`find`, disjoint-never-
  reported, uncounted-`continue`), and the control passes before *and* after
  each, so the harness itself cannot silently pass.
- `--selftest` exercises the comparator on synthetic input **and** requires all
  four documented live instances to still be found, so a future change that
  quietly stops detecting the class fails loudly instead of printing a smaller,
  cleaner-looking number. When the corpus is absent it prints `SKIP` and says
  plainly that this is **not** a pass.
- Registered in `scripts/analysis/determinism_check.py`.
- Exit codes: `0` clean · `5` missing input (never a clean verdict) · `1` with
  `--fail-on N` · plus `coverage.py`'s `3`/`4`/`6`.

## See Also

- [relocation-names-are-unmetered.md](relocation-names-are-unmetered.md) — the
  sibling class: a wrong *callee* name that the normalized ruler also scores at zero cost.
- [msvc-vtable-overload-name-grouping.md](msvc-vtable-overload-name-grouping.md) —
  a different mechanism with the same symptom (a vtable shift from an
  innocuous-looking declaration).
- `docs/analysis/dispatch-data-rescan-20260818.md` — where this pair was first
  filed as cosmetic, and the correction that followed.
- `docs/tools/SCANNER_TRUTHFULNESS.md` — the contract this scanner is built to.
