# Tier-2 ("we wrote no body at all") triage, 2026-08-19

Follow-up lane to `03e0202ac`, which fixed `scripts/analysis/fake_impl_scan.py`'s blind spot
and handed over "66 core engine/game rows" to work. This file preserves the **triage
method, the negative results and the residual worklist**, which is the part that rots
fastest if it lives only in a transcript.

Re-derived (not copied) with:

    python3 scripts/analysis/fake_impl_scan.py --project . --max-pct 96 --min-target-size 24

840 rows have `our_pct == 0` and `our_real_insns == 0` — the tier-2 pool. Filtering out
the SDK-coupled units (`binkxenon/`, `zlib/`, `jpeg/`, `synth_xbox/`, `rnddx9/`, `*_Xbox`)
and the compiler-generated shapes (`fn_`, `merged_`, `??_*`, `?$` templates, STL) leaves
**42 core rows** by this lane's cut. The handoff said 66; the delta is entirely filter
choice (this cut also drops `moviebink/` — kept here — and the `??_E`/`??_G` deleting
destructors). The population is the same; only the fence moved.

## The discriminator that made the triage cheap

Two oracles that were not being used, and together they decide every row without guessing:

1. **`orig/373307D9/ham_xbox_r.map` flag column.** `f i` = the linker saw this as an
   *inline* (COMDAT) function; `f` alone = an ordinary out-of-line function. The map also
   names the **contributing .obj** and puts every ICF fold member at the same address.
2. **`build/373307D9/asm/<unit>.s`** — the split's target assembly for the unit. Grepping
   it for `bl "<symbol>"` says whether the target's own TU actually *calls* the thing.

    map `f i` + a `bl` in that unit's .s   -> real placement/call-site row, RECOVERABLE
    map `f i` + NO `bl` anywhere in the .s -> PHANTOM ODR-USE, not recoverable (below)
    map `f`   + our .obj defines a fold peer at the same address -> ICF NAMING ARTIFACT
    map `f`   + nothing of ours there      -> genuinely absent body, write it

## Result

| disposition | rows | bytes | meaning |
|---|---:|---:|---|
| RECOVERED this lane | 13 | 3 592 | see the commits on `fix/comdat-placement-tier2` |
| ICF naming artifact | 20 | 1 924 | our bytes are already there under the fold partner's name |
| Genuinely absent body | 3 | 3 156 | `JoypadPollCommon` + DrawUtl's two copy loops, both delegated |
| Phantom odr-use | 4 | 296 | the target proves there is no call site to find |
| Placement / call-site, open | 2 | 308 | DrawUtl `YUVtoRGB` (delegated), HamDirector `DancerSkeleton` copy ctor |

Whole-build effect of the 13: `build/373307D9/report.json` on
`match_percent_normalized` against the merge base `03e0202ac` — 17 rows move, 16 of them
0 -> 100%; 29 419 -> 29 435 functions at 100%. The one down-row is a documented
`__unwind$` funclet-pairing phantom in `meta_ham/HamProfile` (objdiff pairs EH funclets by
byte signature; the unit's own object is unchanged apart from MSVC's internal `$M<n>`
label numbering).

## Negative result 1 — PHANTOM ODR-USE is a real, unrecoverable class

Four rows are a header-inline COMDAT whose folded survivor sits in TU X's address range
while X's target assembly contains **neither a call to it nor an inlined copy of it**:

| symbol | size | parked in | actual callers in the image |
|---|---:|---|---|
| `?Int@Rand@@QAAHHH@Z` | 148 | `char:Waypoint.obj` | Rand.cpp, MicNull.cpp, Crowd.cpp, CameraManager.cpp, ContextChecker.cpp |
| `?NormalizeTo@@YAXABVQuat@Hmx@@AAV12@@Z` | 100 | `char:CharBonesSamples.obj` | math/Key.cpp only (3 calls) |
| `??3FileStream@@SAXPAX@Z` | 24 | `midi:MidiReader.obj` | — |
| `??3Task@@SAXPAX@Z` | 24 | `flow:FlowTimer.obj` | — |

