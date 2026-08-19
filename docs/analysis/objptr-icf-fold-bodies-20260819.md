# ObjPtr ICF fold bodies — lane record, and what verification refuted about it

**Lane:** `fix/objptr-icf-bodies`, landed 2026-08-19.
**Scope:** bodies that ICF-folded in retail and therefore score 0 % on every
name-paired ruler, so the metric cannot see them being fixed.

This page exists because three of the lane's most reusable findings lived only in
commit messages, and two of its arguments were wrong in ways that would mislead the
next reader. The *verdicts* all held; the *reasoning* did not.

---

## 1. The `0x8285AE50` fold group — what settles it

`orig/373307D9/ham_xbox_r.map` co-lists **104** names at the single address
`0x8285AE50`:

| count | symbol family |
|-------|---------------|
| 33 | `?Replace@?$ObjPtrList@…` |
| 33 | `?RefOwner@?$ObjPtrList@…` |
| **19** | `?Replace@?$ObjPtrVec@…` |
| 19 | `?RefOwner@?$ObjPtrVec@…` |

Reproduce with `grep -ic '0*8285ae50' orig/373307D9/ham_xbox_r.map` and the four
per-family greps. **The lane's comment said 33 `ObjPtrVec::Replace`; it is 19.**
33 + 33 + 19 + 19 = 104.

### The argument that does not work

The lane argued: *"a `bool`-returning `Replace` and a pointer-returning `RefOwner`
can only ICF-fold if both are the `MILO_FAIL` stub."*

**That is false as a general principle.** `return false;` and `return nullptr;` both
lower to `li r3,0; blr` on PowerPC. Return *type* is a front-end concept; ICF folds
COMDATs by machine code and relocations, and two functions with different C++ return
types fold happily whenever their codegen agrees. The premise proves nothing.

### The evidence that does work

Disassemble the body itself. At `0x8285AE50` (file offset `0x84F850`, image base
`0x82000000`):

```
mflr  r12                       ; prologue
stw   r12, -8(r1)
...
lis   r11, 0x8201
addi  r3,  r1, 0x50
addi  r4,  r11, -0x7EE8         ; r4 = 0x82008118
bl    <FormatString>
addi  r3,  r1, 0x50
bl    <...>
lis   r11, 0x82F6
mr    r4,  r3
addi  r3,  r11, 0x55D8          ; TheDebug
li    r5,  0
bl    <Debug::Fail>
li    r3,  0                    ; return 0
...
blr
```

`0x82008118` in `.rdata` is the literal string **`"should never be called"`**. Its
`.rdata` neighbour is **`"n != NULL && mNodes != NULL"`** — `ObjPtrList::Unlink`'s own
`MILO_ASSERT` at `ObjPtr_p.h:0x26B`, i.e. the strings from this very header, pooled
together. That settles what the body is outright, with no appeal to return types.

**Transferable technique:** to identify an unknown ICF fold target, resolve the
`lis`/`addi` pair feeding the assert helper's `r4` and read the `.rdata` string. The
string pool's *neighbours* usually tell you which source file the group came from.

`RefOwner`/`Replace` themselves landed on `main` as `cf73f5c37`, eleven minutes before
this lane's version of the same finding, in a better shape: the `MILO_FAIL` is inline in
`src/system/obj/Object.h` under `#ifndef HX_NATIVE`, and the walking bodies are
**preserved for `HX_NATIVE`** (the native port has not been proven unable to reach them
through an `ObjRefOwner*`). This lane took main's side on rebase and kept only its
`FindRef` half.

---

## 2. `ObjPtrList::remove` — negative result, downgraded

**Recorded as:** "not source-reachable; MSVC sinks `++it` past the compare."
**Restated as:** **four spellings tried, none moved it; cause unknown.**

The observation reproduces. Our `?remove@?$ObjPtrList@VRndLight@@…` is 96 bytes against
the target's 100 at `0x825C6868` (`merged_ObjPtrListRemove`, unit `system/obj/TypeProps`)
and differs in one instruction plus the register naming it induces:

```
target                          ours
mr   r10, r11    ; old = it     lwz  r10, 0xc(r11)   ; *it
lwz  r11, 0x14(r11) ; ++it      cmplw r10, r4
lwz  r9,  0xc(r10)  ; *old      beq  found
cmplw r9, r4                    lwz  r11, 0x14(r11)  ; ++it
```

Spellings tried, all producing the identical 96-byte body byte for byte:

1. `auto old = it++;` (what is in the tree)
2. `iterator old = it; ++it;`
3. `for (...; ++it) { if (*it == target) { erase(it); ... } }`
4. a fourth variant tried during verification — same 96 bytes

**But the explanation is refuted.** A minimal case built with the same compiler, the
same flags and `ObjPtrList`'s exact layout emits **the target's schedule** from
`It old = it++;`. So MSVC is *not* categorically sinking the increment; something in the
real TU decides, and what that something is has not been identified. "Not source
reachable" closes a door that is demonstrably still open — do not treat this as a floor
certificate.

