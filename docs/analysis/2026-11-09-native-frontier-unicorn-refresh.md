# Native frontier: unicorn re-measured, 2026-11-09

Every stored `unicorn_verdict` in `decomp.db` was taken on or before 2026-08-19
(29,350 rows at h4/build `eda64e956`, 58 at h1) while hundreds of source commits
landed since. This re-runs the 223 non-100 rows that the native port compiles,
so the behavioural picture describes the tree as it is.

Reproduce with `scripts/analysis/refresh_native_frontier.py` (added alongside this
note). Verdicts below come from `scripts/unicorn_runner/run.py --unit --symbol --json`.

## Result

| verdict | rows |
|---|---|
| EQUIVALENT | 147 |
| DIVERGENT | 71 |
| SKIPPED | 5 |
| **total** | **223** |

## What changed against the stale database

| stored verdict | re-measured | rows |
|---|---|---|
| EQUIVALENT | EQUIVALENT | 123 |
| DIVERGENT | DIVERGENT | 67 |
| DIVERGENT | EQUIVALENT | 17 |
| NONE | EQUIVALENT | 7 |
| NONE | SKIPPED | 5 |
| NONE | DIVERGENT | 3 |
| EQUIVALENT | DIVERGENT | 1 |

`DIVERGENT -> EQUIVALENT` means the stored verdict no longer holds. That is not
necessarily a fix: the stored verdict may have been wrong, or the source moved.
Either way, any conclusion drawn from the old verdict about these rows was unsafe.

### Flipped out of divergence (17)

- `??DHmx@@YA?AVMatrix4@0@ABV10@0@Z` (99.99329)
- `?UtilDrawPlane@@YAXABVPlane@@ABVVector3@@ABVColor@Hmx@@HM_N@Z` (99.98421)
- `?PreLoad@ObjectDir@@UAAXAAVBinStream@@@Z` (99.89712)
- `?ClosestMove@HamDirector@@IAA?AVSymbol@@XZ` (99.34641)
- `?Save@TypeProps@@QAAXAAVBinStream@@@Z` (99.28673)
- `?DrawShowing@RndFlare@@UAAXXZ` (98.94405)
- `?DrawShowing@RndText@@UAAXXZ` (98.80604)
- `?Select@NgEnviron@@UAAXPBVVector3@@@Z` (98.55215)
- `?Clip@@YAXABVPolygon@Hmx@@ABVRay@2@AAV12@@Z` (98.43750)
- `?SetupCharacter@FontMap@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z` (98.40945)
- `??0BmpCache@HiResScreen@@QAA@II@Z` (98.29060)
- `?UtilDrawCigar@@YAXABVTransform@@QBM1ABVColor@Hmx@@H@Z` (98.01835)
- `?SmoothResults@RndAmbientOcclusion@@IBAXPAVRndMesh@@@Z` (96.11723)
- `?SetCharInfo@RndFont@@IAAXPAUCharInfo@1@AAVRndBitmap@@ABVVector2@@H@Z` (95.60000)
- `?UpdateLine@RndLine@@IAAXPAVPoint@1@0@Z` (92.34319)
- `?DrawMeter@Synth@@QAAXAAMMMPBD@Z` (91.25581)
- `?BuildNGQuad@Spotlight@@IAAXAAUBeamDef@1@W4Constraint@RndTransformable@@@Z` (90.63071)

### Flipped INTO divergence (1)

- `?SetMoveState@HollaBackMinigame@@QAAXHVSymbol@@@Z` (99.40120), `call_arg_mismatch`
  r4 `2147614728` vs `2147614720` — both
  trampoline addresses, 8 bytes apart. Not a value difference.

## Classifying the 71 divergences

The runner reports a raw `reason`. The decisive question for each is whether the
differing operand is a REAL value or an address the harness lays out independently
per side (STACK / OBJECT / GLOBAL / RDATA / TRAMPOLINE, 64 KB each).

| what actually differs | rows |
|---|---|
| call_arg, operands in RDATA | 31 |
| call_arg, operands in GLOBAL+RDATA | 11 |
| cap_exhausted_both (both sides hit the instruction cap) | 10 |
| wild_jump_match (both sides crashed, at different PCs) | 6 |
| call_arg, operands in STACK | 5 |
| memory_mismatch, OBJECT slot holding trampoline pointers | 3 |
| call_arg, operands in RDATA+STACK | 3 |
| scratch_return_reg (fn does not return in r3/f1) | 2 |

**0 rows present a differing operand that is not a harness address.**

## Caveats, stated so this is not read as stronger than it is

- `EQUIVALENT` on a row where BOTH sides fault identically is weak evidence. Two
  rows in this set read EQUIVALENT only because both hit the same CPU exception.
- The runner stops at the FIRST divergence, so nothing after that call was compared.
  A row reported here as one bad operand was not checked beyond it.
- Artifact classification above is a region test, not an adjudication. It says the
  differing bits are harness-assigned addresses; it does not prove the surrounding
  logic is right.
- Five rows cannot run at all.

## Unrunnable (5)

- `fn_825CCF20`
- `merged_ObjPtrListRemove`
- `fn_8277AFC0`
- `fn_8263A168`
- `fn_8263A360`

## Bottom line

No row in the native frontier presents a behavioural divergence whose differing
value is something other than a harness-assigned address. That is a statement
about the 223 rows the native port compiles, not about the whole binary.
