# Dispatch + data-symbol scanner re-run, 2026-08-18

Periodic re-run of the two standing "bugs the metric hides" scanners (last run 2026-06-16,
waves 10-12), on base `ccd4c8036`, worktree plane. Fixes landed on this branch; this doc
preserves the **negative result and the residual worklist**, which otherwise lived only in
a session transcript.

## vtable_dispatch_scan.py: CLEAN NEGATIVE — the wrong-target-dispatch class looks exhausted

`--all` over 1,642 raw<norm functions: 284 hits (31 strong / 63 medium / 190 weak).
Filtered to alignment-certain rows (every neighbouring instruction matches): 11 survive,
and **none is a wrong vtable slot**:

- `UIListSlot::Draw` 99.9%, `RndMesh::MakeWorldSphere` 99.8% — OFFSET_SWAP on struct
  fields (`mDrawOrder`, `mVerts`), not dispatch.
- `Voice::Init` 99.5% "strong" — same two instructions in swapped order; scheduling.
- `RndTexRenderer::DrawToTexture` ×2 — slots `0xc`/`0x18` match both sides; only receiver
  *addressing* differs (`this+0x84` vs `&mDrawable + 0xc` — same field, folded
  displacement vs held pointer).
- `HolmesClient::DumpHolmesLog`, `TexBlender::DrawBlendList`,
  `HamNavList::LinkRibbonDrawState`, `FreestyleMoveRecorder`, `BinkIntegration`,
  `HamCamShot::SetPreFrame` — member-offset/arg loads or sub-95% alignment noise.

Prior-known skips re-confirmed: the 3 `CalcShaderOpts`, `Rnd::DrawPreClear`,
`DingoJob::SendCallback`, `ChunkStream::Eof`, `StorePanel` (wave-16 floors),
`BinkMovieImpl` (certified floor).

## data_symbol_scan.py: the productive plane — missing-override proof method

Candidate rows become provable by cross-checking `orig/373307D9/ham_xbox_r.map`: if the
target's slot address hosts `Owner::Method` and our slot points at `Base::Method`, the
original has an override we never declared. ICF representatives disassembled:
**0x82AEAE70 = `li r3,0; blr`** (return false/NULL group), **0x82E2AB00 = `li r3,1; blr`**
(return true group). 21 overrides/RTTI/return-type fixes landed on this branch (see
commits). Candidate-bug rows: 144 → 110.

Note: the scanner had a thread race (map oracle published empty then filled inside the
worker pool) making June's candidate counts partly nondeterministic; fixed in
`0e91b968b`. Coverage limit: 6,194 of 18,549 data symbols fail with "Symbol not found in
target" — dtk's split doesn't reconstruct every RTTI symbol.

## Residual worklist (documented, not guessed)

**36 missing-override rows need real function bodies** (non-trivial ICF addresses) —
reference-less reconstruction, the wave-16 boundary. Several are `meta_ham` game classes
where `../og-dc3-decomp` is the right source, not synthesis:

- `*HeaderNode` family: `GetItemCount` (= `lwz r3,0x58(r3)`), `Select`,
  `UpdateItemCount`, `OnUnHighlight` ×4 classes
- `SongSortNode`/`MQSongSortNode`/`PlaylistSortNode::GetAlbumArtPath`
- `SongSort`/`PlaylistSort`/`FitnessCalorieSort::Handle`
- `RndPollAnim::{StartAnim,EndAnim,SetFrame}` (virtual-base adjustor thunks)
- `HighFiveGestureFilter`/`SigninScreen`/`FitnessFilterObj::SyncProperty`
- `CacheIDXbox::GetDeviceID`, `SampleInst360::EndLoopImpl`,
  panel `Enter`/`Exiting`/`FinishLoad`

**Documented floors (deliberately skipped, wide blast radius for zero layout change):**

- ~20 `??_R1` base-class-descriptor access-attribute divergences: `ObjPtrVec<T>` derives
  *privately* from `ObjRefOwner` in the original (20 instantiations); likewise
  `CacheXbox`/`ThreadCallback`, `XboxPurchaser` (`EA`=0x40 public vs `EN`/`EJ` private).
