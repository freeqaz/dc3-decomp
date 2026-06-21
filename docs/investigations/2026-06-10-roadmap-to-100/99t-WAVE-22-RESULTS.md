# Wave 22 Results — GAME+ENGINE pivot (skip Xbox): json-c build fix, filterdesign, gesture, lazer

**Date:** 2026-06-21 · all-Opus, native-gated. **User redirect: skip Xbox-specific code paths;
game + engine highest priority.** This wave moved the REAL matched headline (unlike the
metric-invisible Xbox stub waves): done-without-certs bytes 83.69→83.74, done-with-certs
99.07→99.08% fns / 96.80% bytes. Native 418/418 (3 flaky audio-lock failures confirmed
non-reproducing). A concurrent agent is also working world/rndobj (landed RenderCone/BeatClock
to 100% — coordinated around).

## Lane A — cross-platform engine: json-c + filterdesign (`e367d52d`)
**Real BUILD bug:** json-c units compiled `/TC` but the target used `/TP`, so 13 json-c static
functions were 0% (C-mangled vs the target's C++-mangled names). Flipped net_xbox to `/TP`
(config.json + objdiff.json) → json_escape_str, json_object_object_to_json_string + 11 more
0→100. config.h HAVE_STRNCASECMP guarded `_MSC_VER` (strnicmp MSVC / POSIX native — both planes
verified, json-c IS native-built). filterdesign (DSP, native-built): compute_s/applyWarp 0→100,
createFilter 95.6, copyresults 96.2, compute_bpres 66→92.5, compute_apres 40→72. normalize
honest-blocked (FP-spill floor). **Whole-program: 0 regressions, 23 improvements.**

## Lane B — gesture/DepthBuffer3D (`34fa3da9`)
Cross-platform Kinect gameplay (native-built). SetUpWorkingMat 0→100, DrawMesh 0→99.8;
SkeletonExtentTracker::UpdateAttachment 86→94.9 (+Normalize bugfix), ApplyToMeshVerts 69→78.9.
DrawUtl/DrawShowing/NuiTransform honest-blocked as Xbox-specific (skipped per redirect).

## Lane C — game (lazer) UI (`c09a5338`)
ShellInput::Poll 99.6→100 (dead-guard), VoiceInputPanel::ActivateVoiceContext 84.8→86.6,
NavListSort::SetHighlightID 89.1→89.6. RenderCone dropped (concurrent agent landed it 100%).

## Loop status
**The game/engine pivot is metric-visible and productive** (vs the metric-invisible Xbox stub
waves). Key new lever: **build-flag audit** (json-c /TC→/TP) — there may be other wrong-flag
units. Game (lazer) PPC frontier is nearly exhausted; cross-platform engine is partly floor.
A concurrent agent covers world/rndobj. Highest remaining game/engine value: any other
build-flag mismatches + native-port runtime correctness (the engine running the game).