MSVC emits an inline function's COMDAT from any TU that odr-uses it, **including uses that
produce no instructions** — a call in a branch the optimiser folded away, or a use inside a
`static` helper it then discarded. Waypoint.cpp's only `Rand` contact is `RandomFloat()`;
`Waypoint::Find` and `FindNearest` are plain linear scans with no random pick.
CharBonesSamples' `Relativize`/`EvaluateChannel` diffs show no missing dot-product-and-negate
cluster. There is nothing in the target to point a call site at.

**This is exactly why `../og-dc3-decomp` "solves" `NormalizeTo` with a literal
`charbonessamplesdummyfunclmao` stub function.** That hack is still rejected: it buys a
metric row by inventing a function the original does not have. Recording the reason here so
the next lane does not re-derive it or re-import it.

Same shape, larger: `default/system/hamobj/HamDirector` carries an entire
`std::vector<DancerFrame>` instantiation set (7 rows, ~1 012 B — `??0DancerSkeleton@@`
copy ctor, `??1DancerFrame@@`, allocate/deallocate, `~vector`, `__uninitialized_fill_n`,
`_M_insert_overflow_aux`). The two `bl "??0DancerSkeleton@@QAA@ABV0@@Z"` calls come from
`__uninitialized_fill_n` and `_M_insert_overflow_aux` — i.e. the cluster only calls itself.
No HamDirector function reaches it. Same verdict: a use that left no code.

## Negative result 2 — 35% of the whole tier-2 pool is a dtk NAMING choice, not missing code

**292 of the 840 tier-2 rows (33 420 B) are ICF naming artifacts.** `/OPT:ICF` folded two or
more byte-identical COMDATs; the map lists every member at the one surviving address; dtk
writes ONE name into `config/373307D9/symbols.txt`; objdiff pairs by name; our object defines
the *other* member of the fold set at exactly that offset, so the row scores 0% while the
bytes are already correct and already ours. Worked example:

    ham_xbox_r.map:
      0005:00430708  ?Save@FxSendBitCrush@@UAAXAAVBinStream@@@Z    82760708 f  synth:FxSendBitCrush.obj
      0005:00430708  ?Save@FxSendDistortion@@UAAXAAVBinStream@@@Z  82760708 f  synth:FxSendDistortion.obj
    symbols.txt: only the BitCrush name, at .text:0x82760708
    splits.txt:  0x82760708 lies in system/synth/FxSendDistortion.cpp's .text range
    our FxSendDistortion.obj defines ?Save@FxSendDistortion@@ (COFF symbol table)

**Verified at the byte level, not inferred.** Extracting the target's body from
`build/373307D9/asm/system/synth/FxSendDistortion.s` and our
`?Save@FxSendDistortion@@` from the `.text` COMDAT in
`build/373307D9/src/system/synth/FxSendDistortion.obj`, the two 112-byte bodies are
identical **except for the displacement field of three `bl` instructions** — i.e. exactly
the relocations, which is what `/OPT:ICF` folding requires and what objdiff's normalized
plane ignores anyway:

    target  ... 38810050 4807d3a9 7fc4f378 7fe3fb78 48008fed c01f0060 ...
    ours    ... 38810050 4bffffd1 7fc4f378 7fe3fb78 4bffffc5 c01f0060 ...
                         ^^^^^^^^                   ^^^^^^^^

So the row is 0% purely because objdiff cannot pair the two names. Nothing is missing.

**Swept over all 292 rows** with `scripts/analysis/icf_pairing_bodytest.py` (added on this
branch): mask every `b`/`bc` displacement and the low 16 bits at each relocated offset, then
compare our fold peer's COMDAT body to the target body.

    286 / 292  BYTE-IDENTICAL  -- witnessed artifact, our bytes are already right
      6 / 292  DIFFERENT       -- listed below, worth a second look