- `??_R2` virtual-base ordering sets (`HamNavList`, `UIComponent`, `UILabel`,
  `MeterDisplay`, `LabelShrinkWrapper`, `MiniLeaderboardDisplay`, `AppLabel`,
  `HamProfile`): target entries carry vdisp/`FA`=0x50 `BCD_VBOFCONTOBJ` where ours are
  direct bases; converting to virtual inheritance would move every offset.

## Open lead — ~~OPEN~~ **CLOSED, AND WRONG AS WRITTEN. See the 2026-08-19 follow-up below.**

> The paragraph that follows is preserved as written on 2026-08-18 and is a **misreading**.
> 0x823E3B70 is the generic 769-member empty-body ICF group, not a RefOwner body, and on PPC
> a `const` method returning `this` compiles to exactly `blr` (this arrives in r3, the return
> goes out in r3). **Our source was never wrong.** Independently re-verified from the PE.

`Hmx::Object::RefOwner`: the original's body at 0x823E3B70 is a bare `blr` — it falls off
the end without setting `r3`; ours is `return const_cast<Object *>(this);`
(Object.h:1264). (Commit `45d64fbe4`'s message mis-states ours as `return nullptr` —
the code was never changed; the PhysicsManager::RefOwner override fix is independently
map-proven.) The target/our semantic relationship here deserves its own look.

---

# Follow-up, 2026-08-19: the residual worked, on branch `fix/missing-override-bodies`

## The "Open lead" above is CLOSED — a non-issue

`Hmx::Object::RefOwner` is at 0x823E3B70 in `ham_xbox_r.map`, which is the **769-member
ICF group for the empty body `{ }`** — the address is a single `blr`, size 4. It is not a
function that "falls off the end without setting r3". On PowerPC, a `const` method that
returns `this` needs **zero** instructions: `this` arrives in r3 and the return value goes
in r3, so `return const_cast<Object *>(this);` compiles to exactly `blr` and then folds in
with every `void f() {}` in the binary. **Our source is correct; change nothing.** The
`??_7...` slot rows that look divergent here are naming noise: the target names the ICF
representative (`OnlyReturns`), we name `?RefOwner@Object@Hmx@@`, same address.

## Method used for the worklist (reproducible, does not need the scanner)

`data_symbol_scan.py` defaults to `--max-symbols 4000` and dropped 14,549 symbols as
`capped` on a full run, so it sees only a slice. A direct sweep is cheaper and complete
for the vtable question: for every `??_7` symbol in every unit, run
`objdiff-cli diff -u <unit> <sym> --include-data`, keep relocation rows whose `kind`
is not `equal` **and where target and base symbols resolve to different addresses in
`ham_xbox_r.map`** (equal addresses = proven ICF fold = benign). That gives an
unambiguous, dedup-able count.

**141 divergent vtable slots at `49ad7cfd5` → 52 after this branch.** `lazer/` is now
completely clean.

### Verification for metric-invisible fixes

Adding a *correct* override is usually invisible to objdiff: the split assigns each ICF
address to exactly one owning unit, so the same body added to a different unit reads as
base-only and is never scored. Verify instead by byte-comparing the compiled COMDAT in
`build/373307D9/src/<unit>.obj` against the target's words at the ICF address (from the
`build/373307D9/asm/` listings), comparing only the opcode field on words our object
carries a relocation for. Every function added on this branch was checked this way.

Two gotchas found while doing it: `build/373307D9/obj/` is the **target's** split objects,
`build/373307D9/src/` is **ours**; and two `.s` listings can claim the same address
(`0x829476B8` appears in `PracticeChoosePanel.s` at size 0x4 and in `OptionsPanel.s` at
size 0x1C), so pick by size, don't take the first grep hit.

## The scan's blind spot: name-matched slots with the WRONG BODY

A slot whose symbol resolves to the same address on both sides is filtered as an ICF fold
— which is right for missing overrides but **hides wrong bodies entirely**. Two real bugs
on this branch were found only by reading the ICF body:

- `ChallengeHeaderNode::Select` returned `gNullStr`; the target's is
  `SelectChildren(mChildren, mChallengeCount)`.
- `RndFontBase::BitmapFont` returned `true`; the original returns **false**, and only
  `RndFont` overrides it to true — so every `RndFont3d` claimed to be a bitmap font.

Whoever runs this next: after the slot sweep is clean, disassemble each ICF group your
classes participate in and check the body, not just the name.

## Residual: 52 divergent slots, by class

- **`SynapseAPO` (10)** and **`BinkMovieImpl` (4)** — external XAPO SDK base classes
  (`CXAPOBase`/`CXAPOParametersBase`) and a certified floor. Not workable here.
- **`StorePanel` (6), `AsyncFile::GetFileHandle`, `FileCacheHelper::CacheFile`** — the
  *target* holds `_purecall` where **we supply a body**. The original leaves these pure
  virtual. Removing our implementations is a native-port question, not a decomp one.
- **`RootContent` (8), `HamSongData::{OnTempo,OnTimeSig}`,
  `DisplacementNode::SkipFirstFrame` ×2, `MicNull`/`StreamNull`/`Sequence`/`Synth`/
  `StreamReceiver360`/`NetCacheMgrXbox` (9)** — the inverse of this branch's work: **we
  declare an override the original does not have** (the base symbol is absent from the map
  and the target slot holds a trivial ICF body). Mechanically the fix is to delete our
  override so the slot inherits, but only where the inherited body is byte-identical to
  the target's ICF body — each needs its own check, and several bodies are load-bearing
  for the native port.
- **`StandardStream` (5)** — a genuine **virtual-declaration ORDER** divergence in the
  `Stream` base: target `+0xc8`=`GetChannel`, `+0xd8`=`SetADSR(ADSRImpl)`,
  `+0xdc`=`SetJumpSamples` vs our `SetADSR`/`SetJumpSamples`/`GetSampleRate`. Reordering
  moves every slot below it; needs its own lane.
- **`JsonObject` / `RndVelocityBuffer` (2)** — `??_E` vs `??_G` deleting-destructor thunk
  naming at slot 0. Cosmetic ICF naming.
- **`HamDirector::PollEnabled`** — subtle: `HamDirector.h:150` declares a *non-virtual*
  `bool PollEnabled() const { return mPollEnabled; }`, but `RndPollable` declares
  `virtual bool PollEnabled() const`, so it **accidentally overrides**. The original has
  no `?PollEnabled@HamDirector@@` and its slot is `merged_Returns1` (always true). Real
  divergence; changing it touches native poll scheduling, so it is left flagged.

## Also settled

`../og-dc3-decomp` is **not** a source for any of these. Its `NavListNode.h`,
`SongSortNode.h`, `ChallengeSortNode.h`, `PlaylistSortNode.h`, `MQSongSortNode.h` and
`FitnessCalorieSortNode.h` are this repo's headers *minus the same overrides*, and in
places it is actively worse (`NavListFunctionNode::IsEnabled() { return IsEnabled(); }`
infinite-recurses; it also has `NavListSortNode`'s `IsEnabled` at 0x9c and `IsActive` at
0xa0, the reverse of ours — the target's own `??_7SongHeaderNode@@6B@` names
`?IsActive@SongHeaderNode@@UBA_NXZ` in slot `+0x9c`, so **ours is right and og's is
wrong**). Everything on this branch is reconstruction from target asm + the linker map.

## Not touched, still true

The `??_R1` access-attribute and `??_R2` virtual-base ordering floors above stand.

---

# Follow-up, 2026-08-19: the method is now a tool, and the count was 139 → 50

The "Method used for the worklist (reproducible, does not need the scanner)"
section above described a real procedure in prose. It is now
`mcp__orchestrator__run_symbol_sweep(kind="vtable_slots")` /
`python3 -m scripts.orchestrator.symbol_sweep --kind vtable_slots`
(`scripts/orchestrator/symbol_sweep.py`, branch `tools/mcp-capability-gaps`).

