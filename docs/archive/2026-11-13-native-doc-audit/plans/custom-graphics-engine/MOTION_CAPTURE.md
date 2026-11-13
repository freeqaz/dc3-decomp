# Motion Capture: Kinect Replacement

## Context

Dance Central 3 requires full-body skeleton tracking for its core gameplay. The
original game uses Microsoft Kinect v1 (Xbox 360) via the NUI (Natural User Interface)
SDK. For a native port, we need body tracking from a standard webcam using ML-based
pose estimation.

## Current Kinect Integration

### What DC3 Uses

From the `gesture/` subsystem:

| Component | File | Purpose |
|-----------|------|---------|
| `GestureMgr` | `gesture/GestureMgr.cpp` | Core gesture manager, up to 6 skeletons |
| `LiveCameraInput` | `gesture/LiveCameraInput.cpp` | Camera/depth stream polling |
| `SkeletonUpdate` | `gesture/SkeletonUpdate.cpp` | Skeleton frame handling |
| `SpeechMgr` | `gesture/SpeechMgr.cpp` | Voice recognition |
| `CameraTilt` | `gesture/CameraTilt.cpp` | Kinect motor control |
| Gesture filters | `gesture/*Filter.cpp` | Motion recognition |

### Gesture Filters

These operate on abstract skeleton coordinates and do NOT depend on Kinect directly:

| Filter | Purpose |
|--------|---------|
| `DirectionGestureFilter` | Arm direction detection |
| `HighFiveGestureFilter` | High-five recognition |
| `HandRaisedGestureFilter` | Hand raise detection |
| `HandsUpGestureFilter` | Both hands up |
| `StandingStillGestureFilter` | Idle pose detection |

**Key insight**: These filters consume `BaseSkeleton` / `Skeleton` data. They don't
care whether it came from a Kinect, a webcam, or synthetic data. The abstraction
boundary is the skeleton data structure.

### Kinect v1 Skeleton Format

Kinect v1 tracks **20 joints** per skeleton, up to **2 active skeletons**:

```
Head, ShoulderCenter, ShoulderLeft, ShoulderRight,
ElbowLeft, ElbowRight, WristLeft, WristRight,
HandLeft, HandRight, Spine, HipCenter,
HipLeft, HipRight, KneeLeft, KneeRight,
AnkleLeft, AnkleRight, FootLeft, FootRight
```

Each joint has:
- 3D position (x, y, z) in meters, relative to the sensor
- Tracking state (Tracked, Inferred, NotTracked)

The `NUI_SKELETON_FRAME` provides all skeletons per frame at ~30 FPS.

---

## Option A: MediaPipe Pose Landmarker (Recommended)

### What It Is

Google's ML-based body tracking solution. Tracks **33 3D landmarks** from a single
RGB webcam image. Part of the MediaPipe framework.

### Landmark Coverage

33 landmarks, including:

**Head**: nose, left/right eye (inner, outer), left/right ear, mouth (left, right)
**Upper body**: left/right shoulder, left/right elbow, left/right wrist,
left/right pinky, left/right index, left/right thumb
**Torso**: left/right hip
**Lower body**: left/right knee, left/right ankle, left/right heel,
left/right foot index

### Comparison to Kinect v1

| Feature | Kinect v1 | MediaPipe Pose |
|---------|-----------|----------------|
| Joints/landmarks | 20 | 33 |
| Tracked skeletons | 2 active (6 detected) | Multi-person supported |
| Input | IR depth sensor | Standard RGB webcam |
| Depth sensing | Yes (hardware) | Estimated (ML) |
| Frame rate | 30 FPS | 30+ FPS (CPU only) |
| Latency | ~100ms | ~50-100ms |
| Hardware required | Kinect sensor ($150) | Any webcam ($10-30) |
| Coordinate system | Meters, sensor-relative | Normalized, hip-relative |
| Occlusion handling | Good (depth data) | Moderate (monocular) |
| Lighting sensitivity | Low (IR-based) | Moderate (RGB-based) |

### Accuracy

Research comparing MediaPipe to Kinect V2 shows:
- **Comparable absolute error and RMS** to Kinect V2
- **Lower dispersion values** in some studies
- Error within range of standard clinical goniometers
- Particularly strong for **front-facing poses** (exactly the dance game scenario)

### Architecture for DC3 Integration

```
USB Webcam (any)
    │  Raw frames (640x480 or 1280x720)
    ▼
MediaPipe Pose Pipeline
    │  Person detection (ROI) → Landmark tracking
    │  Runs on CPU at 30+ FPS
    ▼
Landmark Output (33 landmarks × [x, y, z, visibility])
    │
    ▼
Coordinate Mapping Layer (new code)
    │  Map 33 MediaPipe landmarks → 20 Kinect joints
    │  Convert normalized coords → meter-scale 3D
    │  Apply temporal smoothing
    ▼
NUI_SKELETON_FRAME (existing format)
    │
    ▼
GestureMgr / Gesture Filters (existing code, unchanged)
```

### Joint Mapping: MediaPipe → Kinect

