# Session: Native Shader Pipeline, MetaMaterial, TexProc

Date: 2026-03-05

## Investigation Summary

Three areas identified from DECOMP_GAPS.md as native port gaps. Research agents
investigated each in parallel.

### 1. Shader Select/CalcShaderOpts (24+12 stubs)

**Finding: Stubs are CORRECT. No implementation needed.**

The native WebGPU renderer completely bypasses the Xbox 360 shader system:
- `Select()` configures Xbox DX9 render state — irrelevant to WebGPU
- `CalcShaderOpts()` returns a u64 bitmask for Xbox HLSL macro variants — irrelevant
- Native renderer has its own pipeline: `PipelineManager` + `standard_wgsl.inc`
- Material properties extracted directly into `MaterialUniforms` struct
- Pipeline keyed by `PipelineKey` (blend, zMode, cull, stencil, layout, alphaCut)

**Real gap**: The WebGPU shader handles all materials with ONE unified WGSL shader.
Some Xbox shader types have special behavior not replicated:
- `kParticlesShader` — billboard geometry (partially handled)
- `kFurShader` — hair shell rendering (not implemented)
- `kSyncTrackShader` — rhythm indicator visualization
- `kPostProcShader` — post-processing effects (bloom, tone mapping)

**Action**: Update DECOMP_GAPS.md to reflect this. No code changes needed for the stubs.

### 2. MetaMaterial Loading in RndMat::Init

**Finding: Low priority for rendering. MetaMaterials control EDITOR permissions, not render behavior.**

MetaMaterial stores `mMatPropEditActions[64]` — a per-property enum:
- `kPropDefault(0)` — use default, hidden in editor
- `kPropForce(1)` — force value, read-only in editor
- `kPropEdit(2)` — allow editing

Materials render correctly without MetaMaterials. All properties are serialized
directly in each RndMat instance. MetaMaterial only matters for:
- Material property editing UI (filtering which props are editable)
- Property synchronization between templates and instances
- Hot-reloading material templates

Current guards are correct:
- `RndMat::Init()`: `sMetaMaterials = nullptr` on native
- `CreateMetaMaterial()`: Returns nullptr if sMetaMaterials is null

**Action**: Can implement for 1:1 parity — it's just loading `metamaterials.milo` from ARK
via `LoadMetaMaterials()`. Low risk, moderate effort.

### 3. TexProc::DrawShowing and TexProc::Poll

**Finding: Visual polish only. Requires WebGPU render-to-texture pipeline.**

TexProc applies shader effects to textures:
- **Twirl** (kShaderTwirl=0): Sinusoidal wave distortion on UVs. Used for HUD move icons.
- **KillAlpha** (kShaderKillAlpha=1): Forces alpha to 1.0.

Used in UI (hud_objects.dta): move icon distortion in setlist/difficulty selection.

Implementation would need:
1. WebGPU render-to-texture (render pass targeting an RndTex)
2. Twirl pixel shader in WGSL
3. KillAlpha pixel shader in WGSL
4. Phase animation in Poll() (integrate mPhaseVel)

**DrawShowing/DrawPreClear are actually already implemented in TexProc.h** — they just
call `DrawToTexture()` which is the real stub.

**Action**: Implement DrawToTexture() with WebGPU render-to-texture. Medium effort.
Poll() is simple phase animation.

## Revised Priority

1. **MetaMaterial loading** — Low effort, high parity impact. Just remove the #ifdef
   and let `LoadMetaMaterials()` run. Need to verify metamaterials.milo is in the ARK.
2. **TexProc** — Medium effort, visible UI improvement. Needs WebGPU render-to-texture.
3. **Shader stubs** — No action needed. Already correctly handled.

## Implementation Log

### MetaMaterial Loading

Status: INVESTIGATING

Checking if metamaterials.milo exists in the ARK and if LoadMetaMaterials() works on native...

### TexProc Implementation

Status: INVESTIGATING

Checking DrawToTexture() decompilation and what's needed for WebGPU render-to-texture...