Re-running the published measurement through the tool, on freshly built trees at
both commits:

| | published (prose method) | tool |
|---|---|---|
| `49ad7cfd5` | 141 | **139** |
| after `fix/missing-override-bodies` | 52 | **50** |
| delta | 89 | **89** |

The delta is identical, and it agrees with `56ac7c6f0`'s own subject line
("89 vtable slots stop diverging"). Both endpoints are lower by exactly the same
**2**, and those two are identified:

* `??_7JsonObject@@6B@` and `??_7RndVelocityBuffer@@6B@`, slot `+0x0`,
  `??_G…` (target) vs `??_E…` (ours) — the deleting-destructor thunk pair this
  doc already called "Cosmetic ICF naming".

> **CORRECTED 2026-08-19.** The paragraph that stood here made two factual
> errors, and a real decomp bug was hiding behind them. `50` is still the
> correct residual — the conclusion survives, the reasoning did not.
>
> It claimed *"neither `??_G` nor `??_E` appears in `ham_xbox_r.map` at all"* and
> that they form *"one weak-alias group at `0x826B17E8`"*. Both are wrong, and the
> first is **internally inconsistent with the tool it was describing**: absence
> from the map is exactly the `unresolved-target` class
> (`symbol_sweep.py`, `cls = "wrong-target" if (ta and ba) else "unresolved-target"`,
> and `SLOT_CLASSES = {"wrong-target", "unresolved-target"}`), so if they really
> were absent the sweep would have **kept** both rows, not dropped them. The
> answer would have been 52, not 50.
>
> **All four are in `ham_xbox_r.map`**, in **two** groups, each pair at one address:
>
> ```
> 47873: ??_GJsonObject@@MAAPAXI@Z         82563bf0  net:JsonUtils.obj
> 47874: ??_EJsonObject@@MAAPAXI@Z         82563bf0  net:JsonUtils.obj
> 54065: ??_ERndVelocityBuffer@@EAAPAXI@Z  826b17e8  rndobj:VelocityBuffer.obj
> 54066: ??_GRndVelocityBuffer@@EAAPAXI@Z  826b17e8  rndobj:VelocityBuffer.obj
> ```
>
> The grep that "found nothing" searched the **`U`** mangling *our own build
> emits*, which is genuinely absent from the map. The map spells them **`M`**
> (protected) and **`E`** (private).
>
> ### The real finding: we declare both deleting destructors `public`
>
> That mangling difference is **not cosmetic — it is an unrecorded decomp bug.**
> MSVC encodes member access in the mangled name, and the original's is not ours:
>
> | | original (`ham_xbox_r.map`) | our build | access |
> |---|---|---|---|
> | `JsonObject` | `??_GJsonObject@@`**`M`**`AAPAXI@Z` | `??_GJsonObject@@`**`U`**`AAPAXI@Z` | **protected** vs public |
> | `RndVelocityBuffer` | `??_ERndVelocityBuffer@@`**`E`**`AAPAXI@Z` | `??_ERndVelocityBuffer@@`**`U`**`AAPAXI@Z` | **private** vs public |
>
> (`M` = protected virtual, `E` = private virtual, `U` = public virtual.) Both
> destructors sit under `public:` in our headers —
> `src/system/net/JsonUtils.h` (`virtual ~JsonObject()`) and
> `src/system/rndobj/VelocityBuffer.h` (`virtual ~RndVelocityBuffer()`) — where the
> original had them protected and private respectively. `docs/dc_symbols.txt:54004-5`
> independently records RndVelocityBuffer's as *"private: virtual"*; the ground
> truth was already in the tree.
>
> **It is invisible to every diff by construction**, not merely metric-invisible.
> Our `U` spelling has been baked into the symbol naming applied to the *target*:
>
> ```
> config/373307D9/symbols.txt:137107  ??_GJsonObject@@UAAPAXI@Z        = .text:0x82563BF0;
> config/373307D9/symbols.txt:147328  ??_GRndVelocityBuffer@@UAAPAXI@Z = .text:0x826B17E8;
> scripts/target_symbol_map.json      "0x82563bf0": "??_GJsonObject@@UAAPAXI@Z"
> scripts/target_symbol_map.json      "0x826b17e8": "??_GRndVelocityBuffer@@UAAPAXI@Z"
> ```
>
> so the split relabels the target's own symbols with *our* mangling and both sides
> then agree. `orig/373307D9/ham_xbox_r.map` is the only artefact in the tree where
> the original access survives. On top of that, **the ICF-fold drop rule introduced
> alongside this doc now hides the row in the vtable sweep as well** — which is
> precisely why this is written down here rather than left for the sweep to re-find.
> It also matches the project's standing rule in `CLAUDE.md`: *"Keep members
> protected/private unless confirmed public via DWARF or asserts."*
>
> **Follow-up (open), two parts:**
>
> 1. Tighten both destructors to match the original access. Not done on this
>    tooling branch — it is a source change with caller-visibility consequences
>    (a `protected` `~JsonObject` forbids `delete` through a `JsonObject*`) and
>    belongs in a lane that can rebuild and re-verify the dependents. Markers
>    left at both declaration sites so the next reader of the header finds this.
> 2. Correct the two `symbols.txt` / `target_symbol_map.json` entries once (1) is
>    done, so the target stops being relabelled with our mangling. Note that
>    editing `config/373307D9/symbols.txt` re-triggers SPLIT, and `dtk xex split`
>    must remain a fixed point of its input — see
>    [docs/tools/BUILD_SYSTEM.md](../tools/BUILD_SYSTEM.md). Do the two together
>    or the build and the map will disagree in the other direction.
>
> **Method note for whoever audits this class next:** the lane that wrote the
> original paragraph grepped the mangling *its own build emits*. Any "absent from
> `ham_xbox_r.map`" claim about a member function should be re-run across the
> access letters (`A`/`E`/`I`/`M`/`Q`/`U` …) or against the demangled name in
> `docs/dc_symbols.txt`, which is access-annotated in plain English and would have
> answered this in one grep.

