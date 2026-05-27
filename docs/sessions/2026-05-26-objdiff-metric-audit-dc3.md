# Session 2026-05-26 — objdiff fuzzy-metric audit (DC3 port)

## What this is

A port of the objdiff fuzzy-metric audit that just ran on the **RB3 decomp**
sister project (`/home/free/code/milohax/rb3/docs/sessions/2026-05-26-objdiff-metric-audit.md`)
to the **DC3 decomp** here. The deliverable is a self-contained handoff: the
audit found ~82 in-scope functions where the headline `match_percent_normalized`
counted the function as 100%-matched while it was actually hiding real
semantic differences (wrong constants, wrong struct offsets, wrong vtable
slots, wrong called functions). This doc explains the bug, lists the candidates,
and provides ready-to-paste dispatch prompts for fix-work.

The same metric bug bit RB3 — fixing 6 of its top candidates surfaced **5 real
runtime bugs** (wrong virtual called, class size off by 12 bytes, wrong save-
stream field order, wrong switch labels, wrong sentinel value). DC3's
shared-engine candidates are likely the same bugs since the Milo engine is
shared between the two projects.

## Bug class — what the metric was masking

objdiff produces two scores per function:

```
max_score                = instruction_count × PENALTY_INSERT_DELETE (100)
diff_score               = sum of per-instruction penalties
match_percent            = 1 − diff_score / max_score                    (raw)
match_percent_normalized = 1 − (diff_score − arg_diff_score) / max_score
```

`match_percent_normalized` is the field `report.json` reports per-function and
is used to gate `matched_functions`. The bug: when opcodes matched but an
*argument* differed, the penalty was added to **both** `diff_score` AND
`arg_diff_score` regardless of what kind of argument:

| Arg type | Truly benign for a port? | Was folded into `arg_diff_score`? |
|---|---|---|
| **Registers** | Yes — host compiler reallocates | yes |
| **Branch destinations** | Yes — relative-layout noise | yes |
| **Immediates** (`li r3, 5`, `lwz r3, 0x18(r4)`) | **NO** — these are wrong constants, wrong struct field offsets, wrong vtable slot indices | yes |
| **Relocations** (called function / global) | Mostly noise (pool addends, ICF aliases), but real wrong-callee diffs hide here | yes |

So a function with *only* wrong constants / wrong member offsets / wrong vtable
slots / wrong called functions normalized to **100%** — counted as matched.

**These diffs survive into a native port.** Register allocation gets rebuilt
by the host compiler; constants/offsets/vtable-slots stay exactly as written
in source. The metric was systematically hiding the real-bug class while
giving full credit for the merely-cosmetic class.

For the full mechanism breakdown including the two conflicting "normalized"
definitions across objdiff modules, see the RB3 session doc linked above.

## Setup verification

### objdiff binary status — **UPDATED 2026-05-26**

`bin/objdiff-cli` was swapped from the stale Mar-24 file to a symlink at
`/home/free/code/milohax/objdiff/target/release/objdiff-cli` (matching RB3's
convention). Old binary preserved at `bin/objdiff-cli.old-2026-05-26`.
report.json regenerated against the honest metric.

| Measure | Before (stale) | After (honest metric) | Δ |
|---|---:|---:|---:|
| `matched_functions` | 27,785 | **27,595** | −190 |
| `matched_functions_percent` | 59.08% | **57.00%** | −2.08pp |
| `matched_code_percent` | 42.33% | 42.12% | −0.21pp (raw-gated, mostly stable) |
| `fuzzy_match_percent` | 52.55% | 52.29% | −0.27pp |
| `total_functions` | 47,032 | **48,413** | +1,381 (fresh objdiff enumerates more symbols) |

The 190-function drop is honest reclassification: those fns had immediate
diffs (wrong constants/offsets/vtable slots) that the old metric was folding
into `arg_diff_score`. They're still in the codebase; they're just no longer
counted as "matched." The total-functions bump (+1,381) is from the newer
objdiff version's symbol enumeration finding more symbols to score.

The per-function audit results below were already produced via the
metric-tweak build (the audit script pins to it directly), so they're
unchanged by this rebuild. The dashboard numbers are now consistent with
the audit's per-function classifications.

### Audit script status

`scripts/analysis/audit_normalized_masking.py` already exists in DC3 (ported
during this session). Same auditor as RB3 with three DC3-specific tweaks:

- Pins to `../objdiff/target/release/objdiff-cli` (the metric-tweak build)
  via `OBJDIFF_CANDIDATES`. Now redundant since `bin/objdiff-cli` was
  symlinked to the same path on 2026-05-26, but kept as a defensive fallback.
- No paired-singles opcodes (`psq_l`/`psq_st`) — Xbox 360 PowerPC is plain PPC.
- Doubleword load/store (`ld`, `std`, `ldu`, `stdu`) added to the LOADSTORE set
  — MSVC PPC uses these for 8-byte spills.
- `SDA_REGS = {"r2"}` — Xbox 360 doesn't materialise SDA-relative addressing
  the way MetroWerks Wii does; r2 is the TOC pointer only.
- Symbol-name normaliser collapses MSVC-mangled pool noise: `__real@40400000`,
  `__xmm@...`, `??_C@_<...>` string literals.

**Bug fix shipped this session**: `extra_frame_regs` (set of MSVC PPC frame
pointer registers, usually r31) was computed by `detect_frame_ptr_regs` but
never passed to `value_sig`, so functions with large stack frames where r31 =
sp-N were falsely flagged with dozens of "member-offset" diffs that were
actually stack-locals. Fixed by threading the set through and by detecting
frame-pointer setup on BOTH target and base sides (target might use r31 while
source uses r30). After the fix, CamShot::Load et al. drop from 5+ member
concerns to 2 reloc_target concerns, and BENIGN count rose 203→217 / REVIEW
dropped 204→190.

## Audit results (post-fix)

Run: `python3 scripts/analysis/audit_normalized_masking.py --workers 6`

```
inflated (norm==100, raw<100):  408
  BENIGN  :  217   (pure register/reorder/frame/SDA noise)
  REVIEW  :  190   (has reloc-target / member-offset / constant diffs)
  errors  :    1   (?FillCompressedVertex — symbol missing in target obj)
```

Raw REVIEW set was passed through six additional noise filters (all
universal-benign by construction):

| Noise class | Drops | Why benign |
|---|---:|---|
| `lbl_8XXXXXXX` vs named global (`gRev`, `?$S<N>@...`) | 766 | Target binary lost the symbol name during disassembly; same address |
| `??_7<X>` / `??_8<X>` / `??$?5/6` ICF aliases | 29 | MSVC vftable/vbtable/`<<` template instantiations folded by ICF to a different name |
| `__savegprlr_N` / `__restgprlr_N` | 28 | Callee-saved count differs → different outline helper called |
| `?$S<N>@<scope>` static-discriminator drift | 25 | Function-local static count off by ±1 (extra/missing static decl) — usually a symbol you can add or remove for asm match, but no runtime semantic change |
| `bl merged_<addr>` ICF alias | 20 | Linker folded identical-code functions; same machine code, different symbolic name |
| `bl fn_8XXXXXXX` ↔ `bl ??3@`/`??_V@`/`??_G` | 8 | Target binary's unrecovered `operator delete` / `operator delete[]` / typeinfo |
| `??__F<func>` / `??_C@_` pool entries | 8 | MSVC `__FUNCSIG__` static-init flags and string-literal pool entries — layout-dependent |
| `bl OnlyReturns` thunk alias | 4 | ICF-folded empty virtual overrides; identical code |

| Port-scope category | REVIEW count |
|---|---:|
| `system/` (Milo engine — IN-scope) | 139 |
| `lazer/` (game code — IN-scope, except `net_ham/`) | 29 |
| `system/{os,net,synth_xbox,rndxenon}` (Xbox glue — OUT) | 10 |
| `lazer/net_ham` (networking — OUT) | 3 |
| `xdk/` (Xbox SDK — OUT) | 0 in the REVIEW set |

