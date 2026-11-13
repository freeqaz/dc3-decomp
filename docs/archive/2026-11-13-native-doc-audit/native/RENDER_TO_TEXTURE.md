# Render-to-Texture (RTT) on Native

RTT is **fully implemented** in the WebGPU native port. The infrastructure supports all engine RTT consumers via `RndCam::SetTargetTex()` → `RndCam::Select()` → draw → `FinishDrawTarget()`.

## Architecture

### Engine API (original Milo engine)

| Class | Method | Purpose |
|---|---|---|
| `RndCam` | `SetTargetTex(RndTex*)` | Assigns a render target texture to a camera |
| `RndCam` | `Select()` | Makes camera current; calls `MakeDrawTarget()` on target tex if set |
| `RndCam` | `TargetTex()` | Returns current target texture |
| `RndTex` | `MakeDrawTarget()` | Begin rendering to this texture (virtual, overridden on native) |
| `RndTex` | `FinishDrawTarget()` | End rendering to this texture |
| `RndTex` | `IsRenderTarget()` | Returns `(mType & kRendered)` |
| `RndTexRenderer` | `DrawToTexture()` | High-level draw-to-texture pipeline (used by reflections, depth maps) |

### WebGPU Implementation (`native/src/platform/`)

**Tex_Wgpu.cpp** — GPU texture side table + lazy RTT allocation:
- `EnsureRenderTargetData(RndTex*)` — Creates GPU color + depth textures on first use
- `GetGpuTexView(RndTex*)` — Returns color view (lazy-allocates for render targets)
- `GetGpuTexDepthView(RndTex*)` — Returns depth view (only for `kRendered`, not `kRenderedNoZ`)
- `IsGpuTexRenderable(RndTex*)` — Validates texture has `RenderAttachment` usage flag
- `RndTex::MakeDrawTarget()` — Calls `EnsureRenderTargetData()`, then `gWgpuRnd->SelectRenderTarget()`
- `RndTex::FinishDrawTarget()` — Calls `gWgpuRnd->FinishRenderTarget()`

**Rnd_Wgpu.cpp** — Render pass management:
- `SelectRenderTarget(RndTex*)` — Ends current pass, begins texture pass
- `BeginTexturePass(RndTex*)` — Creates `wgpu::RenderPassDescriptor` targeting the texture's view
- `FinishRenderTarget(RndTex*)` — Ends texture pass, clears active target
- `MakeDrawTarget()` (no args) — Resumes rendering to the main surface (after RTT)

### GPU Resource Allocation

```
RndTex (engine object)
  └─ GpuTexData (side table entry)
       ├─ texture      — wgpu::Texture (RGBA8UnormSrgb, RenderAttachment | CopyDst)
       ├─ view         — wgpu::TextureView (color attachment)
       ├─ depthTexture — wgpu::Texture (Depth24Plus, for kRendered only)
       ├─ depthView    — wgpu::TextureView (depth/stencil attachment)
       ├─ renderTarget — bool (true if created with RenderAttachment usage)
       └─ uploaded     — bool (tracking for regular textures)
```

### Format Selection

| Texture Type | GPU Format | Depth |
|---|---|---|
| `kRendered` (0x2) | RGBA8UnormSrgb | Depth24Plus |
| `kRenderedNoZ` (0x22) | RGBA8UnormSrgb | None |
| `kDepthVolumeMap` (0xA2) | RGBA8Unorm (linear) | None |
| `kShadowMap` (0x42) | RGBA8UnormSrgb | Depth24Plus |

## Engine Consumers

### Working

| Consumer | Location | Status |
|---|---|---|
| **RndTexRenderer** | `rndobj/TexRenderer.cpp` | Works. Camera targets texture, draws scene, restores previous camera. |
| **Video (TexMovie)** | `platform/WebMovieImpl.cpp` | Works. CPU-side RGBA upload to render target via `WriteTexture`. |

### Partially Working / Untested

| Consumer | Location | Status | Issue |
|---|---|---|---|
| **WorldCrowd** | `world/Crowd.cpp` | Crowd stacking bug | Billboard impostor system renders character to impostor texture, then draws billboard quads at instance positions. RTT infrastructure works, but the impostor pipeline has issues (see below). |
| **SpotlightDrawer** | `world/SpotlightDrawer_NG.cpp` | Untested | Spotlight depth map + fog density rendering. Uses `SetTargetTex()` + `Select()`. |
| **Reflection** | `world/Reflection.cpp` | Untested | Mirror/water reflection camera. |
| **ShadowMap** | `rndobj/ShadowMap.cpp` | Partial | Depth-only shadow pass. Basic infrastructure exists but not fully wired in shader. |

## WorldCrowd Impostor System (Known Issue)

The WorldCrowd billboard system is a multi-step rendering pipeline:

1. **Impostor texture** — `gImpostorCamera` targets `gImpostorTex[lod]` (256×512 `kRendered`)
2. **Character render** — Crowd character drawn at origin via `curChar->DrawShowing()` with impostor camera selected
3. **Billboard update** — Quad mesh vertices updated with screen-space collider bounds
4. **Instance draw** — `RndMultiMesh::DrawShowing()` places billboard quads at each instance transform

**Known bugs:**
- Crowd characters appear stacked at origin between dancers (observed on web port, 2026-03-16)
- Possible causes under investigation:
  - Impostor camera viewport/frustum mismatch
  - Billboard mesh material not binding impostor texture correctly
  - `gImpostorMat`'s `SetDiffuseTex()` not syncing GPU texture from RTT output
  - `Draw3DChars()` previously skipped drawing on native (`#ifndef HX_NATIVE` guard) — fixed
  - `BuildBillboard()` creates mesh geometry that may need GPU sync on native

**`Draw3DChars()` fix (2026-03-16):**
The `#ifndef HX_NATIVE` guard around the draw call in `Draw3DChars()` was skipping character rendering on native. Added `#ifdef HX_NATIVE` block that calls `curChar->SetShowing(true)` + `curChar->DrawShowing()` directly (without Xbox-specific raw offset access to `SelfShadow`/unk251/unk252 members).

## Known Limitations

- **No cleanup**: GPU textures in the side table are never freed (leaked until shutdown). Should hook into `RndTex` destructor.
- **Compressed textures**: BC1/BC3 textures cannot be render targets (lack `RenderAttachment` usage). Silently skipped in `BeginTexturePass()`.
- **Nested RTT**: Engine blocks nested render-to-texture — logs a warning and skips if already rendering to a texture (TexRenderer.cpp line 311-317).
- **Clear behavior**: WebGPU spec allows undefined initial contents for render targets. `EnsureRenderTargetData()` clears to black via `WriteTexture` after creation (browsers display purple/magenta otherwise).
