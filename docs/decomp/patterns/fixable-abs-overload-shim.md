# `Abs`: a non-retail overload shim, and why the PCH is not the culprit

**Project: dc3-decomp (Dance Central 3, Xbox 360, MSVC PPC).** All numbers below
are dc3-decomp's, measured whole-build at `main` = `e982abd4c`.

## The symptom

`Abs(someFloatExpr)` lowers two different ways:

| lowering | comes from | asm |
| --- | --- | --- |
| `fabs` | `inline float Abs(float x) { return fabsf(x); }` | a single `fabs` |
| compare/negate | `template <class T> inline const T Abs(T x)` | `fcmpu` / `bgt` / `fneg` |

Both live in `src/system/math/Utl.h`. Some of our functions want the first
lowering, some want the second, and whichever we pick, the other set regresses.

## The hypothesis that was wrong

The natural guess is an **include-shape** problem: retail's include graph made the
float overload visible in some TUs and not others, and our PCH makes it visible
everywhere. The PCH half of that is *true*:

```
src/system/decomp_pch.h  ->  obj/Object.h  ->  obj/MessageTimer.h  ->  math/Utl.h
```

`ninja -t deps build/373307D9/pch/system.pch` confirms `math/Utl.h` is one of the
PCH's 178 headers, so all ~370 PCH-eligible TUs (`configure.py: pch_eligible_dirs`)
see it.

**But the conclusion does not follow.** Both overloads are declared in the *same
header*, three lines apart. No include shape can make one visible without the
other. Include surgery cannot express the distinction, so it cannot be the fix.

## What retail actually did

Retail's `math/Utl.h` had **only the template**. Evidence:

1. **`../og-dc3-decomp`** — an actual DC3 source drop, same compiler and target —
   `src/system/math/Utl.h` declares only `template <class T> inline const T Abs(T x)`.
   No float overload.
2. **`../rb3`** — same Milo engine — likewise template-only.
3. **`../rb3-xenon` does have the float overload, and is not independent evidence:**
   `git log -S` dates it to commit `c5c1650f` *"Scaffold engine + math library from
   dc3-decomp"*. It was copied **from us**. Our invention propagated downstream.

Retail reached `fabs` by *spelling it differently*, not by overload resolution.
og-dc3's `rndobj/Shader.cpp` — which `#include`s `math/Utl.h` directly, so it
definitely saw the header — writes:

```cpp
if (selected && !NearlyZero(selected->Amplitude()) && mat->AllowDistortionEffects()
    && !NearlyZero(mat->ShockwaveMult())) {
```

`NearlyZero(f)` is `fabs(f) < 0.0001f` — it gets `fabs` from `<cmath>`, with no
`Abs` involved. We had written `Abs(x) < 0.0001f`, which only produced `fabs`
because of the shim. Meanwhile og-dc3's `AmbientOcclusion.cpp` writes:

```cpp
MILO_ASSERT(Abs(1.0f - Length(inVector)) <= kSmallFloat, 0x298);
```

and, with no float overload in scope, that instantiates the template.

**So retail's include shape is identical to ours. The defect is the header's
*content*, not its reachability.**

## Blast radius (the number that decides it)

`Abs` is named at only **17 authorable call sites in 5 files**, so the shim's
visibility provably cannot affect any other TU. Those sites sit in 8 functions:

| function | size | wants |
| --- | --- | --- |
| `CheckDistortionOpts` (rndobj/Shader) | 224 B | `fabs` |
| `CheckDistortion` (rndobj/Shader) | 216 B | `fabs` |
| `DepthBuffer3D::DrawShowing` | 5188 B | `fabs` |
| `RndMeshDeform::VertArray::AppendWeights` | 700 B | `fabs` |
| `RndAmbientOcclusion::BurnTransform` | 680 B | `fabs` |
| `LiveCameraInput::NuiAudioDataCallback` | 260 B | `fabs` |
| `RndAmbientOcclusion::BuildSHCoeff` | 244 B | template |
| `LiveCameraInput::GetTweakedAutoexposure` | 328 B | template |

**7,268 B want `fabs` vs 572 B want the template.** The question is real but small,
and it is lopsided in favour of keeping the shim.

## The measurement (negative result)

Deleting the float overload outright, plus respelling Shader's four sites as
`NearlyZero` and restoring og's `Abs(...)` in `BuildSHCoeff`, whole-build:

| | matched fns (norm==100) | matched code bytes |
| --- | --- | --- |
| baseline | 29,383 / 32,213 | 4,949,820 |
| overload deleted | 29,383 / 32,213 | 4,950,032 |

