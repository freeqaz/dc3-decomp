# COMDAT fold adjudication of the relocation-name charges — dc3-decomp, 2026-08-17

**Repo: dc3-decomp (title 373307D9).** rb3-xenon and rb3 share symbol names and
address ranges with this tree; every number and every symbol below is dc3's.
Tooling ported from rb3-xenon, which was read only and is unmodified.

Ruler: `functionRelocDiffs=name_check`, read from
`build/373307D9/report.json` `provenance.diff_config` (22 keys) — the graded
ruler. An orchestrator `run_objdiff` number is a `none` number and would call
most of these rows 100%.

## The question

`name_check` charges a site when the relocation there names a different symbol on
the two sides. Such a charge has two explanations that demand OPPOSITE actions:
a legitimate `/OPT:ICF` fold (install an alias) or a wrong callee in our source
(fix the source). Getting it wrong in the fold direction is FAIL-OPEN: an alias
does not close a gap, it stops the gap from ever being measured again.

Three instruments, applied in order of what each can conclude:

1. **The shipped linker map** (`orig/373307D9/ham_xbox_r.map`). ICF folds
   byte-identical COMDATs and the map co-lists every folded name at the surviving
   address — 0x82901c78 carries thirteen. Decisive both ways when both names are
   present.
2. **The fold proof on our own objects** (`scripts/analysis/fold_proof.py`) —
   ICF's own condition: byte-identical AND relocation-set-identical (offsets,
   types, and target symbol NAMES). Byte-only equality is never accepted: two
   `bl`s to different callees are the same four bytes.
3. **Structural classification of the mangled names** — for pairs instrument 2
   must decline, which is most of this bucket, because a name the target spells
   and our build does not emit cannot be looked up in our objects at all. This
   stage only ever REFUTES.

## Headline

**The bucket that could not be adjudicated from the map is not a bucket of hidden
folds. It is a bucket of real source differences that the charges were reporting
faithfully.** Of 119 rows / 44,760 B, 3 rows / 504 B are provable folds and
108 rows / 42,024 B are refuted — 94% of the bytes.

## Rows whose ONLY charges are relocation names (the whole population)

| map verdict | rows | bytes |
|---|---:|---:|
| NOT_IN_MAP (this lane) | 119 | 44,760 |
| MAP_REFUTES_FOLD (other lane) | 14 | 4,096 |
| MAP_CONFIRMS_FOLD (alias lane) | 4 | 9,320 |
| **total** | **137** | **58,176** |

Measured on this lane rebased onto `38b723a17`, so it already includes the
map-refuted lane's fixes (which is why MAP_REFUTES_FOLD is down to 14 rows from
24) and this lane's five.

Provenance, so the drift is legible:

| tree state | clean rows | bytes | NOT_IN_MAP | MAP_REFUTES | MAP_CONFIRMS |
|---|---:|---:|---:|---:|---:|
| research artifact, named rows only | 157 | — | 122 | 24 | 11 |
| this lane's first census, same base | 158 | 66,792 | 122 | 24 | 12 |
| after this lane's 5 fixes | 154 | 65,000 | 118 | 24 | 12 |
| rebased onto `38b723a17` | 137 | 58,176 | 119 | 14 | 4 |

The first two lines agree to the row, which is the census's validation against
the research lane. The single MAP_CONFIRMS disagreement is
`?Handle@HamDirector@@`, which moved out of NOT_IN_MAP because of the
`OnSaveFaceanims` rename; byte totals reconcile exactly
(51,252 − 9,004 + 4,088 = 46,336).

The NOT_IN_MAP classification is unchanged by the rebase — REFUTED 108 rows /
42,024 B, PROVEN_FOLD 3 / 504, UNDECIDABLE 8 / 2,232 (one row more than before
the rebase). Every per-pair list below is from the rebased tree.

### On the brief's "115 rows / 33,792 B"

Not reproducible, and the difference is a filter, not a disagreement. The
research lane's 272-row clean set includes **115 `fn_*` funclet rows totalling
4,124 B**, which this census excludes by name prefix; on the 157 NAMED rows the
two agree to the row. The artifact's own 219-row adjudication total could not be
derived from its own inputs under any precedence order over the three verdicts.
The numbers in this document are re-derived end to end and each is reproducible
with the commands in the appendix.

## NOT_IN_MAP — the three-way classification

| class | rows | bytes | pairs |
|---|---:|---:|---:|
| PROVEN_FOLD | 3 | 504 | 3 |
| REFUTED | 108 | 42,024 | 159 |
| UNDECIDABLE | 7 | 2,016 | 7 |
| **total** | **118** | **44,544** | **169** |

### REFUTED sub-classes (pairs)

| sub-class | pairs | what it means |
|---|---:|---|
| LOCAL_STATIC_SCOPE_SKEW | 72 | same variable, same enclosing function, different MSVC scope index — our source has a different number of preceding lexical scopes or local statics |
| STRING_LITERAL | 21 | two `??_C@` literals whose decoded text differs — on this tree almost all are `__FILE__` path prefixes |
| SPLIT_CONFIG_NAMING | 20 | our compiler emits the unmangled internal-linkage label MSVC gives a file-scope static; the target-side spelling was synthesised in `config/373307D9/symbols.txt` and appears nowhere in the shipped map |
| DIFFERENT_SYMBOL | 15 | our COMDATs for the two names differ, and the names follow no known spelling rule — a genuinely different callee |
| STORAGE_CLASS_SKEW | 10 | same leaf name under a different owner — class static vs file static vs function-local |
| ANON_NS_HASH | 8 | same symbol in an unnamed namespace under two different `?A0x` hashes — a build-environment artifact `scripts/obj_anon_ns_patcher.py` owns, not a source bug |
| LOCAL_STATIC_KIND_CHANGE | 7 | `??_B` init guard on one side, `?$S` counter on the other — the local static has a different shape |
| IMMEDIATE_VS_SYMBOL | 2 | the target relocates a symbol where we emit a bare immediate |
| C_LINKAGE_SKEW | 2 | the same name with C linkage on one side and C++ linkage on the other |
| LOCAL_STATIC_RENAME | 1 | same enclosing function and same scope index, the local is simply named differently — a one-line fix |
| LOCAL_STATIC_SCOPE_RENAME | 1 | both the name and the scope index differ |

## NOT_IN_MAP → PROVEN_FOLD (alias candidates — NOT installed here) — 3 pairs

Handed to the alias-install lane, which owns `scripts/symbol_aliases.json`.
Each carries its proof: byte- AND relocation-set-identical in our own build,
with at least one relocation, so the zero-relocation cheapness guard did not
have to fire. Evidence tier: body test, within-build.

- **292 B** `default/lazer/meta_ham/MetagameRank` → `?_M_erase@?$vector@V?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@stlpmtx_std@@V?$StlNodeAlloc@V?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@stlpmtx_std@@@2@@stlpmtx_std@@IAAPAV?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@2@PAV32@0ABU__true_type@2@@Z`
  - target `??$__destroy_range@PAV?$vector@PAUUnlockable@?A0x9d17dd81@@V?$StlNodeAlloc@PAUUnlockable@?A0x9d17dd81@@@stlpmtx_std@@@stlpmtx_std@@V12@@stlpmtx_std@@YAXPAV?$vector@PAUUnlockable@?A0x9d17dd81@@V?$StlNodeAlloc@PAUUnlockable@?A0x9d17dd81@@@stlpmtx_std@@@0@00@Z`
  - ours   `??$__destroy_mv_srcs@PAV?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@stlpmtx_std@@V12@@stlpmtx_std@@YAXPAV?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@0@00@Z`
  - `-` — byte- AND relocation-set-identical (84 B, 1 relocations) => /OPT:ICF must merge them
- **124 B** `default/system/utl/BufStream` → `?StartChecksum@BufStream@@QAAXPBD@Z`
  - target `?DeleteChecksum@FileStream@@AAAXXZ`
  - ours   `?DeleteChecksum@BufStream@@QAAXXZ`
  - `-` — byte- AND relocation-set-identical (72 B, 1 relocations) => /OPT:ICF must merge them
- **88 B** `default/system/utl/BufStream` → `??1BufStream@@UAA@XZ`
  - target `?DeleteChecksum@FileStream@@AAAXXZ`
  - ours   `?DeleteChecksum@BufStream@@QAAXXZ`
  - `-` — byte- AND relocation-set-identical (72 B, 1 relocations) => /OPT:ICF must merge them

