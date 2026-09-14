# Stack-slot sharing: MSVC packs sibling-scope locals unless an *inlined* callee gets their address

**Established 2026-09-11** (lane w3-s) with a 30-probe minimal-TU matrix compiled under the
project's exact command line, a whole-TU slot census cross-referenced against `report.json`, and
three functions moved (exact `report.json` `match_percent_normalized`, rounded headline in bold):
`HamDirector::OnPopulateMoves` 99.55015 → 99.63717 (**99.6**, 107 → 48 rows, frame fixed),
`UILabel::PreLoad` 99.0766 → 99.77305 (**99.8**), `RndFont::Load` 98.18632 → 99.69578 (**99.7**).
Regression check: the three touched TUs hold 525 functions; a per-function diff of the worktree's
`report.json` against main's shows exactly those three rows changed, all upward. (The whole-tree
`measure_progress.sh` run did not complete — its temporary baseline worktree failed the
post-compile verify step against the main checkout's build dir — so that is the check on record.)

**This is a source lever, not a flag.** The wave-3 description that started the lane — *"our build
packs sibling-scope PODs and vtable-less objects into ONE stack slot; the target NEVER does"* — is
half right. Our build does pack them. So does the target: five 100%-matched functions in the tree
share sibling-scope slots (below), including two `char block[256]` arrays on one slot in
`ByteGrinder::GrindArray`. The difference on the three blocked functions was never the compiler;
it was that the original source handed the local to something that gets **inlined**.

## The observable

`run_diff_inspect mode=stack-layout` shows a structural frame delta that is a multiple of a local's
size, a `TGT_ONLY` slot for the missing copy, and a `DIFFER`/`PERMUTED` row where our build has two
or three variables of the same type on one offset:

```
Frame size:  TGT 0x1350  BASE 0x1260  Δ -0xf0        HamDirector::OnPopulateMoves
  0x100  DIFFER   target: fp   base: merger,merger,merger   (three sibling if-blocks)
Frame size:  TGT 0x320   BASE 0x220   Δ -0x100       UILabel::PreLoad
  0x1d0  TGT_ONLY (256 B, one access)                (a second char name[256])
Frame size:  TGT 0x2a0   BASE 0x230   Δ -0x70        RndFont::Load
  0x160  base: buf[0x80] AND theChars[0x60]           (target: 0x1d0 and 0x170)
```

The `/FAs` listing shows it directly: every packed local gets the **same** `name$NNNN = offset` line.

## The rule (measured, not inferred)

MSVC 16.00.11886 at `/O1 /Oi /EHsc /GR` assigns frame slots in two classes:

1. **Packed.** Locals in *disjoint lexical scopes* share one slot. Siblings pack whether they are
   `char[256]`, a POD struct, a type with an extern ctor+dtor (EH funclets and all), or a type with a
   vptr. Same-block locals never pack (their scopes overlap). The packed region sits **above** the
   pinned region; within a region, offsets go **smaller object lower** whichever scope is the
   enclosing one (`m_sz1`/`m_sz2`), ties in first-use order.
2. **Pinned.** A local gets its own slot when **its address is passed into a callee that is
   inlined** — as `this` of an inlined ctor/dtor/member, or as a pointer/reference parameter of an
   inlined function — and that callee dereferences it *or merely forwards it on*. Nothing else pins.

Every row below is one probe in [`scripts/analysis/slotprobe/`](../../../scripts/analysis/slotprobe/)
(`probe.sh <name>` compiles `<name>.cpp` with the `msvc` rule's flags plus `/FAs` and prints the
frame size and the slot table; `probe2.sh` adds the project include paths for real headers, e.g.
`q1.cpp` is the three-block FilePath+Merger shape from `OnPopulateMoves`).
Three sequential `if (Cond(n)) { T x; ... }` blocks unless stated; "shared" = one offset for all.