**After full noise filter + port-scope filter: 82 in-scope candidates.**
Most-severe shortlist persisted at `/tmp/objdiff_audit/dc3_curated.json`.

## Validated bug candidates (in-scope, asm-evidenced)

These are the top REVIEW outputs after manual triage. They all need normal
decomp-workflow confirmation (read source, look at headers, check vtable
layout against DWARF), but the asm divergence is concrete.

### Class member-offset / struct-layout (HIGH confidence)

| # | Function | Unit | Asm signal | Bug class |
|---|---|---|---|---|
| 1 | `RndCam::UpdateLocal` | `system/rndobj/Cam` | `stfs f*, 0x158, r31` vs `0x160, r31`; `0x124` vs `0x130` (r31 = `this`, not frame) | Member offset shift ~8B somewhere in RndCam (Transform/Matrix4 sub-object placement). Header says `mLocalProjectXfm @ 0x100`, but uses don't add up — verify against DWARF. |
| 2 | `MoggClip::Play` | `system/synth/MoggClip` | `lwz r3, 0x48, r30` (`r30 = this − 0x2c`) vs `lwz r3, 0x1c, r31` (`r31 = this`) | Multiple-inheritance sub-object cast missing in source. Target casts `this` down to MoggClip's primary base then accesses offset 0x48 of *that*; source goes through `this` direct. Either MI base order or missing `static_cast<Synth*>` etc. |
| 3 | `MoggClip::ApplyLoop` | `system/synth/MoggClip` | `lwz r11, 0xdc(r11)` vs `0xd8(r11)` | 4-byte member shift |
| 4 | `MoggClip::Save` | `system/synth/MoggClip` | `lfs f0, 0x40, r30` vs `0x44, r30` | 4-byte member shift (sibling to above) — probably one float field moved across two siblings |
| 5 | `HamVisDir::~HamVisDir` | `system/hamobj/HamVisDir` | `lwz r10, 0x10(r11)` vs `0x8(r11)` (3×) | 8-byte member shift in `HamVisDir` — possibly extra base or missing field |
| 6 | `Spotlight::Generate` | `system/world/Spotlight` | `lbz r11, 0x4(r30)` vs `lbz r11, 0x1f8(r31)` | Wrong `this` register AND wrong offset — likely an MI cast issue, accessing the wrong sub-object |
| 7 | `CharUpperTwist::PollDeps` | `system/char/CharUpperTwist` | `lwz r11, 0x14, r31` vs `0x28, r31`; `0x28` vs `0x3c` | 20-byte member shift, paired — looks like an early field is 20 bytes too big or one is missing |
| 8 | `StreamRecorder::Poll` | `system/gesture/StreamRecorder` | `lwz r10, 0x1ac(r11)` vs `0x9c(r11)` (`r11 = this`, then `lwz r11, 0x4(r10)` vs `0x18(r10)`) | Two-level member: `this->field1` at offset 0x1ac vs 0x9c (~272B shift), then field2 0x4 vs 0x18 — likely wrong struct/typedef for the indirected member |
| 9 | `UILabel::OnSetHeightFromText` | `system/ui/UILabel` | `lwz r4, 0xc4(r31)` vs `0x90(r31)` | 52-byte member shift in UILabel layout |
| 10 | `Splash::{Suspend,Resume,EndSplasher,UpdateThread}` | `system/movie/Splash` | `lwz r11, 0x13c(r11)` vs `0x138(r11)` (or swapped) | 4-byte member shift; 4 sibling functions agree — single field in Splash off by 4 |
| 11 | `Character::Draw` (`DrawPtrVec::Draw`) | `system/char/Character` | `lwz r11, 0x14(r11)` vs `0x18(r11)` | 4-byte member shift inside DrawPtrVec |
| 12 | `UIListSubList::Draw` | `system/ui/UIListSubList` | `lwz r11, 0x28(r11)` vs `0x2c(r11)` | 4-byte shift in UIListSubList |
| 13 | `StoreEnumeration::IsSuccess` (`XboxEnumeration`) | `system/meta/StoreEnumeration` | `lbz r3, 0x1c(r31)` vs `0x24(r31)` | 8-byte shift in XboxEnumeration |
| 14 | `FlowSetProperty::SetProperty` (`PropertyTask::SetProperty`) | `system/flow/FlowSetProperty` | `lwz r11, 0x78(r3)` vs `lwz r11, 0x4(r4)` | Wrong `this` register and offset — semantic mismatch; possibly wrong overload or wrong arg passed |
| 15 | `NetCacheMgr::PollLoaders` | `system/utl/NetCacheMgr` | `lwz r11, 0x8(r31)` vs `0x10(r30)` | Wrong register and 8-byte shift — out-of-port-scope but signal of bigger structural mismatch in NetCacheMgr |

### Wrong called function — possibly wrong virtual / wrong helper

| # | Function | Unit | Asm signal | Suspected bug |
|---|---|---|---|---|
| 16 | `AccomplishmentProgress::AddAccomplishment` | `lazer/meta_ham/AccomplishmentProgress` | TGT: `bl ?Notify@Debug`, `bl ?Parent@Node@ObjPtrVec`, `bl ?GetGroup@AccomplishmentCategory`, `bl ?PostDownload@NetLoader`; SRC: `bl ??6TextStream`, `bl ?GetPadNum@Profile`, `bl ?GetAward@AccomplishmentGroup`, `bl ?MakeDirty@Profile` | **Four** wrong-function calls in this routine. Source is calling completely different helpers. Logic shape is wrong — needs source review. |
| 17 | `AccomplishmentManager::Poll` | `lazer/meta_ham/AccomplishmentManager` | `bl ?GetAccomplishmentProgress@HamProfile@@QBA` (const) vs `bl ?AccessAccomplishmentProgress@HamProfile@@QAA` (mutable) | Wrong overload — calling the **non-const** Access instead of const Get. Possibly intentional, but semantic difference. |
| 18 | `AccomplishmentManager::IsAvailable` | `lazer/meta_ham/AccomplishmentManager` | TGT `bl ?GetMaxProcess@CFEWaveStreamDecoder@NUISPEECH` vs SRC `bl ?GetDynamicPrereqsNumSongs@Accomplishment@@QBAHXZ` | Target's call IS bound to the ICF-folded `?GetMaxProcess@CFEWaveStreamDecoder` thunk (returns some int). Source calls a real function. Validate with vtable inspection — could be a real wrong virtual. |
| 19 | `ProfileMgr::Poll` | `lazer/meta_ham/ProfileMgr` | `bl ?Parent@Node@?$ObjPtrVec@VRndTransformable@@...` vs `bl ?GetPadNum@Profile@@QBAHXZ` | Looks identical to RB3's `Profile::GetPadNum` issue — wrong call inside a profile loop. |
| 20 | `KinectSharePanel::OnUpload` / `OnPostLink` | `lazer/meta_ham/KinectSharePanel` | Same `bl ?Parent@Node@ObjPtrVec...` vs `bl ?GetPadNum@Profile` pattern | Likely same root cause as ProfileMgr::Poll above |
| 21 | `Leaderboards::ShowGamercard` | `lazer/meta_ham/Leaderboards` | Same `?Parent@Node@ObjPtrVec` vs `?GetPadNum@Profile` | Same pattern — these 3 sites likely share a header-level helper signature drift |
| 22 | `DataArray::~DataArray` | `system/obj/DataArray` | `bl ??_GVocalEvent@MidiParser@@QAAPAXI@Z` vs `bl ??_GDataNode@@QAAPAXI@Z` | Target binds the scalar-deleting destructor of `MidiParser::VocalEvent` (ICF-folded into the same address as `DataNode::~scalar-del-dtor`). Likely benign ICF, but confirm. |
| 23 | `CharBone::Load` | `system/char/CharBone` | `bl ??_DRndTransformable@@` vs `bl ??_DRndTransformableRemover@@` | `??_D` is a vbase destructor helper. Wrong base class destructor flavour — possibly an MI vbase issue |
| 24 | `WorldCrowd::Load` | `system/world/Crowd` | `bl ?OnGetOccluded@CamShot@@IAA` vs `bl ?OnRebuild@WorldCrowd@@IAA` | Target binds to `CamShot::OnGetOccluded` (ICF-folded with something here?), source binds to the legitimate `WorldCrowd::OnRebuild`. Validate ICF or genuine wrong-method. |
| 25 | `MetaPerformer::PopulatePlaylistSongProvider` | `lazer/meta_ham/MetaPerformer` | TGT `bl ?GetRequest@HttpReqCurl@@UAA` vs SRC `bl ?Title@HamSongMetadata@@QBAPBDXZ` | Target's symbol is a *completely* unrelated function — almost certainly ICF noise (`HttpReqCurl::GetRequest` and `HamSongMetadata::Title` happen to fold). Validate via address. |
| 26 | `Crowd::Load`, `BustAMovePanel::CacheObjects`, `OptionsPanel::OnMsg`, `HamNavList::NumItems/GetDisabledCount` | various | `bl ?GetContainerName@MemcardXbox`, `bl ?gathering@CUgtFilter@NUISPEECH`, `bl ?SongBlock@SongMetadata` | All have the shape "target calls an unrelated cross-module function." These are almost certainly **MSVC ICF aliases** — the linker folded multiple identical-bodied functions and bound the call site to whichever symbol got dedup-elected. Cross-check the address in `default.map` or the binary; if same address, it's benign ICF. |