---

## 3. What an independent instrument says the lane actually changed

`scripts/analysis/icf_foldcheck_pe.py` + `scripts/analysis/icf_bucket_census.py` were
written from scratch during verification: the target side is `ham_xbox_r.exe`'s PE
`.text` with body lengths taken from the binary's own `.pdata` RUNTIME_FUNCTION table.
No dtk, no `symbols.txt`, no objdiff, no `report.json` percentages — so it shares no
machinery with the lane's own scanner.

Run over both trees (`python3 scripts/analysis/icf_bucket_census.py <tree>`):

| | `main` @ `feaea3e3d` | this lane |
|---|---|---|
| rows TESTED | 373 | 373 |
| **real DIFFER** | **11** | **8** |
| no decodable target body | 16 | 16 |

The three that closed are exactly the three bodies the lane claims to have fixed, and
nothing else moved:

- `?Unlink@?$ObjPtrList@VEventTrigger@@…` (284 B, `system/ui/UIList`)
- `?erase@?$ObjPtrVec@VRndTex@@…` (276 B, `system/rndobj/Font`) — the `noinline` drop
- `?FindRef@?$ObjPtrVec@VRndTransformable@@…` (120 B, `system/world/LightPreset`)

Still DIFFER after the lane, and openly so: two of the three `ObjPtrVec::erase` fold
groups (`system/world/LightPreset` `HamMove` 276 B, `system/flow/FlowManager`
`merged_ObjPtrVecErase` 240 B — consistent with "12 of 19 erase bodies exact", not 19 of
19), `merged_ObjPtrListRemove` (§2), and five unrelated bodies in
`rndobj/Mesh`, `char/CharSignalApplier`, `char/CharEyes` and
`meta_ham/PlaylistSongProvider`.

### How little of the binary this measures

Of **48,344** `report.json` rows, **373 — 0.8 %** ever reach the byte comparison:

```
30,141  we-define-it        1,893  no-fold-peer
15,764  no-object             163  no-address
   373  TESTED                 10  addr-not-in-map
```

Any "this class is clean" verdict from this instrument is a statement about that 0.8 %.
Quote the denominator.

### Numbers this lane published that should not be re-cited

- **"~1,730 map-blind rows"** — never derived. The measured figures are **1,512**
  synthetic `merged_*`/`fn_*` labels inside **1,687** rows whose symbol is absent from
  `ham_xbox_r.map`.
- **"17 whole-binary failures"** — instrument-dependent, not a fact. Three passes over
  the same tree gave 9, then 17, then 14 (PE/`.pdata` instrument), and 11 on `main`
  today. Always name the instrument and the tree.
- **Defect ordering.** `merged_ObjPtrVecErase` looked clean because of scanner defect
  **(i)** — the map lookup returning `not-in-map` for every synthetic dtk label — not
  defect (ii). (ii), `target_body()` matching only `.fn "NAME"` while dtk emits synthetic
  labels bare, is downstream and only becomes reachable once the address resolves.
  (Across every split `.s` file the count of *quoted* synthetic labels is **0**; the bare
  count depends on how many files you sample.)

---

## 4. The reusable compiler rule

The lane's `StandardStream` fix rests on an MSVC vtable rule that generalises well beyond
this lane. It has its own page, with three runnable cases compiled by the project's own
`cl.exe`:

- [../decomp/patterns/msvc-vtable-overload-name-grouping.md](../decomp/patterns/msvc-vtable-overload-name-grouping.md)
- [../decomp/experiments/msvc-vtable-overload-grouping/](../decomp/experiments/msvc-vtable-overload-grouping/)

Short form: **MSVC slots all virtuals sharing a name as one group, at that name's first
declaration in the class.** The lane's "hoists to the front of the new-virtual block"
phrasing is only the special case where the override precedes every other new virtual.

---

## 5. Delta

Canonical ruler, `report.json` `match_percent_normalized`, this lane vs `main`
@ `feaea3e3d`: **exactly 3 rows changed, all upward, 0 regressions.**
Normalized-100 count 29,505 → 29,508 of 48,344.

| row | before | after |
|-----|--------|-------|
| `?GetChannel@StandardStream@@UBAPAVStreamReceiver@@H@Z` | 0.0 | 100.0 |
| `?ApplyLoop@MoggClip@@AAAX_NHH@Z` | 99.96296 | 100.0 |
| `?SetJump@StandardStream@@UAAXMMPBD@Z` | 99.9899 | 100.0 |

`measure_progress.sh` additionally reports 5 regressions / 158 improvements on the
**fuzzy** ruler; 5 of 5 and 157 of 158 of those are marked `~` (fuzzy-only, canonical
unchanged) and are the usual ICF/atexit-thunk re-fold churn.

The `noinline` A/B is the sharpest illustration of why this lane is metric-invisible:
dropping `__declspec(noinline)` from `ObjPtrVec::Set` moved **0 rows on either ruler, at
+0.00 %**, while the byte checker on those same two builds went **19/19 DIFFER →
12/19 MATCH**.
