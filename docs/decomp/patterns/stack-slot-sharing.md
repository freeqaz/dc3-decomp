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