### Wrong logic constant (HIGH confidence — these change behaviour)

| # | Function | Unit | TGT vs SRC | Likely bug |
|---|---|---|---|---|
| 27 | `ParticleCommonPool::InitPool` | `system/rndobj/Part` | `lis r11, 0x147` / `ori r11, r11, 0xae14` (= 0x147ae14, decimal **21,490,196**) vs `0x141 / 0x4141` (= 0x1414141, decimal 21,053,761); `mulli r3, r3, 0xc8` vs `0xcc`; `addi r9, ..., 0xc8` vs `0x68` | Multiple wrong constants in particle pool initialiser — pool size, particle size, struct size. Real bug. |
| 28 | `OptionsPanel::OnMsg(RCJobCompleteMsg)` | `lazer/meta_ham/OptionsPanel` | `lis r10, 0xa` vs `0x800a` (~big-vs-small offset); `ori r10, r10, 0x2` vs `0x3`; `cmplwi r11, 0x1` vs `0x4` | Three wrong constants — possibly wrong RC job status enum values or wrong handler dispatch index |
| 29 | `UIList::UpdateExtendedEntries` | `system/ui/UIList` | `li r11, 0x3ef` vs `0x3fd`; `0x3f6` vs `0x404`; `0x401` vs `0x40f`; `0x40c` vs `0x41a` | Uniform offset of `+14 (0x0e)` across all four constants — looks like an enum / token base value shift. Real bug or pool layout? The constants land in r11 immediately before a store — suspect they're MILO_ASSERT line numbers (likely **benign** — line drift only). Validate by reading the source. |
| 30 | `FxSend::Save` | `system/synth/FxSend` | `lis r11, 0x8 / ori r11, r11, 0x7` (=0x80007) vs `lis r11, 0x7 / ori r11, r11, 0x8` (=0x70008) | Two constants swapped — probably a version/rev pair like `(rev=8, alt_rev=7)` written backwards in the source. Real bug. |
| 31 | `ObjectDir::PostLoad` | `system/obj/Dir` | `li r11, 0x45d` vs `0x466` | Single line-number-like constant drift; could be MILO_ASSERT line, validate. |
| 32 | `yylex` (DataFlex/lex) | `system/obj/DataFlex` | `cmpwi r8, 0x7d` vs `0x7b`; `cmplwi r11, 0x266` vs `0x1db`; `bl gen_yystate_82xxx_NNN` vs different — also two `cmpwi` against state-machine constants | This is a generated lexer state machine (flex/yacc). Wrong constants here = wrong parser. Validate against the original `.l` grammar source. Could be very impactful or could be regeneration drift. |
| 33 | `yy_get_previous_state` | `system/obj/DataFlex` | `cmpwi r7, 0x7d` vs `0x7b` (2×) | Same lexer family — same constant difference, smaller blast radius |
| 34 | `RndShader::SelectConfig` | `system/rndobj/Shader` | `li r10, 0x0` vs `li r11, 0x1` | Wrong register AND wrong value — could be an inverted bool or wrong shader-config enum default |

### Cross-reference with RB3

The audit found a few items where DC3 and RB3 share the engine code and the
bug class:

| DC3 candidate | Likely RB3 equivalent | Notes |
|---|---|---|
| `MoggClip::Play` (MI sub-object cast) | RB3 has its own `MoggClip` — check if same | Shared engine |
| `RndCam::UpdateLocal` (member shift) | RB3 `RndCam` ditto — header `system/rndobj/Cam.h` shared | Co-fix opportunity |
| `Spotlight::Generate` and `Spotlight::Load` | RB3 already validated `SpotDrawParams::Load` wrong field-read order (`feedback_spotlight_load_*`) — different function in DC3 (Spotlight vs SpotDrawParams) but same family | RB3 caught a related bug; check if Spotlight class is shared |
| `ObjectDir::PostLoad` constant drift | RB3 `ObjectDir::PostLoad` ditto | Shared `system/obj/Dir.cpp` |
| `DataArray::~DataArray` ICF | RB3 will have a similar `__dt__9DataArrayFv` — verify same | Shared `system/obj/DataArray.cpp` |
| `CharBone::Load` vbase dtor | RB3 has `CharBone::Load` — `system/char/CharBone.cpp` shared | Header-level fix would affect both |
| `OptionsPanel::OnMsg` wrong constants | RB3 OptionsPanel is game-specific | Probably NOT shared |
| `AccomplishmentProgress::AddAccomplishment` wrong helpers | RB3 has its own `AccomplishmentProgress` | Game-specific |

Cross-fixing the shared-engine bugs (`RndCam`, `MoggClip`, `Spotlight`,
`ObjectDir`, `CharBone`) on whichever project lands first lets the other
project port the fix verbatim. Mark these as "DC3↔RB3 co-fix" candidates.

## Dispatch-ready agent prompts

Copy-paste each block into a fresh Claude Code session in the DC3 repo. Each
prompt is self-contained.

> **Universal preamble for all agents**: Always build via `ninja` (DC3 doesn't
> use the `ninja-locked` wrapper — its concurrent-build safety comes from a
> different mechanism; just run `ninja` directly). Read-only investigation
> first (Ghidra/m2c via `bin/analyze-function`), then minimal source edit,
> then validate match% with `mcp__orchestrator__run_objdiff`.

---

### Agent 1 — RndCam::UpdateLocal member-offset shift

**Symbol**: `?UpdateLocal@RndCam@@IAAXXZ`
**Unit**: `default/system/rndobj/Cam`
**File**: `src/system/rndobj/Cam.cpp`, `src/system/rndobj/Cam.h`
**Raw match%**: 99.95% (norm masked to 100%, dropping post-rebuild)

```
[member] TGT stfs f0,  0x158, r31        SRC stfs f0,  0x160, r31
[member] TGT stfs f13, 0x124, r31        SRC stfs f13, 0x130, r31
[member] TGT stfs f0,  0x118, r31        SRC stfs f0,  0x120, r31
[member] TGT stfs f0,  0x164, r31        SRC stfs f0,  0x170, r31
```

r31 holds `this` (no `subi r31, r1, N` prologue), so these are class members.
Pattern: target ≈ source − 8 across all four offsets.

**Header** `src/system/rndobj/Cam.h` declares:
```
Transform mInvWorldXfm;          // 0xc0
Transform mLocalProjectXfm;      // 0x100
Transform mInvLocalProjectXfm;   // 0x140
Transform mWorldProjectXfm;      // 0x180
Transform mInvWorldProjectXfm;   // 0x1c0
float mNearPlane;                // 0x2c0
float mFarPlane;                 // 0x2c4
float mYFov;                     // 0x2c8
```

