# Harmonix / Milo / Rock Band / Dance Central Repo Index

A quick index of useful public repositories for reverse-engineering and parsing Harmonix-era game assets. All repos are cloned under `harmonix-repos/`.

## Repos

---

### [MiloEditor](harmonix-repos/MiloEditor)
**Language:** C# (.NET 8.0) | **Size:** ~8 MB
**GitHub:** [ihatecompvir/MiloEditor](https://github.com/ihatecompvir/MiloEditor)

A cross-platform editor for Milo engine scene files, with best support for Rock Band 3 and Dance Central 1. Includes **MiloLib** (a .NET Standard library for programmatic Milo scene manipulation), **MiloUtil** (CLI tool), and **ImMilo** (cross-platform ImGui-based UI). Best general starting point for understanding Milo scene structure and common object types.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `MiloLib.sln` |
| **MiloLib** (core library) | `MiloLib/` |
| Milo file format (open/save/compress) | `MiloLib/MiloFile.cs` |
| Base object & NodeType enum | `MiloLib/Assets/Object.cs` |
| Asset factory & directory metadata | `MiloLib/Assets/DirectoryMeta.cs` |
| Rendering assets (mesh, mat, tex, cam, etc.) | `MiloLib/Assets/Rnd/` (~50 files) |
| Character assets (clips, bones, IK, hair) | `MiloLib/Assets/Char/` (~20 files) |
| Dance Central assets (moves, sequences, skeleton) | `MiloLib/Assets/Ham/` (~11 files) |
| UI widget hierarchy | `MiloLib/Assets/UI/` (~30 files) |
| Band-specific assets | `MiloLib/Assets/Band/` (~17 files) |
| Synth/audio assets | `MiloLib/Assets/Synth/` (~6 files) |
| World/environment assets | `MiloLib/Assets/World/` (~5 files) |
| Endian-aware binary I/O | `MiloLib/Utils/Endian/` |
| **ImMilo** (cross-platform ImGui GUI) | `ImMilo/` |
| ImMilo main window & scene loading | `ImMilo/Program.cs` |
| ImMilo scene tree UI | `ImMilo/Program.SceneTree.cs` |
| ImMilo property editor | `ImMilo/EditorPanel.cs` |
| ImMilo texture viewer | `ImMilo/BitmapEditor.cs` |
| ImMilo mesh preview | `ImMilo/MeshEditor.cs` |
| **MiloEditor** (legacy WinForms GUI) | `MiloEditor/` |
| WinForms main form | `MiloEditor/MainForm.cs` |
| **MiloUtil** (CLI tool) | `MiloUtil/` |
| CLI entry point (info, extract, uncompress) | `MiloUtil/Program.cs` |
| Unit tests | `MiloLib.Tests/` |

</details>

---

### [Mackiloha](harmonix-repos/Mackiloha)
**Language:** C# (.NET 8.0) | **Size:** ~2 MB
**GitHub:** [PikminGuts92/Mackiloha](https://github.com/PikminGuts92/Mackiloha)

A modding toolkit for Milo engine games, providing three main CLI tools: **Ark Helper** (unpack/repack `.ark` archives from Amplitude PS2 through RB3), **P9 Song Tool** (venue authoring for The Beatles: Rock Band), and **SuperFreq** (unpack/pack RND/Milo scene archives for GH1/GH2-era games).

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `Mackiloha.sln` |
| **Core library** | `Src/Core/Mackiloha/` |
| ARK archive reader/writer (v2-v10) | `Src/Core/Mackiloha/Ark/ArkFile.cs` |
| DTB format parser | `Src/Core/Mackiloha/DTB/DTBFile.cs` |
| Milo container (block compression) | `Src/Core/Mackiloha/Milo/MiloFile.cs` |
| Serialization dispatcher | `Src/Core/Mackiloha/IO/MiloSerializer.cs` |
| Format-specific serializers | `Src/Core/Mackiloha/IO/Serializers/` (Tex, Bitmap, PropAnim, etc.) |
| Texture object definition | `Src/Core/Mackiloha/Render/Tex.cs` |
| Endian-aware binary I/O | `Src/Core/Mackiloha/AwesomeReader.cs`, `AwesomeWriter.cs` |
| Compression (ZLIB/GZIP) | `Src/Core/Mackiloha/Compression.cs` |
| Encryption (RC4-based DTB/ARK) | `Src/Core/Mackiloha/Crypt.cs` |
| App support library | `Src/Core/Mackiloha.App/` |
| **Ark Helper** CLI | `Src/Apps/ArkHelper/` |
| Ark Helper entry point | `Src/Apps/ArkHelper/Program.cs` |
| ARK extract/repack commands | `Src/Apps/ArkHelper/Apps/Ark2DirApp.cs`, `Dir2ArkApp.cs` |
| **P9 Song Tool** CLI | `Src/Apps/P9SongTool/` |
| MIDI-to-animation converter | `Src/Apps/P9SongTool/Helpers/Midi2Anim.cs` |
| **SuperFreq** CLI | `Src/Apps/SuperFreq/` |
| Milo extract/repack commands | `Src/Apps/SuperFreq/Apps/Milo2DirApp.cs`, `Dir2MiloApp.cs` |
| PNG-to-texture converter | `Src/Apps/SuperFreq/Apps/Png2TextureApp.cs` |

</details>

---

### [pikaxe](harmonix-repos/pikaxe)
**Language:** Rust (2024 edition) | **Size:** ~3 MB
**GitHub:** [PikminGuts92/pikaxe](https://github.com/PikminGuts92/pikaxe)

A Rust toolkit and successor to Mackiloha for working with Harmonix game file formats. Provides CLI tools for audio (encode/decode), meshes (Milo to glTF conversion), and scene/archive manipulation (Milo, ARK). Also exposes a Python API via PyO3. Under active construction.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Workspace root | `Cargo.toml` |
| **Core library** | `core/pikaxe/` |
| Library entry point & module exports | `core/pikaxe/src/lib.rs` |
| Platform detection (PS2/PS3/Wii/X360) | `core/pikaxe/src/system.rs` |
| Milo container structure | `core/pikaxe/src/scene/milo.rs` |
| Scene object type enum (20+ types) | `core/pikaxe/src/scene/object.rs` |
| Scene binary I/O | `core/pikaxe/src/scene/io.rs` |
| Mesh parsing | `core/pikaxe/src/scene/mesh/` |
| Material parsing | `core/pikaxe/src/scene/mat/` |
| Transform/animation parsing | `core/pikaxe/src/scene/trans/`, `anim/`, `prop_anim/` |
| Character objects (bone, lip sync, hair) | `core/pikaxe/src/scene/character/`, `char_bone/`, `char_lip_sync/` |
| ARK archive format | `core/pikaxe/src/ark/ark.rs`, `io.rs` |
| DTA parser (nom-based, WIP) | `core/pikaxe/src/dta/parser.rs` |
| Audio codecs (XMA, WAV, ADPCM, VGS) | `core/pikaxe/src/audio/` |
| Texture formats (DXT, TPL, bitmap) | `core/pikaxe/src/texture/` |
| Binary streams & archive I/O | `core/pikaxe/src/io/stream.rs`, `archive.rs` |
| Encryption/compression | `core/pikaxe/src/io/crypt.rs`, `compression.rs` |
| Trait definitions (MiloObject, etc.) | `core/pikaxe_traits/` |
| Proc macros (`#[milo(...)]` derives) | `core/pikaxe_macros/` |
| MIDI parsing | `core/pikaxe_midi/` |
| glTF utilities | `core/pikaxe_gltf/` |
| **scene_tool** CLI (milo2dir, dir2milo) | `apps/cli/scene_tool/` |
| **audio_tool** CLI (decode, encode) | `apps/cli/audio_tool/` |
| **mesh_tool** CLI (milo2gltf, anim) | `apps/cli/mesh_tool/` |

</details>

---

### [LibForge](harmonix-repos/LibForge)
**Language:** C# (.NET 4.7.1) | **Size:** ~2 MB
**GitHub:** [maxton/LibForge](https://github.com/maxton/LibForge)

A library and toolset for the Forge engine (Rock Band 4 / Rock Band VR). Includes **ForgeTool** (CLI for converting between Forge MIDI, standard MIDI, textures, meshes, and CON-to-PS4-PKG conversions) and **ForgeToolGUI** (archive browser with texture, model, songdta, and MIDI previewing). Also contains 010 Editor templates documenting RB4 file structures.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `LibForge/LibForge.sln` |
| **Core library** | `LibForge/LibForge/` |
| Core engine data types & properties | `LibForge/LibForge/Engine/DataTypes.cs` |
| RBMid data structure | `LibForge/LibForge/Midi/RBMid.cs` |
| RBMid-to-MIDI converter (largest file) | `LibForge/LibForge/Midi/RBMidConverter.cs` |
| Texture format converter | `LibForge/LibForge/Texture/TextureConverter.cs` |
| Mesh format (HxMesh) | `LibForge/LibForge/Mesh/HxMesh.cs` |
| Lipsync animation | `LibForge/LibForge/Lipsync/` |
| Song metadata (.songdta) | `LibForge/LibForge/SongData/` |
| RBSong venue data | `LibForge/LibForge/RBSong/` |
| Fuser format support | `LibForge/LibForge/Fuser/` |
| ARK archive handling | `LibForge/LibForge/Ark/` |
| Milo file format | `LibForge/LibForge/Milo/` |
| Binary I/O (endian-aware) | `LibForge/LibForge/Util/BinReader.cs`, `BinWriter.cs` |
| PS4 PKG creation | `LibForge/LibForge/Util/PkgCreator.cs` |
| **ForgeTool** CLI | `LibForge/ForgeTool/Program.cs` |
| **ForgeToolGUI** | `LibForge/ForgeToolGUI/` |
| GUI archive browser | `LibForge/ForgeToolGUI/ForgeBrowser.cs` |
| GUI inspector plugins | `LibForge/ForgeToolGUI/Inspectors/` (~21 files) |
| **010 Editor templates** | `010/` |
| MIDI format template | `010/ForgeMidi.bt` |
| Core Forge types template | `010/ForgeTypes.bt` |
| Texture format template | `010/ForgeTex.bt` |
| DTB format template | `010/dtb.bt` |
| Milo archive template | `010/Milo.bt` |
| DLC package structure docs | `files-in-DLC-packages.md` |
| Fuser song format docs | `fuser_songfmt.md` |
| RBSong structure docs | `rbsong_contents.txt` |

</details>

---

### [GameArchives](harmonix-repos/GameArchives)
**Language:** C# (.NET 3.5) | **Size:** ~1 MB
**GitHub:** [maxton/GameArchives](https://github.com/maxton/GameArchives)

A general-purpose C# library for reading various video game archive formats: Harmonix ARK (`.hdr`/`.ark`, versions 1-10), FSG-FILE-SYSTEM (DJ Hero 2, GH Live), FSAR (DJ Hero), PFS (PS4 DLC), PSARC (PS3/PS4), STFS/CON/LIVE (Xbox 360), XDVDFS (Xbox disc images), U8 (Wii), SingStar PACKAGE, and Seven45 PK. Includes an ArchiveExplorer GUI.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `GameArchives.sln` |
| **Core library** | `Library/` |
| Core interfaces (IFile, IDirectory, AbstractPackage) | `Library/ArchiveInterfaces.cs` |
| Format type registration & detection | `Library/PackageType.cs` |
| Auto-detect format & open packages | `Library/PackageReader.cs` |
| Utility functions (extract, copy, local I/O) | `Library/Util.cs` |
| Shared implementations (OffsetFile, MultiStream) | `Library/Common/` |
| **Format parsers** (one directory each): | |
| Harmonix ARK | `Library/Ark/` (+ `HdrCryptStream.cs`) |
| Xbox 360 STFS/CON/LIVE | `Library/STFS/` |
| PS3/PS4 PSARC | `Library/PSARC/` |
| FreeStyleGames FSAR | `Library/FSAR/` (+ `AesCryptStream.cs`) |
| FSG disc filesystem | `Library/FSGIMG/` |
| PS4 PFS | `Library/PFS/` (+ `XtsCryptStream.cs`) |
| Wii U8 | `Library/U8/` |
| Xbox/X360 ISO | `Library/XISO/` |
| SingStar PKG | `Library/PKF/` |
| Seven45 PK | `Library/Seven45/` (+ `PowerChordCryptStream.cs`) |
| Local filesystem adapter | `Library/Local/` |
| **ArchiveExplorer** GUI | `ArchiveExplorer/ArchiveExplorer.cs` |
| GUI shared components | `LibArchiveExplorer/` |
| Package manager singleton | `LibArchiveExplorer/PackageManager.cs` |
| Archive browser control | `LibArchiveExplorer/PackageView.cs` |

**Architecture:** Plugin-style format registration. Each format implements `IFile`/`IDirectory` over its binary structure. `PackageReader.ReadPackageFromFile()` auto-detects format via magic bytes. Streams are lazy-loaded with offset/crypt wrappers.

</details>

---

### [DtxCS](harmonix-repos/DtxCS)
**Language:** C# (.NET 3.5) | **Size:** ~300 KB
**GitHub:** [maxton/DtxCS](https://github.com/maxton/DtxCS)

A C# library for parsing and executing Harmonix's DTA/DTB/DTX scripting language — the Lisp-like "Data Array" system used across the Rock Band and Guitar Hero series. Supports reading plaintext DTA and serialized/encrypted DTB formats, executing script commands with basic built-in functions, and exporting back to plaintext DTA.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `DtxCS.sln` |
| **Core library** | `Library/` |
| Main parser entry (`FromDtaString`, `FromDtb`, etc.) | `Library/DTX.cs` |
| Type system (DataType enum, DataNode hierarchy) | `Library/DataTypes/Types.cs` |
| Container types (DataArray, DataCommand, DataMacro) | `Library/DataTypes/ArrayTypes.cs` |
| Builtin functions (+, -, if, >, <, abs, etc.) | `Library/Builtins.cs` |
| DTB decryption stream (XOR cipher w/ key rotation) | `Library/CryptStream.cs` |
| Binary stream helpers (LE/BE reads) | `Library/StreamExtensions.cs` |
| **DTAView** GUI (WinForms) | `DTAView/` |
| DTAView main form (DTB viewer + REPL console) | `DTAView/MainForm.cs` |

**Type hierarchy:** `DataNode` (abstract) -> `DataAtom` (string/int/float), `DataVariable` ($ref), `DataSymbol`, `DataArray` (parens/braces/brackets), `DataDirective` (#ifdef, #include, #merge, etc.)

**DTB format:** 3 versions; optional encryption auto-detected. Chunk type bytes: `0x00`=int, `0x01`=float, `0x02`=var, `0x05`=symbol, `0x10`=array, `0x11`=command, `0x12`=string, `0x13`=macro, `0x20-0x25`=directives.

</details>

---

### [dtab](harmonix-repos/dtab)
**Language:** Haskell | **Size:** ~300 KB
**GitHub:** [mtolly/dtab](https://github.com/mtolly/dtab)

A Haskell library and CLI tool to read, write, encrypt, and decrypt Harmonix DTA (text) and DTB (binary) data files. Built on prior work by xorloser (ArkTool/DtbCrypt) and deimos (dtb2dta).

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Build config (Cabal) | `dtab.cabal` |
| Stack config | `stack.yaml` |
| CLI entry point (modes: -a, -A, -b, -d, -e, -D, -E) | `Main.hs` |
| **Core library** | `src/Data/DTA/` |
| Main API (`lFromDTB`, `hToDTA`, `toDTB`, etc.) | `src/Data/DTA.hs` |
| Type definitions (DTA, Tree, Chunk w/ 16 constructors) | `src/Data/DTA/Base.hs` |
| Alex lexer rules (.dta tokenization) | `src/Data/DTA/Lex.x` |
| Happy parser grammar (.dta -> AST) | `src/Data/DTA/Parse.y` |
| Encryption engines (old/new XOR stream cipher) | `src/Data/DTA/Crypt.hs` |
| Pretty printer (AST -> .dta text) | `src/Data/DTA/PrettyPrint.hs` |

**Chunk types:** Int, Float, Var, Sym, Unhandled, IfDef, Else, EndIf, Parens, Braces, String, Brackets, Define, Include, Merge, IfNDef, Autorun, Undef. Binary codec has DTAVersion1 (RB3) and DTAVersion2 (Fantasia) variants.

</details>

---

### [dtb2dta](harmonix-repos/dtb2dta)
**Language:** C | **Size:** ~130 KB
**GitHub:** [Deimos/dtb2dta](https://github.com/Deimos/dtb2dta)

A single-file C program that converts Harmonix DTB (binary data tree) files back to DTA (human-readable text) format. Covers Guitar Hero 1/2/80s and Rock Band 1/2 era titles. Simple and self-contained.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Entire program (295 lines) | `dtb2dta.c` |

**Key functions:** `main()` (line 19) opens DTB and calls `parse_tree()` (line 40) which recursively handles chunk types: `0x00`=int, `0x01`=float, `0x02`=var, `0x05`=symbol, `0x07-0x09`=#ifdef/#else/#endif, `0x10`=array(), `0x11`=command{}, `0x12`=string, `0x13`=macro[], `0x20-0x27`=directives.

Build: `gcc dtb2dta.c -o dtb2dta` (no dependencies).

</details>

---

### [Boomy](harmonix-repos/Boomy)
**Language:** C# (.NET 8.0), TypeScript (Electron + React) | **Size:** ~13 MB
**GitHub:** [NORXND/Boomy](https://github.com/NORXND/Boomy)

An all-in-one song editor for Dance Central 3. Handles choreography editing, camera shots, MIDI, MOGG audio creation, and Xbox 360 package building. Includes an Electron/React editor frontend and a .NET backend builder, plus a bundled copy of MiloLib.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Solution file | `Boomy.sln` |
| **Editor frontend** (Electron + React 19 + TypeScript) | `BoomyEditor/` |
| Electron main process | `BoomyEditor/src/main.ts` |
| IPC handlers (file I/O, build invocation) | `BoomyEditor/src/main/ipcHandlers.ts` |
| React app root | `BoomyEditor/src/renderer.tsx` |
| Main editor layout (9 editor sections) | `BoomyEditor/src/app/editor/EditorRoot.tsx` |
| Song/choreo type definitions (critical) | `BoomyEditor/src/app/types/song.ts` |
| Song state management (Zustand) | `BoomyEditor/src/app/store/songStore.ts` |
| Move choreography timeline | `BoomyEditor/src/app/editor/timeline_new/ChoreographyTimeline.tsx` |
| Camera shot timeline | `BoomyEditor/src/app/editor/timeline_new/CameraShotsTimeline.tsx` |
| Dancer face/viseme timeline | `BoomyEditor/src/app/editor/timeline_new/DancerFacesTimeline.tsx` |
| Song loaders | `BoomyEditor/src/app/loaders/songLoader.ts`, `song3loader.ts` |
| Electron Forge packaging config | `BoomyEditor/forge.config.ts` |
| **Builder backend** (.NET 8.0) | `BoomyBuilder/` |
| Builder entry point (JSON stdin/file) | `BoomyBuilder/Program.cs` |
| Main build orchestrator | `BoomyBuilder/Builder/BuildOperator.cs` |
| Choreography builder | `BoomyBuilder/Builder/ChoreoMaker.cs` |
| Camera shot builder | `BoomyBuilder/Builder/Camerator.cs` |
| MIDI generator | `BoomyBuilder/Builder/MidiMaker.cs` |
| Drums/percussion builder | `BoomyBuilder/Builder/Drumer.cs` |
| Viseme/lip-sync builder | `BoomyBuilder/Builder/DancerFaceMaker.cs` |
| Battle mode builder | `BoomyBuilder/Builder/BattleMaster.cs` |
| Practice sections builder | `BoomyBuilder/Builder/PracticeSectioner.cs` |
| Build request data model | `BoomyBuilder/Builder/Models/BuildRequest.cs` |
| **Bundled MiloLib** | `BoomyDeps/MiloLib/` |
| MiloLib Milo file I/O | `BoomyDeps/MiloLib/MiloFile.cs` |
| DC3 move definition | `BoomyDeps/MiloLib/Assets/Ham/HamMove.cs` |
| DC3 dancer sequence | `BoomyDeps/MiloLib/Assets/Ham/DancerSequence.cs` |
| DC3 battle/party/BAM data | `BoomyDeps/MiloLib/Assets/Ham/HamBattleData.cs`, `HamPartyJumpData.cs`, `BustAMoveData.cs` |
| **Exporter** CLI (extract moves from stock DC games) | `BoomyExporter/ExportOperator.cs` |
| **Converters** (album art, Xbox packages) | `BoomyConverters/` |

</details>

---

### [PyMilo](harmonix-repos/PyMilo)
**Language:** Python | **Size:** ~160 KB
**GitHub:** [PikminGuts92/PyMilo](https://github.com/PikminGuts92/PyMilo)

A Python library for reading and managing Milo container files (the scene/asset archive format used by Harmonix games). Includes a GUI and utilities for inflating Milo files from ARK archives.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Milo container parser (compression, block types) | `MiloContainer.py` |
| Binary reader (struct unpacking, endianness) | `AwesomeReader.py` |
| CLI entry point | `PyMilo.py` |
| Tkinter GUI | `PyMiloGUI.py` |
| ARK archive extraction utility | `inflatemilosfromarchive.py` |

**Key classes:** `CompressionType` enum (NONE, GZIP, MILO_A/B/C/D), `MiloContainer.__init__()` (parses header and decompresses blocks), `AwesomeReader` (context manager for binary I/O).

</details>

---

### [milo-rnd-library](harmonix-repos/milo-rnd-library)
**Language:** Binary data (`.milo_xbox`, `.milo_ps3`, `.rnd_ps2`, etc.) | **Size:** ~63 GB
**GitHub:** [hmxmilohax/milo-rnd-library](https://github.com/hmxmilohax/milo-rnd-library)

A massive archive of extracted rendering/scene files from across the Harmonix catalog. Contains ~60,000 Milo/RND binary scene files with meshes, textures, animations, and UI layouts across multiple platform variants. Primarily useful as reference data for validating parsers and studying Milo engine rendering assets.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Extraction instructions | `README.md` |
| **Game directories** (each contains platform subdirs): | |
| Rock Band 3 (360, 360_rev1, 360_TU5, ps3, wii) | `rb3/` (~11 GB) |
| Lego Rock Band | `lrb/` (~11 GB) |
| Dance Central 3 | `dc3/` (~9.4 GB) |
| Rock Band 2 | `rb2/` (~3 GB) |
| Dance Central 2 | `dc2/` (~3 GB) |
| Rock Band 1 | `rb1/` (~2.3 GB) |
| Dance Central 1 | `dc1/` (~1.4 GB) |
| The Beatles: Rock Band | `tbrb/` (~1.1 GB) |
| RB Blitz, GH1, GH2, GH 80s, GDRB, KRP, RBMS, Amplitude, Frequency | `blitz/`, `gh1/`, `gh2/`, `gh80s/`, `gdrb/`, `krp/`, `rbms/`, `amp03/`, `freq/` |

**DC3 asset layout** (typical): `dc3/char/gen/`, `dc3/config/`, `dc3/flow/`, `dc3/ui/`, `dc3/world/`, `dc3/songs/`, `dc3/sfx/`

</details>

---

### [milo-script-library](harmonix-repos/milo-script-library)
**Language:** DTA scripts (Harmonix's Lisp-like scripting language) | **Size:** ~745 MB
**GitHub:** [hmxmilohax/milo-script-library](https://github.com/hmxmilohax/milo-script-library)

A comprehensive archive of extracted game scripts (DTA/DTB) from nearly every Harmonix title, spanning FreQuency (2001) through Super Beat Sports. Covers official releases, prototypes, and demos.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Extraction status table (all games) | `README.md` |
| **Game directories** (each may have version/platform subdirs): | |
| Dance Central 3 (final + proto) | `dc3/1.0 final/`, `dc3/proto/` |
| Rock Band 3 (360, ps3, wii, wii protos) | `rb3/360/`, `rb3/ps3/`, `rb3/wii/`, `rb3/wii_proto_bank*/` |
| Dance Central 1 & 2 | `dc1/`, `dc2/` |
| Rock Band 1 & 2 | `rb1/`, `rb2/` |
| Guitar Hero 1, 2, 80s | `gh1/`, `gh2/`, `gh80s/` |
| The Beatles: Rock Band | `tbrb/` |
| Frequency, Amplitude | `freq/`, `amp2003/`, `amp2016/` |
| Rock Band 4, VR, Blitz | `rb4/`, `rbvr/`, `rbblitz/` |
| Fantasia, Super Beat Sports | `fantasia/`, `superbeatsports/` |
| + many more (krp, lrb, gdrb, phase, magma, etc.) | |

**Typical script files** (e.g., in `dc3/`): `char_objects.dta`, `beatmatch.dta`, `flow.dta`, `joypad.dta`, `milo_objects.dta`, `rnd_objects.dta`

</details>

---

### [awesome-game-file-format-reversing](harmonix-repos/awesome-game-file-format-reversing)
**Language:** Markdown (curated list) | **Size:** ~1 MB
**GitHub:** [VelocityRa/awesome-game-file-format-reversing](https://github.com/VelocityRa/awesome-game-file-format-reversing)

An "awesome list" covering tools, documentation, communities, and resources for reverse engineering video game file formats across many studios and engines. Not a parser itself, but a strong meta-index for finding additional repos and references.

<details><summary>Key files & paths</summary>

| Purpose | Path |
|---------|------|
| Master resource list (3,366 lines) | `README.md` |
| Harmonix-specific tools section | `README.md` (line ~1703) |
| Navigation sidebar | `_sidebar.md` |
| Docsify site config | `index.html` |

**Sections:** Communities & Wikis, General Tools (viewers, extractors, audio, hex editors), Engines (GameMaker, Unreal, Unity), Middleware & SDKs (RenderWare, Havok, CRI), Game & Studio Tools (organized by developer).

</details>

---

## Suggested starting order

If the goal is to build a parser for classic Rock Band / Dance Central assets, a practical order is:

1. **MiloEditor** — Milo scene structure and core object parsing
2. **dtab** / **DtxCS** — DTA/DTB structured data parsing
3. **GameArchives** / **Mackiloha** — archive extraction and packfile handling
4. **Boomy** — Dance Central 3-specific hints
5. **milo-script-library** / **milo-rnd-library** — sample data for testing and schema discovery
