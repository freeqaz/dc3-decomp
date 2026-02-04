## Session: January 23, 2026 (Render Focus - Continued)

### Summary

Continued parallel subagent session on **system/rndobj/** using **objdiff CLI with instruction-level diffs**. This approach proved highly effective - agents could pinpoint exact instruction mismatches and distinguish fixable issues from compiler/linker limits. Achieved **5 new 100% matches** total.

### Functions Fixed This Session (100% Match)

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| PostProc.cpp | `RndPostProc::UpdateTimeDelta` | 99.2% | **100%** | Replaced `Max()`+`Min()` with `Clamp(0.0f, 1.0f, delta)` |
| Rnd.cpp | `Rnd::UpdateOverlay` | 97.2% | **100%** | Removed intermediate `ret` variable, modified parameter directly |
| Rnd.cpp | `Rnd::SetPostProcOverride` | 96.7% | **100%** | Changed `!ptr` to `ptr == 0` for null checks |
| Trans.cpp | `RndTransformable::Handle` | 99.9% | **100%** | Added explicit `(bool)` cast to ternary expression |
| Font.cpp | `KerningTable::SetKerning` | 97.1% | **100%** | Used separate `entryIdx` counter instead of loop variable `i` |

### Functions Improved

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| Draw.cpp | `RndDrawable::Load` | 99.5% | **99.85%** | Changed `int count` to `unsigned int count` |

### Functions at Compiler/Linker Limit (No Change Possible)

These were investigated using objdiff CLI and confirmed at their limits:

| File | Function | Match | Diagnosis via objdiff |
|------|----------|-------|----------------------|
| Trans.cpp | `RndTransformable::Copy` | 99.04% | All diffs are `diff_arg` to merged functions |
| Dir.cpp | `RndDir::SyncObjects` | 99.89% | Linker-merged template instantiations |
| Dir.cpp | `RndDir::PreLoad` | 99.72% | Instruction scheduling + `__FILE__` macro |
| Dir.cpp | `RndDir::OldLoadProxies` | 99.45% | Register allocation + merged functions |
| Lit.cpp | `RndLight::Save` | 99.96% | All diffs are merged function calls |
| Cam.cpp | `RndCam::Load` | 99.8% | Register allocation limit |
| Mesh.cpp | `RndMesh::MakeWorldSphere` | 99.8% | Already optimal |

### objdiff CLI for Diagnosis

The [objdiff CLI](OBJDIFF_CLI_USAGE.md) with `--include-instructions` was critical for this session:

```bash
# Get instruction-level diff showing exactly what's different
objdiff-cli diff -p . "RndTransformable::Handle" -f json --include-instructions \
  | jq '[.instructions[] | select(.match_type | test("diff|replace"))][:10]'
```

**Key insight from Trans.cpp:** The diff showed `mr r5, r30` (decomp) vs `clrlwi r5, r30, 24` (original). The `clrlwi` masks to a byte, indicating the original expected a `bool` type. Adding `(bool)` cast fixed it.

**Match types and what they mean:**
- `diff_arg` - Same instruction, different target address (usually linker-merged functions - **unfixable**)
- `diff_op` - Different opcode (wrong instruction generated - **fixable**)
- `replace` - Completely different instruction (logic error - **fixable**)

### New Patterns Discovered

1. **Clamp vs Max+Min**: `Clamp(min, max, val)` matches better than separate `Max(min, Min(max, val))` calls
2. **Null check style**: `ptr == 0` can match differently than `!ptr` depending on context
3. **Unsigned loop counters**: Assembly using `cmplwi` (unsigned compare) requires `unsigned int` type
4. **Explicit bool casts**: Ternary returning bool may need `(bool)` cast - look for `clrlwi` (mask to byte) in diff
5. **Separate loop counters**: When filtering in a loop, use separate counter for output index vs input index

### Render System Research (system/rndobj/)

**Scale:** 85 files, 4916 functions total, 1334 needing work

**Near-Match Opportunities (90-99%):** 178 functions across these files:

| File | Near-Matches | Status |
|------|-------------|--------|
| PropAnim | 8 | Copy 99.7%, Load 99.6% - at limit |
| Text | 7 | SetFont 100%, SetText 100% (templates done) |
| Mesh | 6 | MakeWorldSphere 99.8% - at limit |
| Trans | 6 | **Handle 100% ✓**, Copy 99.04% - at limit |
| Rnd | 6 | **UpdateOverlay 100% ✓**, **SetPostProcOverride 100% ✓** |
| PostProc | 6 | **UpdateTimeDelta 100% ✓**, Copy 99.9%, Save 99.9% |
| PropKeys | 6 | Print 99.9%, SetFrame 99.9% |
| EventTrigger | 6 | Template functions (100%) |
| Cam | 5 | Load 99.8% - at limit |
| Mat | 5 | GetRefractEnabled 97.1%, LoadOld 97.0% - **needs work** |
| AmbientOcclusion | 5 | GatherObjects 99.88%, Load 99.78% - at limit |
| Font | 5 | **SetKerning 100% ✓** |
| Anim | 4 | Load 99.93%, OnAnimate 99.03% - at limit |
| Line | 4 | Load 99.82%, Handle 99.07% - at limit |
| Shader | 4 | MatShaderFlagsOK 95.3% - **needs work** |
| Part | 4 | InitPool 99.8%, SyncProperty 99.8% |
| TransAnim | 4 | MakeTransform 99.0%, Copy 96.5% |
| BaseMaterial | 4 | Save 100%, Copy 99.9% |
| Group | 3 | Load 98.4% - **needs work** |

**Small Unimplemented Functions (<150 bytes):**

| File | Count | Notes |
|------|-------|-------|
| Utl | 87 | Core render utilities |
| Text | 52 | Text rendering |
| Mesh | 35 | Mesh operations |
| PropAnim | 33 | Property animation |
| EventTrigger | 29 | Event system |
| AmbientOcclusion | 22 | Lighting |
| TexBlender | 22 | Texture blending |
| MeshAnim | 20 | Mesh animation |
| Part | 18 | Particle system |
| Font | 17 | Font rendering |

### Files Modified This Session

- `src/system/rndobj/PostProc.cpp` - UpdateTimeDelta fix (Clamp pattern)
- `src/system/rndobj/Rnd.cpp` - UpdateOverlay, SetPostProcOverride fixes
- `src/system/rndobj/Draw.cpp` - Load unsigned int fix
- `src/system/rndobj/Trans.cpp` - Handle bool cast fix
- `src/system/rndobj/Font.cpp` - SetKerning separate counter fix

### Remaining Work (Render)

Functions still needing fixes (not at compiler/linker limit):

| File | Function | Match | Notes |
|------|----------|-------|-------|
| Mat.cpp | `RndMat::GetRefractEnabled` | 97.1% | Agent interrupted |
| Mat.cpp | `RndMat::LoadOld` | 97.0% | Agent interrupted |
| Shader.cpp | `RndShader::MatShaderFlagsOK` | 95.3% | Agent interrupted |
| Group.cpp | `RndGroup::Load` | 98.4% | Agent interrupted |

---