The diff is around `0x118-0x170` so somewhere in the four `Transform` block.
A `Transform` is **0x40 bytes** in source.

**Tasks**:
1. Run `/struct-check RndCam` (or `/ghidra-decompile UpdateLocal@RndCam`) to
   compare the declared header layout against DWARF.
2. If a member is mis-sized or mis-ordered, fix in the header. **`grep -rn` the
   blast radius first** — `RndCam` is used by every camera-touching function.
3. Build, then `mcp__orchestrator__run_objdiff` UpdateLocal to confirm.
4. Report before/after match% and what was wrong.

**Rule**: do not introduce padding hacks. Find the actual mis-declared member.

---

### Agent 2 — MoggClip::Play MI sub-object cast

**Symbol**: `?Play@MoggClip@@UAAXM@Z`
**Unit**: `default/system/synth/MoggClip`
**File**: `src/system/synth/MoggClip.cpp`, `src/system/synth/MoggClip.h`
**Raw match%**: 99.44%

```
TGT: subi r30, r3, 0x2c          # r30 = this - 0x2c   (cast to a primary base)
     mr   r31, r3                 # r31 = this
     ...
     lwz  r3, 0x48, r30           # access member at offset 0x48 of base
     stfs f0, 0x40, r30
     lfs  f13, 0x44, r30
SRC: (no `subi r30, r3, ...`)
     lwz r3, 0x1c, r31            # access member at offset 0x1c of this
     stfs f0, 0x18, r31
     lfs f13, 0x14, r31
```

Source goes through `this` directly. Target casts down to a sub-object first.

**Hypotheses (priority order)**:
1. MoggClip's multiple-inheritance base order in source is wrong: target's
   `this` is the *secondary* base; primary base sits at `this - 0x2c`. Our
   source has the bases declared in the wrong order, so the secondary IS the
   primary in our build.
2. The source is missing an explicit cast to a base type when accessing the
   members.

**Tasks**:
1. Run `/vtable MoggClip` to dump the MWCC/MSVC class layout and base offsets.
2. Look at `MoggClip` header — check `class MoggClip : public Synth, public Foo`
   declaration order vs the inheritance order DWARF reports.
3. Try swapping base order if MI; otherwise hunt for a missing
   `static_cast<TheBase*>(this)` in the .cpp.
4. Build, `mcp__orchestrator__run_objdiff` Play, ApplyLoop, Save (all 3 share
   the bug). Report before/after.

---

### Agent 3 — Splash 4-byte member swap (4 functions)

**Symbols** (all in `default/system/movie/Splash`):
- `?Suspend@Splash@@QAAXXZ`
- `?Resume@Splash@@QAAXXZ`
- `?EndSplasher@Splash@@QAAXXZ`
- `?UpdateThread@Splash@@IAAXXZ`

```
TGT lwz r11, 0x138, r11   ↔   SRC lwz r11, 0x13c, r11     (Resume, EndSplasher)
TGT lwz r11, 0x13c, r11   ↔   SRC lwz r11, 0x138, r11     (Suspend, UpdateThread)
```

The diff toggles in pairs — looks like **two adjacent 4-byte members are
swapped** in the `Splash` class. Probably an `mFoo` / `mBar` pair where the
source order is inverted vs target. 0x138 and 0x13c are 4 bytes apart.

**Tasks**:
1. Find `Splash` class definition; identify the two members around `0x138/0x13c`.
2. Swap their declaration order.
3. Build, re-diff all 4 symbols. Report before/after for each.

This should be a **single edit fixing 4 functions** — high ROI.

---

### Agent 4 — AccomplishmentProgress::AddAccomplishment wrong helpers (4 calls)

**Symbol**: `?AddAccomplishment@AccomplishmentProgress@@QAA_NVSymbol@@@Z`
**Unit**: `default/lazer/meta_ham/AccomplishmentProgress`
**Raw match%**: 99.15%

```
TGT:                                          SRC:
bl ?Notify@Debug@@QAAXPBD@Z                   bl ??6TextStream@@QAAAAV0@PBD@Z
bl ?Parent@Node@?$ObjPtrVec@...               bl ?GetPadNum@Profile@@QBAHXZ
bl ?GetGroup@AccomplishmentCategory@@QBA...   bl ?GetAward@AccomplishmentGroup@@QBA...
bl ?PostDownload@NetLoader@@IAAXXZ            bl ?MakeDirty@Profile@@QAAXXZ
```

This isn't a single wrong call — it's **four wrong calls in one function**.
The source's logic shape is fundamentally different. Likely the source was
written from RB3 reference (different class layout / different helper API).

**Tasks**:
1. `/dc3-pair AddAccomplishment@AccomplishmentProgress` — find if RB3 has it.
2. `/analyze-function ?AddAccomplishment@AccomplishmentProgress@@QAA_NVSymbol@@@Z -u default/lazer/meta_ham/AccomplishmentProgress`
3. Read the target asm carefully. The function appears to:
   - `Debug::Notify(...)` log a message (not `TextStream << ...`)
   - Walk an `ObjPtrVec<RndTransformable,ObjectDir>` parent chain
   - Call `AccomplishmentCategory::GetGroup` (not `AccomplishmentGroup::GetAward`)
   - Notify `NetLoader::PostDownload` (not `Profile::MakeDirty`)
4. Rewrite the source to match. Validate with objdiff.
5. **Likely real runtime bug** — wrong helpers being called means the wrong
   completion logic fires when an accomplishment is added.

---

### Agent 5 — ParticleCommonPool::InitPool wrong constants

**Symbol**: `?InitPool@ParticleCommonPool@@QAAXXZ`
**Unit**: `default/system/rndobj/Part`
**File**: `src/system/rndobj/Part.cpp` (search for `ParticleCommonPool` and `InitPool`)
**Raw match%**: 99.38%

```
TGT:                                  SRC:
lis    r11, 0x147                     lis    r11, 0x141
ori    r11, r11, 0xae14               ori    r11, r11, 0x4141
   → constant 0x147ae14 (21490196)       → constant 0x1414141 (21053761)

mulli  r3, r3, 0xc8                   mulli  r3, r3, 0xcc
   → struct size 200 bytes               → struct size 204 bytes

addi   r6, r9, 0xc8                   addi   r6, r9, 0x68
   → stride 200                          → stride 104
```

Three wrong constants. The `0x147ae14` looks like it might be a magic
allocation tag or pool ID. `0xc8` vs `0xcc` is **the particle struct size**
(`sizeof(Particle)` = 0xc8 = 200, source has 0xcc = 204 — 4 bytes too large).
The `0x68` vs `0xc8` stride is a sibling — same struct walked at a different
stride.

**Tasks**:
1. Find `class ParticleCommonPool`. Identify the particle struct it pools.
2. Likely fix: a member in `Particle` (or its base) is 4 bytes too large or
   one extra. Diff against DWARF.
3. Build, re-diff. **Real bug** — running with wrong stride / wrong struct
   size in a pool init = use-after-free / wrong-data-in-wrong-slot at runtime.

---

### Agent 6 — Spotlight class & related (3 fns)

**Symbols** (all in `default/system/world/Spotlight`):
- `?Load@Spotlight@@UAAXAAVBinStream@@@Z`
- `?Generate@Spotlight@@IAAXXZ`
- `?PreLoad@LightHue@@UAAXAAVBinStream@@@Z` (related, same `Load`-family bug)

Spotlight::Generate has:
```
TGT lbz r11, 0x4, r30      SRC lbz r11, 0x1f8, r31
```
where r30 and r31 are *different* `this`-related registers. Suggests an MI
cast issue similar to MoggClip.

Spotlight::Load has stack-layout shifts (r31 = frame ptr, so member-style
offsets are stack offsets — investigate separately).

**Tasks**:
1. `/vtable Spotlight` — check class layout / base order.
2. RB3 has `feedback_spotlight_load_wrong_fields` — see if same bug class.
3. **DC3↔RB3 co-fix candidate** — coordinate with whoever fixes the RB3
   `Spotlight` if active.

