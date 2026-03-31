# HamAudio Fail Investigation for Xenia

## Executive Summary

On the original Xbox 360 binary, `Synth::NewBufStream()` creates a **`StreamNull`**, not a `StandardStream`. `StreamNull::IsReady()` always returns `true` and `Stream::Fail()` always returns `false`. The actual XMA audio decoding happens at a lower level through XAudio2 hardware APIs, completely outside the `Stream` class hierarchy. This means on Xenia:

- `HamAudio::Fail()` will always return `false` (StreamNull never fails)
- `HamAudio::IsReady()` should return `true` once `FinishLoad()` runs

**The real blocker is not audio failure but the loading pipeline**: if the `FileLoader` for the `.mogg` file never completes (because the `LoadMgr` isn't polling it, or because Xenia's file I/O for the ark is broken), then `mSongStream` stays null and `IsReady()` returns `false` forever.

---

## 1. How HamAudio::Fail() Works

**Source**: `src/system/hamobj/HamAudio.cpp:140`
```cpp
bool HamAudio::Fail() { return mSongStream && mSongStream->Fail(); }
```

**XEX address**: `0x825291F0`

The fail chain is:
1. `HamAudio::Fail()` calls `mSongStream->Fail()` (virtual dispatch)
2. `Stream::Fail()` (base class) returns `false` by default
3. `StreamNull` does NOT override `Fail()`, so it inherits the base `return false`
4. `StandardStream::Fail()` returns `mRdr && mRdr->Fail()` (only used on HX_NATIVE)
5. `VorbisReader::Fail()` returns `mFail` member (set on file I/O errors or corrupt ogg)

**On the original Xbox 360 binary**: `mSongStream` is a `StreamNull` (confirmed via Ghidra decompilation of `Synth::NewBufStream` at the merged address). `StreamNull::Fail()` is never overridden, so `Fail()` always returns `false`.

**There is no member `mFailed` on HamAudio itself.** The fail state is entirely delegated to the Stream object.

---

## 2. How HamAudio::IsReady() Works

**Source**: `src/system/hamobj/HamAudio.cpp:65-83`
**XEX address**: `0x8252B9E0` (confirmed via Ghidra symbol lookup)

```cpp
bool HamAudio::IsReady() {
    if (!mSongStream && !mRawBuffer) {
        if (mFileLoader && mFileLoader->IsLoaded()) {
            FinishLoad();
        } else {
            return false;   // <-- THE BLOCKING POINT
        }
    }
    mReady = mSongStream && mSongStream->IsReady();
    return mReady;
}
```

State machine for IsReady:
1. **Before Load()**: `mSongStream=null`, `mRawBuffer=null`, `mFileLoader=null` --> returns `false` (early return)
2. **After Load() (mogg path)**: `mFileLoader` is set, loading in progress --> returns `false` until loaded
3. **FileLoader done**: `FinishLoad()` runs, creates `StreamNull`, sets `mSongStream` --> `StreamNull::IsReady()` returns `true`
4. **After FinishLoad**: `mReady = true`, returns `true`

**Key insight**: On the original binary, once `FinishLoad()` runs, `StreamNull::IsReady()` always returns `true`. So `IsReady()` will succeed as soon as the mogg file is loaded from the ark.

---

## 3. The Audio Loading Chain

```
Game::LoadSong()
  --> HamMaster::Load(songInfo, false)
    --> mSongData->Load(songInfo, ...)    // MIDI loading
    --> creates HamMasterLoader, adds to TheLoadMgr

  [TheLoadMgr polls...]

  HamMasterLoader::PollLoading()
    --> HamMaster::LoaderPoll()
      --> mSongData->Poll()               // waits for MIDI to finish
      --> mAudio->Load(mSongInfo, false)   // creates FileLoader for .mogg
      --> mLoaded = true

  [TheLoadMgr polls the FileLoader...]

  HamAudio::IsReady() [called from Game::HandleWait()]
    --> mFileLoader->IsLoaded()?
    --> FinishLoad()
      --> mRawBuffer = mFileLoader->GetBuffer()
      --> TheSynth->NewBufStream(mRawBuffer, ...)
          // On Xbox 360: creates StreamNull (timing only)
          // On native: creates StandardStream + VorbisReader
      --> mSongStream = stream
      --> sets up faders, channel pans, volumes
    --> mSongStream->IsReady()
          // StreamNull: always true
          // StandardStream: true when kReady/kPlaying/kStopped
```