## NOT_IN_MAP → UNDECIDABLE — 8 pairs

Stated as WHY nothing can decide, not as a soft refusal.

- **800 B** `default/system/synth/ThreeDSound` → `?Load@ThreeDSound@@UAAXAAVBinStream@@@Z`
  - target `?gRevs@?1??Load@ThreeDSound@@UAAXAAVBinStream@@@Z@4QBGB`
  - ours   `gAltRev`
  - `DIFFERENT_SYMBOL` — names are not related by any known spelling rule
- **376 B** `default/system/hamobj/HamCamTransform` → `?Load@HamCamTransform@@UAAXAAVBinStream@@@Z`
  - target `??$?5VTransformArea@@@@YAAAVBinStream@@AAVBinStreamRev@@AAV?$ObjVector@VTransformArea@@@@@Z`
  - ours   `??5@YAAAVBinStream@@AAVBinStreamRev@@AAV?$ObjVector@VTransformArea@@@@@Z`
  - `DIFFERENT_SYMBOL` — names are not related by any known spelling rule
- **276 B** `default/system/synth_xbox/SampleInst360` → `??0SampleInst360@@QAA@PAVSynthSample360@@_NHH@Z`
  - target `?DrawHighlightMat@RndShaderMgr@@UAAPAVRndMat@@XZ`
  - ours   `?GetDataAddr@SynthSample360@@QBAIXZ`
  - `DIFFERENT_SYMBOL` — names are not related by any known spelling rule
- **216 B** `default/system/os/System` → `?SystemTerminate@@YAXXZ`
  - target `OnlyReturns`
  - ours   `?MemTerminate@@YAXXZ`
  - `ICF_SYNTHETIC_TARGET_NAME` — the target side is dtk's shape-derived placeholder `OnlyReturns` for a fold class with no surviving public name; it names no symbol, so nothing can look it up
- **180 B** `default/system/rndobj/PostProc_NG` → `?CheckHueConverge@NgPostProc@@IAAXXZ`
  - target `merged_Returns1`
  - ours   `?ColorXfmEnabled@RndPostProc@@QBA_NXZ`
  - `ICF_SYNTHETIC_TARGET_NAME` — the target side is dtk's shape-derived placeholder `merged_Returns1` for a fold class with no surviving public name; it names no symbol, so nothing can look it up
- **172 B** `default/system/synth/FxSend` → `?Copy@FxSend@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`
  - target `merged_SetObjConcrete`
  - ours   `?SetObj@?$ObjRefConcrete@VFxSend@@VObjectDir@@@@QAAPAVObject@Hmx@@PAV23@@Z`
  - `ICF_SYNTHETIC_TARGET_NAME` — the target side is dtk's shape-derived placeholder `merged_SetObjConcrete` for a fold class with no surviving public name; it names no symbol, so nothing can look it up
- **112 B** `default/lazer/meta_ham/MetagameRank` → `??0?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@stlpmtx_std@@QAA@ABV01@@Z`
  - target `OnlyReturns`
  - ours   `?get_allocator@?$vector@PAUUnlockable@?A0xf8e4b4b5@@V?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@stlpmtx_std@@@stlpmtx_std@@QBA?AV?$StlNodeAlloc@PAUUnlockable@?A0xf8e4b4b5@@@2@XZ`
  - `ICF_SYNTHETIC_TARGET_NAME` — the target side is dtk's shape-derived placeholder `OnlyReturns` for a fold class with no surviving public name; it names no symbol, so nothing can look it up
- **100 B** `default/system/gesture/SkeletonUpdate` → `??0SkeletonFrame@@QAA@XZ`
  - target `OnlyReturns`
  - ours   `??0PaddedJointPos@@QAA@XZ`
  - `ICF_SYNTHETIC_TARGET_NAME` — the target side is dtk's shape-derived placeholder `OnlyReturns` for a fold class with no surviving public name; it names no symbol, so nothing can look it up

## NOT_IN_MAP → REFUTED (real differences)

Grouped by sub-class, largest byte total first.

### LOCAL_STATIC_SCOPE_SKEW — 72 pairs

- **3008 B** `default/system/world/CameraShot` → `?SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?_s@?HP@??SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?_s@?JD@??SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **3008 B** `default/system/world/CameraShot` → `?SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?_s@?IK@??SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?_s@?JO@??SyncProperty@CamShot@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?bustamove@?DA@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?bustamove@?DC@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?campaign_practice@?BE@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?campaign_practice@?BG@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?challenge@?DD@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?challenge@?DF@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?current_campaign_era@?DJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?current_campaign_era@?DL@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?era_tan_battle@?DJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?era_tan_battle@?DL@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?gameplay_mode@?DJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?gameplay_mode@?DL@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?is_in_campaign_mode@?BH@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?is_in_campaign_mode@?BJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?is_in_campaign_stinger@?BH@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?is_in_campaign_stinger@?BJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?just_intro@?BN@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?just_intro@?BP@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?mind_control@?BN@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?mind_control@?BP@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?practice@?BE@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?practice@?BG@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?rhythm_battle@?DJ@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?rhythm_battle@?DL@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2500 B** `default/App` → `?IsUselessLoad@@YA_NPBD@Z`
  - target `?strike_a_pose@?DG@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
  - ours   `?strike_a_pose@?DI@??IsUselessLoad@@YA_NPBD@Z@4VSymbol@@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?$S3@?4??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4IA`
  - ours   `?$S2@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4IA`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?BandMeshLauncher@?9??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?BandMeshLauncher@?7??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?CharClip@?4??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?CharClip@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?CharClipSamples@?4??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?CharClipSamples@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?CharTransDraw@?BA@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?CharTransDraw@?M@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?CompositeTexture@?BF@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?CompositeTexture@?BA@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?P9TransDraw@?BA@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?P9TransDraw@?M@??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **2260 B** `default/system/obj/DirLoader` → `?FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z`
  - target `?PartLauncher@?9??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
  - ours   `?PartLauncher@?7??FixClassName@DirLoader@@AAA?AVSymbol@@V3@@Z@4V3@A`
- **1916 B** `default/lazer/meta_ham/SongSortNode` → `?Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?ham1@?BH@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?ham1@?CP@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **1916 B** `default/lazer/meta_ham/SongSortNode` → `?Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?ham2@?BH@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?ham2@?CP@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **1916 B** `default/lazer/meta_ham/SongSortNode` → `?Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?ham3@?BH@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?ham3@?CP@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **1916 B** `default/lazer/meta_ham/SongSortNode` → `?Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?song_select_song_prefix@?FF@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?song_select_song_prefix@?HK@??Text@SongSortNode@@UBAXPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **1600 B** `default/system/synth/ThreeDSound` → `?SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?_s@?DM@??SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?_s@?FE@??SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **1600 B** `default/system/synth/ThreeDSound` → `?SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?_s@?EP@??SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?_s@?FP@??SyncProperty@ThreeDSound@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **1072 B** `default/lazer/meta_ham/AccomplishmentOneShot` → `?AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z`
  - target `?omg@?CM@??AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z@4V5@A`
  - ours   `?omg@?EC@??AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z@4V5@A`
- **1072 B** `default/lazer/meta_ham/AccomplishmentOneShot` → `?AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z`
  - target `?stars_earned@?CP@??AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z@4V5@A`
  - ours   `?stars_earned@?EF@??AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z@4V5@A`
- **1024 B** `default/lazer/meta_ham/Leaderboards` → `?Text@Leaderboards@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?rank_fmt@?CB@??Text@Leaderboards@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?rank_fmt@?CD@??Text@Leaderboards@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **848 B** `default/system/obj/Dir` → `?Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU23@@Z`
  - target `??_B?CA@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU23@@Z@5CA@`
  - ours   `??_B?CK@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU23@@Z@5CK@`
- **848 B** `default/system/obj/Dir` → `?Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU23@@Z`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU34@@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU34@@Z@4VDebugNotifyOncer@@A`
- **836 B** `default/system/utl/Symbol` → `?Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z`
  - target `??_B?CA@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@5CA@`
  - ours   `??_B?CK@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@5CK@`
- **836 B** `default/system/utl/Symbol` → `?Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@4VDebugNotifyOncer@@A`
- **804 B** `default/system/utl/MemTracker` → `?Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV2@@Z`
  - target `??_B?CA@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV2@@Z@5CA@`
  - ours   `??_B?CK@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV2@@Z@5CK@`