---

### Agent 7 — UIList::UpdateExtendedEntries constant drift

**Symbol**: `?UpdateExtendedEntries@UIList@@IAAXABVUIListState@@@Z`
**Unit**: `default/system/ui/UIList`
**Raw match%**: 99.56%

```
TGT: li r11, 0x3ef    SRC: li r11, 0x3fd       (Δ = +14)
TGT: li r11, 0x3f6    SRC: li r11, 0x404       (Δ = +14)
TGT: li r11, 0x401    SRC: li r11, 0x40f       (Δ = +14)
TGT: li r11, 0x40c    SRC: li r11, 0x41a       (Δ = +14)
```

Uniform +14 (`0x0e`) drift across 4 constants. **Most likely** these are
MILO_ASSERT line numbers and the source file is exactly 14 lines longer than
target near these asserts. Verify before chasing:

1. Find the four `MILO_ASSERT(...)` calls in `UpdateExtendedEntries`.
2. Check the source line numbers vs the target asm line numbers.
3. If line numbers are the only discrepancy: **benign**, no fix needed beyond
   adding `// blah` comments to align line counts. Mark as at-limit.
4. If constants are NOT line numbers: report the actual semantics.

---

### Agent 8 — CharUpperTwist::PollDeps 20-byte member shift

**Symbol**: `?PollDeps@CharUpperTwist@@UAAXAAV?$list@...@@0@Z`
**Unit**: `default/system/char/CharUpperTwist`

```
TGT lwz r11, 0x14, r31     SRC lwz r11, 0x28, r31    (Δ = +0x14 = 20B)
TGT lwz r11, 0x28, r31     SRC lwz r11, 0x3c, r31    (Δ = +0x14 = 20B)
```

20-byte shift suggests one extra member (maybe a `Vector3` or a `Color` =
~16B + padding) early in `CharUpperTwist` or its base.

**Tasks**:
1. `/struct-check CharUpperTwist`
2. Identify the extra/missing member.
3. Fix in header (after grep for blast radius).
4. Build, re-diff. Report.

---

### Agent 9 — UILabel::OnSetHeightFromText 52-byte member shift

**Symbol**: `?OnSetHeightFromText@UILabel@@IAA?AVDataNode@@PAVDataArray@@@Z`
**Unit**: `default/system/ui/UILabel`

```
TGT lwz r4, 0xc4, r31     SRC lwz r4, 0x90, r31      (Δ = +0x34 = 52B)
```

Large shift — likely a substantial mis-declared member or a missing base in
UILabel. `/struct-check UILabel`, find the gap, fix in header.

---

### Agent 10 — FxSend::Save swapped version constants

**Symbol**: `?Save@FxSend@@UAAXAAVBinStream@@@Z`
**Unit**: `default/system/synth/FxSend`
**Raw match%**: 99.91%

```
TGT lis r11, 0x8        SRC lis r11, 0x7
TGT ori r11, r11, 0x7   SRC ori r11, r11, 0x8
   → packed (rev=8 alt=7)    → packed (rev=7 alt=8)
```

Two adjacent 16-bit constants are swapped. Look for `bs << kRev`-style code
in FxSend::Save where the file revision and the "alt revision" / minimum-rev
are written. Probably the version constants in the header are inverted.

Easy one-line fix.

---

### Agent 11 — HamVisDir destructor 8-byte member shift

**Symbol**: `??1HamVisDir@@UAA@XZ`
**Unit**: `default/system/hamobj/HamVisDir`

```
TGT lwz r10, 0x10, r11      SRC lwz r10, 0x8, r11    (3× repeated)
TGT addi r3, r11, 0x10      SRC addi r3, r11, 0x8
```

8-byte shift in HamVisDir layout. Likely a missing 8-byte member (or pointer-
pair) early in the class. Check with `/struct-check HamVisDir`.

---

### Agent 12 — DataFlex lexer constants (yylex, yy_get_previous_state)

**Symbols** in `default/system/obj/DataFlex`:
- `yylex`
- `yy_get_previous_state`

Both show:
```
TGT cmpwi r8, 0x7d           SRC cmpwi r8, 0x7b   (Δ = +2)
TGT cmplwi r11, 0x266        SRC cmplwi r11, 0x1db  (Δ = +0x8B)
```

`0x7d` and `0x7b` are ASCII `}` and `{`. The lexer state-machine is comparing
characters — wrong char compares **will mis-tokenise input** and corrupt every
DTA file load. Very high priority if confirmed.

**Tasks**:
1. Find the `.l` flex source (`src/system/obj/DataFlex.l` or similar).
2. Check tokenizer table generation. Possibly the source was regenerated
   against a slightly different grammar.
3. **Real bug** if it actually parses files differently. May affect game
   bringup.

---

### Agent 13 — Wrong-virtual / wrong-helper sweep (4 fns share `Profile::GetPadNum` pattern)

**Symbols** (`bl ?Parent@Node@?$ObjPtrVec...` ↔ `bl ?GetPadNum@Profile@@QBAHXZ`):
- `?Poll@ProfileMgr@@QAAXXZ` (lazer/meta_ham/ProfileMgr)
- `?OnUpload@KinectSharePanel@@AAA...` (lazer/meta_ham/KinectSharePanel)
- `?OnPostLink@KinectSharePanel@@AAA...` (lazer/meta_ham/KinectSharePanel)
- `?ShowGamercard@Leaderboards@@QAA...` (lazer/meta_ham/Leaderboards)
- `?HandleNetCacheLoaderFailure@StorePanel@@QAAXH@Z` (system/meta/StorePanel)

All four call `?Parent@Node@?$ObjPtrVec<RndTransformable,ObjectDir>@@QBA` (a
const Parent() getter on `Node<ObjPtrVec<...>>`) in target. Source calls
`?GetPadNum@Profile@@QBAHXZ` (a Profile method).

**These are unrelated functions** at different addresses (almost certainly).
This is **MSVC ICF folding masking** — the linker dedup'd 5+ identical-bodied
functions to one address; objdiff bound the call site to the named symbol
that won the dedup election.

**Tasks**:
1. Grab `default.map` (`build/373307D9/default.map`) and search for both
   symbols. If they're the **same address**, this is ICF noise — add to the
   audit script's filter and move on.
2. If they're different addresses, one of them is genuinely being called and
   the source is wrong. Investigate the call shape in m2c/Ghidra.

This is a **noise audit, not a fix dispatch** — it just needs verification.

---

### Agent 14 — Audit-tool maintenance + noise sweep

**Action items** that aren't a single function fix:

1. **Verify ICF address-pairing**: for every `bl <X>` vs `bl <Y>` reloc_target
   diff where X and Y look unrelated (different classes, different modules),
   look up both in `build/373307D9/default.map`. If they resolve to the same
   address, add them to the ICF-alias list in
   `scripts/analysis/audit_normalized_masking.py`.
2. **Add a `--map` flag** to the audit script that consults `default.map` and
   auto-classifies reloc-target diffs as ICF-alias-benign when both symbols
   resolve to the same address.
3. **Verify MILO_ASSERT line-number drift**: for every "wrong constant" with
   a small uniform delta across multiple `li rN, K`, check whether they're
   passed to an assert. If yes → benign; if no → real.
4. **Rerun the audit after each fix wave**: `python3
   scripts/analysis/audit_normalized_masking.py --workers 6`. Read-only,
   completes in ~30s.

---

### Agent 15 — Cross-project DC3↔RB3 co-fix candidates

These need coordination, not parallel dispatch:

| Shared engine file | DC3 candidate | RB3 candidate |
|---|---|---|
| `system/rndobj/Cam.cpp,h` | RndCam::UpdateLocal | check RB3 has same |
| `system/synth/MoggClip.cpp,h` | Play, ApplyLoop, Save | check RB3 |
| `system/world/Spotlight.cpp,h` | Generate, Load | RB3 already validated `SpotDrawParams::Load` field-order bug |
| `system/obj/Dir.cpp` | ObjectDir::PostLoad const | check RB3 |
| `system/char/CharBone.cpp,h` | Load (vbase dtor) | check RB3 |
| `system/movie/Splash.cpp,h` | 4 fns member swap | check if RB3 has Splash |