All **20** of the core-set ICF rows are in the 286.

**A correction worth keeping.** The first version of that sweep reported 203/292 and 89
differences. It was wrong: it masked relocated immediates on OUR side only, so every
`lis rX, sym@ha` / `lfs fY, sym@l(rX)` pair read as a difference. Hand-diffing
`?UpdateSphere@RndParticleSys@@` against our `?UpdateSphere@RndGenerator@@` showed the two
"differences" were at offsets `0x10` and `0x24` and were exactly `3d60821c` vs `3d600000`
and `c00b2fc8` vs `c00b0000` -- the resolved vs unresolved halves of one relocation. Mask
both sides with the same offset set and it is 286. Recorded because an 89-difference number
would have looked like 89 bugs.

The 6 that still differ are a LEAD, not a conclusion, and they point the other way from the
rest of this file: `/OPT:ICF` folding proves the ORIGINAL's two bodies were byte-identical,
so if our fold peer is not identical to the target's folded body, **our peer is the wrong
one** -- and its own row has no target counterpart to score against, so the metric cannot
see it. Five are template instantiations whose fold set has several members in our object
(the sweep takes the first that matches, so a miss may just be peer selection), and one is
`??_GSynthSample@@` vs `??_GSynthSample360@@`:

| row | unit | peer tried |
|---|---|---|
| `?Unlink@ObjPtrList<EventTrigger>` | `ui/UIList` | `ObjPtrList<Hmx::Object>` |
| `?erase@ObjPtrVec<HamMove>` | `world/LightPreset` | `ObjPtrVec<Spotlight>` |
| `?erase@ObjPtrVec<RndTex>` | `rndobj/Font` | `ObjPtrVec<RndMat>` |
| `?FindRef@ObjPtrVec<RndTransformable>` | `world/LightPreset` | `ObjPtrVec<RndLight>` |
| `?Replace@ObjPtrList<CamShot>` | `world/ThreeDSoundManager` | `ObjPtrList<ThreeDSound>` |
| `??_GSynthSample@@` | `synth_xbox/SynthSample` | `??_GSynthSample360@@` |

Two limits of the instrument, stated because they bound what a pass means: it masks branch
displacements ENTIRELY, so it cannot see a `bl` to a *different* callee (a false-positive
risk on the 286, bounded by ICF requiring identical relocation resolution); and `??_E<T>`
rows resolve through a COFF weak external to `??_G<T>`, so the target symbol is a thunk
rather than the same body. For a stronger verdict use decomp-synth's
`probe_icf_foldtest.py`. This still respects the project rule that **name shapes are
arguments, not witnesses** (`scripts/symbol_aliases.json`'s `_comment`, after an earlier
triage called 33 `merged_` rows benign from their names and the body test found 9 genuinely
different): 286 rows now have a witness rather than a name.

**281 of the 292 (32 288 B) would be fixed by one rule in dtk's splitter:** when an address
carries several names, prefer the fold member whose contributing .obj is the .obj that owns
the address range, instead of the first-seen name. Verified mechanically — for 281 rows a
fold peer's map-obj equals the unit's own object.

Not attempted here. `dtk` lives in `../jeff`, needs a manual `cargo build --release`, and
`bin/objdiff-cli`/dtk are shared with rb3 and rb3-xenon, so the blast radius is three repos.
Hand-editing `config/373307D9/symbols.txt` to force pairings is explicitly the wrong fix
(CLAUDE.md: never hand-revert generated config to work around a generator bug).

Note the existing ICF machinery does **not** cover this: `scripts/symbol_aliases.json` ->
`build/373307D9/icf_aliases.map` feeds objdiff's `map_file` -> `reloc_eq`, which makes fold
members equal **as relocation targets**. It does not make objdiff *pair two differently-named
functions* for diffing, which is what these rows need.

## Lead — the 6 non-identical ICF rows are metric-invisible divergences in core containers

