# Session 60 Plan: Scene Animation + TexMovie Render-to-Texture

**Date**: 2026-03-12
**Goal**: Unblock game_screen animation (venue lighting, character) and implement render-to-texture for HUD flashcard textures.

## Current State

Game_screen renders stably at 505 draw calls/frame, 10000 frames, clean exit. But:
- **Scene is completely static** — no lighting changes, no camera movement, no character animation
- **HUD shows pink rectangles** — TexMovie render targets never written to

## Part 1: Song.anim DTA Crash — Root Cause & Fix

### What Happens

```
HamDirector::Poll()
  → SongAnim(0)->SetFrame(frame)
  → RndPropAnim::SetFrame() iterates PropKeys
  → ObjectKeys::SetFrame()
  → mTarget->SetProperty(mProp, obj)
  → DataNode::GetObj() [DataNode.cpp:535]
  → gDataDir->FindObject("some_game_object", true, true)
  → Object not found → MILO_FAIL_DTA(kNotObjectMsg) → SIGABRT
```

The song.anim PropAnim has **ObjectKeys** that reference game objects by name (HUD panels, score elements, crowd audio controllers). These objects exist on Xbox but don't exist in the native port's minimal environment.

### Fix Strategy: Graceful Object Lookup Failure

The cleanest fix is in `DataNode::GetObj()` at `src/system/obj/DataNode.cpp:544-548`:

```cpp
ret = gDataDir->FindObject(str, true, true);
if (!ret) {
#ifdef HX_NATIVE
    // Song.anim DTA scripts reference game objects that don't exist
    // on native (HUD panels, score elements, etc). Log and return null.
    fprintf(stderr, "DC3 Native: GetObj failed to find '%s' in '%s'\n",
            str, PathName(gDataDir) ? PathName(gDataDir) : "??");
    return nullptr;
#endif
    MILO_FAIL_DTA(kNotObjectMsg, str, msg);
}
```

Then guard callers that dereference the result:
- `ObjectKeys::SetFrame()` — skip `SetProperty()` if obj is null
- Any other PropKeys path that calls `GetObj()` and assumes non-null

**Risk**: Low. This only affects `#ifdef HX_NATIVE` paths. On PPC, behavior is unchanged.

**Impact**: Unblocks song.anim driving for Float/Color/Bool/Symbol/Quat/Vector3 keys. Only ObjectKeys with missing targets get skipped. Camera shots, light presets, and numeric animations would all work.

## Part 2: LightPreset::Load — Remove Stubs

### Status

LightPreset::Load is **fully implemented** in the decomp source (`src/system/world/LightPreset.cpp:1242-1353`) with 110+ lines of multi-revision binary format handling. It's only missing from native because it's in `engine_stubs_generated.cpp` as a weak stub.

### Stubbed Functions (9 total)

| Function | Stub | Critical? |
|----------|------|-----------|
| `LightPreset::Load(BinStream&)` | stub_fn_457 | YES — blocks all preset loading |
| `LightPreset::Animate(float)` | stub_fn_458 | YES — blocks animation |
| `LightPreset::SetFrameEx(float,float,bool)` | stub_fn_452 | YES |
| `LightPreset::CacheFrames()` | stub_fn_453 | Medium |
| `LightPreset::SetKeyframe(Keyframe&)` | stub_fn_454 | Medium |
| `LightPreset::OnSetKeyframe(DataArray*)` | stub_fn_455 | Low |
| `LightPreset::FillEnvPresetData(...)` | stub_fn_456 | Medium |
| `LightPreset::Replace(ObjRef*,Object*)` | stub_fn_459 | YES — ObjRef lifecycle |
| `LightPreset::GetKey(float,...) const` | stub_fn_1290 | Medium |

### Fix Strategy

Remove these 9 stubs from `engine_stubs_generated.cpp`. The decomp source (`LightPreset.cpp`) is already compiled into the native build. The stubs override it because they're `__attribute__((weak))` — wait, weak means the real symbol wins. Let me verify...