**XMA never enters the Stream class hierarchy.** On Xbox 360, the real XMA decoding is handled entirely by XAudio2 hardware (through `Synth360`, `XAudioGetSpeakerConfig`, etc.). The `StreamNull` provides song timing (`GetTime()` uses `VarTimer`) but produces no audio data.

---

## 4. Why IsReady() Returns False on Xenia

There are two possible reasons:

### 4a. FileLoader never completes
The `FileLoader` for the `.mogg` file requires:
1. `FileLoader::OpenFile()` -- opens the file from the archive
2. `File::ReadAsync()` -- reads the entire mogg buffer
3. `FileLoader::LoadFile()` -- waits for `ReadDone()`
4. Transitions to `DoneLoading` state

If Xenia's ark/STFS file system doesn't properly handle the file I/O for mogg files, `FileLoader` stays in the `OpenFile` or `LoadFile` state and `IsLoaded()` never returns true.

### 4b. HamMaster::LoaderPoll() never reaches mAudio->Load()
`HamMasterLoader` is in `TheLoadMgr`'s queue. It needs to be polled. `LoaderPoll()` first waits for `mSongData->Poll()` (MIDI reading) to return true. If MIDI reading is stuck (file not found, corrupt, etc.), `mAudio->Load()` is never called, so `mFileLoader` stays null.

### 4c. Diagnosis approach
Check which of these is true by examining:
- Is `mFileLoader` null (0x30 in HamAudio = never called `Load()`)
- Or is `mFileLoader` non-null but `IsLoaded()` returns false (= file I/O stuck)
- Or is `mSongStream` non-null but `IsReady()` returns false (= StandardStream stuck, should not happen on original binary)

---

## 5. Member Offsets and XEX Addresses

### HamAudio member offsets (absolute from object start)
| Offset | Member | Type |
|--------|--------|------|
| 0x30 | mFileLoader | FileLoader* |
| 0x34 | mRawBuffer | char* |
| 0x38 | mRawBufferSize | int |
| 0x3C | mSongInfo | SongInfo* |
| 0x40 | mSongStream | Stream* |
| 0x44 | mStreams[0] | Stream* |
| 0x48 | mStreams[1] | Stream* |
| 0x4C | mReady | bool |

### Game member offsets (absolute from object start)
| Offset | Member | Type |
|--------|--------|------|
| 0x50 | mMaster | HamMaster* |
| 0x54 | mGameInput | GameInput* |
| 0x5E | mPaused | bool |
| 0x5F | mTimePaused | bool |
| 0x60 | mRealTime | bool |
| 0x62 | mHasIntro | bool |
| 0xA4 | mWaitState | int |

### Key XEX addresses (original debug XEX)
| Address | Function |
|---------|----------|
| 0x825291F0 | HamAudio::Fail() |
| 0x8252B9E0 | HamAudio::IsReady() |
| 0x8252B430 | HamAudio::FinishLoad() |
| 0x82867288 | Game::HandleWait() |
| 0x82865128 | Game::PostWaitStart() |
| 0x8276F1E0 | StandardStream::Fail() |

### Synth::NewBufStream (merged/ICF address)
Both `Synth::NewBufStream` and `Synth::NewStream` resolve to the same function (ICF merged). This function creates a `StreamNull` regardless of input. Address is the same for both Synth base and Synth360 override.

---

## 6. Can We Trigger Fail() by Stubbing Upstream?

**No, and we don't need to.** Here's why:

### The original binary's audio path never fails
On the original Xbox 360 binary, `StreamNull::Fail()` inherits `Stream::Fail()` which returns `false`. There is no code path in the original binary that can make `HamAudio::Fail()` return `true`. The XMA audio either works (via XAudio2 hardware) or it doesn't, but the `Stream` class doesn't know or care.

### Making Fail() return true would not help anyway
Looking at the original binary's `HandleWait()` (Ghidra at 0x82867288):
```
if (audio->Fail()) return true;   // "wait handled, done"
```
And `PostWaitStart()` (Ghidra at 0x82865128):
```
if (!audio->Fail()) {
    // Play(), unpause, start timer
}
// If Fail()==true: function returns without unpausing
```

**If `Fail()` returns true, `HandleWait` returns `true` (wait done), but `PostWaitStart` skips the unpause.** Result: `mPaused` stays `true`, `mRealTime` stays `false`, game is frozen. This is the original game's intended behavior -- if audio fails, the game is stuck.