**Tasks**:
1. For each, `lookup_rb3 <symbol>` (DC3 has the `mcp__orchestrator__lookup_rb3`
   tool) to find the RB3 equivalent.
2. If the bug is in the shared header, fix once and verify in both projects.

## Followups

1. **Rebuild objdiff-cli and report.json** to pick up the metric tweak so the
   dashboard stops over-reporting:
   ```bash
   ln -sf /home/free/code/milohax/objdiff/target/release/objdiff-cli \
          /home/free/code/milohax/dc3-decomp/bin/objdiff-cli
   cd /home/free/code/milohax/dc3-decomp
   rm -f build/373307D9/report.cache  # force regen with new metric
   ninja build/373307D9/report.json
   ```
   Expected: matched_functions drops by 200-400, no change to matched_code.
2. **Re-run the audit periodically** as the fleet lands fixes. The script is
   fast (~30s with 6 workers) and read-only — safe to run alongside the
   permuter fleet.
3. **Verify ICF aliasing via default.map**. The current audit has ~20 entries
   in the `merged_<addr>` and `bl fn_82E21268` filters as approximations; a
   map-based check would be exact.
4. **Lint the audit output for MILO_ASSERT line-drift**: many "wrong constant"
   small-delta entries are line numbers. Add a heuristic that recognises
   `li rN, K` immediately preceding `bl ?Fail@Debug@@` or similar and
   classifies as line-drift-benign.
5. **Propagate to RB3 memory**: the noise classes specific to MSVC PPC builds
   (static-discriminator `?$S<N>` drift, `merged_<addr>` ICF aliases,
   `lbl_NNNNN`-vs-named global) are new in the DC3 audit and would let the
   RB3 audit also filter them out. Currently RB3's audit is MWCC-only.

## Artifacts produced

- `scripts/analysis/audit_normalized_masking.py` — DC3 port of the auditor
  (already existed at session start; bug fix applied this session for
  frame-ptr threading)
- `/tmp/objdiff_audit/dc3_results_v2.json` — full 408-fn audit output, fresh
- `/tmp/objdiff_audit/dc3_curated.json` — 82 in-scope, post-noise-filter
  candidates (the source of the dispatch list above)
- `/tmp/objdiff_audit/dc3_in_scope_final.json` — 87 in-scope candidates with
  partial noise filter (pre-merged-alias / static-disc filter)
- This doc: `docs/sessions/2026-05-26-objdiff-metric-audit-dc3.md`

## Reference

- RB3 session doc: `/home/free/code/milohax/rb3/docs/sessions/2026-05-26-objdiff-metric-audit.md`
- objdiff metric-tweak branch: `/home/free/code/milohax/objdiff`,
  branch `metric-honest-immediates`, commit `f62bc9c`
- Audit tool source: `scripts/analysis/audit_normalized_masking.py`

## Triage results (2026-05-27 follow-up)

The handoff was acted on across two waves of parallel subagents plus
inline fixes. Net result so far: **10 functions reached 100% match**,
**10 audit candidates confirmed benign-ICF** (no fix needed, audit script
needs a smarter filter), build infrastructure unblocked along the way.

### Build infrastructure: dtk fix landed

Before the triage could verify anything, the DC3 build was wedged in a
`ninja: error: manifest 'build.ninja' still dirty after 100 tries` loop
because `dtk xex split` was failing with `ends within symbol
'vftable_8226BC34'` on the `xdk/xaudio2/filterskin.cpp` `.rdata` split.
Root cause: dtk's `FindXboxVtables` heuristic was emitting synthetic
`vftable_<addr>` candidates even at addresses already covered by a user
symbol (`??_7CFilterSkin@LEAPCORE@@6BILeapFilter@@@` at 0x8226BC24
size 0x20 — every slot 0x8226BC24..0x8226BC44 is a real code pointer
in the original binary; the heuristic merely picked up the tail four
pointers plus the first of the next file's vtable and emitted a
5-pointer "vftable_8226BC34" spanning the file boundary). Fixed
upstream in `../jeff/src/analysis/pass.rs` by adding a
user-symbol-overlap check to `FindXboxVtables::maybe_emit`. dtk commit
`f4a3eff`; DC3 split now produces `config.json` and ninja converges.

### Fixed to 100% (10 functions, 2 commits)

Commit `3ca539f7` — wave 1:

| Function | Unit | Before → After | Root cause |
|---|---|---|---|
| `?PollDeps@CharUpperTwist@@…` | `default/system/char/CharUpperTwist` | 99.95% → 100% | Wrong member names in the three `list::push_back` calls; swapped to `(mTwist2; mUpperArm; mTwist1)`. |
| `?Save@FxSend@@UAAXAAVBinStream@@@Z` | `default/system/synth/FxSend` | 99.97% → 100% | `SAVE_REVS(8,7)` was backwards; target packs `(rev=7,alt=8)`. |
| `?OnSetHeightFromText@UILabel@@…` | `default/system/ui/UILabel` | 99.97% → 100% | `ComputeHeight(mCurScrollChars, …)` should be `ComputeHeight(mNumLinesRendered, …)` — same offset, wrong field name. |
| `?InitPool@ParticleCommonPool@@QAAXXZ` | `default/system/rndobj/Part` | 99.85% → 100%norm | Two compounding bugs: (a) bogus trailing `mBirthVelocityZ` pushed `RndFancyParticle` to 0xcc instead of the target 0xc8; (b) `mPoolParticles` was typed `RndParticle*` so `mPoolParticles[i]` used the wrong stride. Fixed by removing the spurious member and re-typing `mPoolParticles` to `RndFancyParticle*` in `Part.h`. Velocity reads in MoveParticles relabeled to the now-correct adjacent fields. |

Wave 3 (additional commits — `aeb1e5df`, `dca0b4e6`, `7d89ff55`, and partials d938521b/af01330c/cef0a559 for MoveParticles, plus `659e8b02` HamVisDir, `a5511687` XboxEnumeration, `377314f1` UIListSubList, `b687680a` MoggClip):

