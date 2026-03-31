# XMA Audio Crash Investigation

**Date**: 2026-03-30
**Status**: Research complete, actionable fixes identified

## Summary

The XMA audio crash in Xenia is caused by the DC3 binary's statically-linked XAudio2 library calling `XMAHALAllocateContexts` (an XDK function at guest address `0x82E77250`), which internally calls `XMACreateContext` (Xenia kernel export `0x224`). Xenia's XMA subsystem is **fully implemented** -- the crash is not due to a missing stub. The SIGSEGV at `0x82E7732C` (offset `+0xDC` into `XMAHALAllocateContexts`) occurs because `XMAHALAllocateContexts` uses `MmMapIoSpace` to map XMA context data into guest-accessible memory, and the subsequent pointer arithmetic or MMIO access within the statically-linked XDK code dereferences an address that Xenia's memory mapping doesn't fully support.

## 1. Root Cause Analysis

### What is XMAHALAllocateContexts?

`XMAHALAllocateContexts` is a **statically-linked XDK library function** inside the DC3 binary (NOT a kernel export). It sits within the XAudio2 HAL (Hardware Abstraction Layer) at:
- Address: `0x82E77250`, size: `0x110`
- Part of a family: `XMAHALCreate`, `XMAHALInitialize`, `XMAHALAllocateContexts`, `XMAHALFreeContexts`, `XMAHALSubmitData`, etc.
- Called from `CX2SourceVoiceXMA` (the XMA specialization of XAudio2's source voice) during voice initialization

### The Call Chain

```
Voice::Init() (decomp: Voice.cpp)
  -> Voice::createOrReuse()
    -> CX2Engine::CreateSourceVoice() (guest: 0x82E65650)
      -> CX2SourceVoiceXMA::Initialize() (guest: 0x82E75A90) [format tag 0x166 = XMA]
        -> CX2SourceVoiceXMA::XMAVoiceInitialize() (guest: 0x82E75898)
          -> XMAHALAllocateContexts() (guest: 0x82E77250)  <-- CRASH at +0xDC
            -> XMACreateContext() (kernel export 0x224)  [Xenia: implemented]
            -> MmMapIoSpace() (kernel export)  [Xenia: returns src_address passthrough]
```

### Why It Crashes

`XMAHALAllocateContexts` calls `XMACreateContext` to allocate an XMA hardware context, then calls `MmMapIoSpace` to map the physical context address into user-space. Xenia's `MmMapIoSpace` implementation is minimal:

```cpp
// xenia/kernel/xboxkrnl/xboxkrnl_memory.cc
dword_result_t MmMapIoSpace_entry(dword_t unk0, lpvoid_t src_address, dword_t size, dword_t flags) {
  // I've only seen this used to map XMA audio contexts.
  // The code seems fine with taking the src address, so this just returns that.
  assert_true(unk0 == 2);
  assert_true(size == 0x40);
  assert_true(flags == 0x404);
  return src_address.guest_address();
}
```

The XDK's `XMAHALAllocateContexts` then does additional pointer manipulation on the returned MMIO address -- likely writing to XMA hardware registers or setting up DMA descriptors. On real hardware, `MmMapIoSpace` maps physical MMIO regions; Xenia's passthrough may return an address that's valid for reading context data but causes a SIGSEGV when the XDK code writes to it in a way Xenia doesn't expect (e.g., writing to an MMIO register offset that isn't covered by Xenia's `AddVirtualMappedRange` for the XMA decoder at `0x7FEA0000`).

The crash at offset `+0xDC` in the function (address `0x82E7732C`) is consistent with post-allocation setup code that writes to the mapped context or its associated hardware registers.

## 2. Xenia's XMA Support Level: FULL (for the kernel API path)

Xenia has a **complete XMA decode pipeline** using FFmpeg:
- `xma_decoder.cc` -- Worker thread that processes 320 XMA contexts (FFmpeg-based decode)
- `xma_context.cc` -- Per-context state machine for XMA frame decode
- `xboxkrnl_audio_xma.cc` -- All 18 kernel XMA functions implemented:
  - `XMACreateContext`, `XMAReleaseContext`, `XMAInitializeContext`
  - `XMAEnableContext`, `XMADisableContext`, `XMABlockWhileInUse`
  - `XMASetInputBuffer0/1`, `XMAIsInputBuffer0/1Valid`, `XMASetInputBuffer0/1Valid`
  - `XMAGetOutputBufferReadOffset/WriteOffset`, `XMASetOutputBufferReadOffset/Valid`
  - `XMAIsOutputBufferValid`, `XMAGetPacketMetadata`, `XMASetLoopData`
  - `XMASetInputBufferReadOffset`, `XMAGetInputBufferReadOffset`

**The problem is not missing XMA support.** Games that use the kernel XMA API directly (XMACreateContext etc.) work fine. DC3 uses the higher-level XAudio2 API with XMA format tag `0x166`, which causes the statically-linked `CX2SourceVoiceXMA` code to use the HAL layer (`XMAHALAllocateContexts`) instead of the kernel API. The HAL layer expects real hardware MMIO behavior that Xenia doesn't fully emulate.

## 3. Can We Stub XMAHALAllocateContexts?

**Yes, this is the most promising fix.** Since `XMAHALAllocateContexts` is a guest function (not a kernel export), we can patch it in Xenia's DC3 hack pack. Two approaches:

### Option A: Patch XMAHALAllocateContexts to return error (simplest)

Overwrite the function entry at `0x82E77250` with:
```ppc
li r3, -1    # Return error code (HRESULT failure)
blr          # Return immediately
```
This would make `CX2SourceVoiceXMA::XMAVoiceInitialize` fail, which should propagate up through `CreateSourceVoice` as an `HRESULT` failure. The `Voice::Init` code asserts on the result (`MILO_ASSERT(SUCCEEDED(hr), 0x19d)`), so the debug assert would fire but it would be non-fatal.

### Option B: Patch CreateSourceVoice to reject XMA format (better)

Patch `CX2Engine::CreateSourceVoice` at `0x82E65650` to reject format tag `0x166` (XMA), returning `E_INVALIDARG`. This is cleaner because it prevents the XMA voice path entirely, and the Milo engine's Voice code already checks the HRESULT.

### Option C: Patch Voice::Init to skip XMA voices (cleanest for DC3)

Add a hack pack patch that detects `mXMA == true` in `Voice::Init` and skips the `createOrReuse` call entirely. Since DC3's song audio uses `.mogg` files (Vorbis/Ogg), **XMA voices are only used for SFX samples**. Skipping them would give silent SFX but preserve song audio.

## 4. Can HamAudio::Fail() Return True?

**No, not through the natural code path with the current crash pattern.** Here's why:

```cpp
// HamAudio.cpp
bool HamAudio::Fail() { return mSongStream && mSongStream->Fail(); }

// StandardStream.cpp
bool StandardStream::Fail() { return mRdr && mRdr->Fail(); }

// VorbisReader.h
virtual bool Fail() { return mFail; }  // mFail set by File I/O errors
```

`HamAudio::Fail()` returns true only when:
1. `mSongStream` is non-null (a `StandardStream` was created), AND
2. The `StandardStream`'s `VorbisReader` has `mFail == true` (set by Vorbis/Ogg file I/O errors)

The XMA crash happens in a **completely different subsystem** -- the SFX sample playback path (`SynthSample360 -> SampleInst360 -> Voice -> XAudio2 CreateSourceVoice`), not the song stream path (`HamAudio -> StandardStream -> VorbisReader`). The song stream uses Vorbis decoding (`.mogg` files), not XMA.

The audio limbo state happens because:
1. The XMA crash during SFX voice creation corrupts/stalls the XAudio2 engine state
2. `StandardStream::IsReady()` returns true only when `mState == kReady || kPlaying || kStopped`
3. The stream is stuck in `kInit` or `kBuffering` because the underlying XAudio2 render driver callback never fires (the engine is stalled by the crash)

**Making Fail() return true would require** either:
- Patching `Synth360::Fail()` to return true (currently inherits base `Synth::Fail()` which returns false)
- Patching `HamAudio::Fail()` to return true unconditionally
- Corrupting the VorbisReader to set `mFail = true` (fragile)

The existing Xenia hack pack "Patch 8" already handles this by bypassing `HandleWait`'s `IsReady()` check entirely.

## 5. Xenia CVars and Flags

| Flag | Default | Effect |
|------|---------|--------|
| `--apu=nop` | `any` | Use NOP audio backend -- `CreateDriver` returns `X_STATUS_NOT_IMPLEMENTED`. XMA decoder still initializes. The XAudio2 library in the guest still tries to initialize with the render driver, gets a dummy handle, but **XMA voice creation still crashes** because it goes through HAL, not the render driver path. |
| `--apu=sdl` | `any` | Use SDL audio backend (Linux default with `--apu=any`) |
| `--mute` | `false` | Mutes host audio output but does NOT affect guest XMA/XAudio2 initialization |

**Important**: `--apu=nop` does NOT prevent the XMA crash. The nop backend only affects the audio render driver (`XAudioRegisterRenderDriverClient`). The XMA hardware abstraction layer is initialized separately by the guest's XAudio2 library when it creates an XMA source voice, and this path is completely independent of the render driver.

## 6. Recommended Fix Approach

### Short-term (Xenia hack pack patch):

**Patch `XMAHALCreate` to return error.** This function at `0x82E77EF0` (size `0x2E8`) is called once during XAudio2 engine initialization to set up the entire XMA HAL subsystem. If it returns an error, no `XMAHALAllocateContexts` calls will ever happen:

```cpp
// In dc3_hack_pack.cc or emulator.cc, add to the DC3 patches:
constexpr uint32_t kXMAHALCreate = 0x82E77EF0;
patch4(kXMAHALCreate,     0x38600000, "li r3, 0 (XMAHALCreate -> success stub)");
patch4(kXMAHALCreate + 4, 0x4E800020, "blr (XMAHALCreate return)");
```

Wait -- returning success (0) from `XMAHALCreate` might cause later HAL calls to use uninitialized data. Returning an error might be better, but we need to check what `CX2Engine::Initialize` does with a failed `XMAHALCreate`. The safest approach is to **stub all XMAHAL functions that write to hardware**:

**Recommended**: Patch `XMAHALAllocateContexts` to return error:
```
0x82E77250: li r3, 0x80004005   # E_FAIL  (lis r3, 0x8000 / ori r3, r3, 0x4005)
0x82E77254: (second half of load)
0x82E77258: blr
```

This prevents the crash. `CX2SourceVoiceXMA::XMAVoiceInitialize` will get an error, `Initialize` will fail, `CreateSourceVoice` will return an error HRESULT, and the Milo `Voice::Init` assert will fire but won't be fatal in the debug build.

### Medium-term (Xenia enhancement):

Add a `--dc3_disable_xma_hal` cvar that patches all XMAHAL guest functions at load time. This is the cleanest solution and can be added to the existing DC3 hack pack infrastructure.

### Long-term (proper fix):

Implement proper XMAHAL support in Xenia by routing `XMAHALAllocateContexts` through Xenia's existing `XmaDecoder::AllocateContext()` infrastructure. The XMAHAL functions are just a higher-level wrapper around the same XMA context system that Xenia already supports. The challenge is that the HAL path uses MMIO register writes instead of kernel API calls, and the statically-linked XDK code has hardcoded expectations about memory layout.

## Architecture Diagram

```
DC3 Song Audio Path (WORKS):
  HamAudio::Load() -> FileLoader(".mogg") -> Synth::NewBufStream()
    -> StandardStream -> VorbisReader (Ogg/Vorbis decode)
    -> StreamReceiver (PCM output)
    [No XMA involvement -- this path works fine]

DC3 SFX Sample Path (CRASHES):
  SynthSample360::NewInst() -> SampleInst360 -> Voice(isXMA=true)
    -> Voice::Init() -> createOrReuse()
    -> CX2Engine::CreateSourceVoice(format=0x166)
    -> CX2SourceVoiceXMA::Initialize()
    -> XMAVoiceInitialize()
    -> XMAHALAllocateContexts()  <-- SIGSEGV
    -> [XMACreateContext + MmMapIoSpace + MMIO writes]

Xenia XMA Kernel Path (WORKS for other games):
  Game calls XMACreateContext() directly (kernel export)
    -> XmaDecoder::AllocateContext()
    -> XmaContext setup
    -> FFmpeg decode in worker thread
    [Works because no HAL/MMIO involved]
```

## Key Files

**Xenia:**
- `/home/free/code/milohax/xenia/src/xenia/kernel/xboxkrnl/xboxkrnl_audio_xma.cc` -- XMA kernel exports (all 18 implemented)
- `/home/free/code/milohax/xenia/src/xenia/kernel/xboxkrnl/xboxkrnl_audio.cc` -- XAudio render driver exports + nop handling
- `/home/free/code/milohax/xenia/src/xenia/kernel/xboxkrnl/xboxkrnl_memory.cc` -- `MmMapIoSpace` (passthrough stub)
- `/home/free/code/milohax/xenia/src/xenia/apu/xma_decoder.cc` -- XMA decode worker thread (FFmpeg)
- `/home/free/code/milohax/xenia/src/xenia/apu/nop/nop_audio_system.cc` -- NOP audio backend
- `/home/free/code/milohax/xenia/src/xenia/emulator.cc` -- DC3 Patch 8 (audio bypass hack, line ~2944)
- `/home/free/code/milohax/xenia/src/xenia/app/xenia_main.cc` -- `--apu` cvar definition

**DC3 Decomp:**
- `/home/free/code/milohax/dc3-decomp/src/system/hamobj/HamAudio.cpp` -- Song audio loading/playback
- `/home/free/code/milohax/dc3-decomp/src/system/synth_xbox/Voice.cpp` -- XAudio2 voice management (calls CreateSourceVoice)
- `/home/free/code/milohax/dc3-decomp/src/system/synth_xbox/SynthSample.cpp` -- Sample data (XMA vs PCM)
- `/home/free/code/milohax/dc3-decomp/src/system/synth_xbox/SampleInst360.cpp` -- Creates Voice with isXMA flag
- `/home/free/code/milohax/dc3-decomp/src/system/synth/StandardStream.cpp` -- Vorbis stream (IsReady/Fail)
- `/home/free/code/milohax/dc3-decomp/src/system/synth/Synth.cpp` -- Synth subsystem init, NewBufStream

**DC3 Binary Symbols (in-binary XDK functions):**
- `XMAHALCreate` = `0x82E77EF0` (size `0x2E8`) -- HAL subsystem init
- `XMAHALAllocateContexts` = `0x82E77250` (size `0x110`) -- Context alloc (crash site)
- `CX2SourceVoiceXMA::Initialize` = `0x82E75A90` (size `0x13C`)
- `CX2SourceVoiceXMA::XMAVoiceInitialize` = `0x82E75898` (size `0x1F8`)
- `CX2Engine::CreateSourceVoice` = `0x82E65650` (size `0x21C`)