### The native port's solution is the right one
The native port adds an `else` branch to `PostWaitStart`:
```cpp
else {  // audio failed
    mPaused = false;
    MetaPerformer::Current()->StartGameplayTimer();
    mRealTime = true;
    mGameInput->SetTimeOffset();
}
```
This is the only path that works: acknowledge audio failure but use wall-clock timing instead.

---

## 7. Concrete Patch Plan for Xenia

### Option A: Fix the loading pipeline (root cause)
The most likely issue is that the mogg `FileLoader` never completes because Xenia's STFS/ark file I/O is broken for that file type, or `TheLoadMgr` isn't polling it.

**Diagnostic steps**:
1. Add a host-side interceptor at `HamAudio::IsReady` (0x8252B9E0) that logs:
   - `mFileLoader` (r3+0x30): null means `Load()` was never called
   - `mSongStream` (r3+0x40): null means `FinishLoad()` never ran
   - `mRawBuffer` (r3+0x34): null means file not loaded yet
2. If `mFileLoader` is null, the blocker is in `HamMaster::LoaderPoll()` / `HamSongData::Poll()`
3. If `mFileLoader` is non-null, add interceptor at `FileLoader::PollLoading` to check its state

### Option B: Replicate native port's wall-clock fallback via patches
If fixing the loading pipeline is too hard, we can patch the original binary to replicate what the native port does.

**Required patches (on the original debug XEX)**:

#### Patch 1: Make HandleWait fall through on Fail() (already needed)
At `Game::HandleWait` (0x82867288), the original code does:
```
if (audio->Fail()) return true;
```
Change this to fall through to the dispatch (PostWaitStart, etc.) instead of returning early. The exact instruction is at approximately 0x82867314-0x82867318 (the `beq` after `Fail()`'s clrlwi+cmpwi). NOP the branch so execution falls through to the IsReady check and then to the dispatch.

But since `Fail()` returns false on the original binary (StreamNull), **this patch alone doesn't help**. The real issue is `IsReady()` returning false.

#### Patch 2: Make IsReady() return true (force skip loading)
Patch `HamAudio::IsReady` (0x8252B9E0) to always return `true`:
```
li r3, 1    # 38600001
blr         # 4E800020
```
This bypasses the loading pipeline entirely. `FinishLoad()` will never be called, so `mSongStream` will be null. But `PostWaitStart` will try to call `Play()` which asserts `mSongStream != null`.

#### Patch 3: Skip Play() in PostWaitStart (guard the assert)
At `PostWaitStart` (0x82865128), after the `Fail()` check passes:
- The call to `Play()` at `_Play_HamAudio__QAAXXZ` will crash because `mSongStream` is null
- Patch: NOP the call to `Play()` and let `mPaused=false` still happen
- Also set `mRealTime=true` (byte at Game+0x60) so `CurrentMs()` uses wall-clock timer
- Also trigger `SetTimeOffset()` call

This is complex. A simpler approach:

#### Patch 2 (Alternative): Stub HamAudio::IsReady AND Fail
Instead of returning true from `IsReady()`, make `Fail()` return true:

**Patch HamAudio::Fail (0x825291F0)**:
```
li r3, 1    # 38600001
blr         # 4E800020
```

**Then patch HandleWait to fall through on Fail (instead of returning true)**:
The branch at the `Fail()` result check (approximately 0x82867318) should be NOPped so execution continues to the dispatch.

**Then patch PostWaitStart (0x82865128) to handle Fail gracefully**:
The original PostWaitStart checks `!Fail()` and only unpauses if audio is OK. We need to add the else branch. This is hard to do by binary patching alone because there's no space for new code.

#### Option C: Direct memory writes (simplest)
Instead of patching code, write directly to Game object memory after HandleWait detects the stall:

1. **Intercept HandleWait** via a JIT override at 0x82867288
2. In the host handler:
   - Read `this->mWaitState` (offset 0xA4): if non-zero, check `this->mMaster->mAudio->mSongStream`
   - If `mSongStream` is null after N frames, force:
     - `this->mPaused = false` (offset 0x5E = 0)
     - `this->mRealTime = true` (offset 0x60 = 1)
     - `this->mWaitState = 0` (offset 0xA4 = 0)
   - Call the original HandleWait and override its return value to `true`

This replicates what the native port does without modifying guest code.

### Recommended approach: Option C

The cleanest solution is a **host-side JIT override for `Game::HandleWait`** that detects the audio stall and performs the same recovery as the native port's `PostWaitStart` else-branch:

```cpp
// In dc3_hack_pack.cc, register a JIT override for HandleWait (0x82867288):
void Dc3HandleWaitOverride(PPCContext* ctx, KernelState* ks) {
    auto* mem = ks->memory()->virtual_membase();
    uint32_t game = ctx->r[3];  // 'this' pointer

    // Read mWaitState (offset 0xA4)
    int wait_state = xe::load_and_swap<int32_t>(mem + game + 0xA4);
    if (wait_state == 0) {
        ctx->r[3] = 1;  // return true
        return;
    }

    // Read mMaster (offset 0x50), then mAudio (+0x34 in HamMaster)
    uint32_t master = xe::load_and_swap<uint32_t>(mem + game + 0x50);
    uint32_t audio = xe::load_and_swap<uint32_t>(mem + master + 0x34);

    // Read mSongStream (offset 0x40 in HamAudio)
    uint32_t song_stream = xe::load_and_swap<uint32_t>(mem + audio + 0x40);
    // Read mFileLoader (offset 0x30 in HamAudio)
    uint32_t file_loader = xe::load_and_swap<uint32_t>(mem + audio + 0x30);

    // Stall detection: if no songStream and no fileLoader, audio never loaded
    static int stall_count = 0;
    bool audio_stalled = (song_stream == 0 && file_loader == 0);

    if (audio_stalled) {
        stall_count++;
        if (stall_count > 120) {  // ~2 seconds at 60fps
            // Force recovery: unpause, set wall-clock timing, clear wait
            mem[game + 0x5E] = 0;  // mPaused = false
            mem[game + 0x60] = 1;  // mRealTime = true
            xe::store_and_swap<int32_t>(mem + game + 0xA4, 0);  // mWaitState = 0
            // Call MetaPerformer::Current()->StartGameplayTimer()
            // (may need to invoke guest code for this)
            stall_count = 0;
            ctx->r[3] = 1;  // return true (wait done)
            return;
        }
    } else {
        stall_count = 0;
    }

    // Fall through to original HandleWait
    // (call original guest code)
}
```

The key addresses for this patch:
- `Game::HandleWait`: **0x82867288**
- `Game::PostWaitStart`: **0x82865128**
- `HamAudio::Fail`: **0x825291F0**
- `HamAudio::IsReady`: **0x8252B9E0**

The key memory writes:
- `Game+0x5E`: `mPaused = false` (write 0x00)
- `Game+0x60`: `mRealTime = true` (write 0x01)
- `Game+0xA4`: `mWaitState = 0` (write big-endian int32 0x00000000)

---

## Appendix: Why the StreamNull Path Matters

The original Xbox 360 game uses a dual architecture for audio:

1. **Stream class hierarchy** (`Stream` / `StreamNull` / `StandardStream`): Provides timing, faders, markers, jumps, and crossfade logic. On Xbox 360, song streams are `StreamNull` which only provides a `VarTimer` for timing.

2. **XAudio2 hardware pipeline** (`Synth360`, XAudio2 API calls): Handles actual PCM/XMA decoding and audio output. This is entirely outside the decomp's Stream classes.

When Xenia can't process XMA audio (because XAudio2 emulation is incomplete), the `StreamNull` still works fine -- it just provides timing without sound. The game should theoretically work silently. The actual blocker is somewhere in the loading pipeline, not in audio failure detection.

## Appendix: Native Port's Complete Audio Failure Handling

The native port handles audio failure at multiple points:

1. **HandleWait** (Game.cpp:969): Falls through on `Fail()` instead of returning true
2. **PostWaitStart** (Game.cpp:351-366): `else` branch sets `mRealTime=true`, `mPaused=false`, calls `SetTimeOffset()`
3. **CurrentMs** (LiveInput.cpp:35): When `mRealTime=true`, uses `mTimer.Ms() + mTimeOffset` (wall-clock) instead of `mAudio.GetTime()` (stream time)
4. **VorbisReader** (VorbisReader.cpp:189): Sets `mFail=true` on persistent ogg page sync failure (corrupt mogg decryption)

On native, the `VorbisReader` actually fails because mogg files require decryption keys. This triggers the fail chain: `VorbisReader::mFail` --> `StandardStream::Fail()` --> `HamAudio::Fail()` --> `Game::HandleWait` falls through --> `PostWaitStart` uses wall-clock timing.

On the original Xbox 360 binary, none of this fail detection exists because `StreamNull` is used.