| Kinect Joint | MediaPipe Landmark(s) | Notes |
|-------------|----------------------|-------|
| Head | Nose (0) | Or average of ears |
| ShoulderCenter | Midpoint of shoulders (11, 12) | Synthetic |
| ShoulderLeft | Left shoulder (11) | Direct |
| ShoulderRight | Right shoulder (12) | Direct |
| ElbowLeft | Left elbow (13) | Direct |
| ElbowRight | Right elbow (14) | Direct |
| WristLeft | Left wrist (15) | Direct |
| WristRight | Right wrist (16) | Direct |
| HandLeft | Average of left hand landmarks (17-20) | More precise |
| HandRight | Average of right hand landmarks (21-24) | More precise |
| Spine | Midpoint of shoulders and hips | Synthetic |
| HipCenter | Midpoint of hips (23, 24) | Synthetic |
| HipLeft | Left hip (23) | Direct |
| HipRight | Right hip (24) | Direct |
| KneeLeft | Left knee (25) | Direct |
| KneeRight | Right knee (26) | Direct |
| AnkleLeft | Left ankle (27) | Direct |
| AnkleRight | Right ankle (28) | Direct |
| FootLeft | Left foot index (31) | Direct |
| FootRight | Right foot index (32) | Direct |

15 of 20 Kinect joints map directly to a single MediaPipe landmark. The remaining 5
(Head, ShoulderCenter, Spine, HipCenter, and hands) are computed from combinations
of MediaPipe landmarks.

### Coordinate System Conversion

| Property | MediaPipe | Kinect | Conversion |
|----------|-----------|--------|------------|
| Origin | Hip center | Sensor position | Offset by estimated distance |
| X range | [0, 1] normalized | Meters | Scale by frame width / focal length |
| Y range | [0, 1] normalized | Meters | Scale by frame height / focal length |
| Z range | Relative depth | Meters (from depth sensor) | Estimate from body proportions |
| Handedness | Right-handed | Right-handed | Compatible |

The Z (depth) estimation is the weakest part of monocular tracking. For dance game
scoring that primarily evaluates lateral body position and arm angles, this may be
acceptable. For moves that depend on forward/backward position, accuracy will be lower.

### C++ Integration

MediaPipe is written in C++ and built with **Bazel**:

```bash
# Build MediaPipe pose tracking for desktop
bazel build -c opt \
    --define MEDIAPIPE_DISABLE_GPU=1 \
    mediapipe/examples/desktop/pose_tracking:pose_tracking_cpu
```

Integration via the Tasks API (higher-level):
```cpp
#include "mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarker.h"

auto options = PoseLandmarkerOptions();
options.base_options.model_asset_path = "pose_landmarker_full.task";
options.running_mode = RunningMode::LIVE_STREAM;
options.num_poses = 2;  // Track up to 2 dancers

auto landmarker = PoseLandmarker::Create(options);

// Per frame:
auto result = landmarker->DetectForVideo(frame, timestamp_ms);
for (auto& pose : result.pose_landmarks) {
    for (auto& landmark : pose.landmarks) {
        // landmark.x, landmark.y, landmark.z, landmark.visibility
    }
}
```

### Model Variants

| Model | Size | Speed | Accuracy |
|-------|------|-------|----------|
| **Lite** | 3 MB | Fastest | Good for simple poses |
| **Full** | 6 MB | Balanced | Good all-around |
| **Heavy** | 26 MB | Slowest | Best accuracy, complex poses |

For a dance game with complex poses, **Full** is the minimum. **Heavy** may be needed
for accurate scoring of intricate choreography.

### Build System Challenge

MediaPipe uses **Bazel**, not CMake/Meson. Options for integration:
1. Build MediaPipe as a shared library (`.so`/`.dylib`) and link against it
2. Use Bazel for the entire project (significant build system change)
3. Use the Python API via embedded Python (adds Python dependency)
4. Use MediaPipe's pre-built task bundles with the Tasks C++ API

Option 1 is most practical. Build MediaPipe once as `libmediapipe_pose.so`, install
it, and link from CMake.

---

## Option B: MoveNet (TensorFlow Lite)

### What It Is

Google's lighter-weight pose estimation model, designed for high-performance
on-device inference.

### Key Properties

- **17 keypoints** (fewer than MediaPipe's 33 or Kinect's 20):
  nose, eyes, ears, shoulders, elbows, wrists, hips, knees, ankles
- **Missing**: hands, feet, spine — significant gaps for dance scoring
- **Two variants**: Lightning (fast) and Thunder (accurate)
- **Runs via TensorFlow Lite** — simpler integration than MediaPipe's full framework
- Multi-person support via the "multipose" model

### Verdict

**Not recommended.** Only 17 keypoints is insufficient — missing hands, feet, and
spine makes accurate dance scoring difficult. MediaPipe's 33 landmarks provide better
coverage with comparable performance.

---

## Option C: OpenPose

### What It Is

CMU's multi-person pose estimation framework. The oldest and most cited of the
ML pose estimation systems.

### Key Properties