| Function | Unit | Before → After | Root cause |
|---|---|---|---|
| `?IsSuccess@XboxEnumeration@@UBA_NXZ` | `default/system/meta/StoreEnumeration` | 99.3% → 100% | Hardcoded `*((bool*)((u8*)this + 0x24))` read `mOverlapped.InternalHigh` instead of `mEnumerating` (target reads +0x1c). Replaced with named `return mEnumerating;` (also drops the spurious `#ifdef HX_NATIVE` split). |
| `??1HamVisDir@@UAA@XZ` | `default/system/hamobj/HamVisDir` | 99.3% → 100% (norm) | `unk2cc` was declared `std::vector<unsigned int>` (size 0xC); target has `std::vector<bool>` (STLport _Bvector_base, size 0x14). Changed type; the two phantom `unk2d8`/`unk2dc` slots are the vector's internal `_M_finish` halves, not real members. |
| `?Draw@UIListSubList@@UAA…` | `default/system/ui/UIListSubList` | 99.99% → 100% | `UIListElementDrawState` had `mElementState` (0x28) before `mComponentState` (0x2c); target has them reversed. Swap. Sibling `UIListDir::DrawWidgets` stays 100%. |
| `?Play@MoggClip@@UAAXM@Z` | `default/system/synth/MoggClip` | 99.8% → 100% | Missing inline `SetControllerVolume(mControllerVolume)` helper made Play, Save, SyncProperty, PreLoad all confuse `mVolume` (0x44) and `mControllerVolume` (0x40). Added the helper + fixed the four call sites. **4 functions fixed at once.** |
| `?Save@MoggClip@@UAAXAAVBinStream@@@Z` | same | 99.9% → 100% | Same root cause as above. |
| `?SyncProperty@MoggClip@@…` | same | 99.2% raw → 100% | Same. |
| `?PreLoad@MoggClip@@UAAXAAVBinStream@@@Z` | same | 99.8% → 100% | Same. |
| `?AddAccomplishment@AccomplishmentProgress@@QAA_NVSymbol@@@Z` | `default/lazer/meta_ham/AccomplishmentProgress` | 99.15% → 100% (norm) | `MILO_LOG` → `MILO_NOTIFY` in the error branch. The handoff doc listed four wrong-call divergences here; three turned out to be ICF noise (same address, different name), only one was a real wrong-helper. |
| `?OnMsg@OptionsPanel@@…(RCJobCompleteMsg…)` | `default/lazer/meta_ham/OptionsPanel` | 94.19% → 99.7% | The `switch ((unsigned int)res)` cast forced unsigned compares (`cmplw`); target uses signed dispatch (`cmpw` then `subic.`/`subf.`). Drop the cast and re-mark the four HRESULT case labels `0x800AXXXX` as `(int)` so they're valid for a signed switch. **5.5%-point improvement on a 1416-byte function.** Remaining 22 mismatches are callee-saved regswap noise (verdict: AtLimit). |
| `?Draw@DrawPtrVec@@QBAXXZ` | `default/system/char/Character` | 99.97% → 100% | Source called `it->Obj()->DrawShowing()`; target calls `it->Obj()->Draw()` (the guarded entry that frustum-culls before dispatching to `DrawShowing`). Confirmed by `dump_vtable.py`'s slot-5 vs slot-6 distinction. (The non-HX_NATIVE definition of the same function at `Draw.cpp:154` already calls `->Draw()` — confirming intent.) |
| `?MoveParticles@RndParticleSys@@IAAXMM@Z` | `default/system/rndobj/Part` | 75.0% → 80.0%+ | Multi-step: (1) introduce intermediate scalar temps so the compiler emits `fmuls+fadds` separately instead of `fmadds`; (2) re-reference `p->pos`/`p->vel` instead of the locally-bound `pos`/`vel` (so the compiler uses the heap-stored copies in a different register set); (3) use the 3-arg `Multiply(Vec3, Matrix3, Vec3 &)` form. Still iterating; large function with deep regalloc + 5 OFFSET_SWAP residues. |

Commit `a10eec2a` — wave 2:

| Function | Unit | Before → After | Root cause |
|---|---|---|---|
| `?UpdateLocal@RndCam@@IAAXXZ` | `default/system/rndobj/Cam` | 99.95% → 100% | Six wrong field accesses (`m.z.x` should be `m.y.z`; `v.x` should be `m.z.y`) — same offsets, wrong column. Ported verbatim from RB3 reference. RndCam.h layout was correct; this is the bug class also waiting at `Cam.cpp:468` inside `RndCam::GetViewProjectXfms` (still 60%, has other unrelated regalloc/control-flow diffs and is out of scope here). |
| `?Suspend@Splash@@QAAXXZ` | `default/system/movie/Splash` | 99.5% → 100% | NgRnd base class declared `virtual void Suspend()` before `virtual void Resume()` — target has them in the opposite vtable order (slot 0x138 = Resume, slot 0x13c = Suspend). Swap in `Rnd_NG.h` and DxRnd override in `rnddx9/Rnd.h`. **One vtable order swap fixes all 4 Splash functions below.** |
| `?Resume@Splash@@QAAXXZ` | `default/system/movie/Splash` | 99.3% → 100% | Same NgRnd vtable swap. |
| `?EndSplasher@Splash@@QAAXXZ` | `default/system/movie/Splash` | 99.0% → 100% | Same NgRnd vtable swap. |
| `?UpdateThread@Splash@@IAAXXZ` | `default/system/movie/Splash` | 99.2% → 100% | Same NgRnd vtable swap. |
| `?CreateElement@UIListLabel@@…` | `default/system/ui/UIListLabel` | 99.5% → 100% | `l->Copy(mLabel, kCopyDeep)` should be `kCopyShallow`. RB3 uses `l->ResourceCopy(mLabel)` (a shallow-copy wrapper not present in DC3). |

### Confirmed benign — ICF noise, no fix needed

`mcp__orchestrator__lookup_merged_symbol` resolves "wrong-called-function"
asm differences to identical addresses. These audit REVIEW entries are
the heuristic catching the disassembled-name-vs-source-name difference
of an ICF-folded function. Address resolved → multiple symbols → benign.

| Function | Audit "wrong call" | Resolved address | ICF cluster size |
|---|---|---|---|
| `?SynthPoll@SampleInst@@…` | `bl ?GetMaxProcess@CFEWaveStreamDecoder` vs `bl ?GetSampleRate@SynthSample` | 0x82AF68F0 | 10 functions |
| `?IsAvailable@AccomplishmentManager@@…` | same `GetMaxProcess` vs `GetDynamicPrereqsNumSongs@Accomplishment` | 0x82AF68F0 | 10 |
| `?Poll@ProfileMgr@@QAAXXZ` | `bl ?Parent@Node@ObjPtrVec<RndTransformable>…` vs `bl ?GetPadNum@Profile` | 0x82378970 | 166 functions |
| `?ShowGamercard@Leaderboards@@…` | same Parent@Node vs GetPadNum | 0x82378970 | 166 |
| `?OnUpload@KinectSharePanel@@…` | same | 0x82378970 | 166 |
| `?OnPostLink@KinectSharePanel@@…` | same | 0x82378970 | 166 |
| `?HandleNetCacheLoaderFailure@StorePanel@@…` | same | 0x82378970 | 166 |
| `??1DataArray@@…` | `bl ??_GVocalEvent@MidiParser` vs `bl ??_GDataNode@@` | 0x8254CC60 | 2 (two empty scalar-deleting dtors) |
| `?Load@CharBone@@…` | `bl ??_DRndTransformable@@` vs `bl ??_DRndTransformableRemover@@` | 0x8264A208 | 2 (vbase dtor helpers) |
| `?Load@WorldCrowd@@…` | `bl ?OnGetOccluded@CamShot` vs `bl ?OnRebuild@WorldCrowd` | 0x82901C78 | 13 (empty `OnXxx(DataArray*) → DataNode` handlers) |
| `?PopulatePlaylistSongProvider@MetaPerformer@@…` | `bl ?GetRequest@HttpReqCurl` vs `bl ?Title@HamSongMetadata` | 0x82B05A38 | 8 (single-load getters) |

The handoff anticipated this (its Agent-13 task was a noise-audit, not a
fix dispatch). Followup carved out below.

### Session totals (2026-05-27 03:20 UTC)

**23 functions taken to 100% match** + 4 with substantial improvements (now AT_LIMIT
due to regalloc residue) + tool/infrastructure fixes. ~12 audit REVIEW items
verified as benign ICF (no source change needed, just better filtering).

| Outcome | Count |
|---|---:|
| Functions at 100% match (committed) | 23 |
| Substantial improvements (AT_LIMIT residue) | 4 |
| ICF-benign verifications (no fix needed) | 12 |
| Infrastructure fixes (dtk auto-vtable, audit script TMPDIR, dump_vtable UX) | 3 |
| Session commits | 17 |

### Final wave (4 more wins)

