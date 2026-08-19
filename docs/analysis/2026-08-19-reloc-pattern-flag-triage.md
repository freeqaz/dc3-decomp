# Triaging the three newly-populated `has_*` pattern flags — dc3-decomp, 2026-08-19

**Repo: dc3-decomp (title 373307D9).** rb3 and rb3-xenon share symbol names and address
ranges with this tree; every symbol, address and number below is dc3's, measured on
`fix/pattern-flag-triage-20260819` off `07fdaeea7` in a fully-rebuilt worktree.

Rebased onto main `8455b09be` and rebuilt after the fact; both `DingoJob::Start`
(100.000 normalized / 100.000 name_check) and `DataArray::Execute` (95.833) hold there.
The whole-build A/B below is measured at the branch point, where the only difference
between the two trees is this lane.

Task #121. Population handed over: `has_linker_merged` 1,310 / `has_prologue_mismatch`
221 / `has_makestring_mismatch` 63, first populated by `scripts/backfill_reloc_patterns.py`
(merge `07fdaeea7`) after the discovery that these detectors were reloc-starved rather
than dead.

`has_scope_counter_mismatch` (81 rows) is excluded — a concurrent lane owns it.

---

## Headline

| bucket | rows (as recorded) | rows (reproduced) | measured artifact rate | real subset |
|---|---:|---:|---:|---:|
| `has_linker_merged` | 1,310 | **1,052** | **≥ 65.0 %** proven-benign, and only **1.9 %** of the bucket names an ICF fold at all | 18 named rows, most already adjudicated by two earlier lanes |
| `has_prologue_mismatch` | 221 | **218** | **100 %** as an *independent* finding — 0 of 218 carry the prologue as their only pattern | 0 rows where the prologue is the limiting defect; it is a liveness co-signal, as the docs already say |
| `has_makestring_mismatch` | 63 | **63** | **77.8 %** (38 forgiven by the graded ruler + 11 same-fold-class renames) | **14 rows, each a concrete wrong-callee bug (5 fixed)** |

**Three findings matter more than the fixes:**

1. **`has_prologue_mismatch` and `has_makestring_mismatch` are strict subsets of
   `has_linker_merged`.** Not overlapping — *contained*. The union of the three flags is
   1,310 functions, not 1,594.
2. **`has_linker_merged` is not an ICF detector.** On 20.7 % of its rows the "merged
   function" it names is a `__savegprlr_N` / `__restgprlr_N` register-save helper, and on
   6.3 % it is a `MakeString<>` instantiation. Only 1.9 % name a symbol that is ICF by
   construction.
3. **The recorded population does not reproduce.** Two settled build trees at
   essentially the same commit give 1,052 and 1,069 LINKER_MERGED rows. Neither is 1,310.

Five fixes landed, four reverted with their reasoning recorded. Whole-build effect
predicted **+1 function / +308 B** on the canonical ruler and observed **+1 / +308**, with
exactly two rows moving there, five on the graded ruler, and zero regressions anywhere.

---

## The discriminator

`backfill_reloc_patterns.py` runs the detectors under `functionRelocDiffs=all`, which
charges **every** relocation-name difference — including the ~2,992 `/OPT:ICF` folds this
project has already adjudicated and registered in `build/373307D9/icf_aliases.map`
(8,719 entries). Under that config a flag says only "some `bl` names a different symbol on
the two sides".

The project's graded ruler is `functionRelocDiffs=name_check` — it is what
`build/373307D9/report.json` is generated with (`provenance.diff_config`), it consults the
alias map, and it applies principled exemptions (placeholder `fn_8xxxxxxx` / `lbl_*`
target names, MSVC counter-suffixed literals verified by content, section/pool anchors
resolved through the addend, COFF weak-external aliases).

So: **re-run the same detectors under `name_check` and keep only the flags that survive.**

    class A   fires under `all`, gone under `name_check`
              -> a fold the project has already proved. Artifact.
    class B   survives, and match_percent_normalized == 100
              -> the ONLY thing wrong with the function is a name.
                 Either an unregistered fold or a wrong callee. The prize.
    class C   survives, and match_percent_normalized < 100
              -> the function has structural mismatches too; the flag is a
                 co-symptom, not the limiting factor.

Two new scripts do this: `scripts/analysis/reloc_pattern_census.py` (the same pass, with
the detector payload kept instead of collapsed to a boolean) and
`scripts/analysis/reloc_flag_triage.py` (the A/B/C partition). Neither writes `decomp.db`.

