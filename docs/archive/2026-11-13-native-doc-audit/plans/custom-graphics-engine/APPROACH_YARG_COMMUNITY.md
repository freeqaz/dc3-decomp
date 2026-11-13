# Approach: YARG and Community Projects

## Overview

Survey of existing MiloHax community projects and rhythm game open-source work to
identify what can be borrowed or built upon for a DC3 native port.

## YARG (Yet Another Rhythm Game)

### What It Is

YARG is an open-source rhythm game built in **Unity 6** (C#). It supports plastic
instrument controllers (guitar, drums, keys) and is the most active open-source
rhythm game project.

**GitHub**: https://github.com/YARC-Official/YARG
**Engine**: Unity 6000.2.12f1
**Language**: C# (89.3%), ShaderLab (8.4%), HLSL (2.3%)

### Relationship to Milo Engine

**None.** YARG shares zero code or technology with Harmonix's Milo engine. It is a
clean-room Unity implementation inspired by Rock Band and Guitar Hero gameplay.

- Does not read `.milo` or `.ark` file formats
- Uses standard MIDI charts (`.mid`) and `.ini` configuration
- Different architecture, different rendering pipeline, different audio system

### What Happened to YARG

In August 2025, YARG's creator (EliteAsian) and key team members left to join
**RedOctane Games**, a new studio founded by Guitar Hero veterans (the original
RedOctane founders Charles and Kai Huang). Their new game **Stage Tour** targets
fall 2026 on PC and consoles, with a Gibson licensing partnership.

YARG continues under new project managers but lost its core leadership.

### What's Borrowable

Very little for a Milo engine port:
- **PlasticBand-Unity**: Controller input library for plastic instruments. Unity-specific
  but the button mapping tables could be referenced.
- **BASS audio library**: YARG uses BASS for audio. BASS is cross-platform C/C++ and
  could be used in DC3's native port (though it's not free for commercial use).
- **Chart parsing**: YARG.Core has MIDI chart parsing, but DC3 uses its own format.

### Verdict on YARG

**Not useful for this project.** Different engine, different language, different file
formats, different architecture. YARG is a rhythm game in the same genre but shares
no technical foundation.

---

## MiloHax Ecosystem

### MiloEditor / MiloLib

**GitHub**: https://github.com/ihatecompvir/MiloEditor
**Language**: C# (.NET)
**Status**: Active

A cross-platform editor for Milo scene files (`.milo`). Can:
- Open, manipulate, and save Milo scenes
- View and export/import textures
- Handle complex scene hierarchies including inlined subdirs
- Best support for Rock Band 3 and Dance Central 1

**Cannot render 3D scenes** — it is an asset editor, not a renderer.

**What's borrowable**:
- **MiloLib** (the underlying library) documents the Milo file format in C# code.
  Useful as a reference for writing format loaders in C++, though DC3's decompiled
  code already contains the original loaders.
- Texture format documentation (DXT, Xbox 360 tiled formats)
- Scene hierarchy structure

### Mackiloha / pikaxe

**Mackiloha**: https://github.com/PikminGuts92/Mackiloha (C#, archived Jan 2025)
**pikaxe**: https://github.com/PikminGuts92/pikaxe (Rust, under construction)

Tools for hacking Milo engine games:
- Ark Helper: unpacks `.ark` archives
- SuperFreq: unpacks `.rnd`/`.gh`/`.milo` files
- P9 Song Tool: venue authoring

pikaxe (Rust rewrite) supports archives, DTA, GLTF export, and models.

**What's borrowable**:
- `.ark` archive format documentation
- GLTF export code in pikaxe could validate mesh extraction
- DTA (DataArray) format parsing (though we have the original C++ implementation)

### milo_blender

**GitHub**: https://github.com/ihatecompvir/milo_blender
**Status**: Highly experimental (2 commits)

Blender 4.1 plugin that can export Blender characters to RB3-format Milo scenes.
Currently cannot import existing Milo files. No texture/UV support. Frequent crashes.

**What's borrowable**: Almost nothing. Too early-stage.

### milo-script-library

**GitHub**: https://github.com/hmxmilohax/milo-script-library

Raw DTA scripts extracted from various Harmonix titles (Frequency, Amplitude,
Guitar Hero, Rock Band, Dance Central). Useful for understanding game configuration
but not directly relevant to the rendering port.

### milo-rnd-library

**GitHub**: https://github.com/hmxmilohax/milo-rnd-library

Rendering files (`.milo` archives) from various Harmonix games. Could be useful for
testing the renderer — load these files and verify visual output.

---

## RB3 Decomp

**GitHub**: https://github.com/DarkRTA/rb3
**Progress**: 54.23% decompiled, 10.65% fully linked
**Target**: Wii version of Rock Band 3
**Tracked at**: https://decomp.dev/DarkRTA/rb3

The closest parallel project to DC3 decomp. Same Milo engine, different game,
different platform (Wii vs Xbox 360).