| Function | Unit | Before → After | Root cause |
|---|---|---|---|
| `?Activate@FlowDistance@@UAA_NXZ` | `default/system/flow/FlowDistance` | 99.7% raw → 100% norm | `kWhenAble = 5` was wrong; correct value is `4`. Cross-verified against `og-dc3-decomp/src/system/flow/FlowNode.h:24`. Added `kPassThrough = 5` (the new enum value, observed in `FlowQueueable::ChildFinished` as a "pass through to base FlowNode" sentinel). |
| `?SelectConfig@RndShader@@SAXPAVRndMat@@W4ShaderType@@_N@Z` | `default/system/rndobj/Shader` | 99.2% → 100% norm | The `doError` flag was initialized to `true` with combinator logic; target inits to `false` and only sets inside the `(mat && ShowMetaMatErrors)` branch. Source-level semantic bug; 6 register-swap mismatches all disappear once the regalloc anchor instruction at idx 59 is correct. |
| `?Load@Spotlight@@UAAXAAVBinStream@@@Z` | `default/system/world/Spotlight` | 99.7% raw → 100% norm | (a) `mFlare->mSizes` / `mFlare->mRange` serialise via `Key<float>::operator>>`, not `Vector2::operator>>` (same 8-byte payload, different template instantiation; DC3-specific — RB3 uses Vector2). (b) Three `char buf[0x80]` locals inside the legacy-rev compat blocks were being aliased to one stack slot by the compiler — hoist to function-scope as `bufSpot`/`bufFlare`/`bufLens` to force three distinct slots. |
| `?SetProperty@PropertyTask@@IAAXAAVDataNode@@@Z` | `default/system/flow/FlowSetProperty` | 99.5% → 100% norm | The field at `PropertyTask + 0x78` was declared `float mElapsed` but target reads it as int compared to `kDataString` (18). It's actually an int caching `mStartValue.Type()` at ctor time (saved before the atoi conversion clobbers `mStartValue`'s type). Replaced the spurious mElapsed declaration with `int mStartValueType`. |

### Spotlight::Generate, MoveParticles, OptionsPanel::OnMsg — AT_LIMIT

- `Spotlight::Generate` — 99.2% raw / 99.5% norm. r30↔r31 callee-saved register-allocation swap; no source-level transformation moves it. `mr r3, r31` is hoisted above the if/else chain in target; ours keeps `r31 = this+0x1f4` indirect. **Unfixable.**
- `MoveParticles` — 70.9% → 80.0% (+9 points over 5 commits). Remainder is 113 r29↔r30 swap instructions (40% of the noise), plus 35 f0↔f13 / 22 f12↔f13 FPR swaps, plus 12 small stack-shift store offsets after the +48 frame-size recovery. **AT_LIMIT** per orchestrator verdict. Five distinct root causes nailed (field-name mismatches in bubble + rotate blocks, fmuls+fadds vs fmadds, size() caching, Plane stack local, Vector4 refs to hoist pos/vel into callee-saved regs, `Multiply(Vec3, Matrix3, Vec3 &)` inline).
- `OptionsPanel::OnMsg(RCJobCompleteMsg)` — 94.2% → 99.7% (+5.5 points). Remaining 22 mismatches are callee-saved r20↔r21 / r27↔r29↔r30 register-swap noise. **AT_LIMIT (High)**.

### Additional ICF-benign verifications (audit REVIEW → no fix needed)

| Function | Audit "wrong call" → resolved address | ICF cluster size |
|---|---|---|
| `?Poll@AccomplishmentManager@@…` | `bl ?GetAccomplishmentProgress@HamProfile@@QBA…` (TGT) vs `bl ?AccessAccomplishmentProgress@HamProfile@@QAA…` (SRC) → both 0x828DFB08 | 2 (Get/Access merged) |
| `?NumItems@HamNavList@@ABAHXZ` and `?GetDisabledCount@HamNavList@@ABAHH@Z` | `bl ?gathering@CUgtFilter@NUISPEECH@@…` (TGT) → 0x826018C8 | 5 (template-instantiated empty-returns getters) |

### Infrastructure work (parallel)

1. **dtk auto-vtable false-positive fix** (`../jeff` upstream commit `f4a3eff`): `FindXboxVtables` no longer emits synthetic `vftable_<addr>` candidates whose address range overlaps a user-declared symbol. Unblocks the DC3 build (was failing every `xex split` with "ends within symbol 'vftable_8226BC34'" because the heuristic was hitting the tail of `??_7CFilterSkin@LEAPCORE@@6BILeapFilter@@@` plus the first pointer of the next file's vtable).
2. **Audit script TMPDIR support** (`scripts/analysis/audit_normalized_masking.py`): hard-coded `/tmp/objdiff_audit` is now `$TMPDIR/objdiff_audit` so the script works inside the harness sandbox (read-only `/tmp`).
3. **dump_vtable.py UX overhaul** (`scripts/dump_vtable.py`, `.claude/skills/vtable/SKILL.md`): replaced the broken hardcoded `[Object]` slot annotations with class-header-derived declaration-order names. Parses the class header to extract `virtual` methods, walks the parent chain to count inherited slots, and labels each slot as `[inherited from <Parent>]` or `[new in <Class>]`. Also added `--diff-pair OFFSET1 OFFSET2` for vtable-mismatch diagnosis. Tested on RndDrawable (slot 5 = Draw, slot 6 = DrawShowing — ICF-folded as OnlyReturns).

### StreamRecorder::Poll — final win (commit `4fdb3115`)

`?Poll@StreamRecorder@@UAAXXZ`: 99.77% → **100%** raw. The three
`[off:-272]` / `[off:+20]` MI cast pattern wasn't `DrawShowing` after
all — it was a **wrong virtual** entirely. Source called
`mInputDir->DrawShowing()` (RndDrawable vtable @ +0x9c, slot 6); target
calls `mInputDir->Poll()` (RndPollable sub-vtable @ +0x1ac, slot 1).
Semantically correct: StreamRecorder's per-frame `Poll()` recurses
into the input dir's poll chain so its `RndTexRenderer` advances one
frame before the sampled texture is read. Fix is `Poll()` not
`DrawShowing()`. (Coincidentally similar root-cause shape to
`DrawPtrVec::Draw` — wrong virtual on the same object — but a
different wrong virtual.)

### Final totals (24 functions to 100%, 18 session commits)

Updated final tally above is **24** (not 23) once StreamRecorder lands.

### Audit-script followups still owed

1. **ICF address-pairing filter.** The auditor still flags ICF-folded
   wrong-call diffs as REVIEW. The orchestrator MCP exposes
   `lookup_merged_symbol` keyed on address; adding a `--map` flag to
   `scripts/analysis/audit_normalized_masking.py` that resolves both
   `bl <X>` and `bl <Y>` symbols to addresses (via objdiff's per-instr
   target field, not the symbol name) and classifies same-address pairs
   as benign would drop all 7 of the entries in the table above plus
   probably another 10-15 across the REVIEW set.

2. **Demote-to-`lbl_`/`fn_` noise.** The working tree has ~700 hunks of
   `symbols.txt` demoting named statics to `lbl_<addr>` / `fn_<addr>` —
   that's the canonical "target binary lost the symbol name" case the
   auditor filters, but only when the *target* side shows `lbl_`. After
   these demotions land, many functions show `lbl_` on **both** sides
   and the diff naturally normalizes; before they land, the auditor's
   filter does the heavy lifting. Worth coordinating: either commit the
   demotions or revert them, so the auditor's noise classes settle.

3. **Static-init `??__F<name>` mangle drift.** Several REVIEW items
   diverge only in the *name* of a function-local static
   (`??__FshowMicrophoneMsg` vs `??__Fshow_microphone_msg`). Two
   compilations chose different identifier spellings for the same local
   static; semantically identical. The auditor's filter catches the
   `??__F` prefix pair but not when the name normaliser sees them as
   distinct. Refine the filter.

4. **FlowDistance::Activate** & **FlowManager::Poll** both pass `4` as
   the QueueState arg to a virtual `Execute(qs)` call. Our enum has
   `kIgnore=0, kQueue=1, kQueueOne=2, kImmediate=3, kWhenAble=5` — a
   gap at 4. Ghidra confirms target calls Execute with literal 4 in
   both spots. The switch in `FlowQueueable::Activate` compiles to
   `cmplwi r11, 0x5` for `case kWhenAble` (confirming 5 elsewhere), so
   it's a missing enum member at 4 used only in these two call sites.
   Canonical name unknown — not present in RB3 (flow is DC3-specific).
   Defer until someone can name it; one-line fix once named.

5. **`Cam.cpp:468`** — `mLocalProjectXfm.v.x` is referenced in
   `RndCam::GetViewProjectXfms` but provably never written (the
   matrix's `m.z.y` is what holds the FOV scaling — see the
   `UpdateLocal` fix above). Same bug class, different function. Worth
   fixing when someone touches `GetViewProjectXfms`.