Worth pulling out of the table above, because it inverts the direction of the rest of this
file. `/OPT:ICF` only folds COMDATs that are byte-identical, so the ORIGINAL's fold members
were the same bytes. If our fold peer is **not** identical to the target's folded body, our
peer is the wrong one — and since every instantiation folds, the target names the survivor
exactly once and **no instantiation of that method is scored anywhere in the build**. The
metric cannot see these at all.

Three of the six are `ObjPtr` container methods, i.e. engine-wide:

| method | target | ours | delta |
|---|---:|---:|---|
| `ObjPtrList<T,U>::Unlink(Node*)` | 284 B | 312 B | +28, 36 differing words from offset 0x8c |
| `ObjPtrVec<T,U>::erase(iterator)` | 276 B | 240 B | -36 |
| `ObjPtrList<T,U>::Replace(ObjRef*, Object*)` | 80 B | 76 B | -4 |

`Unlink` is diagnosed. `src/system/obj/ObjPtr_p.h:725` returns early from three of its four
branches and repeats `mSize--` in each. The target has **one** `mSize--` and **one**
`return`, with every path funnelling its result into `r3` and falling through to a shared
tail (`lwz r11, 0x4(r31); subi r11, r11, 1; stw r11, 0x4(r31)` at 0xf8, reached by `b` from
each branch). Its head-removal branch also merges the has-next and no-next cases before the
store — both paths land on `stw r11, 0x8(r31)` / `mr r3, r11` with `r11 = nullptr` on the
empty path — where ours has a separate `mNodes = nullptr; mSize--; return nullptr;`. So the
target's shape is:

    Node *ret;
    if (node == mNodes)            { ...; mNodes = n; ret = n; }
    else if (node == mNodes->prev) { ...; ret = mNodes->prev; }
    else                           { ...; ret = node->next; }
    mSize--;
    return ret;

**Not attempted here.** `ObjPtr_p.h` is included by most of the tree and `erase()` inlines
`Unlink`, so re-shaping it will move scored functions in both directions; it needs its own
lane with a whole-build A/B, not a drive-by. The other three rows
(`ObjPtrVec::FindRef`, `ObjPtrList<CamShot>::Replace`, `??_GSynthSample@@` vs
`??_GSynthSample360@@`) are unexamined and may still be peer-selection artifacts of the
sweep rather than real differences.

## Residual worklist

Ordered by size. Everything here is real work, not an artifact.

- **`JoypadPollCommon`, 2 644 B, `os/Joypad`** — plain `f` in os:Joypad.obj, declared
  (`Joypad.h:301`), called (`Joypad_Xbox.cpp:55`), never defined. rb3's copy is itself an
  unfinished two-`MILO_WARN` stub, so it is not a reference. Delegated to
  `fix/joypadpollcommon`.
- ~~`BinkMovieImpl` 5 rows, 2 664 B~~ — **DONE on this branch.** `SetRect`, `FinishOpen`,
  `EndianSwapBuffer`, `BeginFrame` and `EndFrame` were all 0% and all reach 100%.
  Reference-less: og-dc3 defines none of them either.
- **`gesture/DrawUtl` `CopyDepth` (360) + `CopyPlayerMask` (152) + `YUVtoRGB` (176)** plus the
  three conversion loops missing from `UpdateBufferTex` (1 284 B at 60.6%) — delegated to
  `fix/drawutl-buffer-copies`. `YUVtoRGB`'s row is expected to stay 0% regardless: it folded
  with LiveCameraInput.obj's same-named anon-namespace copy and dtk named the survivor with
  LiveCameraInput's hash (`?A0x8e584365`) while parking it in DrawUtl's range, so our
  DrawUtl.obj can only ever emit `?A0xad24ca77`. Instance of negative result 2.
- **19 ICF naming artifacts in the core set** (2 138 B) — blocked on the dtk rule above.
- **4 phantom odr-uses** (296 B) + the HamDirector `vector<DancerFrame>` cluster (~1 012 B) —
  closed as unrecoverable, see negative result 1. Do not reopen without new evidence.