> ⚠ `objdiff --batch` emits `"match_percent_normalized": null` on **every** row. The first
> version of the triage read it from there, got `None -> 0.0` for all 1,310 functions, and
> reported class B as identically empty — which reads exactly like "there is no prize
> slice", the failure mode this lane exists to avoid. The normalized figure is only real
> in `report.json`.

### The partition

```
=== LINKER_MERGED: 1052 rows carrying the flag under functionRelocDiffs=all
    684   65.0%  A forgiven fold (gone under name_check)
     71    6.7%  B charged, normalized==100 (PRIZE)
    297   28.2%  C charged, normalized<100 (co-symptom)

=== PROLOGUE_MISMATCH: 218 rows
    218  100.0%  C charged, normalized<100 (co-symptom)

=== MAKESTRING_TEMPLATE_MISMATCH: 63 rows
     38   60.3%  A forgiven fold (gone under name_check)
      4    6.3%  B charged, normalized==100 (PRIZE)
     21   33.3%  C charged, normalized<100 (co-symptom)
```

---

## Finding 1 — the three flags are one flag

```sql
LM only        1032
LM + PRO        221      PRO not LM   0
LM + MS          63      MS  not LM   0
union          1310
```

`detect_linker_merged`'s general branch fires on any `bl`/`b` whose two sides name
different *plausible function* symbols. `bl __savegprlr_28` vs `bl __savegprlr_29` matches
that test. So does `bl ??$MakeString@KH@@…` vs `bl ??$MakeString@HH@@…`. Both are already
classified, more precisely, by their own detectors — and `detect_linker_merged`'s own doc
comment calls them "likely ICF merging of unrelated functions with identical machine
code", which is false for both.

Counting what the `merged_functions` payload actually names, over the 1,052 rows:

| what the named symbol is | rows | % |
|---|---:|---:|
| cross-function (a genuinely different callee name) | 401 | 38.1 % |
| template instantiation (`ICF:?$X (template merge)`) | 394 | 37.5 % |
| **register save/restore helper** — i.e. PROLOGUE_MISMATCH re-reported | **218** | **20.7 %** |
| **`MakeString<>` instantiation** — i.e. MAKESTRING re-reported | **66** | **6.3 %** |
| dtk synthetic fold name (`merged_*`, `OnlyReturns`) — ICF by construction | 20 | 1.9 % |
| anonymous-namespace hash (`?A0x…`) — build-env artifact | 11 | 1.0 % |

**Reading `has_linker_merged = 1` as "this function is affected by `/OPT:ICF`" is wrong
about 98 % of the time.** Filtering work on it selects, in descending order: unrelated
callee names, template instantiation spellings, prologue register pressure, and log
formatting.

---

## Finding 2 — the recorded population is not reproducible

| measurement | LINKER_MERGED | PROLOGUE | MAKESTRING |
|---|---:|---:|---:|
| `decomp.db`, written by the backfill at 09:11 | 1,310 | 221 | 63 |
| this worktree, full clean `ninja` at `07fdaeea7` | **1,052** | **218** | 63 |
| main repo's build tree, read-only, ~1 h later | **1,069** | 217 | 63 |

The two settled trees agree on 1,051 of their ~1,060 rows (18 main-only, 1 worktree-only —
1.8 % instability, consistent with main carrying 821 objects that
`verify_objs_patched.py --verify-manifest` reports as produced outside the full build
graph, so the post-compile symbol patchers never ran on them). That instability is real
but it is an order of magnitude too small to explain 1,310.

What is left is the clock. The flags carry `updated_at` between **09:11:03 and 09:11:36**;
`git log` puts main commits at 09:12, 09:16, 09:17 and 09:18, and at least two other
worktrees were mid-`ninja` in the same minutes. **The backfill measured a build tree that
was being rewritten under it.** MAKESTRING — whose instantiation names do not depend on
the patchers — is the one bucket that reproduces exactly.

**Consequence:** `backfill_reloc_patterns.py` should refuse to run against a tree that
fails `verify_objs_patched.py --verify-manifest`, the same way `run_objdiff` was proposed
to warn in the toolchain audit's follow-up item 8. Until then, any number derived from
these columns is a number about a moment, not about the build.

---

## Finding 3 — `PROLOGUE_MISMATCH` is a co-signal, and never appears alone

