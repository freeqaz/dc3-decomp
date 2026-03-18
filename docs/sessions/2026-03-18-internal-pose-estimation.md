# Internal Pose Estimation — Design Document

**Date**: 2026-03-18
**Status**: DESIGN
**Goal**: Replace the external Python pose server with embedded C++ pose estimation

---

## Current Architecture

The native port uses an external process for skeleton tracking:

```
pose_server.py (Python + YOLO11n-pose + OpenCV)
    ↓ Unix socket (/tmp/dc3_pose.sock)
NativeSkeletonProvider::ReaderThread()
    ↓ MapCOCOToDC3() — 17 COCO keypoints → 20 DC3 joints
GestureMgr::PostUpdate()
    ↓
MoveDir scoring pipeline
```

**Protocol**: Binary, little-endian. 16-byte header (frame_id, num_persons, timestamp)
+ up to 6 × 224-byte person records (track_id + 17 × {x,y,confidence}).

**Problems with external server**:
- 5-second startup delay for socket connection
- Requires Python + ultralytics + OpenCV installed
- Unix sockets don't work on Windows
- Process management (fork/exec) is fragile
- Users must install dependencies separately

---

## Requirements

- **Output**: 17 COCO keypoints per person (normalized [0,1] image coordinates)
- **Frame rate**: ~30 FPS
- **Camera**: 640×480 webcam
- **Latency**: <50ms end-to-end (capture → inference → keypoints)
- **Platforms**: Linux, macOS, Windows
- **License**: Permissive (Apache 2.0, MIT, BSD)

The existing `MapCOCOToDC3()` code in `Skeleton_Native.cpp` handles the 17→20 joint
mapping, coordinate system conversion, and confidence thresholding. Only the
inference + camera capture needs replacing.

---

## Evaluated Options

### Option A: ncnn + RTMPose (RECOMMENDED)

**ncnn** (Tencent, BSD-3): Smallest C++ neural network inference framework.
**RTMPose** (OpenMMLab, Apache 2.0): State-of-the-art real-time pose model.

| Metric | Value |
|--------|-------|
| Library size | ~2-7 MB (zero external deps) |
| Model size | ~5 MB (RTMPose-s ONNX → ncnn) |
| GPU | **Vulkan** (complements our WebGPU/Dawn stack) |
| CPU 30fps | Yes (70+ FPS on Snapdragon 865, faster on desktop) |
| Build system | CMake |
| Cross-platform | Linux, macOS, Windows, Android, iOS |

**Why this is best**:
- Zero dependencies — pure C++ implementation, no BLAS, no Protobuf
- Vulkan GPU acceleration aligns with our rendering stack
- Smallest binary footprint of any option
- RTMPose-s/m achieves 70-90+ FPS on CPU alone
- Pre-converted ncnn models available from OpenMMLab
- Battle-tested in production (QQ, WeChat, etc.)

**Integration path**:
1. Add ncnn as CMake subdirectory or pre-built library
2. Download RTMPose ncnn model (pre-converted)
3. Capture webcam via platform API (V4L2/AVFoundation/DirectShow) or minimal OpenCV
4. Feed pixels into `ncnn::Mat`, run inference, extract 17 COCO keypoints
5. Feed into existing `MapCOCOToDC3()` code

### Option B: ONNX Runtime + RTMPose ONNX

**ONNX Runtime** (Microsoft, MIT): Broadest model ecosystem.

| Metric | Value |
|--------|-------|
| Library size | ~7.5 MB |
| Model size | ~5-15 MB (RTMPose/YOLO-Pose ONNX) |
| GPU | CUDA (Linux), DirectML (Windows), CoreML (macOS) — NO Vulkan |
| CPU 30fps | Yes (90+ FPS RTMPose-m on i7-11700) |
| Build system | CMake, pre-built binaries available |
| Cross-platform | Linux, macOS, Windows |

**Why consider it**:
- Largest model ecosystem (any ONNX model works)
- Pre-built binaries — no compilation needed
- Best documentation
- Fallback if ncnn model conversion has issues

**Downsides**:
- No Vulkan GPU — each platform needs different GPU backend
- Larger dependency footprint than ncnn
- GPU inference requires CUDA/DirectML/CoreML (not unified like Vulkan)

### Option C: WebGPU Compute Shaders (Dawn)

Reuse the existing Dawn GPU device for inference via compute shaders.

| Metric | Value |
|--------|-------|
| Library size | 0 (already have Dawn) |
| GPU | Same device as renderer |
| Feasibility | Possible but high effort |

**What works**:
- Dawn compute passes CAN share the same `wgpu::Device` as the renderer
- `gpu.cpp` (header-only, ~1000 lines) provides dispatch primitives for Dawn
- wonnx (archived Rust project) proved ONNX→WGSL compilation is viable
- RTMPose-t needs only ~10 ops (conv2d, depthwise_conv, relu, add, resize,
  matmul, sigmoid) — feasible to hand-write as WGSL compute kernels

**What doesn't work (yet)**:
- ONNX Runtime native WebGPU EP exists (`--use_webgpu`) but has limited
  operator coverage and no documented device sharing with existing renderers
- wonnx is archived/dead (May 2025)
- WebNN API is browser-only, Dawn doesn't expose it

**Verdict**: Viable as a future optimization (hand-written WGSL kernels for
the ~10 ops RTMPose needs). Not viable as the initial implementation — CPU
inference is already fast enough (RTMPose-t: 3.2ms, giving 10x headroom
for 30fps). Revisit if CPU becomes a bottleneck (multi-person, high-res).

### Option D: TensorFlow Lite

| Metric | Value |
|--------|-------|
| Library size | ~1-3 MB (smallest runtime) |
| Model size | ~5 MB (MoveNet Lightning) |
| GPU | OpenCL (Linux), Metal (macOS), limited Windows |
| Build | Complex (embedded in TF monorepo) |