- **804 B** `default/system/utl/MemTracker` → `?Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV2@@Z`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV3@@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV3@@Z@4VDebugNotifyOncer@@A`
- **748 B** `default/lazer/meta_ham/CampaignSongSelectPanel` → `?Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?campaign_song_locked@?BF@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?campaign_song_locked@?BJ@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **748 B** `default/lazer/meta_ham/CampaignSongSelectPanel` → `?Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?song_select_song_prefix@?CH@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?song_select_song_prefix@?CJ@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **748 B** `default/lazer/meta_ham/CampaignSongSelectPanel` → `?Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?tan_battle_song@?BD@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?tan_battle_song@?BH@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **748 B** `default/lazer/meta_ham/ShellInput` → `?Init@ShellInput@@QAAXXZ`
  - target `?$S2@?1??Init@ShellInput@@QAAXXZ@4IA`
  - ours   `?$S2@?BB@??Init@ShellInput@@QAAXXZ@4IA`
- **748 B** `default/lazer/meta_ham/ShellInput` → `?Init@ShellInput@@QAAXXZ`
  - target `?reset_controller_mode_timeout@?1??Init@ShellInput@@QAAXXZ@4VSymbol@@A`
  - ours   `?reset_controller_mode_timeout@?BB@??Init@ShellInput@@QAAXXZ@4VSymbol@@A`
- **732 B** `default/system/meta/PreloadPanel` → `?OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z`
  - target `?msg@?9??OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z@4VMessage@@A`
  - ours   `?msg@?M@??OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z@4VMessage@@A`
- **584 B** `default/system/ui/UI` → `?FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z`
  - target `?button_down@?4??FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z@4VSymbol@@A`
  - ours   `?button_down@?6??FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z@4VSymbol@@A`
- **568 B** `default/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel` → `?Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?mq_difficulty@?BD@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4AAVDataNode@@A`
  - ours   `?mq_difficulty@?BH@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4AAVDataNode@@A`
- **568 B** `default/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel` → `?Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?stars_fraction@?BD@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?stars_fraction@?BH@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **568 B** `default/system/char/ClipCollide` → `?SyncWaypoint@ClipCollide@@IAAXXZ`
  - target `?$S4@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4IA`
  - ours   `?$S3@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4IA`
- **568 B** `default/system/char/ClipCollide` → `?SyncWaypoint@ClipCollide@@IAAXXZ`
  - target `?back@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
  - ours   `?back@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
- **568 B** `default/system/char/ClipCollide` → `?SyncWaypoint@ClipCollide@@IAAXXZ`
  - target `?front@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
  - ours   `?front@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
- **568 B** `default/system/char/ClipCollide` → `?SyncWaypoint@ClipCollide@@IAAXXZ`
  - target `?left@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
  - ours   `?left@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
- **568 B** `default/system/char/ClipCollide` → `?SyncWaypoint@ClipCollide@@IAAXXZ`
  - target `?right@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
  - ours   `?right@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A`
- **540 B** `default/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel` → `?UpdateList@CampaignMqCrewProvider@@QAAXXZ`
  - target `?mq_difficulty@?7??UpdateList@CampaignMqCrewProvider@@QAAXXZ@4AAVDataNode@@A`
  - ours   `?mq_difficulty@?M@??UpdateList@CampaignMqCrewProvider@@QAAXXZ@4AAVDataNode@@A`
- **508 B** `default/lazer/meta_ham/SongSelectPlaylistPanel` → `?Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `?playlist_create@?BB@??Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
  - ours   `?playlist_create@?BJ@??Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4VSymbol@@A`
- **404 B** `default/system/obj/DataFunc` → `?Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z`
  - target `?d@?5??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@4VDataArrayPtr@@A`
  - ours   `?d@?6??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@4VDataArrayPtr@@A`
- **388 B** `default/system/ui/UI` → `?OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z`
  - target `?quick_cheat@?BC@??OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z@4VSymbol@@A`
  - ours   `?quick_cheat@?BF@??OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z@4VSymbol@@A`
- **348 B** `default/system/os/DateTime` → `?MonthToken@?A0x223d738b@@YA?AVSymbol@@H@Z`
  - target `?month_symbols@?1??MonthToken@?A0x53f5bb0a@@YA?AVSymbol@@H@Z@4PAV3@A`
  - ours   `?month_symbols@?6??MonthToken@?A0x53f5bb0a@@YA?AVSymbol@@H@Z@4PAV3@A`
- **332 B** `default/system/obj/Object` → `?ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z`
  - target `?$S6@?4??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4IA`
  - ours   `?$S4@?9??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4IA`
- **332 B** `default/system/obj/Object` → `?ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z`
  - target `?msg@?4??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4VMessage@@A`
  - ours   `?msg@?9??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4VMessage@@A`
- **272 B** `default/system/rndobj/CamAnim` → `??$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `??_B?5???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@55`
  - ours   `??_B?L@???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@5L@`
- **272 B** `default/system/rndobj/CamAnim` → `??$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?frame@?5???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?frame@?L@???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **272 B** `default/system/rndobj/CamAnim` → `??$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
  - target `?value@?8???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
  - ours   `?value@?O@???$PropSync@M@@YA_NAAV?$Key@M@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z@4VSymbol@@A`
- **28 B** `default/system/hamobj/RhythmBattle` → `??__Fmsg@?PC@??OnBeat@RhythmBattle@@AAAXXZ@YAXXZ`
  - target `?msg@?PC@??OnBeat@RhythmBattle@@AAAXXZ@4VMessage@@A`
  - ours   `?msg@?PK@??OnBeat@RhythmBattle@@AAAXXZ@4VMessage@@A`
- **28 B** `default/system/meta/PreloadPanel` → `??__Fmsg@?9??OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z@YAXXZ`
  - target `?msg@?9??OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z@4VMessage@@A`
  - ours   `?msg@?M@??OnMsg@PreloadPanel@@AAA?AVDataNode@@ABVUITransitionCompleteMsg@@@Z@4VMessage@@A`
- **28 B** `default/system/meta/StorePanel` → `??__Fmsg@?EF@??Poll@StorePanel@@UAAXXZ@YAXXZ`
  - target `?msg@?EF@??Poll@StorePanel@@UAAXXZ@4VMessage@@A`
  - ours   `?msg@?EM@??Poll@StorePanel@@UAAXXZ@4VMessage@@A`
- **28 B** `default/system/obj/Object` → `??__Fmsg@?7??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@YAXXZ`
  - target `?msg@?4??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4VMessage@@A`
  - ours   `?msg@?9??ExportPropertyChange@Object@Hmx@@AAAXPAVDataArray@@VSymbol@@@Z@4VMessage@@A`
- **12 B** `default/system/char/CharLookAt` → `??__F_dw@?DI@??Poll@CharLookAt@@UAAXXZ@YAXXZ`
  - target `?_dw@?DI@??Poll@CharLookAt@@UAAXXZ@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?DO@??Poll@CharLookAt@@UAAXXZ@4VDebugNotifyOncer@@A`
- **12 B** `default/system/obj/DataFunc` → `??__Fd@?5??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@YAXXZ`
  - target `?d@?5??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@4VDataArrayPtr@@A`
  - ours   `?d@?6??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@4VDataArrayPtr@@A`
- **12 B** `default/system/obj/Dir` → `??__F_dw@?CA@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU23@@Z@YAXXZ`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU34@@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PBDUEntry@ObjectDir@@@@QAAPAUEntry@ObjectDir@@ABU34@@Z@4VDebugNotifyOncer@@A`
- **12 B** `default/system/utl/MemTracker` → `??__F_dw@?CA@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV2@@Z@YAXXZ`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV3@@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PAXPAVAllocInfo@@@@QAAPAPAVAllocInfo@@ABQAV3@@Z@4VDebugNotifyOncer@@A`
- **12 B** `default/system/utl/Symbol` → `??__F_dw@?CA@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@YAXXZ`
  - target `?_dw@?CA@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?CK@??Insert@?$KeylessHash@PBDPBD@@QAAPAPBDABQBD@Z@4VDebugNotifyOncer@@A`