All 218 rows are class C: **not one of them is at `match_percent_normalized == 100`.** The
co-pattern distribution under `name_check`:

| co-occurring pattern | rows | % |
|---|---:|---:|
| ADDRESS_RELOCATION_NOISE | 218 | 100 % |
| LINKER_MERGED (the helper `bl`, cross-talk) | 218 | 100 % |
| REGISTER_SWAP | 203 | 93.1 % |
| OFFSET_SWAP | 98 | 45.0 % |
| CONTROL_FLOW | 88 | 40.4 % |
| COMMUTATIVE_OP_ORDER | 32 | 14.7 % |
| **prologue is the only pattern on the row** | **0** | **0 %** |

Direction of the register-count delta (`base_first_reg − target_first_reg`; a *lower*
first register means *more* callee-saved registers spilled):

| delta | rows | reading |
|---:|---:|---|
| −4 … −1 | 97 | we spill MORE than the target |
| +1 … +11 | 121 | we spill FEWER than the target |

and the frame size differs on 144 of 218 rows (81 ours smaller, 63 ours larger).

Match distribution: 5 rows ≥ 99 %, 40 in 95–99, 53 in 90–95, 68 in 80–90, **52 below
80 %**. More than half the bucket is deeply unmatched code where the prologue delta is
downstream of a body that does not agree.

**This is exactly what `docs/decomp/patterns/fixable-liveness.md` already says** —
"`PROLOGUE_MISMATCH` is the fingerprint of a value held across a call… a *liveness* tell,
**not** floor evidence" — and the bucket now supplies the evidence for that claim rather
than contradicting it. What it is *not* is a worklist: there is no row here whose prologue
you can fix in isolation, because there is no row here whose prologue is the only thing
wrong.

**Recommended use:** as a *filter* on the existing REGISTER_SWAP liveness lane
(`has_prologue_mismatch = 1 AND has_register_swap = 1`, 203 rows), to prefer functions
where the live *set* differs over ones where only the coloring does. Not as a lane of its
own. Do not open one.

---

## Finding 4 — `has_makestring_mismatch` is the one bucket with real bugs in it

Every one of the 25 rows that survives `name_check` is `sub_type = type`. **Zero are
`FileLength`** — objdiff's `normalize_mangled_array_sizes()`
([MAKESTRING_ICF_EQUIVALENCE.md](../plans/MAKESTRING_ICF_EQUIVALENCE.md)) already
neutralises the `__FILE__`-length class, so it cannot reach this bucket. The 38 class-A
rows are folds the alias map already covers.

`MakeString<T…>` is not opaque: its whole body is `FormatString fs(c); fs << t…; return
fs.Str();`, so an instantiation is decided entirely by *which `FormatString::operator<<`
overload each argument binds to*. And **`orig/373307D9/ham_xbox_r.map` publishes the fold
classes of those overloads directly** — it is the linker's own statement, not an
inference:

| address | folded overloads |
|---|---|
| `0x827ca420` | `int` — **alone** |
| `0x827ca618` | `const char *` |
| `0x827ca848` | `unsigned int`, `long`, `unsigned long`, `long long`, `unsigned long long`, `void *` |
| `0x827ca928` | `float`, `double` |
| `0x827caa18` | `const String &` |
| `0x827caaf8` | `const FixedString &` |
| `0x827cabd8` | `Symbol` |
| `0x827cadf8` | `const DataNode &` |

The surprise, and the reason this had to be read off the map rather than guessed: **`int`
does *not* fold with the rest of the integer family.** `operator<<(int)` is the one
overload whose "doesn't start with kInt" diagnostic is written out longhand
(`FormatString str(...); str << mFmt << mFmtBuf;`) instead of via `MILO_NOTIFY` — our own
source already carries a `// for whatever reason, this has the FormatString expanded out`
comment on it — so it is 504 B where the folded family is smaller, and it is its own fold
class of one.

So the decision procedure is: map each template argument through the usual conversions
(`enum`/`char`/`unsigned char`/`short` → `int`; `char[N]` and `char*` → `const char*`;
`StackString<N>` and `FilePath` → `const String&`), then compare the resulting **fold
class** lists.

* Same fold-class list → the two instantiations are the same machine code under a
  different name. Artifact.
* Different fold-class list, or different arity → **the two builds call different
  functions**. Real.