**Why not recommended**:
- Build system is painful (TensorFlow monorepo)
- Limited model ecosystem (TFLite format only)
- Desktop is secondary to mobile
- ncnn is similarly small but much easier to integrate

### Option E: OpenCV DNN

| Metric | Value |
|--------|-------|
| Library size | 50-100+ MB |
| Bonus | Handles webcam capture too |
| GPU | CUDA, OpenCL |

**Why not recommended**: Binary size is disqualifying for game engine embedding.
However, OpenCV `VideoCapture` could still be used for camera capture alongside
a smaller inference library.

---

## Camera Capture Strategy

All inference libraries need raw pixel frames. Options:

1. **OpenCV VideoCapture** (easiest, ~50MB binary cost)
   - Cross-platform, handles V4L2/AVFoundation/DirectShow
   - Already used by the Python pose server

2. **Platform-native APIs** (smallest, most work)
   - Linux: V4L2 (`/dev/video0`)
   - macOS: AVFoundation (ObjC++)
   - Windows: DirectShow or Media Foundation
   - ~200 lines per platform

3. **GLFW + stb_image** (not viable — GLFW doesn't capture webcam)

4. **libcamera** (Linux-only, modern alternative to V4L2)

**Recommendation**: Start with OpenCV for prototyping (already a dep in many
systems). Long-term, use platform-native APIs for minimal binary size, or
make OpenCV an optional CMake dependency.

---

## Recommended Architecture

```
┌─────────────────────────────────────────────┐
│              dc3-native / milo-viewer        │
│                                              │
│  ┌──────────────┐    ┌───────────────────┐  │
│  │ Camera Capture│    │  ncnn + RTMPose   │  │
│  │ (V4L2/AVF/DS)│───→│  (Vulkan GPU or   │  │
│  │ 640×480 RGB   │    │   CPU fallback)   │  │
│  └──────────────┘    └───────┬───────────┘  │
│                              │               │
│                    17 COCO keypoints         │
│                              │               │
│                    ┌─────────▼───────────┐   │
│                    │  MapCOCOToDC3()     │   │
│                    │  (already exists)    │   │
│                    └─────────┬───────────┘   │
│                              │               │
│                    20 DC3 Skeleton joints     │
│                              │               │
│                    ┌─────────▼───────────┐   │
│                    │  GestureMgr::       │   │
│                    │  PostUpdate()       │   │
│                    └─────────────────────┘   │
└─────────────────────────────────────────────┘
```

**Key design decisions**:
- ncnn runs on a background thread (same pattern as current ReaderThread)
- Camera capture on same background thread
- Double-buffered PersonData (front/back swap on main thread Poll())
- Vulkan inference shares the GPU but uses its own Vulkan instance
  (ncnn manages its own `VkInstance`, separate from Dawn)
- CPU fallback when Vulkan unavailable

---

## Implementation Plan

### Phase 1: ncnn Integration (CMake + inference test)
1. Add ncnn as optional CMake dependency (`-DENABLE_NCNN=ON`)
2. Download RTMPose-s ncnn model files
3. Write `NcnnPoseEstimator` class: load model, run inference on a test image
4. Verify 17 COCO keypoints output matches Python server

### Phase 2: Camera Capture
1. Implement `CameraCapture` interface (abstract)
2. Linux: V4L2 backend (or OpenCV fallback)
3. macOS: AVFoundation backend
4. Windows: DirectShow/Media Foundation backend
5. Test: capture frames, display FPS

### Phase 3: Pipeline Integration
1. Replace `NativeSkeletonProvider::ReaderThread()` with camera+inference loop
2. Remove fork/exec of pose_server.py
3. Remove Unix socket communication
4. Keep `MapCOCOToDC3()` and `PersonData` format unchanged
5. Environment variable: `DC3_POSE=internal` (default) vs `DC3_POSE=external` (legacy)

### Phase 4: Optimization
1. Enable ncnn Vulkan acceleration
2. Profile CPU vs GPU inference latency
3. Model quantization (INT8) for faster CPU inference
4. Adaptive frame skipping (skip inference if skeleton hasn't changed much)

---

## Effort Estimate

| Phase | Files | LOC | Effort |
|-------|-------|-----|--------|
| Phase 1: ncnn CMake + test | 3-4 | ~200 | 1-2 days |
| Phase 2: Camera capture | 3-4 per platform | ~600 | 2-3 days |
| Phase 3: Pipeline integration | 2-3 | ~150 | 1 day |
| Phase 4: Optimization | 2-3 | ~100 | 1-2 days |
| **Total** | ~12-15 | ~1050 | **5-8 days** |

---

## Open Questions

1. **ncnn Vulkan vs Dawn Vulkan**: Can they coexist? ncnn creates its own VkInstance.
   Dawn also creates one. Two VkInstances on the same GPU should work but needs testing.

2. **Camera permissions**: macOS requires `NSCameraUsageDescription` in Info.plist.
   Linux requires `/dev/video0` access. Windows may need manifest.

3. **Model selection**: RTMPose-s (faster, slightly less accurate) vs RTMPose-m
   (slower, more accurate). DC3's gesture detection is fairly coarse (whole-body
   moves, not finger tracking) so RTMPose-s should suffice.

4. **Multi-person**: DC3 supports up to 2 players (4 with party mode). ncnn
   inference is per-crop — need a detector (YOLO) to find people first, then
   RTMPose per person. Or use a single-shot model like YOLO-Pose that detects
   + estimates in one pass.

5. **Fallback**: Keep the external pose server as a fallback? Or remove entirely?
   Recommend keeping as `DC3_POSE=external` option for debugging.