Actually, the stubs use `extern "C" __attribute__((weak))` which means the **real** C++ mangled symbol should override them. If LightPreset.cpp is compiled but Load still stubs, it means LightPreset.cpp isn't producing these symbols. Check:
1. Is `LightPreset.cpp` in `native/CMakeLists.txt`?
2. Does it compile without errors?
3. Are the symbols actually emitted (not ifdef'd out)?

If LightPreset.cpp compiles but some functions are missing (e.g., depend on unimplemented helpers), add targeted `#ifdef HX_NATIVE` stubs for just those helpers.

**Expected result**: Venue .milo deserialization creates real LightPreset objects with keyframe data. `LightPresetManager::SyncObjects()` finds them. `ForcePreset()` or song.anim can drive them.

## Part 3: TexMovie Render-to-Texture

### Architecture

```
TexMovie::DrawToTexture()
  → mTex->MakeDrawTarget()      ← STUB (empty virtual in Tex.h)
  → mMovie.Draw()               ← FFmpeg: RGBA in mRGBABuffer, not uploaded
  → mTex->FinishDrawTarget()    ← STUB (empty virtual in Tex.h)
  → TheRnd.MakeDrawTarget()     ← restore screen render target
```

The FFmpeg backend (`native/src/platform/FFmpegMovieImpl.cpp`) already decodes Bink video to RGBA pixel data in `mRGBABuffer`. The gap is uploading those pixels to a GPU texture.

### What Already Exists

- `FFmpegMovieImpl` — full Bink → RGBA decode pipeline (FFmpeg libavcodec + libswscale)
- `Tex_Wgpu.cpp` — GPU texture upload for static textures (PresyncBitmap)
- `Rnd_Wgpu.cpp` — WebGPU render pipeline with render pass management
- `GpuDevice` — WebGPU device, texture creation, headless rendering
- `TexMovie.cpp` compiled into native build (CMakeLists.txt line 528)
- `TexRenderer.cpp` compiled into native build (CMakeLists.txt line 676)

### Implementation Plan

#### Step 1: GPU Texture Upload in FFmpegMovieImpl::Draw()

The simplest approach — skip render-to-texture entirely for movies, just upload RGBA pixels directly:

```cpp
void FFmpegMovieImpl::Draw() {
    if (!mFrameDecoded) return;
    // Upload mRGBABuffer to the target RndTex via WriteTexture
    // The TexMovie caller has already set mTex as the target
    mFrameDecoded = false;
}
```

This requires `FFmpegMovieImpl` to know about the target `RndTex`. Options:
- **A)** Pass RndTex pointer via `Movie::SetTex()` or `MovieImpl::SetTex()`
- **B)** Have `TexMovie::DrawToTexture()` do the upload after `mMovie.Draw()` returns
- **C)** Implement `MakeDrawTarget()`/`FinishDrawTarget()` properly in WebGPU

Option **B** is cleanest — keep FFmpeg decoding pure, have TexMovie do the GPU work:

```cpp
// In TexMovie::DrawToTexture() — native override or #ifdef block
void TexMovie::DrawToTexture() {
    if (!mTex || !mTex->Width() || !mTex->Height()) return;

    mMovie.Draw(); // Decode frame, clear mFrameDecoded flag

#ifdef HX_NATIVE
    // Get RGBA buffer from FFmpeg backend
    FFmpegMovieImpl *impl = dynamic_cast<FFmpegMovieImpl*>(mMovie.GetImpl());
    if (impl && impl->GetRGBABuffer()) {
        // Upload RGBA pixels to mTex GPU texture
        WgpuUploadTextureData(mTex, impl->GetRGBABuffer(),
                              impl->GetWidth(), impl->GetHeight());
    }
#else
    mTex->MakeDrawTarget();
    mMovie.Draw();
    mTex->FinishDrawTarget();
    TheRnd.MakeDrawTarget();
#endif
}
```

#### Step 2: WebGPU Texture Write for Movie Frames

Add to `Tex_Wgpu.cpp` or `Rnd_Wgpu.cpp`:

```cpp
void WgpuUploadTextureData(RndTex *tex, const uint8_t *rgba, int w, int h) {
    // Look up GPU texture from side table
    // queue.WriteTexture() with RGBA8Unorm data
    // Handle size mismatch (movie resolution vs texture resolution)
}
```

This reuses the existing texture upload infrastructure in `Tex_Wgpu.cpp:PresyncBitmap()` but for dynamic per-frame data.

#### Step 3: Render-to-Texture for RndTexRenderer (Lower Priority)

`RndTexRenderer` is more complex (camera override, scene rendering into texture). For Phase 4, this is lower priority than movie textures. The HUD flashcards use TexMovie (video-to-texture), not RndTexRenderer (scene-to-texture).

However, if flashcards turn out to use RndTexRenderer (drawing move icons as 3D scenes), we need:
- Offscreen render pass creation per RndTexRenderer target
- Camera override in the render pass
- Depth buffer allocation for the offscreen target
- Restore main render pass afterward

### What the HUD Flashcards Actually Use

The move card HUD elements (`flashcard_default.mesh` on `Cam.cam`) have materials with textures that are `TexMovie` render targets. The pipeline:

1. `TexMovie` objects are loaded from the game_hud .milo
2. Each `TexMovie` references a `.bik` file (move icon animations)
3. `TexMovie::DrawPreClear()` is called before main rendering
4. It decodes a Bink frame and uploads to the target texture
5. The mesh material samples that texture during DrawShowing

**Key question**: Are the flashcard `.bik` files in the extracted assets? If not, we may need to extract them from the .ark archives.

## Part 4: Other Missing Subsystems

### Functions That Need Un-Stubbing

Game_screen calls many functions through HamDirector/HamCharacter/etc that are currently weak stubs. Most of the **implementations already exist** in the decomp source — they're stubbed because the native linker couldn't resolve dependencies.

Key clusters to investigate:

| Cluster | Stub Count | Decomp Source | Blocker |
|---------|-----------|---------------|---------|
| LightPreset | 9 | LightPreset.cpp | Missing dependencies? |
| Spotlight | 89 | Spotlight.cpp | Missing RndEnviron/Light helpers? |
| SpotlightDrawer | 88 | SpotlightDrawer.cpp | GPU rendering functions? |
| ClipPlayer | 25 | ClipPlayer.cpp | Character animation deps? |
| HamCamShot | 151 | HamCamShot.cpp | CamShot + DTA deps? |

The approach for each:
1. Remove stubs from `engine_stubs_generated.cpp`
2. Build — collect linker errors
3. Add targeted stubs/implementations for just the missing dependencies
4. Repeat until it links

## Implementation Order

1. **DataNode::GetObj graceful failure** — 5 min, unblocks song.anim for non-object keys
2. **LightPreset stub removal** — 30 min, investigate compile/link issues
3. **TexMovie GPU upload** — 2-4 hours, wire FFmpeg RGBA → WebGPU texture
4. **Song.anim partial driving** — test which keys work with graceful failure
5. **LightPresetManager integration** — verify presets load, test ForcePreset()

## Files to Modify

| File | Change |
|------|--------|
| `src/system/obj/DataNode.cpp` | `#ifdef HX_NATIVE` graceful null return in GetObj |
| `native/src/engine_stubs_generated.cpp` | Remove LightPreset stubs |
| `native/src/platform/FFmpegMovieImpl.h` | Add `GetRGBABuffer()`, `GetWidth/Height()` |
| `native/src/platform/Tex_Wgpu.cpp` | Add dynamic texture upload function |
| `src/system/movie/TexMovie.cpp` | `#ifdef HX_NATIVE` GPU upload path in DrawToTexture |

## Success Criteria

1. `LightPreset` objects appear in venue (currently 0)
2. `ForcePreset()` changes light colors (venue no longer uses fallback lighting)
3. Song.anim SetFrame doesn't crash (missing objects logged, skipped)
4. HUD flashcard rectangles show video frames instead of pink
5. No regressions on PPC decomp build
