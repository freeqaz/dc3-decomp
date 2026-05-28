# DC3 Endianness Port Checklist — Serialized Bitfields & Packed Flag Words

Status: analysis only (2026-05-28). No source changes.
Tool: `rb3/scripts/analysis/endianness_audit.py --repo .../dc3-decomp`

## The hazard

DC3's original target is the Xbox 360 — **big-endian**. The native port runs on
**little-endian** hosts (x86 / ARM). C compilers allocate bitfield members in a
host-endian-dependent order: MSB-first on the BE Xbox360 toolchain, LSB-first on
a LE port compiler. So any bitfield struct whose **raw storage word is persisted
as a unit** — saved to disk, embedded in a `.milo` blob, or sent over the
network — decodes to the *wrong* fields on the port, **even after the
(de)serializer byte-swaps the word**. Byte order and bit order are independent
problems; `ReadEndian` fixes the former, not the latter.

This is invisible to the decomp match metric: the original build is big-endian
and matches the target perfectly. Corruption only manifests on a LE host.

The hazard requires **all** of:
1. fields packed via a C bitfield (or a `union { bitfields; word; }`), and
2. the packed word read/written as a single unit through the binary stream.

If the serializer instead writes each field as its own byte / its own
`bs << field`, there is no shared storage word and no bit-order dependency.

## DC3 bitfield surface (from the sweep)

- **In-scope bitfield structs: 4** (full catalog incl. input/SDK: 7)
- **HIGH risk (serialization signal): 2** — both classify benign on inspection
- **LOW risk (runtime-only): 2 in-scope / 5 full**

### Classification table

| Struct | File:line | Class | Affected word | Port action | Shared vs DC3 |
|---|---|---|---|---|---|
| `HamCamShot::Target` | `system/hamobj/HamCamShot.h:55-64` | **SAFE** (field-by-field) | none — each bitfield written as its own `unsigned char` | none | DC3-game-specific (`hamobj`) |
| `MatShaderOptions` | `system/rndobj/Mat.h:16-54` | **RUNTIME-ONLY (false HIGH)** | `union{...; u32 pack}` never persisted via BinStream | none for serialization; if ever embedded in a shader/material disk format, treat the `pack` word per the RB3 shared analysis | **SHARED** engine (`rndobj`) → see RB3 checklist |
| `kdTree::kdTreeNode` | `system/math/kdTree.h:77-90` | RUNTIME-ONLY | no serialization; spatial index built at load | none | SHARED engine (`math`) |
| `__debug_alloc::__alloc_header` | `system/stlport/stl/_alloc.h:165-166` | RUNTIME-ONLY | allocator header, never persisted | none | SHARED (stlport) — out of port scope |
| `ProGuitarData` / `ProGuitarStringInfo` | `system/os/Joypad.h:156-209` | RUNTIME-ONLY (USB/MIDI wire, not asset) | live controller report parse | n/a — replaced by host input layer | SHARED-ish; `system/os` is replaced in port |
| `ProKeysData` | `system/os/UsbMidiKeyboard.h:10-30` | RUNTIME-ONLY (USB wire) | live MIDI report parse | n/a — replaced by host input layer | `system/os` — replaced in port |

### HIGH-risk detail (cited)

**`HamCamShot::Target` → SAFE.** The serializers split every bitfield into its
own `unsigned char` byte; the packed 0x64/0x68 word is never written/read as a
unit:
- write `system/hamobj/HamCamShot.cpp:451-471` — `unsigned char teleport = t.mTeleport; bs.Write(&teleport,1);` and likewise for `mReturn`, `mSelfShadow`, `unk68p4`, `unk68p3`, `mForceLOD`.
- read `system/hamobj/HamCamShot.cpp:473-507` — symmetric per-byte `bs.Read(&x,1); t.field = (x != 0);`.

No bit-order dependency. **No NEEDS-BIT-REMAP struct exists in DC3.**

