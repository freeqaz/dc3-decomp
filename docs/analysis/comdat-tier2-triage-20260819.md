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
