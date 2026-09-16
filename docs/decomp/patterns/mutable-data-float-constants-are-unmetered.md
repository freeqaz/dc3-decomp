# A mutable `.data` float constant is unmetered — and the detector's blind spots were addressing modes

**Metric-invisible bug class 5.** 54 known instances. Sibling of
[dropped-static-initializer.md](dropped-static-initializer.md) (class 10) and
[relocation-names-are-unmetered.md](relocation-names-are-unmetered.md).

## The mechanism

MSVC puts an *immutable* float literal in `.rdata` as a `__real@<hex>` COMDAT.
A float that ends up in **`.data`** was written there by an initialiser into
*writable* storage — i.e. the source said `static float sFoo = <v>;`. dtk names
the ones `symbols.txt` does not name `lbl_8xxxxxxx`.

Both the **existence** and the **value** of such a constant are things our
decomp guessed, and no ruler this project owns can check either:

* the `lfs` instruction is byte-identical whichever value sits behind it, so the
  code-shape ruler sees nothing — an immediate/displacement diff survives
  normalization, but a *data* value is not an immediate;
* `lbl_*` is a placeholder name, and the graded (`name_check`) ruler **exempts
  placeholder names before the relocation-name detector runs**, so even the
  wrong-callee ruler is silent.

So a wrong value here costs **zero** match% and changes behaviour a lot.
`HamMaster::CheckLevels` read 96 where the image reads 40; `EaseElasticIn`
scaled by period instead of amplitude; `ArcDetector::UpdateOverlay` had no
colour fade at all. All three were found by hand.

`scripts/analysis/mutable_float_audit.py` is the systematic version.

## The real lesson: the detector's blind spots were ADDRESSING MODES, not values

Three times now, this class has been declared covered while the instrument was
looking at one addressing mode. Each fix reopened territory.

### 1. Our-side walker assumed the folded form (fixed `1b75dc677`)

It walked REFLO relocations *as if each were a load*. The image folds the low
half into the load (`lfs f0, lbl@l(r11)`), so there one relocation **is** one
load site. Our MSVC frequently **materialises** the address instead — `lis`/
`addi` into a GPR, then `lfs fN, <disp>(rGPR)` — and then the REFLO lands on the
`addi`, which is not a load, **and** the sibling statics at +4/+8/+12 are read by
loads carrying **no relocation at all**.

### 2. The TARGET parser had the identical defect (fixed 2026-09-16)

`data_float_labels.collect()` recognised only `lfs fN, lbl@l(rX)`. But the image
materialises bases too:

```
lis  r11, lbl_82F16D28@ha
addi r25, r11, lbl_82F16D28@l     <-- the reference; NOT a load
...
addi r26, r25, 0x4                <-- a base DERIVED from a base
lfs  f31, -0x4(r26)               <-- the actual read, NO relocation at all
```

Measured whole-binary: **29** `addi`-materialised references reach a `.data`
float label, covering **17** distinct labels, **9** of which (5 non-XDK) no
folded load reaches at all. Porting the instruction walk to the target side took
resolved float read sites from **92 → 268** (165 folded + **103 materialised**),
functions from **36 → 100**, distinct labels reached from **63 → 116**, and
non-XDK `.data` float labels that nothing appears to read from **6 → 1**.

### 3. Neither side could read a float ARRAY (fixed 2026-09-16)

Our side skipped any symbol whose width was not exactly 4 or 8; the target side
read only `floats[0]`. So a `Vector3`, a `Color`, a `Vector2[5]` or a 3-float
parameter block was 1/N visible on one side and 0/N on the other.
`?sOffset@@3VVector3@@A`, `sValidHandFloats`, `?sDOFOverride@RndPostProc@@…`,
`gBeatLineData` and MoveDir's five colours are all in this class. Our side's
named `.data` float statics went **114 → 152**.

⚠ **dtk ends a blob at the next NAMED symbol**, so a run of unnamed siblings
folds into one blob: `?tpi@?1??dradfg@@9@9` is 0x18 in the image and four bytes
in ours, and the five extra floats belong to four *other* statics. Compare the
**overlapping prefix only**, and only at offsets the target calls `.float` —
`gRemoteGain` is `.float 3` followed by two `.4byte`, and decoding those as
floats manufactures a disagreement out of a pointer.

## Two walker defects worth knowing on their own

**A call is a branch with the LK bit set, not "opcode 18".** Our walker cleared
every volatile GPR on each opcode-18 word. In `?ParseMarkup@RndText@@…` the
anchor for `gSuperscriptScale` is materialised into r10 at +0xac, and
`0x48000010` at +0xc0 — op 18, **LK=0**, an unconditional forward branch —
cleared r10 four bytes before `lfs f0, 0x4(r10)` (`gGuitarScale`) and again
before `lfs f0, 0x8(r10)` (`gGuitarZOffset`). Our source defines all three
statics at exactly the image's values (0.7 / 0.7 / 0.2) and the tool reported us
reading one of them — a DISAGREE manufactured against correct source.

On the target side the same rule must not be spelled `mnem.startswith("bl")`.
dtk's `b*` vocabulary over the whole tree is `bl` 233,601 / `beq` 99,977 /
`b` 86,495 / `bne` 75,319 / `blr` 45,132 / `blt` 24,890 / `bctrl` 15,310 /
`ble` 14,105 / … — a prefix test treats `ble`, `blt`, `blr`, `blelr` and `bltlr`
as calls. The linking mnemonics are exactly `bl`, `bla`, `bctrl`, `blrl`,
`bclrl`, `bcctrl`.