The fold that the sweep actually proves is the one in
`build/373307D9/icf_aliases.map`, which speaks our build's `U` mangling and so
resolves both names in each pair to a single address — **two** weak-alias groups,
`??_GJsonObject@@UAAPAXI@Z`/`??_EJsonObject@@UAAPAXI@Z` at `0x82563BF0` and
`??_GRndVelocityBuffer@@UAAPAXI@Z`/`??_ERndVelocityBuffer@@UAAPAXI@Z` at
`0x826B17E8`. `ham_xbox_r.map` independently proves the same two folds under the
`M`/`E` spellings. Either map is sufficient; reading both is what drops the rows.
**50 is the correct residual**; the doc's own prose verdict on those two rows was
right and its arithmetic was two high.

The tool also reports the denominator the prose method never stated: the
universe is **5,132** `(unit, symbol)` pairs matching `??_7*` in the target split
objects, of which **2,863 are examined** and **2,269 are undefined external
references** (COFF section index 0 — vtables a TU merely *uses*; objdiff answers
"Symbol not found in target" for those). `lazer/` measures **0** divergent slots,
confirming this doc's "lazer/ is now completely clean".

Two things the prose method did not separate, which the tool reports as a second
tier: `insert`/`delete` relocation rows, where only one side has a slot at all.
There are 1,078 of them binary-wide (1,027 target-only, dominated by `??_R4`
RTTI-locator slots; 51 base-only, e.g. `FlowRun`'s `Object` sub-object vtable
where `target_size` is 44 and ours is 88). They are real signal but they are not
slot-for-slot divergences and were never part of the 141/52 count.

The blind spot this doc names — "name-matched slots with the WRONG BODY" — is
**unchanged**. The tool filters equal-address rows exactly as the prose method
did, so it still cannot see a wrong body behind a right name. Whoever picks that
up still needs the ICF-group disassembly step described above.