| class | rows | examples |
|---|---:|---|
| same fold classes → artifact | 11 | `enum` vs `enum` (both `int`); `enum` vs `int`; `char` vs `unsigned char`; `char[N]` vs `const char*`; `char*` vs `const char*`; `StackString<0x800>` vs `StackString<0x80>` |
| different fold classes or arity → **real** | 14 | `void*` vs `int`; `unsigned long` vs `int`; `unsigned int` vs `enum`; `Symbol` vs `const char*`; `String` vs `const char*`; `FilePath` vs `const char*`; three arity mismatches |

Bucket artifact rate: **(38 + 11) / 63 = 77.8 %**. Real: **14 rows**.

The four `MakeString<CamShotFrame::BlendEaseMode>` rows are worth naming explicitly: that
is the *survivor* of the fold class containing every `MakeString<SomeEnum>` in the build,
because every enum promotes to `int`. `SaveLoadManager::Poll`, `DingoServer::OnMsg` and
`FlowSetProperty::Execute` are all correct; they just lost the naming lottery.

### These are wrong-callee bugs that `match_percent_normalized` cannot see

`UIListSlot::Draw` and `CacheMgrXbox::PollMount` are the clean demonstration. Fixing each
left the calling function's instruction stream **byte-identical** — 192 and 223
instructions, the same mismatches before and after — because the difference is entirely in
the relocation target of one `bl`. The canonical ruler forgives relocation names (they are
`arg_diff_score`, which `match_percent_normalized` subtracts out), so **neither row moves
the headline at all.**

They are still real. `MILO_FAIL("%i isn't enough elements (need %i)", …)` was reaching
`FormatString::operator<<(int)` at `0x827ca420` where retail reaches the folded family at
`0x827ca848` — a *different function at a different address*, formatting through a
different code path. This is exactly the channel the toolchain audit found unicorn blind
to as well ("called a different function with identical args → EQUIVALENT"). Two
independent oracles cannot see this class; the MakeString detector can, and it is the only
thing in the build that names it.

### The trap in this bucket

**The instantiation names the fix, not the site, and not the mechanism.** Three of eight
attempts regressed and were reverted:

| function | attempt | result |
|---|---|---|
| `VoiceInputPanel::ActivateVoiceContext` | `.Str()` on the three `Symbol` format arguments | 86.6 % → **83.8 %** normalized, reverted |
| `Locale::Init` | `.Str()` on `SystemLanguage()` | 92.1 % → **91.7 %** normalized, reverted |
| `GetMotdJob::GetMotdData` (first attempt) | dropped the `int i =` temp on `challenge_interval` | 99.99 % → **99.57 %** name_check, reverted |
| `EnvelopeGenerator::DoProcess` | `(int)numChannels` at the `MILO_PRINT_ONCE` | charge cleared but 90.8 % → **88.7 %** name_check — the cast needs a stack temp where the unsigned parameter could be referenced in place. Reverted |

In `Locale::Init` the target does not call `SystemLanguage()` at that point at all — it
reuses a value already in `r14`. The `char*`-vs-`Symbol` tell was true and the `.Str()`
inference was still wrong.

**The payload carries the discriminator and it should be used.** Every
`MakeStringMismatchInfo` records the **instruction index** of the charged site.
`GetMotdData` is the worked example: the obvious candidate is the
`challenge_interval` log near the top of the function, and editing it cost 0.42 pp. The
recorded index is **174**, which lands on the `num_toasts` log much further down;
`int numToasts` → `unsigned int numToasts` took the row to **100.0 % with an empty pattern
list**, and incidentally fixed the signed/unsigned comparison in the loop on the very next
line.

So: **read the index, then edit.** `run_diff_inspect mode=asm_listing` or the unit's
`build/373307D9/asm/**.s` will map it to a source line.

Second rule, from `EnvelopeGenerator::DoProcess`: **prefer changing a variable's declared
type over inserting a cast at the call site.** `MakeString<T>` takes `const T&`, so a cast
materialises a stack temporary that binding to an existing variable does not — the name
charge clears and the function gets worse.

---

## The LINKER_MERGED prize slice, and why it is thin