- **12 B** `default/system/world/Crowd` → `??__F_dw@?CK@??DrawShowing@WorldCrowd@@UAAXXZ@YAXXZ`
  - target `?_dw@?CK@??DrawShowing@WorldCrowd@@UAAXXZ@4VDebugNotifyOncer@@A`
  - ours   `?_dw@?DG@??DrawShowing@WorldCrowd@@UAAXXZ@4VDebugNotifyOncer@@A`

### STRING_LITERAL — 21 pairs

- **1104 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/AAFilter` → `?calculateCoeffs@AAFilter@soundtouch@@IAAXXZ`
  - target `??_C@_0BI@NKFBOHBE@soundtouch?2AAFilter?4cpp?$AA@`
  - ours   `??_C@_0N@JNMAMKG@AAFilter?4cpp?$AA@`
- **640 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/TDStretch` → `?processSamples@TDStretch@soundtouch@@IAAXXZ`
  - target `??_C@_0BJ@PDHCMCAA@soundtouch?2TDStretch?4cpp?$AA@`
  - ours   `??_C@_0O@NGMOJNEL@TDStretch?4cpp?$AA@`
- **436 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/SoundTouch` → `?calcEffectiveRateAndTempo@SoundTouch@soundtouch@@AAAXXZ`
  - target `??_C@_0BK@KPNPAMCC@soundtouch?2SoundTouch?4cpp?$AA@`
  - ours   `??_C@_0P@EOPECIGF@SoundTouch?4cpp?$AA@`
- **436 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/SoundTouch` → `?putSamples@SoundTouch@soundtouch@@UAAXPBMI@Z`
  - target `??_C@_0BK@KPNPAMCC@soundtouch?2SoundTouch?4cpp?$AA@`
  - ours   `??_C@_0P@EOPECIGF@SoundTouch?4cpp?$AA@`
- **428 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/RateTransposer` → `?downsample@RateTransposer@soundtouch@@IAAXPBMI@Z`
  - target `??_C@_0BO@FFGILFFO@soundtouch?2RateTransposer?4cpp?$AA@`
  - ours   `??_C@_0BD@MNJCBEGB@RateTransposer?4cpp?$AA@`
- **396 B** `default/system/os/HolmesClient` → `?WaitForAnyResponse@?A0x49b544a7@@YAXW4Protocol@Holmes@@@Z`
  - target `??_C@_0BL@FBCBGAJI@Holmes?3?3WaitForAnyResponse?$AA@`
  - ours   `??_C@_0CK@FEFLNDPF@?$GAanonymous?9namespace?8?3?3WaitForAn@`
- **328 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/FIFOSampleBuffer` → `?ensureCapacity@FIFOSampleBuffer@soundtouch@@AAAXI@Z`
  - target `??_C@_0CA@POHIMCGA@soundtouch?2FIFOSampleBuffer?4cpp?$AA@`
  - ours   `??_C@_0BF@HHHODNI@FIFOSampleBuffer?4cpp?$AA@`
- **308 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/RateTransposer` → `?processSamples@RateTransposer@soundtouch@@IAAXPBMI@Z`
  - target `??_C@_0BO@FFGILFFO@soundtouch?2RateTransposer?4cpp?$AA@`
  - ours   `??_C@_0BD@MNJCBEGB@RateTransposer?4cpp?$AA@`
- **268 B** `default/system/synth_xbox/Voice` → `?Stop@Voice@@QAAX_N@Z`
  - target `??_C@_0BE@LFCAHGIK@mPoolVoice?4egParams?$AA@`
  - ours   `??_C@_0BA@IBPHIOKF@mEnvelopeParams?$AA@`
- **244 B** `default/system/rndobj/AmbientOcclusion` → `?BuildSHCoeff@RndAmbientOcclusion@@IBAXABVVector3@@PAM@Z`
  - target `??_C@_0CM@NEJDJMOP@Abs?$CI1?40f?5?9?5Length?$CIinVector?$CJ?$CJ?5?$DM?$DN?5@`
  - ours   `??_C@_0BE@NAFKJDLK@diff?5?$DM?$DN?5kSmallFloat?$AA@`
- **240 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/RateTransposer` → `?setChannels@RateTransposer@soundtouch@@QAAXH@Z`
  - target `??_C@_0BO@FFGILFFO@soundtouch?2RateTransposer?4cpp?$AA@`
  - ours   `??_C@_0BD@MNJCBEGB@RateTransposer?4cpp?$AA@`
- **236 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/TDStretch` → `?acceptNewOverlapLength@TDStretch@soundtouch@@IAAXH@Z`
  - target `??_C@_0BJ@PDHCMCAA@soundtouch?2TDStretch?4cpp?$AA@`
  - ours   `??_C@_0O@NGMOJNEL@TDStretch?4cpp?$AA@`
- **208 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/TDStretch` → `?setChannels@TDStretch@soundtouch@@QAAXH@Z`
  - target `??_C@_0BJ@PDHCMCAA@soundtouch?2TDStretch?4cpp?$AA@`
  - ours   `??_C@_0O@NGMOJNEL@TDStretch?4cpp?$AA@`
- **200 B** `default/system/os/UsbMidiGuitar` → `?Initialize@Queue@@QAAXH@Z`
  - target `??_C@_0DC@LGNOKIGB@e?3?2lazer_build_gmc1?2system?2src?2o@`
  - ours   `??_C@_0CL@DPNOGJEF@e?3?2lazer_build_gmc1?2system?2src?2m@`
- **176 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/TDStretch` → `?calculateOverlapLength@TDStretch@soundtouch@@IAAXH@Z`
  - target `??_C@_0BJ@PDHCMCAA@soundtouch?2TDStretch?4cpp?$AA@`
  - ours   `??_C@_0O@NGMOJNEL@TDStretch?4cpp?$AA@`
- **168 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/SoundTouch` → `?setOutPipe@FIFOProcessor@soundtouch@@IAAXPAVFIFOSamplePipe@2@@Z`
  - target `??_C@_0EE@FJGDBIHM@e?3?2lazer_build_gmc1?2system?2src?2s@`
  - ours   `??_C@_0FI@OFLOBHAN@e?3?2lazer_build_gmc1?2system?2src?2s@`
- **164 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/FIFOSampleBuffer` → `??0FIFOSampleBuffer@soundtouch@@QAA@H@Z`
  - target `??_C@_0CA@POHIMCGA@soundtouch?2FIFOSampleBuffer?4cpp?$AA@`
  - ours   `??_C@_0BF@HHHODNI@FIFOSampleBuffer?4cpp?$AA@`
- **156 B** `default/system/rnddx9/TexMgr` → `?ReserveRes@?$ResMgr@X@@QAAXVCRC@Hmx@@PAX@Z`
  - target `??_C@_0BD@FBKKEIMD@res?4Data?$CI?$CJ?5?$DN?$DN?5NULL?$AA@`
  - ours   `??_C@_0BB@BNEBIPAG@res?4mRes?5?$DN?$DN?5NULL?$AA@`
- **148 B** `default/system/flow/FlowSlider` → `?UpdateEase@FlowSlider@@IAAXXZ`
  - target `??_C@_0CN@GGKIFLBP@e?3?2lazer_build_gmc1?2system?2src?2m@`
  - ours   `??_C@_0P@CCHKPLEA@FlowSlider?4cpp?$AA@`
- **148 B** `default/system/synth_xbox/soundtouch/source/SoundTouch/FIFOSampleBuffer` → `?setChannels@FIFOSampleBuffer@soundtouch@@QAAXH@Z`
  - target `??_C@_0CA@POHIMCGA@soundtouch?2FIFOSampleBuffer?4cpp?$AA@`
  - ours   `??_C@_0BF@HHHODNI@FIFOSampleBuffer?4cpp?$AA@`
- **84 B** `default/system/os/UsbMidiGuitar` → `??1Queue@@QAA@XZ`
  - target `??_C@_0DC@LGNOKIGB@e?3?2lazer_build_gmc1?2system?2src?2o@`
  - ours   `??_C@_0CL@DPNOGJEF@e?3?2lazer_build_gmc1?2system?2src?2m@`

### LOCAL_STATIC_RENAME — 1 pairs

- **4088 B** `default/lazer/meta_ham/MetagameRank` → `?compare_deferred_points@@YA_NUDeferredPoints@@0@Z`
  - target `??__Faward_sort_indices@?1??compare_deferred_points@@YA_NUDeferredPoints@@0@Z@YAXXZ`
  - ours   `??__Faward_sort_map@?1??compare_deferred_points@@YA_NUDeferredPoints@@0@Z@YAXXZ`

### LOCAL_STATIC_KIND_CHANGE — 7 pairs

- **748 B** `default/lazer/meta_ham/CampaignSongSelectPanel` → `?Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `??_B?BD@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@5BD@`
  - ours   `?$S1@?BH@??Text@CampaignSongProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4IA`
