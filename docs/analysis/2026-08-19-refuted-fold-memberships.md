# The 135 REFUTED `retailmap:` / `weakalias:` fold memberships — adjudicated, 2026-08-19

**Repo: dc3-decomp (title 373307D9).** rb3 and rb3-xenon share symbol names and address
ranges with this tree; every symbol, address and number below is dc3's.

## What was measured, and with what

`scripts/symbol_aliases.json` carries 2,992 `(survivor, folded)` memberships in the
`retailmap:` (1,626) and `weakalias:` (1,366) tiers. Re-derived with

```
python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \
    --pairs-json <the 2992 memberships> --include-data \
    --map orig/373307D9/ham_xbox_r.map --quiet
```

which reproduced the brief's number exactly: **REFUTED = 135** (124 `retailmap:`,
11 `weakalias:`).

**The cheapness guard was verified live before any verdict was trusted.** It is not a
dead branch: 266 of the 1,660 UNDECIDABLE rows in that same run are
`bodies identical but ZERO relocations`. A direct negative control also fires --
after this lane's fix, `?GetType@AccomplishmentDiscSongConditional@@` and
`?GetType@CmdEditPlaylist@@` are byte-identical 8-byte bodies and the tool still
returns UNDECIDABLE rather than certifying the fold.

## The discriminator this lane added

`fold_proof.py` compares OUR two spellings against EACH OTHER. That says a claim is
wrong; it does not say *which side* is wrong. Since **134 of the 135 are
MAP_CONFIRMS_SAME_ADDR** -- `ham_xbox_r.map` places both names at one address, so the
fold is the linker's own statement -- the answer is never "the config is wrong": the
fold is real and at least one of our bodies is not.

So every name in each class was compared against the **TARGET's own bytes** at the
fold address, read out of the split assembly (`build/373307D9/asm/**.s`), with the
fields our COFF relocates masked off (low 26 bits for `b`/`bl`, low 16 otherwise).
That turns "the two disagree" into "this one is wrong and here is the retail body".

## Result

| verdict | memberships |
|---|---:|
| our-bug, FIXED | 130 |
| our-bug, identified but NOT fixed (fix regresses a measured function) | 1 |
| tool limitation (nested fold; both our bodies match the target) | 3 |
| genuinely ambiguous | 1 |
| config-wrong | 0 |
| **total** | **135** |

**Nothing here was a fabricated alias.** Unlike the rb3-xenon vector this project was
warned about, dc3's `retailmap:` tier derives from dc3's own shipped map, and all 124
`retailmap:` refutations plus 10 of the 11 `weakalias:` ones are co-listed at one
address by that map. The config needed no correction.

After the fixes, the same command over the same 2,992 memberships reports
**REFUTED 135 -> 5**; 10 further rows moved to UNDECIDABLE because their bodies are
now identical *and* relocation-free, which is exactly what the cheapness guard is for.

| after this lane | memberships |
|---|---:|
| PROVEN_FOLD | 118 |
| UNDECIDABLE | 10 |
| REFUTED | 5 |
| PROVEN_MOD_MAP | 2 |

## Whole-build effect: zero, and that is the finding

Predicted 2 measurable rows (the two fold classes whose SPLIT name is one of the
wrong bodies). Observed 0. Against `eda64e956`, `report.json` moves by
**0 regressions / 0 new matched functions**; the only deltas are four 32-byte `fn_*`
EH funclets in `HamProfile`/`OptionsPanel` that objdiff re-paired.

The disagreement is structural, not a measurement error:

1. **A fold class contributes exactly one measured row -- the survivor.** A REFUTED
   membership is by construction a statement about a NON-surviving member, which
   `report.json` cannot see. 23 of the 27 classes here already had a matching survivor.
2. The two exceptions -- `?Replace@?$ObjPtrList@VCamShot@@VObjectDir@@@@...` (80 B,
   split into `system/world/ThreeDSoundManager`) and
   `?GetType@AccomplishmentDiscSongConditional@@...` (8 B, split into
   `system/os/ContentMgr_Xbox`) -- still measure 0%: our object for that unit emits a
   *different* spelling from the same fold class, and objdiff pairs symbols by name.
   Both bodies are now byte-exact against the target; recovering the pairing is
   **task #112's ICF-survivor-name work**, deliberately not touched here.

Byte-level verification instead of the metric: of the 162 distinct names in the 27
affected fold classes, **152 now equal the target body** (relocations masked, best copy
across every object that defines the name), up from **69 at `eda64e956`** -- i.e. 83
COMDATs in our own build changed from "not what the shipped image has" to "byte-exact
against it", none of which `report.json` is able to see.