| probe | local's type / what touches it | result |
|---|---|---|
| p3, p4 | two `char[256]`, uninitialised or `= {0}` | shared |
| p5 / m_p8 | POD struct, only `Use(&a)` | shared |
| m_d8, m_d32 | extern ctor + extern dtor, 8 and 32 B | shared |
| m_v8, m_v32 | vptr, extern ctor, extern virtual dtor | shared |
| m_i8 | extern ctor, **inline empty** dtor `~I8() {}` | shared (nothing stored) |
| m_p8s, m_arr_s | POD / array with a **direct** store in the body (`a.x = 1`) | shared |
| m_arrinit, m_strlen | `const char t[] = "..."` (memcpy init), `strlen(t)` intrinsic | shared |
| m_helper | `char name[256]` declared **inside** an inlined helper, passed only to out-of-line callees | shared with a sibling array |
| m_ms8, m_bs8 | inlined member read on a **sub-object at +4** (member or non-primary base) | shared |
| **m_ic8, m_in8** | **inlined ctor** stores a member (no vptr, with/without dtor) | **distinct** |
| **m_ve8, m_vi8** | **inlined ctor** that only stores the vptr | **distinct** |
| **m_vd8** | vptr, extern ctor, **inlined dtor** (`virtual ~VD8() {}` resets the vptr) | **distinct** |
| **m_fn8, m_fn8r** | inlined member that **stores**, or only **reads** (`a.Get()`) | **distinct** |
| **m_fd8** | same, on a type with an EH-registered extern dtor | **distinct** |
| **m_ptrfn, m_reffn, m_refread** | inlined free function storing/reading through `T*` / `T&` / `const T&` param | **distinct** |
| **m_fwd** | inlined member `Rev::operator>>(T&)` that only **forwards** the reference to an out-of-line call | **distinct** |
| p2 | two `char[256]` in the **same** block | distinct (scopes overlap) |
| m_outer, m_font_a/b | one buffer in an **enclosing** scope, one in a nested block | distinct (scopes overlap) |
| p1 | if/else with identical bodies | **confounded**: MSVC tail-merges the branches, one array left |

Two consequences worth naming:

- The reason `FilePath fp` locals were "unshared on both sides" while `FileMerger::Merger` locals
  were not: `FilePath`'s ctor is `__declspec(noinline)`, but its implicit dtor is inline and resets
  the vptr before calling `??1String` — a `this == &fp` inlined store. `Merger`'s ctor is in-class
  but not expanded (`bl ??0Merger`), and `~Merger() {}` is empty. Vtable-ness was a proxy for the
  real variable.