- **584 B** `default/system/ui/UI` → `?FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z`
  - target `??_B?4??FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z@54`
  - ours   `?$S7@?6??FillButtonMsg@Automator@@AAAXAAVButtonDownMsg@@H@Z@4IA`
- **568 B** `default/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel` → `?Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `??_B?BD@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@5BD@`
  - ours   `?$S1@?BH@??Text@CampaignMqCrewProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4IA`
- **540 B** `default/lazer/meta_ham/CampaignMasterQuestCrewSelectPanel` → `?UpdateList@CampaignMqCrewProvider@@QAAXXZ`
  - target `??_B?7??UpdateList@CampaignMqCrewProvider@@QAAXXZ@57`
  - ours   `?$S2@?M@??UpdateList@CampaignMqCrewProvider@@QAAXXZ@4IA`
- **508 B** `default/lazer/meta_ham/SongSelectPlaylistPanel` → `?Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`
  - target `??_B?BB@??Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@5BB@`
  - ours   `?$S2@?BJ@??Text@SongSelectPlaylistProvider@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z@4IA`
- **404 B** `default/system/obj/DataFunc` → `?Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z`
  - target `??_B?5??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@55`
  - ours   `?$S2@?6??Filter@DataMergeFilter@@UAA?AW4Action@MergeFilter@@PAVObject@Hmx@@0PAVObjectDir@@@Z@4IA`
- **388 B** `default/system/ui/UI` → `?OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z`
  - target `??_B?BC@??OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z@5BC@`
  - ours   `?$S12@?BF@??OnCheatInvoked@Automator@@AAA?AVDataNode@@PBVDataArray@@@Z@4IA`

### SPLIT_CONFIG_NAMING — 20 pairs

- **420 B** `default/system/world/Crowd` → `?BuildBillboard@WorldCrowd@@IAAPAVRndMesh@@PAVCharacter@@M@Z`
  - target `?gImpostorCamera@@3PAVRndCam@@A`
  - ours   `gImpostorCamera`
- **328 B** `default/system/obj/DataFunc` → `?DataFilterNotify@@YA?AVDataNode@@PAVDataArray@@@Z`
  - target `?sNotifyMsg@@3PAVDataArray@@A`
  - ours   `sNotifyMsg`
- **252 B** `default/system/rndobj/Graph` → `?Free@RndGraph@@SAXPBX_N@Z`
  - target `?sFakes@@3V?$list@UFakeGraph@@V?$StlNodeAlloc@UFakeGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sFakes`
- **252 B** `default/system/rndobj/Graph` → `?Free@RndGraph@@SAXPBX_N@Z`
  - target `?sGraphs@@3PAV?$list@PAVRndGraph@@V?$StlNodeAlloc@PAVRndGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sGraphs`
- **220 B** `default/system/rndobj/Graph` → `?Get@RndGraph@@SAPAV1@PBX@Z`
  - target `?sGraphs@@3PAV?$list@PAVRndGraph@@V?$StlNodeAlloc@PAVRndGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sGraphs`
- **216 B** `default/system/rndobj/Graph` → `?DrawAll@RndGraph@@SAXXZ`
  - target `?sCam@@3V?$ObjPtr@VRndCam@@@@A`
  - ours   `sCam`
- **216 B** `default/system/rndobj/Graph` → `?DrawAll@RndGraph@@SAXXZ`
  - target `?sFakes@@3V?$list@UFakeGraph@@V?$StlNodeAlloc@UFakeGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sFakes`
- **216 B** `default/system/rndobj/Graph` → `?DrawAll@RndGraph@@SAXXZ`
  - target `?sOneFrame@@3PAVRndGraph@@A`
  - ours   `sOneFrame`
- **208 B** `default/system/synth/BinkReader` → `??0BinkReader@@QAA@PAVFile@@PAVStandardStream@@@Z`
  - target `?BinkInit@@YAXXZ`
  - ours   `BinkInit`
- **180 B** `default/system/rndobj/Graph` → `?Init@RndGraph@@SAXXZ`
  - target `?sGraphs@@3PAV?$list@PAVRndGraph@@V?$StlNodeAlloc@PAVRndGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sGraphs`
- **148 B** `default/system/rndobj/Graph` → `?GetOneFrame@RndGraph@@SAPAV1@XZ`
  - target `?sOneFrame@@3PAVRndGraph@@A`
  - ours   `sOneFrame`
- **140 B** `default/system/rndobj/Graph` → `?ResetAll@RndGraph@@SAXXZ`
  - target `?sOneFrame@@3PAVRndGraph@@A`
  - ours   `sOneFrame`
- **136 B** `default/system/rnddx9/Rnd_Xbox` → `?InitRenderState@DxRnd@@QAAXXZ`
  - target `?D3DXSetDXT3DXT5@@YAXH@Z`
  - ours   `D3DXSetDXT3DXT5`
- **108 B** `default/system/rndobj/PropAnim` → `?OnReplaceKeyframe@RndPropAnim@@QAA?AVDataNode@@PAVDataArray@@@Z`
  - target `?sReplaceKey@@3_NA`
  - ours   `sReplaceKey`
- **84 B** `default/system/rndobj/Graph` → `??__EsFakes@@YAXXZ`
  - target `?sFakes@@3V?$list@UFakeGraph@@V?$StlNodeAlloc@UFakeGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sFakes`
- **84 B** `default/system/rndobj/PropAnim` → `?OnReplaceFrame@RndPropAnim@@QAA?AVDataNode@@PAVDataArray@@@Z`
  - target `?sFrameReplace@@3MA`
  - ours   `sFrameReplace`
- **60 B** `default/system/rndobj/Graph` → `??__FsCam@@YAXXZ`
  - target `?sCam@@3V?$ObjPtr@VRndCam@@@@A`
  - ours   `sCam`
- **28 B** `default/system/rndobj/PropAnim` → `?OnRemoveKeyframe@RndPropAnim@@QAA?AVDataNode@@PAVDataArray@@@Z`
  - target `?sRemoveFrame@@3_NA`
  - ours   `sRemoveFrame`
- **16 B** `default/system/rndobj/Graph` → `?SetCamera@RndGraph@@SAXPAVRndCam@@@Z`
  - target `?sCam@@3V?$ObjPtr@VRndCam@@@@A`
  - ours   `sCam`
- **12 B** `default/system/rndobj/Graph` → `??__FsFakes@@YAXXZ`
  - target `?sFakes@@3V?$list@UFakeGraph@@V?$StlNodeAlloc@UFakeGraph@@@stlpmtx_std@@@stlpmtx_std@@A`
  - ours   `sFakes`

### STORAGE_CLASS_SKEW — 10 pairs

- **612 B** `default/system/obj/Dir` → `?Iterate@ObjectDir@@IAAXPAVDataArray@@_N@Z`
  - target `?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
  - ours   `?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
- **532 B** `default/lazer/meta_ham/HamSongMgr` → `?SongAudioData@HamSongMgr@@UBAPAVSongInfo@@H@Z`
  - target `?SongBlock@SongMetadata@@QBAPAVSongInfo@@XZ`
  - ours   `?SongBlock@SongMetadata@@QBAPAVDataArraySongInfo@@XZ`
- **448 B** `default/system/os/HDCache` → `?ReadAsync@HDCache@@QAA_NHHPAX@Z`
  - target `?kArkBlockSize@@3HB`
  - ours   `?kArkBlockSize@@3HA`
- **332 B** `default/system/meta/StorePurchaser` → `?Initiate@XboxMultipleItemsPurchaser@@UAAXXZ`
  - target `?sOverlapped@XboxMultipleItemsPurchaser@@0U_XOVERLAPPED@@A`
  - ours   `?sOverlapped@?6??Initiate@XboxMultipleItemsPurchaser@@UAAXXZ@4U_XOVERLAPPED@@A`
- **252 B** `default/system/obj/Dir` → `?PreInit@ObjectDir@@SAXHH@Z`
  - target `?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
  - ours   `?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
- **160 B** `default/system/char/FileMerger` → `??1FileMerger@@UAA@XZ`
  - target `?sFmDeleting@FileMerger@@2PAVObject@Hmx@@A`
  - ours   `?sFmDeleting@FileMerger@@2PAV1@A`
- **132 B** `default/system/obj/Dir` → `??__EsSuperClassMap@@YAXXZ`
  - target `?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
  - ours   `?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
- **44 B** `default/system/obj/Dir` → `?Terminate@ObjectDir@@SAXXZ`
  - target `?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
  - ours   `?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
- **40 B** `default/system/os/Archive` → `?GetArkfileNumBlocks@Archive@@QBAHH@Z`
  - target `?kArkBlockSize@@3HB`
  - ours   `?kArkBlockSize@@3HA`
- **12 B** `default/system/obj/Dir` → `??__FsSuperClassMap@@YAXXZ`
  - target `?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`
  - ours   `?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A`

### LOCAL_STATIC_SCOPE_RENAME — 1 pairs

- **1132 B** `default/lazer/meta_ham/MainMenuPanel` → `?UpdateArtLoaders@MainMenuPanel@@AAAXXZ`
  - target `??__Fmsg@?CI@??UpdateArtLoaders@MainMenuPanel@@AAAXXZ@YAXXZ`
  - ours   `??__Futility_image_loaded@?CL@??UpdateArtLoaders@MainMenuPanel@@AAAXXZ@YAXXZ`

### ANON_NS_HASH — 8 pairs

- **352 B** `default/system/rnddx9/Rnd` → `?DrawRectDepth@DxRnd@@UAAXABVVector3@@AAY03$$CBV2@ABVVector4@@PAVRndMat@@W4ShaderType@@@Z`
  - target `?sDepthRectVerts@?A@@3PAUDepthRectVert@1@A`
  - ours   `?sDepthRectVerts@?A0xc0d8487b@@3PAUDepthRectVert@1@A`
- **176 B** `default/system/os/Joypad_Xbox` → `?XinputJoypadThreadStart@@YAXXZ`
  - target `?sThreadData@?A@@3U<unnamed-type-sThreadData>@1@A`
  - ours   `?sThreadData@?A0x439b694a@@3U<unnamed-type-sThreadData>@1@A`
- **172 B** `default/system/os/Joypad_Xinput` → `?TranslateStick@@YAXPADF_N1@Z`
  - target `?gXboxDeadzone@?A@@3MA`
  - ours   `?gXboxDeadzone@?A0xf503845b@@3MA`
- **112 B** `default/system/os/Joypad_Xinput` → `?JoypadInitXboxPCDeadzone@@YAXPAVDataArray@@@Z`
  - target `?gXboxDeadzone@?A@@3MA`
  - ours   `?gXboxDeadzone@?A0xf503845b@@3MA`
- **80 B** `default/system/os/Joypad_Xbox` → `?XinputJoypadThreadDestruction@@YAXXZ`
  - target `?sThreadData@?A@@3U<unnamed-type-sThreadData>@1@A`
  - ours   `?sThreadData@?A0x439b694a@@3U<unnamed-type-sThreadData>@1@A`
- **52 B** `default/Memory_Xbox` → `??0PhysMemTypeTracker@@QAA@VSymbol@@@Z`
  - target `?gPhysicalType@?A0x2be09a71@@3PBDB`
  - ours   `?gPhysicalType@?A0x2be09a71@@3PADA`
- **32 B** `default/Memory_Xbox` → `??1PhysMemTypeTracker@@QAA@XZ`
  - target `?gPhysicalType@?A0x2be09a71@@3PBDB`
  - ours   `?gPhysicalType@?A0x2be09a71@@3PADA`
- **20 B** `default/Memory_Xbox` → `??__EgPhysicalType@?A0x2be09a71@@YAXXZ`
  - target `?gPhysicalType@?A0x2be09a71@@3PBDB`
  - ours   `?gPhysicalType@?A0x2be09a71@@3PADA`

### DIFFERENT_SYMBOL — 15 pairs

- **296 B** `default/system/net/DingoAuthJob` → `?Start@AuthenticateReqJob@@UAAXXZ`
  - target `??$MakeString@PBDPBDPBD@@YAPBDPBDABQBD11@Z`
  - ours   `??$MakeString@$$BY01$$CBDPBDPBD@@YAPBDPBDAAY01$$CBDABQBD2@Z`
- **132 B** `default/system/gesture/BaseSkeleton` → `?MirrorJoint@BaseSkeleton@@SA?AW4SkeletonJoint@@W42@@Z`
  - target `?sJointParents@BaseSkeleton@@2QBW4SkeletonJoint@@B`
  - ours   `?gMirrorJoints@@3PAW4SkeletonJoint@@A`
- **108 B** `default/lazer/meta_ham/MetaPanel` → `??_DAppLabel@@QAAXXZ`
  - target `??1HamLabel@@UAA@XZ`
  - ours   `??1AppLabel@@UAA@XZ`
- **84 B** `default/system/obj/DataArray` → `??__EgConditional@@YAXXZ`
  - target `??__FgConditional@@YAXXZ`
  - ours   `??__FgDataArrayConditional@@YAXXZ`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderSimple@@YAXXZ`
  - target `?gShaderParticles@@3VRndShaderParticles@@A`
  - ours   `?gShaderSimple@@3VRndShaderSimple@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderParticles@@YAXXZ`
  - target `?gShaderMultimesh@@3VRndShaderMultimesh@@A`
  - ours   `?gShaderParticles@@3VRndShaderParticles@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderMultimesh@@YAXXZ`
  - target `?gShaderStandard@@3VRndShaderStandard@@A`
  - ours   `?gShaderMultimesh@@3VRndShaderMultimesh@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderStandard@@YAXXZ`
  - target `?gShaderPostProc@@3VRndShaderPostProc@@A`
  - ours   `?gShaderStandard@@3VRndShaderStandard@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderPostProc@@YAXXZ`
  - target `?gShaderDrawRect@@3VRndShaderDrawRect@@A`
  - ours   `?gShaderPostProc@@3VRndShaderPostProc@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderDrawRect@@YAXXZ`
  - target `?gShaderUnwrapUV@@3VRndShaderUnwrapUV@@A`
  - ours   `?gShaderDrawRect@@3VRndShaderDrawRect@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderUnwrapUV@@YAXXZ`
  - target `?gShaderVelocity@@3VRndShaderVelocity@@A`
  - ours   `?gShaderUnwrapUV@@3VRndShaderUnwrapUV@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderVelocity@@YAXXZ`
  - target `?gShaderVelocityCamera@@3VRndShaderVelocityCamera@@A`
  - ours   `?gShaderVelocity@@3VRndShaderVelocity@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderVelocityCamera@@YAXXZ`
  - target `?gShaderDepthVolume@@3VRndShaderDepthVolume@@A`
  - ours   `?gShaderVelocityCamera@@3VRndShaderVelocityCamera@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderDepthVolume@@YAXXZ`
  - target `?gShaderFur@@3VRndShaderFur@@A`
  - ours   `?gShaderDepthVolume@@3VRndShaderDepthVolume@@A`