**Zero functions moved across the 100% line.** All three functions the experiment
targeted (`BuildSHCoeff`, `CheckDistortionOpts`, `CheckDistortion`) were *already*
100% at baseline — the "244 B still charged" premise was stale. The +212 headline
bytes are unattributable (no function flipped to or from 100%) and sit inside the
known ±160 dynamic-initializer/atexit thunk nondeterminism band.

The *attributable* effect is a regression in 5 functions:

```
DrawShowing               5188 B   67.05% ->  64.50%  ( -132.0 fuzzy B)
AppendWeights              700 B   70.05% ->  66.62%  (  -24.0 fuzzy B)
BurnTransform              680 B   63.26% ->  60.91%  (  -16.0 fuzzy B)
GetTweakedAutoexposure     328 B   69.88% ->  70.43%  (   +1.8 fuzzy B)
NuiAudioDataCallback       260 B   67.95% ->  56.88%  (  -28.8 fuzzy B)
NET                                                       -199.0 fuzzy B
```

**Deleting the overload is a measured net loss. Reverted.** (An earlier, cruder
run of the same experiment reported −1 function / −196 B; that arithmetic was
`+244 − 440`, i.e. it counted BuildSHCoeff as a gain it had already banked and
Shader's two functions as the loss.)

## What was kept

The shim stays, now carrying a comment saying it is not retail's. Two
source-fidelity changes landed alongside it, both metric-neutral (all three
functions stay at 100%):

- `rndobj/Shader.cpp`: four `Abs(x) < 0.0001f` -> `NearlyZero(x)`, matching og-dc3
  verbatim. Neutral by inspection: same operand, same `0.0001f`, same strict `<`.
  This is what lets the shim be deleted later without costing Shader.
- `rndobj/AmbientOcclusion.cpp`: `BuildSHCoeff`'s hand-inlined
  `float diff = ...; if (diff <= 0) diff = -diff;` -> og's
  `MILO_ASSERT(Abs<float>(1.0f - Length(inVector)) <= kSmallFloat, 0x298)`.

## The reusable lever: `Abs<float>(x)`

An explicit template argument list can only name a template, so `Abs<float>(x)`
bypasses the non-template overload and forces the `fcmpu`/`bgt`/`fneg` lowering
even while the shim is in scope. Use it wherever the target compares-and-negates.

## How to actually retire the shim

Not by include surgery. One call site at a time, respell the sites that want
`fabs` as `NearlyZero` / `NearlyEqual` / `fabs` (mirroring og-dc3 where it has the
file), leaving the shim in place; when the last one is converted, delete the shim
and re-measure whole-build. Shader is already done. The remaining four are
`DepthBuffer3D::DrawShowing` (6 sites, 5188 B, the big fish),
`LiveCameraInput::NuiAudioDataCallback` and `GetTweakedAutoexposure`,
`MeshDeform::AppendWeights`, and `AmbientOcclusion::BurnTransform`. Note that
og-dc3's copies of those four files do not contain the relevant code, so there is
no verbatim source to copy — they need per-site asm evidence.

## Related-but-different: the header-attributed-assert family

`FlowSlider` / `CharBones` / `AllocInfo` / `Flow` show the target attributing an
assert to a *header* where ours attributes it to the `.cpp`. That is a **different
mechanism** from the `Abs` case and this investigation does not explain it — here
the declaration was in the right file with the wrong content; there it is the right
content in the wrong file.

It is, however, real, and og-dc3 has the answer verbatim for at least `CharBones`:

```cpp
// og-dc3-decomp/src/system/char/CharBones.h:63  -- inline, in the header
inline short MakeShortAng(float f) {
    f = f * 1638.4f + 0.5f;
    MILO_ASSERT(f < 32768 && f > -32767, 0x60);
    return floor(f);
}
```

Ours declares it in the header and defines it non-`inline` in the `.cpp`:

```
src/system/char/CharBones.h:148   short MakeShortAng(float);
src/system/char/CharBones.cpp:19  short MakeShortAng(float f) {
```

Deliberately **not** fixed here: moving a definition into a header changes every
`char/` TU and needs its own whole-build measurement, exactly like this one did.

## General lesson

When two lowerings of the same spelling fight each other, check whether the two
declarations are in the same header **before** theorising about include graphs or
PCH contamination. If they are, visibility is not the variable — the call site's
spelling is, and a sibling source drop will usually tell you which spelling retail
used. And re-measure the premise: two of the three functions this investigation
set out to "fix" were already matching.