### What's Borrowable

- **Shared Milo engine code**: Many engine classes are identical or near-identical
  between RB3 and DC3. We already use RB3 as a reference via `lookup_rb3`.
- **Wii rendering backend**: RB3 uses GX (Wii's graphics API), which is different
  from D3D9 but demonstrates how the Milo `Rnd` abstraction maps to a non-D3D
  backend. This is directly relevant as a reference for writing a new backend.
- **Platform abstraction patterns**: How RB3 handles Wii-specific features while
  maintaining the generic Milo architecture.

### Limitations

- RB3 decomp targets Wii, which has its own quirks (GX API, limited memory, no shaders)
- Only 54% complete — significant portions are still missing
- Wii's fixed-function pipeline is very different from modern GPU programming

---

## Harmonix Themselves

### Dance Central VR (2019)

Harmonix ported Dance Central to Oculus Quest/Rift but used **Unreal Engine 4.21**,
NOT the Milo engine. This was a complete rewrite, not a port.

This tells us that even Harmonix considered the Milo engine too platform-specific to
port directly — they chose to rewrite from scratch on a modern engine.

### Rock Band 4 (2015)

Harmonix built a new engine called **Forge** for PS4/Xbox One, abandoning Milo
entirely. The Milo engine was not designed for modern consoles.

### Implications

If Harmonix — who wrote the engine — chose to rewrite rather than port, that suggests:
1. Porting Milo to modern platforms is non-trivial
2. The rendering pipeline is deeply tied to its target hardware
3. A clean port requires significant new code in the rendering layer

This validates our approach of keeping game logic but writing new platform backends.

---

## Other Relevant Projects

### Unleashed Recompiled

**GitHub**: https://github.com/hedge-dev/UnleashedRecomp

A successful Xbox 360 to PC port of Sonic Unleashed, using:
- **XenosRecomp**: Shader decompiler (Xenos microcode → HLSL)
- Custom D3D12 renderer
- Recompiled game binary (different approach from decompilation)

**What's borrowable**:
- XenosRecomp for understanding DC3's Xbox 360 shader programs
- Architectural patterns for an Xbox 360 → PC port
- Xbox 360 texture deswizzling code

### Xenia Emulator

**GitHub**: https://github.com/xenia-project/xenia

Xbox 360 emulator. We already use xenia-headless for testing the decomp XEX.

**What's borrowable**:
- **Texture deswizzling**: `texture_info.cc` / `texture_extent.cc` — reference
  implementation for converting Xbox 360 tiled textures to linear format
- **Shader translation**: `SpirvShaderTranslator` — Xenos microcode to SPIR-V
  (designed for runtime emulation, but useful as reference)
- **GPU documentation**: `docs/gpu.md` — detailed Xbox 360 GPU architecture

---

## Summary: Community Resource Map

| Resource | Type | Usefulness for Native Port |
|----------|------|---------------------------|
| YARG | Game | Low (different engine) |
| MiloEditor/MiloLib | Tool | Medium (format reference) |
| Mackiloha/pikaxe | Tool | Low (format reference) |
| milo-rnd-library | Assets | Medium (test data) |
| RB3 Decomp | Code | High (shared engine, Wii backend reference) |
| Unleashed Recomp | Code | High (Xbox 360 → PC porting patterns) |
| Xenia | Code | High (texture deswizzling, shader reference) |
| XenosRecomp | Tool | High (shader decompilation) |

## Verdict

**No drop-in solution exists.** Nobody has built a Milo engine renderer. The
community has built parsers (MiloEditor), modding tools (Mackiloha), and games
inspired by Rock Band (YARG), but no rendering pipeline.

The most valuable community resources are:
1. **RB3 decomp** — shared engine code, Wii backend as reference for non-D3D renderer
2. **Xenia** — texture deswizzling, GPU documentation
3. **XenosRecomp** — shader understanding
4. **MiloLib** — file format documentation (though our decomp has the original code)

The native port will need an original rendering implementation. The question is which
API/library to target (see other approach docs in this directory).

## References

- [YARG GitHub](https://github.com/YARC-Official/YARG)
- [YARG Wiki](https://wiki.yarg.in/)
- [MiloEditor](https://github.com/ihatecompvir/MiloEditor)
- [Mackiloha](https://github.com/PikminGuts92/Mackiloha)
- [pikaxe](https://github.com/PikminGuts92/pikaxe)
- [RB3 Decomp](https://github.com/DarkRTA/rb3)
- [MiloHax Organization](https://github.com/hmxmilohax)
- [milo-rnd-library](https://github.com/hmxmilohax/milo-rnd-library)
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp)
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp)
- [Xenia GPU docs](https://github.com/xenia-project/xenia/blob/master/docs/gpu.md)
- [Stage Tour announcement](https://www.shacknews.com/article/148052/stage-tour-redoctane-games-holiday-2026)