**`MatShaderOptions` → RUNTIME-ONLY (false HIGH).** Flagged only because `Mat.h`
`#include`s `utl/BinStream.h`. The struct (with its `union { bitfields; u32 pack;
uint value; bf ...; }`) is constructed and consumed entirely at runtime —
`GetDefaultMatShaderOpts` (`system/rndobj/Utl.cpp:203`), shader compilation
(`ShaderProgram.cpp`, `ShaderMgr.cpp`), mesh draw (`Mesh.cpp`, `Part.cpp`,
`Flare.cpp`, `MultiMesh.cpp`). `RndMat::Load`/`LoadOld`
(`system/rndobj/Mat.cpp:281,567+`) serialize each material property with its own
`d >> field`; the `pack`/`value` word is never streamed. If a future port path
ever embeds this word in an asset, defer to the RB3 shared-engine checklist for
the canonical `MatShaderOptions` bit map.

## Task 3 — explicit packed-flag-word serialization (the class the sweep misses)

DC3 decomps frequently represent flags as **explicit bit-masking on an integer
word** (`flags & 0x40`, `x >> 3 & 1`) rather than C bitfields. The sweep only
finds C bitfields, so this class was checked by hand.

**Finding: this pattern is common, but it is endian-SAFE in DC3, by construction.**

Two reasons:

1. **Integer flag words go through `ReadEndian`/`WriteEndian` as a whole word.**
   `BinStream::operator>>(int&)` calls `ReadEndian(&rhs, sizeof(int))`
   (`system/utl/BinStream.h:88-92`), byte-swapping the 4-byte word to host-native
   order on load. Once the word is a native `int`, bit positions within it are
   **not** endian-dependent: `flags & 0x40` and `flags >> 3` select the same
   logical bits the Xbox360 wrote. Examples of persisted-then-masked flag words —
   all SAFE: `MoveVariant::mFlags` (`hamobj/MoveVariant.cpp:263` read; tested as
   `mFlags & 1`, `& ~1` at :97), `CharClip::mFlags`/`mPlayFlags`
   (`char/CharClip.cpp:559-560`), `CharMeshHide::mFlags`
   (`char/CharMeshHide.cpp:26,85`), `SkeletonClip` `mQualityFlags`
   (`gesture/SkeletonClip.cpp:95`), `CharCollide`/`CharClipGroup`/`CharHair`
   `mFlags` (each `char/*.cpp`), `HamBattleData` `flags`
   (`hamobj/HamBattleData.cpp:54-56`).

2. **Many "flag words" are built at runtime from DTA config, not deserialized.**
   e.g. `MoveVariant::SyncObjects` packs `mFlags |= cfg->FindInt(scored,0) << 1;
   ... << 3; ... << 4; ... | 0x40;` (`hamobj/MoveVariant.cpp:139-149`) from named
   DTA keys, and `PoseFatalities` reads `flag_val = mFeedbackFlags` from an
   in-memory member (`hamobj/PoseFatalities.cpp:362-364`). No disk word, no
   hazard.

**Conclusion for task 3:** explicit packed-flag-word serialization is *prevalent*
in DC3 save/asset paths but is **not an additional endianness concern**, because
every such word is streamed as a full integer through `ReadEndian` (byte-swapped
as a unit) and bit masks index a native integer. The only thing that would break
is a **C bitfield struct whose storage word is persisted as a unit** — and DC3
has zero of those (the one HIGH bitfield with a real serializer,
`HamCamShot::Target`, writes byte-per-field).

### Watch-list (not a current bug; re-check if these paths change)
- If any future serializer writes a `MatShaderOptions`/`union pack` word directly
  (memcpy of the struct, or `bs << opts.pack`), it becomes NEEDS-BIT-REMAP — use
  the RB3 shared `MatShaderOptions` bit map.
- If `HamCamShot::Target` is ever optimized to write the packed `0x64/0x68` word
  as one unit, it flips from SAFE to NEEDS-BIT-REMAP.

## Shared-engine vs DC3-game-specific

- **SHARED engine** (covered by the RB3 checklist, just confirm DC3 copy matches):
  `MatShaderOptions` (`rndobj`), `kdTreeNode` (`math`), `__alloc_header`
  (stlport). DC3 copies are field-for-field equivalent; none is persisted as a
  packed word in DC3.
- **DC3-game-specific** (dance-central / `hamobj`): `HamCamShot::Target` —
  SAFE, no port action.
- **Replaced in port** (`system/os` input): `ProGuitarData`,
  `ProGuitarStringInfo`, `ProKeysData` — live USB/MIDI controller reports, handled
  by the host input layer, not asset endianness.
