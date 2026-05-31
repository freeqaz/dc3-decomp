# Stream 1 — Partial-Port Harvest (og-dc3 → 100%)

**Read `docs/plans/port-harvest/WORKFLOW.md` first** — it has the full worktree + subagent
+ merge mechanics this stream depends on.

## Goal
Convert our partial functions (currently 1-99%) to **exact 100%** by porting og-dc3's
already-matched source. Highest **hit rate** (~75-85%) and **COMPLETE-count** of the three
streams; modest fuzzy% per the WORKFLOW caveat.

## Scope (as of 2026-05-31, regenerate to refresh)
**255 functions / 164K bytes across ~150 units** where og-dc3 matches better than us and we
already have a partial impl. Worklist: `docs/plans/port-harvest/stream1-partial-ports.json`
(fields: symbol, unit, our, og, gain, size).

Top units by candidate count:
CharHair(8), PlatformMgr_Xbox(6), StorePanel(6), DirLoader(5), Voice(5), VorbisReader(4),
DataFile(4), CharLipSync(4), SpotlightDrawer(4), Character(4), TexMovie(4), BinkMovieImpl(3),
CharBonesSamples(3), Splash(3), … (long tail of ~140 units with 1-3 each).

Already harvested (2026-05-30, skip / only residual floors remain): PartyModeMgr, HolmesClient,
WorldCrowd, CharEyes, StorePanel*, Game, CharBones, oggvorbis/psy+mapping0, world/Instance,
flow/FlowAnimate, Character*, os/System, BustAMovePanel, rndobj/Mesh, DirLoader*, LightPreset.
(* = partially done; remaining entries are mostly confirmed regalloc/FPR floors — verify with
`run_objdiff` before spending effort; floors are tagged in decomp.db verdict_reason.)

## Method
Standard port recipe (WORKFLOW.md). Run in **diverse-subsystem waves of 5-6 units**, **≤1
char-family unit per wave** (CharHair/CharLipSync/Character/CharBonesSamples share char headers —
don't edit them in parallel or header changes collide at merge). Per unit: get the candidate
list, port each function from og-dc3, validate, regression-sweep, commit, merge, gate.

Per-unit candidate pull:
```bash
python3 -c "import json;[print(x['our'],x['size'],x['symbol']) for x in json.load(open('docs/plans/port-harvest/stream1-partial-ports.json')) if x['unit']=='<UNIT>']"
```

## Wave plan (suggested first 2 waves, diverse subsystems)
- Wave A: CharHair (Opus, char), Voice (Sonnet, synth_xbox), DataFile (Sonnet, obj), SpotlightDrawer (Sonnet, world), TexMovie (Sonnet, movie)
- Wave B: CharLipSync (Opus, char), VorbisReader (Sonnet, synth — note alloca, see Stream 2 codec.h history), Splash (Sonnet), CharBonesSamples (Opus, char-math), + next-highest from the long tail
Then continue down the per-unit count until the list is exhausted. ~45-50 units of real work.

## Done = success criteria
Each wave: agents report per-fn baseline→final (run_objdiff-confirmed) + 100? y/n + full-unit
regression result. Merge clean FFs, run the build+sync gate, require **0 complete-functions
broken** (small at-limit sibling nicks acceptable). Update memory if you find a new systemic fix.

## Can be run unattended
This stream is mechanical and proven — a good candidate for an autonomous loop driving
wave-after-wave until the worklist is exhausted, with the build+sync gate as the safety check.