- **25 body keypoints** (BODY_25 model), plus optional hand (21 per hand) and face (70)
- **Multi-person**: Designed for crowds, handles occlusion well
- **Heavy**: Requires GPU (CUDA) for real-time performance
- **License**: Non-commercial research only (commercial license available)
- **C++ API**: Native C++, integrates with CMake

### Verdict

**Not recommended for primary use.** Heavier than MediaPipe with no accuracy advantage
for single/dual-person front-facing poses. The non-commercial license is restrictive.
Could be useful as a reference for accuracy comparison.

---

## Option D: Custom ML Model

### What It Is

Train a custom pose estimation model optimized for dance moves specifically.

### Considerations

- Could be trained on dance-specific data for better accuracy on the moves DC3 cares about
- Could output exactly the 20 Kinect joints (no mapping needed)
- Requires ML expertise, training data, and compute resources
- Significant upfront investment
- Risk of overfitting to training data

### Verdict

**Not recommended initially.** MediaPipe is good enough. Could be explored later if
accuracy is insufficient for competitive scoring.

---

## Implementation Plan

### Phase 1: Stub (No motion capture)

Create `GestureMgr_Stub.cpp` that provides empty skeleton frames. The game runs but
motion-based gameplay doesn't work. Menu navigation via controller.

### Phase 2: Recorded Playback

Record skeleton data from a Kinect (or synthesize from animation) and play it back
in the native port. Validates the gesture filter pipeline works correctly.

### Phase 3: MediaPipe Integration

1. Build MediaPipe Pose as a shared library
2. Create `LiveCameraInput_Webcam.cpp`:
   - Open webcam via OpenCV or V4L2
   - Feed frames to MediaPipe
   - Map 33 landmarks to 20 Kinect joints
   - Pack into `NUI_SKELETON_FRAME` format
3. Create coordinate mapping layer:
   - Normalized → meter-scale conversion
   - Temporal smoothing (low-pass filter on joint positions)
   - Visibility → tracking state mapping
4. Test with gesture filters:
   - Verify `DirectionGestureFilter` works with webcam data
   - Verify `HighFiveGestureFilter` triggers correctly
   - Validate scoring accuracy vs original Kinect scoring

### Phase 4: Calibration and Tuning

- Camera position calibration (distance, angle)
- Sensitivity tuning for gesture filters
- Latency compensation (webcam + ML inference adds ~50-100ms)
- Lighting robustness testing

---

## Open Questions

1. **Is MediaPipe's depth estimation accurate enough for dance scoring?**
   Some moves require distinguishing forward/backward position. Monocular tracking
   is weakest on the Z axis. May need to adjust scoring thresholds.

2. **Can MediaPipe run at 60 FPS on modest hardware?**
   The game targets 60 FPS. If MediaPipe runs at 30 FPS, we need interpolation
   between skeleton frames. The "Lite" model may achieve 60 FPS on modern CPUs.

3. **How does lighting affect accuracy?**
   Kinect used IR (lighting-independent). Webcams depend on visible light. Low-light
   conditions (common during dance gameplay with stage lighting effects) may degrade
   tracking quality. Need testing.

4. **Multi-player: can MediaPipe track 2-4 dancers simultaneously?**
   DC3 supports up to 4 players. MediaPipe can detect multiple poses but accuracy
   may degrade with more people. Need performance benchmarking.

5. **What about the speech recognition (`SpeechMgr`)?**
   DC3 uses NUI speech recognition for voice commands. Modern alternatives:
   - Whisper (OpenAI) — accurate but heavy
   - Vosk — lightweight, offline, C API
   - Platform speech APIs (SpeechRecognition Web API, macOS Speech)

6. **Bazel build dependency**: Is there a way to avoid Bazel for MediaPipe integration?
   Pre-built libraries? Alternative implementations of the same models?

## Recommendation

**MediaPipe Pose Landmarker (Full model)** is the clear choice:
- 33 landmarks (superset of Kinect's 20)
- 30+ FPS on CPU (no GPU required)
- Free, open-source (Apache 2.0)
- Works with any USB webcam
- C++ native
- The dance game use case (front-facing, full body visible) is its strongest scenario

Start with the stub (Phase 1), move to MediaPipe (Phase 3) once rendering and audio
are working. Motion capture can be developed in parallel with other subsystems.

## References

- [MediaPipe Pose Landmarker](https://developers.google.com/mediapipe/solutions/vision/pose_landmarker)
- [MediaPipe C++ Framework](https://ai.google.dev/edge/mediapipe/framework/getting_started/cpp)
- [BlazePose paper](https://arxiv.org/abs/2006.10204)
- [MediaPipe vs Kinect V2 accuracy study](https://www.mdpi.com/1424-8220/23/1/3)
- [MoveNet documentation](https://www.tensorflow.org/hub/tutorials/movenet)
- [OpenPose GitHub](https://github.com/CMU-Perceptual-Computing-Lab/openpose)
- [Kinect v1 skeleton tracking](https://learn.microsoft.com/en-us/previous-versions/windows/kinect/dn785512(v=ieb.10))
- [Vosk speech recognition](https://alphacephei.com/vosk/)