- **20 B** `default/system/rndobj/Shader` → `??__FgShaderFur@@YAXXZ`
  - target `?gShaderSyncTrack@@3VRndShaderSyncTrack@@A`
  - ours   `?gShaderFur@@3VRndShaderFur@@A`

### IMMEDIATE_VS_SYMBOL — 2 pairs

- **396 B** `default/system/gesture/SpeechMgr` → `?Enable@SpeechMgr@@QAAX_N@Z`
  - target `lbl_8301000E`
  - ours   `14`
- **396 B** `default/system/gesture/SpeechMgr` → `?Enable@SpeechMgr@@QAAX_N@Z`
  - target `lbl_8301000E`
  - ours   `33537`

### C_LINKAGE_SKEW — 2 pairs

- **60 B** `default/system/utl/BinkIntegration` → `?BinkInit@@YAXXZ`
  - target `BinkSetIO`
  - ours   `?BinkSetIO@@YAXP6AHPAUBINKIO@@PBDI@Z@Z`
- **60 B** `default/system/utl/BinkIntegration` → `?BinkInit@@YAXXZ`
  - target `BinkSetMemory`
  - ours   `?BinkSetMemory@@YAXP6APAXI@ZP6AXPAX@Z@Z`

## Source fixes made by this lane

All verified at 100.0% on the graded ruler after a FULL `ninja`.