71 rows are class B. **53 are `fn_8xxxxxxx` EH funclets** at 40–44 B — objdiff pairs
funclets by byte signature, so the paired funclet routinely belongs to a different parent
and the differing `bl` is a pairing artifact, not a call
([objdiff LEARNINGS: Pattern 6](../tools/objdiff/LEARNINGS.md#pattern-6-eh-funclet-score-wobble)).

That leaves **18 named functions**, and the ground is already worked:

* 5 were adjudicated **UNDECIDABLE** by the 2026-08-17 COMDAT-fold lane —
  `HamCamTransform::Load`, `SampleInst360::SampleInst360`, `NgPostProc::CheckHueConverge`,
  `FxSend::Copy`, `MetagameRank`'s `vector<Unlockable*>` copy ctor. Four of those name a
  dtk synthetic target (`OnlyReturns`, `merged_SetObjConcrete`) that resolves to no symbol.
* 2 are anonymous-namespace hashes (`?Time2IirA@?A0x8ac5fa56@Synapse@DSP@@`) —
  `scripts/obj_anon_ns_patcher.py`'s domain, not a source bug.
* 4 are the MakeString class-B rows already counted above.

`has_linker_merged` is, in short, a **re-derivation of a population two lanes have already
mined** (`docs/analysis/2026-08-17-comdat-fold-adjudication.md`,
`docs/analysis/2026-08-19-refuted-fold-memberships.md`), plus 284 rows of detector
cross-talk, plus ~250 rows that only existed in a mid-rebuild tree. **Do not open a lane
on it.**

---

## Fixes landed

Five of the 14 real MakeString rows.

| function | before | after | ruler |
|---|---:|---:|---|
| `DingoJob::Start` | 94.6 % | **100.0 %, 77/77 equal** | normalized, zero-mismatch |
| `DataArray::Execute` | 94.4 % | **95.8 %** (276 → 272 instructions) | normalized |
| `GetMotdJob::GetMotdData` | 99.987 % | **100.0 %, no patterns left** | name_check |
| `UIListSlot::Draw` | 99.963 % | 99.989 %; caller codegen unchanged, the `bl` now reaches the right overload | name_check |
| `CacheMgrXbox::PollMount` | 95.841 % | 95.864 %; caller codegen unchanged, same | name_check |

**`DingoJob::Start`** is the substantive one and the only one that is a behavioural bug.
The retail instantiation is `MakeString<const char*, const char*, const char*, const char*>`
— four arguments for the four `%s` in `"/%s/%s/%s/%s"`. Ours was
`MakeString<const char*, String, const char*>`, three. `DingoJob.s` settles the rest: MSVC
evaluates right to left and the target spills each argument in turn — `GetURL()` →
`0x50(r1)`, `lwz 0x44(TheServer)` (`unk40`'s `mStr`; `String` is `TextStream` at +0,
`FixedString::mStr` at +4) → `0x54(r1)`, and a vtable+0x80 call on `TheServer` →
`0x58(r1)`. Counting `DingoServer`'s virtuals from the header's own `Poll() // 0x70`
anchor puts `GetPlatform` at 0x80 and `GetHostName` at 0x84; writing it as `GetHostName`
first made objdiff charge exactly that one `lwz`, which is the confirmation. There is also
no `TheServer.Poll()` in the target's `Start()` and no `url` local.

So the shipped URL was missing its platform segment and passing a `String` object where a
`char*` belongs. That is a live defect in the native port, not only a match defect.

**`DataArray::Execute`** routed its failure message through a `const char *msg` temp and
then formatted it a second time with `MILO_FAIL_DTA("%s", msg)`, which is where the
extra `MakeString<const char*>` came from. Retail passes `str`/`str2`/`mFile`/`mLine`
straight into `MILO_FAIL_DTA` in each branch.

**`UIListSlot::Draw`** had an `int numSlotElements = mElements.size()` temp between
`size()` and the format argument; RB3's copy of the same function spells it without the
temp, and the retail instantiation (`MakeString<unsigned long, int>`) agrees.
**`CacheMgrXbox::PollMount`** passed a bare `0x48F` next to two `DWORD`s.

### Whole-build verification

Predicted before rebuilding: `DingoJob::Start` crosses 100 (+1 function, +308 B), and the
other four do not change `match_percent_normalized` at all, because a relocation-name
difference is `arg_diff_score` and the normalized figure subtracts that out.

```
norm==100:            29,491 -> 29,492        (+1)
bytes at norm==100:  5,120,436 -> 5,120,744   (+308)
match_percent_normalized: 2 rows moved, both up
  +4.740   308  ?Start@DingoJob@@UAAXXZ
  +1.193  1076  ?Execute@DataArray@@QAA?AVDataNode@@_N@Z
```

Predicted and observed agree exactly. On the graded `name_check` ruler exactly five rows
move — the five functions touched — and nothing else in the build moves at all:

```
fuzzy_match_percent (name_check): 5 rows moved, all up
  +5.519   308  ?Start@DingoJob@@UAAXXZ
  +1.361  1076  ?Execute@DataArray@@QAA?AVDataNode@@_N@Z
  +0.026   768  ?Draw@UIListSlot@@…
  +0.023   884  ?PollMount@CacheMgrXbox@@AAAXXZ
  +0.013  1568  ?GetMotdData@GetMotdJob@@…
```

---

## Residual worklist

Small and specific, so it does not rot into a fiction:

1. **The 9 unfixed real MakeString rows.** Each already carries its answer in the
   instantiation name. In descending order of expected value:
   `ArcDetector::UpdateOverlay`
   (`MakeString<float>` vs `<float,float>`: an argument we invented),
   `RndText::OnComputeCharWidths` (`FilePath` vs `const char*`),
   `XboxEnumeration::Poll`, `MoveDir::UpdateOverlay` and `GetMotdJob::GetMotdData`
   (`void*` vs `int`), `EnvelopeGenerator::DoProcess` (`unsigned int` vs `int`),
   `SuperEasyRemixer::SaveSuperEasyMoveParents`, `FlowTrigger::ActivateWithParams`,
   `VoiceInputPanel::ActivateVoiceContext` and `Locale::Init`.
   **Attribute the call site first** — see the two reverts above.

   Separately, `Debug::Fail`'s `StackString<0x800>` vs `StackString<0x80>` is a *name*
   artifact (both bind to `operator<<(const String&)`), but the size it names is real
   information about the target's local, and `Debug::Fail` is the one function carrying
   both a MakeString and a prologue flag. Its frame is 8,672 B in the target and 8,528 B
   in ours; a straight `StackString<128>` → `StackString<2048>` does not reconcile that
   (it would overshoot by 1,776 B), so the local being logged is probably not `msgStr`.
2. **Make `backfill_reloc_patterns.py` refuse a drifted tree.** One
   `verify_objs_patched.py --verify-manifest` call before the scan. As it stands the
   columns record whatever the tree happened to be at that minute.
3. **Split `detect_linker_merged`** upstream, or at minimum stop it firing when
   `detect_prologue_mismatch` or `detect_makestring_template_mismatch` already claimed the
   same instruction. 284 of its 1,052 rows are its own siblings' findings.
4. **Do not open a LINKER_MERGED lane and do not open a PROLOGUE lane.** Use
   `has_prologue_mismatch = 1 AND has_register_swap = 1` as a filter inside the existing
   liveness lane.

## Note for `../rb3` and `../rb3-xenon`

`bin/objdiff-cli` is a symlink shared with both trees, so both carry the
`MAKE_STRING_TEMPLATE_MISMATCH` / `MAKESTRING_TEMPLATE_MISMATCH` spelling split described
in `sync_objdiff.py` (serde's `SCREAMING_SNAKE_CASE` splits the internal capital in
"String"; `PatternType::to_str` does not). DC3 now canonicalises both spellings. Neither
sibling does, so `has_makestring_mismatch` is still structurally unsettable there — and,
by the finding above, that is the one bucket of the three with real bugs in it.

## Reproduce

```sh
sqlite3 decomp.db "SELECT DISTINCT symbol FROM functions WHERE excluded=0 AND \
  (has_linker_merged=1 OR has_prologue_mismatch=1 OR has_makestring_mismatch=1)" > /tmp/syms.txt

python3 scripts/analysis/reloc_pattern_census.py --stdin --project-dir . \
    --reloc all        --out /tmp/all.jsonl       < /tmp/syms.txt
python3 scripts/analysis/reloc_pattern_census.py --stdin --project-dir . \
    --reloc name_check --out /tmp/namecheck.jsonl < /tmp/syms.txt

for p in LINKER_MERGED PROLOGUE_MISMATCH MAKESTRING_TEMPLATE_MISMATCH; do
  python3 scripts/analysis/reloc_flag_triage.py --all-jsonl /tmp/all.jsonl \
      --namecheck-jsonl /tmp/namecheck.jsonl --report build/373307D9/report.json \
      --pattern $p --json-out /tmp/$p.json
done
```

Run it on a tree that passes `python3 scripts/verify_objs_patched.py --verify-manifest`,
or finding 2 will happen to you as well.