- `str.c_str()` does **not** pin a `String`: `c_str` is `FixedString`'s, and `FixedString` sits at
  +4 inside `String` (TextStream's vptr is at 0), so the inlined call runs on an interior `this`
  (m_bs8). `d >> str` does pin it, because `BinStreamRev::operator>>(T&)` is an inline template
  that forwards `str` (m_fwd). The rev<3 chain in `RndFont::Load`,
  `d >> a >> b >> c >> dd >> e >> str`, already showed this before anyone looked: `a b c e str` are
  on distinct slots and only `dd` — the `bool` overload is an out-of-line member — is packed.

## The target packs too (why this is not a flag)

Compile a TU with its exact ninja command plus `/FAs`, parse the `x$NNNN = off` lines per `PROC`,
and cross-reference `report.json` (`scripts/analysis/slotprobe/tu_census.py <worktree> <unit>...`
does this; run it from the worktree after a full `ninja`). A function at
`match_percent_normalized == 100.0` has the target's frame byte-for-byte, so a shared slot in *our*
listing is a shared slot in the *target*:

| function (100.0%) | shared slot in our build |
|---|---|
| `ByteGrinder::GrindArray` | `0x1a0: block[256], block[256]`; `0x60: stringArgs[8], callName[16], callName[16]` |
| `DirLoader::LoadObjs` | `0x60: start[68] ×4`; `0xb0: pt[68] ×2` |
| `DirLoader::LoadDir` | `0x60: end[68] ×4` |
| `HamNavList::Save` | `0x50: uc[1] ×7` |
| `CopyTypeProperties` | `0x58: propIdx[4], fromType[4]` |

So the compiler options are not in question. Do not touch `/O1`, `/Ob`, `/GS` or the PCH for this.

## Levers, with what each one bought

**Pin it: call something inline on it.** The three `FileMerger::Merger merger` locals in
`HamDirector::OnPopulateMoves` were only ever handed to out-of-line callees. `FileMerger.h` already
has `void SetSelected(const FilePath &fp, bool b) { mSelected = fp; mForceReload = b; }`;
spelling the two assignments as `merger.SetSelected(fp, true)` inlines it with `this == &merger`.
Frame 0x1260 → **0x1350**, merger slots 0x120/0x200/0x190 as in the target, 107 → 48 rows
(`dcf794459`). The 48 left are pinned-region *order* (fp −8, a scalar permutation), a different
question.

**Pin it: forward it through the inline `d >>`.** `RndFont::Load`'s rev<0x11 `String str` was
folded into the 0x70 temp pool because `d.stream >> str` calls the out-of-line
`BinStream::operator>>(String&)` directly. `d >> str` is byte-identical code and pins it: the last
0x10 of frame delta (`ddb7f2a81`). The same substitution on the three chained reads
`d.stream >> iW >> iH` / `w >> h` / `bw >> bh` did two more things at once: it pins the locals
(which put the `bw`/`bh` `fcfid` temps back on 0x70 and cleared 17 `f12↔f13` swaps), and it
re-loads `d.stream` per link instead of chaining on the returned reference (`mr r29, r3` … `mr r3,
r29`), which is what the target does. 98.3 → **99.7**, 40 → 15 rows (`86acf1e68`).

**Overlap the scopes.** `theChars[]` in `RndFont::Load` cannot move (its `memcpy` is in place at
row 336), so `char buf[0x80]` is declared at function scope; its lifetime now overlaps theChars'
block and the two stop sharing. Frame delta −0x70 → −0x10 (`ddb7f2a81`).

**Overlap the scopes via an inline helper.** `UILabel::PreLoad`'s rev>0x15 block reads a
256-byte name in both arms of an `if`; the target has two buffers (0xd0 and 0x1d0) *and* loads
`d.stream` into a callee-saved register before calling `LStyle(1)`. A load of an escaped local
cannot be hoisted across a call, so the source itself evaluates `d.stream` first — that is
right-to-left argument evaluation of a helper call — and a helper's own buffer overlaps the
enclosing block's in scope. `ReadFontResourceName(LStyle(1).mFontResource, d.stream)` as a
`static inline` (`/O1` is `/Ob1`: only explicit inlines expand) reproduces rows 638-655 exactly:
frame 0x220 → **0x320**, 99.1 → 99.6, 48 → 9 rows (`394dae3f1`). Reading `mAlignment` into a
local before the rev<4 `xfm` stores then took it to **99.8** (`a4b17abaa`).

## Negative results (each measured, each reverted)

- **Split the declaration per branch** (w3-f, `UILabel::PreLoad`): inert. Sibling scopes pack.
- **`operator const char *() const { return c_str(); }` on `String`**, as RB3's `String` has it:
  it *would* pin (`this == &str`), but `str[i]` becomes ambiguous between `FixedString::operator[]`
  and the built-in subscript through the conversion — C2666 in six TUs — and VS2010 has no
  `explicit` conversion operators. Reverted before it reached a commit.
- **A file-local `static inline const char *CStr(const String &)`** pins `str` exactly as `d >> str`
  does (98.3, 40 rows, same bytes). Rejected as a match-only helper once the natural spelling was
  found.
- **`LStyle(0).mColorOverride.Load(d.stream, true, NULL)`** for the rev>0xC read in `PreLoad`: moved
  `d.stream` ahead of `LStyle(0)` as intended (arguments are evaluated before the object
  expression), but materialised `li r5`/`li r6` in the other order, 2 rows → 4. Reverted.
- **`float h, w;` and `d >> w; d >> h;`** for the 0xb8/0xc0 swap in `RndFont::Load`: both inert.
  Pinned-region order is not declaration order and not pin order; unresolved.
- **The type matrix's null results are the doc's controls**: vptr, dtor, size, direct stores,
  intrinsic init and an inlined helper's *own* local were each proposed as the discriminator and
  each compiled to a shared slot. p1 is the confounded control — identical if/else arms are
  tail-merged, and a probe built that way reports one array whatever the rule is.

## Detection and procedure

1. `stack-layout` shows a structural Δ equal to k × sizeof(some local), a `TGT_ONLY` slot of that
   size, and `DIFFER`/`PERMUTED` rows naming the same variable two or three times on one base
   offset.
2. Decide which side is doing what: in the target, a local **below** the packed region (lower
   offset than the arrays) is pinned; one **above**, alone, is packed but has no packable partner.
3. Look for what the original could have inlined on that local: an in-class member that does the
   assignments you spelled out (`SetSelected`), the `d >>` forwarding template instead of
   `d.stream >>`, an implicit inline dtor of a vptr type, a helper whose argument order also explains
   a "loaded before the call" row.
4. If nothing inlines and the target still keeps two slots, the scopes overlap: one of the
   declarations belongs to an enclosing block.
5. Rebuild the probe first when unsure — `probe.sh` is a 2-second answer, a TU rebuild is not.

The 70 AT_LIMIT certificates typed `artifact:stack_layout` should be re-read against this: a frame
delta that is a multiple of a local's size, with the local handed only to out-of-line callees, is
now a known source lever, not a floor.

## Temporaries: highest free slot, full-expression lifetime (w3-s2, 2026-09-11)

The rule above is about *named locals*. The **compiler temporaries** that inline
`operator<<`/`operator>>` create (the by-value `rhs` copy, the `unsigned char uc` of the bool
overload, a `Symbol`/`String` built for an argument) follow a second rule that is fully
determined by the target's store sequence, so it can be *decoded* rather than guessed:

1. a temp takes the **highest currently-free slot** of its size class (bytes and words are
   separate classes); a new slot is opened above only when none is free;
2. **every temp of one full-expression lives to its end**, so a chain `bs << a << b << c`
   holds three slots at once while three single statements all reuse the top one;
3. a temp's slot is freed at the end of its statement, so the next single statement lands
   on the highest of the freed ones.

Read the target's `stw/stfs/stb ... (r1)` sequence and the chain structure falls out. Four
crossings came from exactly this (all `run_objdiff` 100.0, every row equal):

| function | target sequence (temp slots) | decoded source |
|---|---|---|
| `SampleData::Save` | `54 54 \| 54 58 5c 60 \| 50 \| 60` | `bs << mFormat << mNumSamples << mSampleRate << mSizeBytes;` (a 3-chain reads `54 58 5c`, -4 everywhere) |
| `RndPostProc::Save` | `... 58 54 \| 58 58 58 \| 58 54 5c \| 5c ... 5c 58 \| 5c ...` | four chains that were single statements (`mTrailThreshold << mTrailDuration`, the three `mKaleidoscope*`, `mHallOfTimeRate << mHallOfTimeColor << mHallOfTimeMix`, `mBloomStreakAttenuation << mBloomStreakAngle`) |
| `UIListDir::Save` | `54 54 58 \| 50 58 54 5c 60 64 68 51 6c` | `bs << mOrientation << mFadeOffset;` then ONE chain from `mTestMode` to `mScrollHighlightChange` -- the bool at `0x51` is only possible while `mTestMode`'s byte still holds `0x50` |
| `HamCamShot::Target operator<<` | six bytes all on `50`, float on `54` | write the bool bitfields through `operator<<(bool)`; six named `unsigned char` locals overlap in scope and took `50..55` |

The probe is [`scripts/analysis/slotprobe/store_seq_probe.py`](../../../scripts/analysis/slotprobe/store_seq_probe.py)
(`<unit> <symbol> <src> [variant.py ...]`, run from a worktree after a full `ninja`): one
`cl.exe /FAs` compile plus one `objdiff-cli` diff of the scratch object against the target
object, ~20 s, no ninja, prints both sides' store-slot sequences and the differing positions.
`slot_table.py` prints the `name$ = offset` table for one PROC (its regex accepts the
unnumbered `goofy$ = 140` form that `tu_census.py` skips).

#### `HamDirector::OnPopulateMoves`: the "48 rows left" decoded to a 24-slot map (w6-a)

The `fp -8` remark above is the visible corner of a permutation worth writing down, because the
function is 2,712 B at **99.63717** and the *frame sizes already agree* (0x1350 both sides) --
so there is nothing structural left, only temp ordering. 46 `diff_arg` + one `mr r3, r14`
scheduled two slots early (base idx 380 vs target 382).

Everything at or below `0x9c` matches. Everything at or above `0x120` (the three `merger`s,
`fs`) matches. **All 24 permuted slots live in `0xa0..0x118`**, and both sides use the same
count there (27 slots, with the 8-byte `FilePath`s leaving different gaps). Base-side names are
from `/FAs`; target offsets are from the aligned diff rows:

| our slot | our name | target slot | delta |
|---|---|---|---|
| `0xa0` | (temp) | `0xb8` | +0x18 |
| `0xa4` | (temp) | `0xa0` | -4 |
| `0xa8` | (temp) | `0xa4` | -4 |
| `0xac` | (temp) | `0xb0` | +4 |
| `0xb0` | `movesDir` | `0xc0` | +0x10 |
| `0xb4` | `moveSymKeys` | `0xb4` | **match** |
| `0xb8` | `clipKeys` | `0xc8` | +0x10 |
| `0xbc` | `hamMoveName` | `0xbc` | **match** |
| `0xc0` | (temp) | `0xd0` | +0x10 |
| `0xc8` | (temp) | `0xd8` | +0x10 |
| `0xcc` | (temp) | `0xa8` | -0x24 |
| `0xd0` | `moveKeys` | `0xac` | -0x24 |
| `0xd4` | (temp) | `0xcc` | -8 |
| `0xd8` | (temp) | `0xd4` | -4 |
| `0xdc` | (temp) | `0xe0` | +4 |
| `0xe0` | `clipSymKeys` | `0xdc` | -4 |
| `0xe4` | (temp) | `0xe8` | +4 |
| `0xe8` `0xf0` `0xf8` | `fp` x3 | `0xf0` `0xf8` `0x100` | **+8 each** |
| `0x100` `0x104` `0x108` `0x10c` `0x110` `0x114` | (temps) | `0x114` `0x110` `0xe4` `0x118` `0x108` `0x10c` | scrambled |

The `fp +8` has a concrete cause visible in the map: the target fits **one more word slot below
the `FilePath` block** (word slots at `0xdc 0xe0 0xe4 0xe8`, then `fp` 8-aligned at `0xf0`; ours
has only `0xdc 0xe0 0xe4`, so `fp` lands at `0xe8`). That extra word is not an extra
*variable* -- it is our `0x108` temp, which the target places at `0xe4`. So this is squarely the
"highest free slot / full-expression lifetime" rule above, and it should be **decodable** from the
target's store sequence rather than guessed: the lever is which reads are chained into one
full-expression and which are separate statements. `store_seq_probe.py` is the instrument.
Not attempted here for want of lane time; recorded so the next lane starts from the map.

Two more levers the same session confirmed on this class, both on named locals:

- **Implicit conversion vs explicit temporary** (`CharCuff::Load` 99.99 -> 100, `DirLoader::AddTypeObjectMemDelta` 95.4 -> 99.9): `mCategory = "";` reads the temp back from its slot, `mCategory = Symbol("")` reads it through the ctor's return register; `find(String(name))` hands the ctor return straight to `_M_find`, `find(name)` re-materialises the slot address. Which one the target used is visible in the row after the ctor call (`lwz r11, 0x54(r1)` vs `lwz r11, 0x0(r3)`, `mr r4, r3` vs `addi r4, r31, ..`).
- **A by-value parameter's home is a slot too** (`CharLookAt DrawBounds` 99.7 -> 100): the target had no `Vector3 result`; it multiplies back into `lookDir`'s parameter home at frame+0x10 and passes that to `AddLine`.

And the negative result, so nobody repeats it: the *named-local* pinned-region order is still
not a source lever. `RhythmBattle::OnBeat` (99.44997) differs from its target by exactly one
word -- the target packs `inMindControl`/`goofy` on `0x8c`/`0x8d`, ours gives each a word --
and 30+ variants through `slot_table.py` (six declaration orders, provably byte-identical;
const; a Symbol local; direct-init; every deref spelling; a const-ref inline pin) either did
nothing or packed the bools while adding another slot. Same for `CacheMgrXbox::PollSearch`'s
`numFound`/`res` swap (declaration order, both scopings, the type, renaming: inert).

### `RhythmBattle::OnBeat`, re-derived independently (lane w6-a, 2026-09-14)

Worth recording because the *arithmetic* makes this the single largest prize on the board and
the diagnosis is now exact rather than suggestive. 16,508 bytes at 99.44997; second lane, same
conclusion, reached without reading the paragraph above first.

What is measured, not inferred:

- Both sides reference **exactly 203 distinct `r31`-relative slots**. There is no extra
  *variable* on our side -- only an extra *word*: target **202 words**, base **203**.
- The target's **only** multi-byte word is `0x8c`, holding `{0x8c, 0x8d}`. Our build has
  **zero** multi-byte words. `/FAs` names them `inMindControl` and `goofy`.
- That one word *is* the entire 470-row offset residual, and the cascade is arithmetic:
  base pads `0xe4` to 8-align the `DataNode` pair the target puts at `0xe0` (+4 -> +8), then
  pads again to 16-align the `Vector3` block at `0x350` (+8 -> +16). That is the
  `0x760`-vs-`0x750` frame delta. Offset-delta histogram: `+8 x327, +4 x71, +16 x41, 0 x117`.
- `/FAs` also shows **`0x90` holds both `i` and `beat`** (disjoint scopes, shared slot) and sits
  *between* `goofy` (`0x8c`) and `inMindControl` (`0x94`). The target has both bools *below*
  `0x90`. So the question is only "why is `inMindControl` above `0x90` here".

Seven further variants, each compiled and each **inert** -- same slot table, same
`this@0x774`, and for the two measured end-to-end the same canonical/`diff_score`/histogram
to the digit:

| variant | result |
|---|---|
| `bool inMindControl;` hoisted above `UIPanel *focusPanel` | inert |
| `bool inMindControl;` immediately after `bool goofy`, assigned at the original site | inert |
| **both bools computed lexically adjacent**, after the early return | inert |
| the two `static Symbol`s hoisted above `goofy` (nothing separating the bools) | inert |
| `const int beat` / `const int i6cc` (narrowing `0x90`'s occupant) | inert |
| `UIPanel *const focusPanel` | inert |
| `mind_control == ...Sym()` (operand order) | inert |

The third row is the one that settles it: **making the two bools lexically adjacent does not
pack them.** An uninitialised declaration does not move a slot either -- MSVC is not using
declaration position for the byte class in this function. Combined with the 30+ variants above,
this lever is refused by ~37 distinct spellings. Treat `OnBeat`'s remaining 0.55% as a
byte-class placement floor unless someone finds the actual discriminator; the probe to use is
`slot_table.py` (~40 s, no ninja), and the pass/fail signal is `goofy`/`inMindControl` sharing a
word **without** the total slot count rising.

## Frame size is a falsification test, and three w7-g refutations that used it (2026-09-14)

Everything above is about *closing* a frame delta. The same number works in the other
direction, and it is the cheapest guard a lane has against "tidying" a function backwards:
**if a source edit shrinks our frame, the target materialised the thing you just deleted.**
A shrinking frame is never neutral cleanup — it is evidence, and it costs match%.

Three measurements from lane w7-g, all reverted, all with the frame delta as the discriminator:

| function | edit | frame Δ | canonical |
|---|---|---|---|
| `RndAmbientOcclusion::Tessellate` | drop the `RndMesh::Face tA, tB` temporaries in the `splitCount == 1` chain, call `fa.Set(...)`/`fb.Set(...)` directly | **−0x40** | 91.45503 → 88.8 |
| `RndAmbientOcclusion::Tessellate` | move `RndMesh::Vert &vert0/&vert1/&vert2 = mesh->Verts(face.vN);` from above the `Edge edge01, edge12, edge20;` block down to just before the `blendVert01/12/20` declarations | **−0x10** | 91.45503 → 90.5 |
| `DxTex::SyncBitmap` | hoist `D3DSURFACE_PARAMETERS params = { 0 };` above `D3DFORMAT edramFormat = mFormat;` in the EDRAM branch | frame equal, +4 insns | 93.96613 → 93.0 |

The first row is the useful one. The `stw`+`sth` rows on a 6-byte `RndMesh::Face` read exactly
like a struct-copy lowering artefact — merged adjacent `unsigned short`s — so the obvious move is
to delete the temporary and use the field-by-field `Set()`, which emits 3 × `sth`. The frame says
no: the target allocates 0x40 of slots our edit removed, so those temporaries are **in the
original source**, and the `stw`/`sth` split is the consequence of a copy the target really makes.
Do not chase it.

### An address-taken argument temp can be allocated BELOW a named local

`DxTex::SyncBitmap`'s entire 190-row residual is one temp-ordering decision, visible at
**instruction 10**: `addi r3, r31, 0x84` (target, `&tracker`) vs `addi r3, r31, 0x80` (ours).
Decoded target slot map, bottom-up:

```
0x80  4-byte address-taken MakeString argument temp
0x84  PhysMemTypeTracker tracker      <- ours puts this at 0x80
0x88  8-byte _dw static-guard temp
0x90 0x94 0x98   temps
0xa0  depthParams        0xb0  params        0xc8  rect        0xd0  converted
```

Ours opens the temp region *above* `tracker` instead of below it. Everything downstream is
arithmetic on that one shift: the −4/−12/−28/−40 offset deltas, 55 register-swap instructions,
and the `std`-vs-`stw` rows — the last because `params` lands 4-aligned here and 8-aligned there,
and MSVC only pairs two word stores into a 64-bit `std` on an 8-aligned slot. **Slot-class
placement is not reachable from declaration order** (the §"OnBeat" byte-class result above is the
same shape), and the one spelling that moves it — hoisting `params`, which does change *which*
region opens first — buys the -4 and pays for it with a duplicate zero-fill (`addi r10, r31, 0xa4`
+ `stw r29, 0x0(r10)`, 818 → 822 instructions). Net loss. Refused.

### Identical addresses, different register: a regalloc floor, not a declaration question

`SuperFormatString::SuperFormatString(const char *, const DataArray *, bool, Locale &, Symbol)`
has 30 instructions differing only by `r19`↔`r20`. Both sides put the three char arrays at the
**same** frame addresses — `param@0x70`, `phInfo@0xb0`, `tempFmt@0x100` — so there is no slot
question at all; only *which* callee-saved register holds `tempFmt` vs `phInfo` differs. Two
independent declaration reorderings (swapping `int phType`/`int state`; permuting the four
pointer declarations to `tempFmtPos, tempFmtEnd, phInfoPos, paramPos`) were both **inert to the
digit**. When the addresses already agree, stop reordering declarations — the remaining signal is
liveness, and the lever is §"Liveness/scheduling beat declaration reorder", not this doc.