| function | bytes | before → after | fix |
|---|---:|---|---|
| `?Handle@HamDirector@@UAA?AVDataNode@@PAVDataArray@@_N@Z` | 9,004 | 99.99778% → 99.99778% (100.0% once aliased) | `HamDirector::OnSaveFaceAnims` → `OnSaveFaceanims`; the target's own spelling |
| `?OnMsg@ShellInput@@...LeftHandListEngagementMsg` | 672 | 99.4% → **100.0%** | local `voice_commander_help_hide_msg` → `voiceCommanderHelpHide` |
| `?ActivateProcessing@ChatReceiver@@QAAX_N@Z` | 412 | 99.03% → **100.0%** | `extern "C"` on `_xhv_voicechat_mode` in `src/xdk/xvh2/xvh2.h` |
| `?Handle@PlaylistHeaderNode@@UAA?AVDataNode@@PAVDataArray@@_N@Z` | 396 | 99.24% → **100.0%** | `HANDLE_SUPERCLASS(NavListSortNode)` → `HANDLE_SUPERCLASS(NavListHeaderNode)` |
| `?Init@JoypadClient@@AAAXXZ` | 312 | 99.68% → **100.0%** | `gDefaultHoldMs` / `gDefaultRepeatMs` moved into the file's existing unnamed namespace |

**1,792 B released outright**, plus 9,004 B waiting on one alias-file line.

`PlaylistHeaderNode` is a behavioural bug, not a cosmetic one: with
`HANDLE_SUPERCLASS(NavListSortNode)` every message the class did not handle
itself bypassed `NavListHeaderNode`'s handlers entirely. The name charge was the
only instrument reporting it.

### The `?Handle@HamDirector@@` verdict, in full

9,004 B rode on ONE charge:

```
target  bl ?OnGetOccluded@CamShot@@IAA?AVDataNode@@PAVDataArray@@@Z
ours    bl ?OnSaveFaceAnims@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z
```

The fold proof **refuses** this pair, and refusing is correct. Both functions are
`{ return 0; }`, so both compile to the same 16-byte, ZERO-relocation body.
Every unimplemented stub in the tree compiles to those same bytes, so identity
here carries no information about whether the *target* folded them. Byte-only
equality would have certified it; the tool's zero-relocation guard is exactly
what stops that.

The linker map settles it instead, and it settles it the other way from the
brief's expectation. The premise was that `?OnSaveFaceAnims@HamDirector@@`
"appears nowhere in dc3's map", and that map silence is evidence AGAINST a fold.
The first half is true; the conclusion is not. **0x82901c78 carries thirteen
names**, and one of them is

```
0005:005d1c78  ?OnSaveFaceanims@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z  82901c78 f  hamobj:HamDirector.obj
```

— lower-case `a` in `anims`. `grep -c OnSaveFaceAnims` over the map returns 0;
`OnSaveFaceanims` returns 1, at the fold address. The map was never silent about
this function. It was silent about a symbol that does not exist, because we
invented the capitalisation. The DTA message name (`save_face_anims`) was always
right; only the C++ method spelling was wrong.

After the rename the charge reads `?OnGetOccluded@CamShot@@` vs
`?OnSaveFaceanims@HamDirector@@`, which the map **CONFIRMS** as one fold class.
The row still measures 99.99778% because the existing alias group
`retailmap:?OnGetOccluded@CamShot@@...` @ 0x82901c78 lists ten folded members and
not ours — until this change no object in our build emitted that spelling.
Verified end to end by adding the member to the GENERATED (gitignored)
`build/373307D9/icf_aliases.map` and re-measuring: **100.0%, 9,004 B released**,
then restoring the artifact.

## Handover

### To the alias-install lane (owns `scripts/symbol_aliases.json`)

1. **Add one member to the existing group** `retailmap:?OnGetOccluded@CamShot@@IAA?AVDataNode@@PAVDataArray@@@Z` @ `0x82901c78`:
   `?OnSaveFaceanims@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z`.
   Evidence tier: **linker map** — co-listed at 0x82901c78 in
   `orig/373307D9/ham_xbox_r.map` line 65251. Worth 9,004 B.
   Two further names sit at that address and are also absent from the group
   (`??0CatLex@NUISPEECH@@QAA@XZ`, and the group already omits nothing else);
   they are not charged today, so they are optional.
2. **The three PROVEN_FOLD pairs listed above.** Evidence tier: **body test,
   within-build** — byte- and relocation-set-identical in our own objects with a
   non-zero relocation count. Worth 504 B. Note these are the weaker tier: the
   proof is a fact about OUR build, so if the target's sources for the two
   differed, the target did not fold them and our version of one is simply wrong
   in a way that coincidentally equals the other.

**Do not install anything for the REFUTED classes.** An alias there would hide a
real source difference permanently.

### To whoever owns `config/373307D9/symbols.txt`

20 pairs classified `SPLIT_CONFIG_NAMING` are not source bugs and not folds. MSVC
emits internal-linkage file-scope data with an UNMANGLED label (`sGraphs`,
`sFakes`, `sNotifyMsg`, `gImpostorCamera`); the target-side spellings
(`?sGraphs@@3PAV?$list@...A` etc.) exist only in `symbols.txt` and appear nowhere
in the shipped linker map. `?sSuperClassMap@ObjectDir@@2V...` is the clearest
case — the map's own `??__EsSuperClassMap@@YAXXZ` / `??__FsSuperClassMap@@YAXXZ`
mangle with `@@`, i.e. FILE scope, so the split config's guess that it is an
`ObjectDir` class static is wrong and **our spelling is the correct one**.

### To the anon-namespace lane

8 pairs are `ANON_NS_HASH` — same unnamed namespace, different `?A0x` hash.
`scripts/obj_anon_ns_patcher.py` owns this class; it is a build-environment
artifact, not a source difference.

## What remains, and what it is worth

