# RB3 Decomp Reference Guide

This document catalogs shared code between DC3 (Dance Central 3) and RB3 (Rock Band 3), both built on the Harmonix Milo engine.

**RB3 Decomp Location:** `~/code/milohax/rb3/`

---

## Overview

DC3 and RB3 share extensive Milo engine code. Current DC3 status:
- **30.7% code matched**
- **45.2% functions matched** (21,211 of 46,958)

RB3 is an invaluable reference, especially for low-level engine code.

---

## Directory Compatibility Matrix

| Directory | Shared Files | Compatibility | Notes |
|-----------|--------------|---------------|-------|
| system/math/ | 34 | **90%** | Pure math, most portable |
| system/utl/ | 101 | **80%** | Utilities, minimal platform deps |
| system/obj/ | 34 | **70%** | Core object model |
| system/midi/ | 13 | **70%** | MIDI processing |
| system/os/ | 69 | **50%** | Platform layer differs (Xbox vs Wii) |
| system/char/ | 117 | **60%** | Character animation |
| system/rndobj/ | 60+ | **50%** | Rendering, platform differences |
| system/synth/ | 85 | **50%** | Audio engine |

---

## High-Priority Shared Files

### system/math/ (Start Here)

Pure math functions are most portable between projects.

```
Rot.cpp          - Rotation/quaternion (GetXAngle, GetYAngle, GetZAngle)
Vec.cpp          - Vector2, Vector3, Vector4 classes
Color.cpp        - Color operations
Interp.cpp       - Interpolation functions
Geo.cpp          - Geometric operations
Key.cpp          - Keyframe operations
Rand.cpp         - Random number generation
Trig.cpp         - Trigonometric functions
Decibels.cpp     - Audio math
SHA1.cpp         - Hashing
Sort.cpp         - Algorithm utilities
```

### system/utl/ (Second Priority)

Core utilities with minimal platform dependencies.

```
Symbol.cpp       - String interning system
BinStream.cpp    - Binary serialization
TextStream.cpp   - Text serialization
TempoMap.cpp     - Music timing (critical for rhythm games)
BeatMap.cpp      - Beat mapping
MeasureMap.cpp   - Measure mapping
Str.cpp          - String handling
StringTable.cpp  - String tables
MemStream.cpp    - Memory streams
FileStream.cpp   - File streams
Loader.cpp       - Asset loading
Cache.cpp        - Caching
Locale.cpp       - Localization
Profiler.cpp     - Performance tools
Compress.cpp     - Compression
```

### system/obj/ (Core Object Model)

```
Object.cpp       - Base object class
DataArray.cpp    - Data structure system
DataNode.cpp     - Data nodes
DataFile.cpp     - Data file I/O
Dir.cpp          - Directory objects
Msg.cpp          - Message passing
Task.cpp         - Task scheduling
PropSync.cpp     - Property synchronization
```

### system/os/ (Platform Layer)

Note: DC3 is Xbox 360, RB3 is Wii. Platform-specific code differs.

```
Timer.cpp        - Timing/profiling (similar implementations)
Archive.cpp      - Archive handling
ArkFile.cpp      - Ark file format
CritSec.cpp      - Threading primitives
System.cpp       - Platform management
User.cpp         - User management
Joypad.cpp       - Input handling
HDCache.cpp      - File caching
ContentMgr.cpp   - Content management
AsyncFile*.cpp   - Async file I/O (recently completed in DC3)
```

### system/midi/ (Complete List)

All 13 files are shared:

```
MidiParser.cpp
MidiParserMgr.cpp
MidiReader.cpp
MidiReceiver.cpp
MidiVarLen.cpp
DisplayEvents.cpp
DataEventList.cpp
```

### system/char/ (Character Animation)

Extensive overlap (117 files). Key files:

```
CharBones.cpp      - Bone hierarchy
CharClip.cpp       - Animation clips
CharClipSet.cpp    - Clip sets
CharDriver.cpp     - Animation drivers
CharDriverMidi.cpp - MIDI-driven animation
CharIKHand.cpp     - Hand IK
CharIKFoot.cpp     - Foot IK
CharIKHead.cpp     - Head IK
CharLipSync.cpp    - Lip sync
CharEyes.cpp       - Eye animation
CharLookAt.cpp     - Look-at system
```

### system/rndobj/ (Rendering)

```
Anim.cpp         - Animation system
Cam.cpp          - Camera
Draw.cpp         - Drawing utilities
Tex.cpp          - Textures
Mat.cpp          - Materials
Mesh.cpp         - Mesh rendering
Env.cpp          - Environment
Lit.cpp          - Lighting
Trans.cpp        - Transforms
Font.cpp         - Font rendering
Text.cpp         - Text rendering
Part.cpp         - Particles
Console.cpp      - Debug console
```

---

## Workflow for Using RB3 Reference

### 1. Find the Function

```bash
# Search RB3 for a function name
grep -rn "FunctionName" ~/code/milohax/rb3/src/

# Check if class exists in both projects
grep -l "ClassName" ~/code/milohax/rb3/src/**/*.cpp
```

### 2. Compare Class Layouts

RB3 and DC3 may have different member offsets. Always verify:
- Member order
- Virtual function table order
- Inheritance hierarchy

### 3. Adapt the Code

Common differences to watch for:
- Platform-specific `#ifdef` paths
- Different Milo engine versions
- Compiler optimization patterns (MSVC vs GCC)

### 4. Build and Verify

```bash
ninja build/373307D9/src/path/to/file.obj
ninja build/373307D9/report.json
```

---

## Key Differences Between Projects

| Aspect | DC3 | RB3 |
|--------|-----|-----|
| Platform | Xbox 360 | Wii |
| Compiler | MSVC | GCC |
| Target | PowerPC | PowerPC |
| Engine Version | Later Milo | Earlier Milo |

### Compiler Behavior Differences

- **Inlining**: DC3 inlines `strcpy`, `strlen`, `strcmp`, `strcat`
- **Static init**: MSVC uses bit flags (`ori r11, r11, 0x1/0x2/0x4`)
- **Register allocation**: Different patterns between compilers

---

## Quick Reference Commands

```bash
# Search RB3 source
grep -rn "pattern" ~/code/milohax/rb3/src/

# Find similar files
ls ~/code/milohax/rb3/src/system/utl/*.cpp

# Compare file structures
diff <(ls src/system/utl/) <(ls ~/code/milohax/rb3/src/system/utl/)

# Check RB3 class definition
grep -A 50 "class ClassName" ~/code/milohax/rb3/src/**/*.h
```

---

## See Also

- [LOW_HANGING_FRUIT.md](LOW_HANGING_FRUIT.md) - Prioritized function list
- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns and lessons
- [../WORKSESSION.md](../WORKSESSION.md) - Main session notes