**`sc == 2` skips every `static` function.** Requiring external linkage dropped
all internal-linkage functions, which in a C translation unit is most of them:
`dradf4`, `dradfg`, `dradbg` are all `sc=3` in our `smallft.obj`, so their
function-local statics could never pair and the rows read as *"the image has a
mutable static we do not"* — the opposite of the truth.

## Do not feed NAMED blobs into the per-function join

A blob the image names is joined by **exact name**, which needs no function
pairing and no load-order inference. Feeding it into the per-function join as
well adds no coverage and manufactures false leads, because our per-function
walker can only see statics defined in the **same object**. Measured: 20 of 28
MISSING rows were one of `?gUnitsPerMeter@@3MA` (39.3701),
`?gCharHighlightY@@3MA` (-1), `?sIntensity@FlowNode@@1MA` (1),
`?sDOFOverride@RndPostProc@@…` or `?sBloomLocFactor@RndPostProc@@1MA` — every
one of which our tree defines, at the image's value, in another TU, and every
one of which the name join already checks.
`?sBloomLocFactor@RndPostProc@@1MA` is an *undefined external* in
`PostProc_NG.obj` and defined in `PostProc.obj`; it produced the one DISAGREE
row that was not a disagreement.

## The comparison tiers, and what can hide in each

| tier | entry condition | what CANNOT hide |
|---|---|---|
| `agree` | exact ordered match | — |
| `order_only` | equal **multisets** | a wrong VALUE |
| `count_only` | equal distinct-value **sets** | a wrong VALUE (but a dropped/duplicated *read* can) |
| `disagree` | the image reads a constant we never read | — |

`order_only` exists because our MSVC and the image schedule the loads of one
static group differently — on `IsValidSwipePosition` the image reads the +4 slot
before +0 and we read +0 before +4 — so an **ordered** comparison manufactures a
value disagreement out of correct source. `count_only` was added when the target
side widened: the two instruction walkers do not always see a constant the same
number of times. Both are strictly weaker than `agree` and are reported
separately rather than folded into it.

## What the tool still cannot see

* **A static our source lands in `.bss`.** The comparison is `.data` against
  `.data`. `kBlurTaps` is the worked example: the image statically initialises
  slot 0 to 0.1 and stores the other nine at runtime, so it sits in `.data`; our
  `static Vector2 kBlurTaps[5] = {…}` has non-constant initialisers, so MSVC puts
  it in `.bss`. Counted, never silently skipped.
* **A constant the image made mutable and we wrote as a literal or
  `static const`.** It is then in *our* `.rdata` and there is nothing in our
  `.data` to compare. These land in MISSING, which is a **lead, not a bug**.
* **Lookup tables.** A `.data` float blob over 1024 B is generated data (`ATH` is
  5,818 floats), not a hand-written constant; excluded from the per-function
  join and counted.
* **An unmangled static the image does not name.** Of the 34 unmangled
  file-scope statics our side had before array support, **23 are now comparable**
  (12 by exact name, 13 by the per-function join, 2 by both); **11 are not**,
  because the image names them `lbl_<addr>` and no paired function loads them.

### The address-anchor alternative is refuted, not merely unimplemented

The obvious way to reach those 11 is to anchor our `.data` section to a target
address via one known symbol and propagate by offset, since MSVC packs a TU's
file-scope statics into one section. **It does not hold, and the counter-example
is in the same file as the bug this class is named for.** In
`DirectionGestureFilter.obj` our `.data` section is
`sLastSwipeTime`(+0x00,24B) → `sValidHandFloats`(+0x18,12B) →
`sValidScrollHandRadius`(+0x24) → the four swipe-ellipse floats (+0x28…+0x34),
contiguous. The image agrees through `sValidScrollHandRadius`
(`sValidHandFloats` @0x82F444A0, `lbl_82F444AC` @0x82F444AC) and then **diverges**:
its four swipe floats are at 0x82F4453C…0x82F44548, 0x8C bytes further on, after
`?sVal@?1??Update@…@4MA`. Our declaration placement differs from the original's,
so anchor+offset propagation would pair our statics against the *wrong* target
addresses and report confident nonsense. An unreachable row is better than a
wrong one.

## Manual recognizer

For a function at or near 100% that reads a float you cannot find a
`__real@<hex>` for:

1. Find the operand symbol in the target listing. `lbl_<addr>` or a plain name in
   a `.data` `.obj` block ⇒ **mutable static**, and its value is unmetered.
2. Watch for the materialised form — `addi rD, rA, sym@l` followed by
   `lfs fN, <disp>(rD)`, possibly through a second `addi rE, rD, <imm>`. The
   sibling slots carry no relocation and are easy to miss by eye.
3. Read the whole blob, not slot 0. dtk ends it at the next *named* symbol, so a
   `.float 0.1` followed by nine `.float 0` may be one array or ten statics.
4. Check whether our side spells it `static const` or an inline literal. If so it
   is in our `.rdata` and the tool reports MISSING — that is a lead to
   reconstruct the static, not proof of a wrong value.

## Running it

```sh
python3 scripts/analysis/mutable_float_audit.py --selftest   # validate first
python3 scripts/analysis/mutable_float_audit.py
python3 scripts/analysis/data_float_labels.py                # target side alone
```

Coverage is stated on every run: **111 of 177** target `.data` float blobs
examined (62.71%), the remainder dropped as XDK (58), read-by-a-function-we-do-
not-pair (7) or unreferenced-and-unnamed (1). A zero from this tool means
nothing without that block.