| class | rows | bytes | owner |
|---|---:|---:|---|
| `LOCAL_STATIC_SCOPE_SKEW` — recover the scope/local-static count | 26 | 19,052 | open |
| `STRING_LITERAL` — `__FILE__` path prefixes | 21 | 6,516 | open, probably one build-config root cause |
| `LOCAL_STATIC_RENAME` — `compare_deferred_points`, see below | 1 | 4,088 | open, NOT cheap |
| `LOCAL_STATIC_KIND_CHANGE` + scope skew | 7 | 3,740 | open |
| `SPLIT_CONFIG_NAMING` | 17 | 2,640 | `config/373307D9/symbols.txt` owner |
| `STORAGE_CLASS_SKEW` | 10 | 2,564 | open |
| `LOCAL_STATIC_SCOPE_RENAME` — `MainMenuPanel::UpdateArtLoaders` | 1 | 1,132 | open |
| `ANON_NS_HASH` | 8 | 996 | `obj_anon_ns_patcher.py` lane |
| `DIFFERENT_SYMBOL` — genuinely wrong callees | 15 | 840 | open, per-site |
| `IMMEDIATE_VS_SYMBOL` | 1 | 396 | open |
| `C_LINKAGE_SKEW` | 1 | 60 | open |
| **REFUTED total** | **108** | **42,024** | |
| `UNDECIDABLE` | 7 | 2,016 | needs a fourth instrument (the target's own bytes) |

`LOCAL_STATIC_SCOPE_SKEW` is far and away the prize: 26 rows and 19,052 B, all
saying the same thing — the enclosing function has a different number of lexical
scopes or local statics than the target's did. Those are recoverable from the
scope indices themselves (the target's index tells you how many precede it), and
they cluster by function, so one structural fix can close several charges at
once.

Deliberately NOT done: the 4,088 B `compare_deferred_points` row in
`MetagameRank.cpp`. The charged pair is
`??__Faward_sort_indices@?1??...` (target) vs `??__Faward_sort_map@?1??...`
(ours) — the atexit destructor is registered for a differently-named static. The
obvious fix (rename our `std::map award_sort_map` to `award_sort_indices`) is
wrong as stated: our `static Symbol award_sort_indices[]` array already matches
the target's symbol of that name at the same scope, and `Symbol` is trivially
destructible, so the target must have a THIRD object we have not identified. Not
a cheap fix, and guessing here would trade a visible charge for an invisible one.

## Instrument notes worth keeping

**A one-sided instrument error is invisible to a two-sided control.** The COFF
body reader ported here carries rb3-xenon's `$EH`-prefix fix (commit 913a9623):
the interior 8-byte EH-prefix trim is bounded on the `$EH` MARKER and on the
prefix's own byte+relocation signature, never on whether the successor's name
looks like a funclet. dc3 has no EH-boundary patcher, so the marker rule never
fires here and the signature fallback does all the work — 933 interior prefixes
stripped. On rb3-xenon the un-fixed version made target-byte fold proof
IMPOSSIBLE for ~95 STLport functions while the within-build test stayed correct,
because the artifact cancels on both sides, and a whole lane was commissioned to
fix a bug that did not exist.

Reader parity on dc3, stated so it could fail: of the 27,700 functions
`report.json` scores at fuzzy 100, 27,683 resolve in our objects and **27,682
read exactly the size objdiff reports** (99.996%, one +4 outlier). If the
EH-prefix layout story were wrong the deltas would scatter.

**The strict relocation-NAME test is not transitively closed over known folds.**
Two template instantiations with identical bodies each call the element-type
`operator<<` for their own element type, and those callees are a fold class in
their own right. 265 of 2,611 map-CONFIRMED alias memberships fail the strict
test for that reason; `--equiv-json` resolves 256 of them, under the separate
and strictly weaker `PROVEN_MOD_ALIAS` verdict. It is kept separate because a
bad alias in the input can manufacture a proof that is then cited to install
another alias, and that loop must never close.

**`ninja <one>.obj` does not run the obj patchers.** Their stamps
(`anon_ns_patched`, `guard_patched`, `atexit_scope_patched`,
`bool_mangle_patched`, `dynamic_init_patched`) are separate ninja targets, so a
targeted build leaves the new `.obj` UNPATCHED. The `JoypadClient` fix measured
99.10% that way — WORSE than the 99.68% it started at — because the un-run
anon-namespace hash patcher left our `?A0xf41ae7e0` against the target's
`?A0x831dd776`. A full `ninja` puts it at 100.0%.

**9 map-confirmed alias memberships are refuted by our own bytes.** Not this
lane's bucket, recorded for whoever owns them: pairs the shipped linker folded
whose two bodies differ in OUR build, i.e. our decomp of one side is imperfect.
Five `MakeString` array-extent instantiations, the `bad_cast`/`bad_typeid`
constructors, and three 8–80 B pairs
(`?GetCacheName@CacheXbox@@` vs `?MaxDisplay@UIListState@@`,
`?Parent@Node@?$ObjPtrVec@...` vs `?MinDisplay@UIListState@@`,
`?StopLog@Debug@@` vs `?ClearAllTypeProps@Object@Hmx@@`).

### Follow-up, 2026-08-18 — all nine adjudicated; REFUTED is now 0

Worked by the `fix/self-refuted-folds` lane. Three were our source, two classes
were the proof's own limits. Nothing was edited to satisfy a tool.

| membership | wrong side | defect | outcome |
|---|---|---|---|
| `?GetCacheName@CacheXbox@@` ↔ `?MaxDisplay@UIListState@@` | ours (`MaxDisplay`) | returned a hardcoded `1`; retail returns `mMaxDisplay` (0x18) | fixed, `dbee4005a` |
| `?Parent@Node@?$ObjPtrVec@…` ↔ `?MinDisplay@UIListState@@` | ours (`MinDisplay`) | returned a hardcoded `1`; retail returns `mMinDisplay` (0x10) | fixed, `dbee4005a` |
| `?StopLog@Debug@@` ↔ `?ClearAllTypeProps@Object@Hmx@@` | ours (`ClearAllTypeProps`) | a redundant `if (mTypeProps)` around `RELEASE`, which hoists the null test and branches past the `= null` store | fixed, `45599639a` |
| 5 × `MakeString` array-extent rows | NEITHER — instrument | `net_xbox` is a `/GS` cflags group, so `MakeString<…>` is 116 B there and 88 B elsewhere; all 17 occupants of 0x82563b08 are `net:` objects and the target body is 0x74 = 116 B, so the linker kept a `net` copy, but our build emits that spelling only from `obj/DirLoader.obj` and `rndobj/TransAnim.obj` | `COMDAT_SELECTION_MISSING`, `ae174bedc` |
| `??_7bad_typeid@std@@6B@` ↔ `??_7bad_cast@` / `??_7__non_rtti_object@` | NEITHER — instrument | 8 all-zero bytes, 2 relocations, differing only in which `??_E<class>` slot 0 names — and the map co-lists those three destructors at 0x8299dc60 | `PROVEN_MOD_MAP`, `ae174bedc` |

The three source fixes are metric-invisible in both directions: none of
`?MinDisplay@UIListState@@`, `?MaxDisplay@UIListState@@` or
`?ClearAllTypeProps@Object@Hmx@@` survives into `config/373307D9/symbols.txt`
(the linker folded them away), and a full `ninja` before and after in one tree
gives 29,398 matched functions / 4,950,860 matched code bytes both ways.
`MinDisplay`/`MaxDisplay` are nonetheless live bugs — `UIList::Save` serialised
them, `UIList::Copy` copied `1`/`1` over the real values, and HamNavList's
scroll arithmetic used a constant 1 where `mMinDisplay` defaults to 0 and
`mMaxDisplay` defaults to −1 ("no limit", which callers test for explicitly).

Positive control after the lane, 2,611 → 2,612 map-confirmed body-test alias
memberships (one more because the census's pair enumeration is inclusive of the
group's own survivor row): **PROVEN_FOLD 1,400 · PROVEN_MOD_MAP 257 ·
UNDECIDABLE 955 · REFUTED 0.** The 257 formerly `PROVEN_MOD_ALIAS` rows are all
independently bridged by the map, which is a validation of
`scripts/symbol_aliases.json` rather than a weakening of it.

One thing this lane found and did NOT fix, recorded so it is not rediscovered as
a fold problem: the target's
`??$MakeString@$$BY0O@$$CBDH$$BY04$$CBD@@…` at 0x82563b08 (116 B) is scored a
**0% stub** in `default/system/net/JsonUtils`, because no net TU of ours
instantiates that exact `(char[14], int, char[5])` extent triple — retail's came
from `WebSvcMgr.obj`. That is a format-string-length gap in the `net` sources,
visible in the metric and not hidden by ICF, so it belongs to a different lane.

## Appendix — reproducing every number here

```bash
# 1. census: every relocation-NAME charge, on the graded ruler, with the row it gates
python3 scripts/analysis/name_charge_census.py --project . \
    --map orig/373307D9/ham_xbox_r.map --clean-only \
    --json-out /tmp/census_rows.json --pairs-json /tmp/census_pairs.json

# 2. adjudicate the bucket the map cannot settle
python3 scripts/analysis/fold_adjudicate.py --pairs /tmp/census_pairs.json \
    --objects build/373307D9/src --include-data \
    --map orig/373307D9/ham_xbox_r.map \
    --equiv-json scripts/symbol_aliases.json \
    --symbols-txt config/373307D9/symbols.txt \
    --only-map-verdict NOT_IN_MAP --json-out /tmp/adjud.json

# 3. one pair on its own
python3 scripts/analysis/fold_proof.py --objects build/373307D9/src --include-data \
    --pair '?DeleteChecksum@FileStream@@AAAXXZ' '?DeleteChecksum@BufStream@@QAAXXZ'

# 3b. (2026-08-18) with the linker map: adds the PROVEN_MOD_MAP tier and the
#     COMDAT_SELECTION_MISSING guard.  Off by default -- omitting --map
#     reproduces every number above unchanged.
python3 scripts/analysis/fold_proof.py --objects build/373307D9/src --include-data \
    --map orig/373307D9/ham_xbox_r.map \
    --pair '??_7bad_typeid@std@@6B@' '??_7bad_cast@std@@6B@'

# 4. the port's positive/negative controls
python3 scripts/analysis/ruler.py --selftest .
```

Ported tools, all under `scripts/analysis/`: `coffx.py` (COFF reader, verbatim
from rb3-xenon), `coff_bodies.py` (EH-aware function + data COMDAT slicing),
`fold_proof.py`, `ruler.py`, `name_charge_census.py`, `fold_adjudicate.py`.