## One rejected experiment

Removing `ShaderMacro::operator=` is the correct fix for
`__uninitialized_fill_n<ShaderMacro*>` (it is what makes MSVC emit retail's mtctr/bdnz
loop) but it took `ShaderOptions::GenerateMacros` from 100.0% to 97.3% normalized,
**-3612 B whole-build** -- a LINKER_MERGED row traded for 3.6 KB of measured code.
Reverted; the reasoning lives in `src/system/rndobj/ShaderOptions.h` so it is not
retried. Same shape as the rejected `kArkBlockSize const` experiment.

## The 135, by fold class


### 0x8285ae50 — 103 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?Replace@?$ObjPtrList@VCamShot@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z` (80 B in the target)
- evidence: ObjPtrList<T>::RefOwner/Replace were walking implementations; retail is ObjPtrVec's MILO_FAIL("should never be called") stub
- fold_proof after: [('PROVEN_FOLD', 103)]

| name | matches target bytes now |
|---|---|
| `?RefOwner@?$ObjPtrList@VCamShot@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharBone@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharCollide@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharInterest@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharLookAt@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharPollable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VCharacter@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VEventTrigger@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VFader@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| `?RefOwner@?$ObjPtrList@VFlowNode@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ` | yes |
| _... 92 more in this class_ | all yes |

### 0x825e9c80 — 3 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?GetType@AccomplishmentDiscSongConditional@@UBA?AW4AccomplishmentType@@XZ` (8 B in the target)
- evidence: AccomplishmentDiscSongConditional::GetType returned 11; retail `li r3, 5` (AccomplishmentType was RB3's enum)
- fold_proof after: [('UNDECIDABLE', 3)]

| name | matches target bytes now |
|---|---|
| `?GetState@RootContent@@UAA?AW4State@Content@@XZ` | yes |
| `?GetType@AccomplishmentDiscSongConditional@@UBA?AW4AccomplishmentType@@XZ` | yes |
| `?GetType@CmdEditPlaylist@@UAAHXZ` | yes |
| `?GetType@NavListFunctionNode@@UBA?AW4NavListNodeType@@XZ` | yes |

### 0x8277e908 — 3 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?OnMsg@Automator@@AAA?AVDataNode@@ABVUIComponentScrollMsg@@@Z` (80 B in the target)
- evidence: Automator::OnMsg for Select/FocusChange/ScreenChange needed the Scroll overload's named Symbol temp
- fold_proof after: [('PROVEN_FOLD', 3)]

| name | matches target bytes now |
|---|---|
| `?OnMsg@Automator@@AAA?AVDataNode@@ABVUIComponentFocusChangeMsg@@@Z` | yes |
| `?OnMsg@Automator@@AAA?AVDataNode@@ABVUIComponentScrollMsg@@@Z` | yes |
| `?OnMsg@Automator@@AAA?AVDataNode@@ABVUIComponentSelectMsg@@@Z` | yes |
| `?OnMsg@Automator@@AAA?AVDataNode@@ABVUIScreenChangeMsg@@@Z` | yes |

### 0x82527920 — 2 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GFilterVersion@@UAAPAXI@Z` (96 B in the target)
- evidence: Ham1/Ham2FilterVersion declared inline empty dtors; retail has none, so ??_G stores no vptr
- fold_proof after: [('PROVEN_FOLD', 2)]

| name | matches target bytes now |
|---|---|
| `??_GFilterVersion@@UAAPAXI@Z` | yes |
| `??_GHam1FilterVersion@@UAAPAXI@Z` | yes |
| `??_GHam2FilterVersion@@UAAPAXI@Z` | yes |

### 0x82e13dd8 — 2 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?OnMsg@MetaPerformer@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z` (20 B in the target)
- evidence: HamUI::OnMsg(ConnectionStatusChangedMsg) had an invented TriggerEvent body; StorePanel::OnMsg(ProfileSwappedMsg) returned 0 not 1
- fold_proof after: [('UNDECIDABLE', 2)]

| name | matches target bytes now |
|---|---|
| `?OnMsg@HamUI@@IAA?AVDataNode@@ABVConnectionStatusChangedMsg@@@Z` | yes |
| `?OnMsg@MetaPerformer@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z` | yes |
| `?OnMsg@StorePanel@@IAA?AVDataNode@@ABVProfileSwappedMsg@@@Z` | yes |

### 0x8274b700 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GGroupSeq@@UAAPAXI@Z` (76 B in the target)
- evidence: SfxSeq declared an inline empty dtor; retail has none
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GGroupSeq@@UAAPAXI@Z` | yes |
| `??_GSfxSeq@@UAAPAXI@Z` | yes |

### 0x828d3980 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GMetaPerformer@@UAAPAXI@Z` (68 B in the target)
- evidence: QuickplayPerformer declared an inline empty dtor; retail has none
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GMetaPerformer@@UAAPAXI@Z` | yes |
| `??_GQuickplayPerformer@@UAAPAXI@Z` | yes |

### 0x82986170 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GNavListItemNode@@UAAPAXI@Z` (88 B in the target)
- evidence: MQSongSortNode had an out-of-line empty dtor; retail has NavListItemNode's inline one
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GMQSongSortNode@@UAAPAXI@Z` | yes |
| `??_GNavListItemNode@@UAAPAXI@Z` | yes |

### 0x8294d538 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GSongSelectPlaylistCustomizePanel@@UAAPAXI@Z` (88 B in the target)
- evidence: SongSelectPlaylistPanel had an out-of-line empty dtor; retail has none
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GSongSelectPlaylistCustomizePanel@@UAAPAXI@Z` | yes |
| `??_GSongSelectPlaylistPanel@@UAAPAXI@Z` | yes |

### 0x829613a0 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GSongSortByLocation@@UAAPAXI@Z` (100 B in the target)
- evidence: SongSortBySong had an out-of-line empty dtor; retail has SongSortByLocation's inline one
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GSongSortByLocation@@UAAPAXI@Z` | yes |
| `??_GSongSortBySong@@UAAPAXI@Z` | yes |

### 0x82e42cf8 — 1 membership(s), tier `weakalias:` — **our-bug (FIXED)**

- split/survivor symbol: `??_GSynthSample@@UAAPAXI@Z` (76 B in the target)
- evidence: SynthSample360 declared an inline empty dtor; retail has none
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `??_GSynthSample360@@UAAPAXI@Z` | yes |
| `??_GSynthSample@@UAAPAXI@Z` | yes |

### 0x82371970 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?PollDeps@CharFaceServo@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z` (12 B in the target)
- evidence: CharServoBone::PollDeps pushed `this`; retail tail-calls CharBonesMeshes::StuffMeshes
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `?PollDeps@CharFaceServo@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z` | yes |
| `?PollDeps@CharServoBone@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z` | yes |

### 0x824b0be8 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?Mirrored@HamMove@@QBA?AW4MoveMirrored@@XZ` (16 B in the target)
- evidence: XboxEnumeration::IsEnumerating read the mEnumerating byte at 0x1c; retail reads the WORD mHandle at 0x3c
- fold_proof after: [('UNDECIDABLE', 1)]

| name | matches target bytes now |
|---|---|
| `?IsEnumerating@XboxEnumeration@@UBA_NXZ` | yes |
| `?Mirrored@HamMove@@QBA?AW4MoveMirrored@@XZ` | yes |

### 0x8267c7c0 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `??$?5VColor@Hmx@@@@YAAAVBinStreamRev@@AAV0@AAV?$Key@VColor@Hmx@@@@@Z` (76 B in the target)
- evidence: Key<Hmx::Quat>'s BinStreamRev reader needed Key<Hmx::Color>'s bs.stream specialisation
- fold_proof after: [('PROVEN_MOD_MAP', 1)]

| name | matches target bytes now |
|---|---|
| `??$?5VColor@Hmx@@@@YAAAVBinStreamRev@@AAV0@AAV?$Key@VColor@Hmx@@@@@Z` | yes |
| `??$?5VQuat@Hmx@@@@YAAAVBinStreamRev@@AAV0@AAV?$Key@VQuat@Hmx@@@@@Z` | yes |

### 0x82696cc8 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?TextToken@RndText@@UAA?AVSymbol@@XZ` (56 B in the target)
- evidence: PlaylistSortMgr::MoveOn built its Symbol from 0; retail from gNullStr
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `?MoveOn@PlaylistSortMgr@@UAA?AVSymbol@@XZ` | yes |
| `?TextToken@RndText@@UAA?AVSymbol@@XZ` | yes |

### 0x827026f0 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?Handle@UIListWidget@@UAA?AVDataNode@@PAVDataArray@@_N@Z` (292 B in the target)
- evidence: HolmesInput::Handle was a 16 B stub; retail is the 292 B BEGIN_HANDLERS/HANDLE_SUPERCLASS(Hmx::Object) body
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `?Handle@HolmesInput@@UAA?AVDataNode@@PAVDataArray@@_N@Z` | yes |
| `?Handle@UIListWidget@@UAA?AVDataNode@@PAVDataArray@@_N@Z` | yes |

### 0x8276c078 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?op40@@YA?AVDataNode@@PAVDataArray@@@Z` (116 B in the target)
- evidence: op58 was spelled differently from op40 although nop58 == nop40 (one rlwinm mask differed)
- fold_proof after: [('PROVEN_FOLD', 1)]

| name | matches target bytes now |
|---|---|
| `?op40@@YA?AVDataNode@@PAVDataArray@@@Z` | yes |
| `?op58@@YA?AVDataNode@@PAVDataArray@@@Z` | yes |

### 0x82805960 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `??$?6UPresetOverride@WorldDir@@V?$StlNodeAlloc@UPresetOverride@WorldDir@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$list@UPresetOverride@WorldDir@@V?$StlNodeAlloc@UPresetOverride@WorldDir@@@stlpmtx_std@@@stlpmtx_std@@@Z` (120 B in the target)
- evidence: WorldDir::MatOverride's stream writer wrote mat2, which neither the reader nor the propsync touches
- fold_proof after: [('PROVEN_MOD_MAP', 1)]

| name | matches target bytes now |
|---|---|
| `??$?6UMatOverride@WorldDir@@V?$StlNodeAlloc@UMatOverride@WorldDir@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$list@UMatOv` | yes |
| `??$?6UPresetOverride@WorldDir@@V?$StlNodeAlloc@UPresetOverride@WorldDir@@@stlpmtx_std@@@@YAAAVBinStream@@AAV0@ABV?$list@` | yes |

### 0x82dc4288 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?EasiestDifficulty@@YA?AW4Difficulty@@XZ` (8 B in the target)
- evidence: AccomplishmentOneShot::GetType returned 9; retail `li r3, 3`
- fold_proof after: [('UNDECIDABLE', 1)]

| name | matches target bytes now |
|---|---|
| `?EasiestDifficulty@@YA?AW4Difficulty@@XZ` | yes |
| `?GetType@AccomplishmentOneShot@@UBA?AW4AccomplishmentType@@XZ` | yes |

### 0x82de9aa8 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?SetEngaged@DirectionGestureFilterSingleUser@@UAAX_N@Z` (8 B in the target)
- evidence: UIListState::SetScrollPastMaxDisplay was an empty body; retail stores the argument at 0x1c
- fold_proof after: [('UNDECIDABLE', 1)]

| name | matches target bytes now |
|---|---|
| `?SetEngaged@DirectionGestureFilterSingleUser@@UAAX_N@Z` | yes |
| `?SetScrollPastMaxDisplay@UIListState@@QAAX_N@Z` | yes |

### 0x82e171b0 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?IsPurchasing@XboxMultipleItemsPurchaser@@UBA_NXZ` (44 B in the target)
- evidence: XboxPurchaser::IsPurchasing was `== purchasestate1`; retail is XboxMultipleItemsPurchaser's three-way state test
- fold_proof after: [('UNDECIDABLE', 1)]

| name | matches target bytes now |
|---|---|
| `?IsPurchasing@XboxMultipleItemsPurchaser@@UBA_NXZ` | yes |
| `?IsPurchasing@XboxPurchaser@@UBA_NXZ` | yes |

### 0x82e3dc20 — 1 membership(s), tier `retailmap:` — **our-bug (FIXED)**

- split/survivor symbol: `?GetSampleRate@MicNull@@UBAHXZ` (12 B in the target)
- evidence: MicXbox::GetSampleRate returned 16000; retail returns 48000
- fold_proof after: [('UNDECIDABLE', 1)]

| name | matches target bytes now |
|---|---|
| `?GetSampleRate@MicNull@@UBAHXZ` | yes |
| `?GetSampleRate@MicXbox@@UBAHXZ` | yes |

### 0x8295f6e0 — 1 membership(s), tier `weakalias:` — **tool-limitation**

- split/survivor symbol: `??_GFitnessCalorieSort@@UAAPAXI@Z` (100 B in the target)
- evidence: both spellings match the target at the TU the linker selected; the strict test lands on the 76 B copies whose relocs name ??1FitnessCalorieSort vs ??1FitnessCalorieSortByCalorie -- a fold one level down
- fold_proof after: [('REFUTED', 1)]

| name | matches target bytes now |
|---|---|
| `??_GFitnessCalorieSort@@UAAPAXI@Z` | yes |
| `??_GFitnessCalorieSortByCalorie@@UAAPAXI@Z` | yes |

### 0x824896f8 — 1 membership(s), tier `weakalias:` — **tool-limitation**

- split/survivor symbol: `??_GHamNavProvider@@UAAPAXI@Z` (76 B in the target)
- evidence: same nested-fold shape: ??1HamNavProvider vs ??1AppNavProvider
- fold_proof after: [('REFUTED', 1)]

| name | matches target bytes now |
|---|---|
| `??_GAppNavProvider@@UAAPAXI@Z` | yes |
| `??_GHamNavProvider@@UAAPAXI@Z` | yes |

### 0x82626548 — 1 membership(s), tier `weakalias:` — **genuinely-ambiguous**

- split/survivor symbol: `??_EString@@UAAPAXI@Z` (108 B in the target)
- evidence: ??_E (vector deleting dtor, 108 B) and ??_G (scalar, 76 B) are DIFFERENT functions. The weakalias tier is sound only where ??_E is an UNRESOLVED weak external; our rndobj/HiResScreen.obj emits a strong ??_E, which is the copy the linker kept and which matches the target. ??_GString is not in ham_xbox_r.map at all, so nothing can grade it.
- fold_proof after: [('REFUTED', 1)]

| name | matches target bytes now |
|---|---|
| `??_EString@@UAAPAXI@Z` | yes |
| `??_GString@@UAAPAXI@Z` | NO |

### 0x82461b08 — 1 membership(s), tier `retailmap:` — **our-bug (NOT fixed - regresses)**

- split/survivor symbol: `??$__uninitialized_fill_n@PAU?$pair@HH@stlpmtx_std@@IU12@@stlpmtx_std@@YAPAU?$pair@HH@0@PAU10@IABU10@ABU__false_type@0@@Z` (48 B in the target)
- evidence: __uninitialized_fill_n<ShaderMacro*> lowers to a countdown loop, retail to mtctr/bdnz; the cause is ShaderMacro's hand-written operator=, but removing it takes ShaderOptions::GenerateMacros 100.0% -> 97.3% (-3612 B). Reverted; see the header comment.
- fold_proof after: [('REFUTED', 1)]

| name | matches target bytes now |
|---|---|
| `??$__uninitialized_fill_n@PAU?$pair@HH@stlpmtx_std@@IU12@@stlpmtx_std@@YAPAU?$pair@HH@0@PAU10@IABU10@ABU__false_type@0@@` | yes |
| `??$__uninitialized_fill_n@PAUShaderMacro@@IU1@@stlpmtx_std@@YAPAUShaderMacro@@PAU1@IABU1@ABU__false_type@0@@Z` | NO |

### 0x828c5378 — 1 membership(s), tier `retailmap:` — **tool-limitation**

- split/survivor symbol: `??_DAppLabel@@QAAXXZ` (108 B in the target)
- evidence: ??_DAppLabel and ??_DHamLabel are byte-identical and both match the target; only ??1AppLabel vs ??1HamLabel differs -- a fold one level down
- fold_proof after: [('REFUTED', 1)]

| name | matches target bytes now |
|---|---|
| `??_DAppLabel@@QAAXXZ` | yes |
| `??_DHamLabel@@QAAXXZ` | yes |

## Reproduce

```sh
# 1. the census (must print REFUTED=5 on this branch, 135 at eda64e956)
python3 - <<'EOF' > /tmp/pairs.json
import json
d=json.load(open('scripts/symbol_aliases.json'))
print(json.dumps([{'target':g['survivor'],'base':f,'tier':g['name'].split(':')[0],
                   'address':g.get('address')}
                  for g in d['groups']
                  if g['name'].split(':')[0] in ('retailmap','weakalias')
                  for f in g['folded']]))
EOF
python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \
    --pairs-json /tmp/pairs.json --include-data \
    --map orig/373307D9/ham_xbox_r.map --quiet

# 2. the cheapness guard must still refuse (expect UNDECIDABLE, not PROVEN_FOLD)
python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \
    --pair '?GetType@AccomplishmentDiscSongConditional@@UBA?AW4AccomplishmentType@@XZ' \
           '?GetType@CmdEditPlaylist@@UAAHXZ'
```

