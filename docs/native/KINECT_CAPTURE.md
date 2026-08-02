# Capturing real Kinect skeleton ground truth from an Xbox 360

**Goal:** record the *actual* Kinect v1 skeleton stream that Dance Central 3 sees, in sync with
video of the same performance, so we can validate the native port's MediaPipe-based pose
pipeline against hardware ground truth.

DC3 already contains a complete skeleton recorder — `SkeletonClip`
(`src/system/gesture/SkeletonClip.cpp`). It serializes every polled `SkeletonFrame` to a
`.clp` file: 20 joints × (position + tracking state), floor plane, quality flags, tracking ID
and the song time of each frame. We recovered the exact on-disk format; the parser is
[`tools/pose_corpus/parse_skeleton_clip.py`](../../tools/pose_corpus/parse_skeleton_clip.py)
(run `--selftest` to verify it).

> **Read this first:** the recorder is **not reachable on a retail disc build**. See
> [Trigger path](#1-trigger-path-what-actually-turns-recording-on) for the evidence and
> [Getting recording working](#2-getting-recording-working-on-your-console) for the ladder of
> options. Budget an evening for the console-side setup before the first real capture session.

---

## 0. What a `.clp` contains

| Field | Notes |
| --- | --- |
| header | version (always 8), record date/time, song shortname, difficulty, build string, frame count |
| per frame | NUI frame number, ms since previous NUI frame, floor normal + floor clip plane |
| per frame | `is_tracked`, 20 × (x, y, z in metres) + 20 × tracking state (0 not-tracked / 1 inferred / 2 tracked) |
| per frame | quality flags (clipped-edge bits), NUI tracking ID |
| per frame | **`song_seconds`** — `MoveDir::SongSeconds()` at capture time. This is the sync key. |

Joint order (`enum SkeletonJoint`, `src/system/gesture/BaseSkeleton.h`):

```
0 hip_center      5 elbow_left      10 wrist_right    15 hip_right
1 spine           6 wrist_left      11 hand_right     16 knee_right
2 shoulder_center 7 hand_left       12 hip_left       17 ankle_right
3 head            8 shoulder_right  13 knee_left      18 foot_left
4 shoulder_left   9 elbow_right     14 ankle_left     19 foot_right
```

Everything is **little-endian** (yes, on a big-endian console — both the writer and the reader
open the `FileStream` with `lilEndian = true`). Full byte-level spec is the docstring of
`parse_skeleton_clip.py`.

Two things that matter for capture planning:

* Only **one** skeleton is recorded per clip — the active player's
  (`TheGestureMgr->GetActiveSkeletonIndex()`). **Play solo.**
* `PollRecording` **drops any frame whose `song_seconds` has not advanced**. So nothing is
  recorded outside of an actively-playing song: menus, pauses and the pre-song countdown
  contribute no frames. Your sync event must happen *during* the song.
* Capacity is 18 000 frames (`ReserveFrames()`), i.e. ~10 minutes at 30 fps. One song is fine.

---

## 1. Trigger path: what actually turns recording on

Chain, all verified against the decompiled (byte-matching) source:

1. **`toggle_song_record`** — a `DataFunc` registered in `GameInit()`
   (`src/lazer/game/Game.cpp:1224`). It calls `ReserveFrames()` and flips the static
   `MoveDir::sGameRecord`.
2. It is bound to a **keyboard cheat** in the shipped data:
   `orig-assets/extracted/config/cheats.dta`, in the `(keyboard …)` section —

   ```
   (r alt "Game: Toggle Song Recording" (filters game)
      {cheat_display show_bool "Song recording" {toggle_song_record}})
   (R alt "Game: Toggle Song Recording for both players" …{toggle_song_record_double})
   ```

   `(filters game)` is **not** an execution gate — `CheatsManager::CallCheatScript` only scans
   `filters` for the symbol `safe`; mode gating uses a `(modes …)` array, which this entry does
   not have. So `Alt+R` fires from any screen. Keyboard input comes from
   `XInputGetKeystroke` (`src/system/os/Keyboard_Xbox.cpp`), polled every frame from
   `SystemPoll` — a plain USB keyboard in the console.
3. When a song starts, `MoveDir::ResetDetection()` / `ResetDetectFrames()` call
   `MoveDir::SetupSongRecordClip()`, which — **if `sGameRecord`** — calls `SetupRecordClip()`:

   ```cpp
   clip->SetName(clipName + ".clp", dir);
   const char *path = MakeString("devkit:\\%s", clip->Name());
   clip->StartXboxRecording(path);
   ```

   Clip name is `RecordClipName()`:
   `<datecode>~<song>~<difficulty-char>~<dancer>~<mode>.clp`, truncated to 38 chars.
   `<mode>` is `pi` (normal play), `bid` (practice) or `ktb` (rhythm battle).
4. Every camera frame, `MoveDir::PostUpdate()` calls `SkeletonClip::PollRecording(frame)`.
5. At song end, `GamePanel::OnMsg(EndGameMsg)` → `Game::ClearState()` →
   `MoveDir::FinishGameRecord()` → `StopRecording()` → `StopRecordingNoClear()` opens
   `FileStream(mFile, kWrite, /*lilEndian*/ true)` and `WriteClip()`s it.

### Verdict

**Config-only triggering is NOT possible on a retail build. A patched/alternate executable is required.**

Two independent blockers, both confirmed:

**(a) The retail executable does not contain the trigger.** We decrypted the retail XEX
(`orig-assets/default.xex`, title id `373307D9`, retail-signed, AES + "basic" block
compression) with `xex1tool` and diffed its string pool against the debug build:

| Marker | Retail | Debug |
| --- | --- | --- |
| `toggle_song_record`, `toggle_song_record_double` | **absent** | present |
| `skeleton_clip_remap_paths` | **absent** | present |
| the substring `devkit` (anywhere), `.clp` | **absent (0 hits)** | present (7 / 2) |
| `SkeletonClip` (class factory name), RTTI `.?AVSkeletonClip@@` | present | present |
| `xbox_start_record`, `xbox_load_frames`, `is_recording`, `start_recording`, `stop_recording` | present | present |
| `swap_move_record`, `flush_move_record` | present | present |

This diff is meaningful because we measured the control: retail strips ~99 % of
`MILO_LOG`/`MILO_NOTIFY`/`MILO_FAIL` strings but retains **85 %** of `DataRegisterFunc` name
literals and **93 %** of message-handler names. `toggle_song_record` is a `DataRegisterFunc`
literal, so its absence is a real `#ifdef`, not string stripping. Likewise `devkit` appears in
`MakeString` arguments, not debug macros, and is completely gone.

Conversely the `SkeletonClip` *class* and its `xbox_start_record` / `stop_recording` message
handlers **are linked into retail** (verified by disassembling the `static Symbol` init guards
that reference those string addresses). So the machinery survives; only the DTA entry points
and the devkit file paths were compiled out.

**(b) `devkit:\` is a development-kernel drive.** The title does not create it with an
`ObCreateSymbolicLink` of its own; it comes from the dev kernel / dev HDD partition. On stock
retail hardware `CreateFile("devkit:\…")` fails and `StopRecordingNoClear` hits
`MILO_FAIL("Recording failed; could not open output file (%s).")` — which, note, is **fatal**
(`Debug::Modal` ends in `Exit(1, true)`, `src/system/os/Debug.cpp:439-444`), so a failed open
kills the title at the end of the song and you lose the take.

> **Correction (was wrong in an earlier revision of this doc).** This section used to claim
> `\??\game:` and `\??\cache:` "are created by the NUI speech code". What the binary actually
> shows is weaker: the literals `\??\game:` (`0x821BB168`), `\??\cache:` (`0x821BB148`) and
> `\Device\Harddisk0\` (`0x821BB154`) all belong to **`nuispeech:datacollection.obj`**
> (`orig/373307D9/ham_xbox_r.map`) — the XDK speech *voice-data-collection* object, which only
> runs when collection is enabled. They are not a boot-time guarantee from DC3.
>
> The title *does*, however, call **`DmMapDevkitDrive()`** unconditionally during startup, from
> `Locale.cpp:179` (alternate-locale probe; also `HolmesUtl.cpp:8`, `BinkMovieImpl_Xbox.cpp:29`).
> It is one of the 7 xbdm imports. So on a console whose xbdm can satisfy it, `devkit:\` is
> mapped for free with **no patch at all** — which is why "try it unpatched first" is rung 0 of
> the ladder below.

One more consequence worth knowing: editing `config/cheats.dta` in the ark **does nothing**.
`CachedDataFile()` (`src/system/obj/DataFile.cpp:584`) redirects any `.dta` request to
`<path>/gen/<base>.dtb` whenever `UsingCD()` is true, and `cheats.dta` is `#include`d into
`config/ham_keep.dta`, so the live data is the pre-compiled `config/gen/ham_keep.dtb`.

---

## 2. Getting recording working on your console

Three routes, cheapest first. **Route A is strongly recommended.**

### Route A — run the *debug* executable (preferred)

We already have the debug build: `orig-assets/debug.xex` (16,887,808 bytes, byte-identical to
`orig-assets/default_debug.xex` and to `orig/373307D9/default.xex`). It is dev-signed,
uncompressed and unencrypted, contains the full recorder, the cheat system and the `Alt+R`
binding, and its build date (map: 2012-09-15) is essentially the same milestone as retail
(2012-09-16), so the retail disc's ark should be compatible.

RGH/JTAG consoles run dev-signed XEXs. Caveat: `debug.xex` imports **`xbdm.xex`**, so the
console must have XBDM available (standard on most RGH setups — enable it in `launch.ini` /
install the xbdm plugin).

Steps:

1. Copy the game to HDD or USB as an **extracted folder** (not an ISO / not from disc) — this
   is what makes the game folder writable. Put `debug.xex` next to the ark files as
   `default.xex` (keep the original retail `default.xex` under another name so you can switch
   back).
2. **Patch the two `devkit:` string literals** so the clip lands somewhere writable. The debug
   XEX is a plain image, so this is a two-string byte edit — no re-signing logic, no code
   changes:

   | File offset in `debug.xex` | Current bytes | Replace with | Slot size |
   | --- | --- | --- | --- |
   | `0x00058AEC` | `64 65 76 6B 69 74 3A 5C 25 73 00` (`devkit:\%s\0`) | `64 3A 5C 25 73 00` (`d:\%s\0`), zero-pad to 12 | 12 bytes (max 11 chars) |
   | `0x00254EAC` | `64 65 76 6B 69 74 3A 5C 25 73 2E 63 6C 70 00` (`devkit:\%s.clp\0`) | `64 3A 5C 25 73 2E 63 6C 70 00` (`d:\%s.clp\0`), zero-pad to 20 | 20 bytes |

   (`.rdata` VAs `0x82055AEC` and `0x82251EAC`; file offset = VA − `0x82000000` + `0x3000`.
   Slot sizes confirmed against `orig/373307D9/ham_xbox_r.map`: the next symbol after
   `0x82055AEC` is at `0x82055AF8`, and `0x82251EAC` is followed by C++ EH data at
   `0x82251EC0`.) The first literal is `/GF`-pooled and also used by
   `FreestyleMoveRecorder`'s debug dumps — harmless collateral. Only the **first** literal
   matters for song capture (it is the one `SetupRecordClip` uses, `MoveDir.cpp:1021`); the
   second is only used by `SkeletonClip::FlushMoveRecord` (the manual rhythm-battle flush) and
   is optional.

   > **⚠ Do NOT patch these to `GAME:\` — it is fatal.**
   >
   > An earlier revision of this doc told you to write `GAME:\%s`. **That patch hard-kills the
   > title at the end of the song, after you have already played it.** Evidence, from the
   > shipped debug image (not just source):
   >
   > * `FileIsLocal` (`src/system/os/File_Win.cpp:9-13`) is
   >   `MILO_ASSERT(!strieq(drive, "game"), 0x24)`. `File_Win.obj` **is** the object linked into
   >   the Xbox 360 image — `orig/373307D9/ham_xbox_r.map` lists
   >   `??_C@_0BH@JLBBPNDG@?$CBstrieq?$CIdrive?0?5?$CCgame?$CC?$CJ?$AA@` (the assert-expression
   >   string) at `.rdata:0x820807C4` owned by `os:File_Win.obj`. `?FileIsLocal@@YA_NPBD@Z` is at
   >   `.text:0x825EEEB8` and our decomp matches it **100 %**; its target listing is literally
   >   `bl FileGetDrive` → `bl stricmp` (against `"game"`) → `bl Debug::Fail`. So the check is
   >   live in this build and **case-insensitive** — `GAME:`, `Game:` and `game:` all trip it.
   > * The clip write **does** go through it: `StopRecordingNoClear`
   >   (`SkeletonClip.cpp:593`, decomp 100 % vs `0x82DF2FA0`) builds
   >   `FileStream(mFile, kWrite, true)` → `NewFile(file, 0x301)` (`File.cpp:597`), and `NewFile`
   >   calls `FileIsLocal(iFilename)` **unconditionally**, for writes as well as reads
   >   (`File.cpp:610`). There is no lower-level bypass.
   > * A tripped `MILO_ASSERT` is not survivable here: `Debug::Fail` → `Debug::Modal(kModalFail…)`
   >   → `Exit(1, true)` (`Debug.cpp:439-444`). No "Continue".
   > * Worst of all, the assert fires at `EndGameMsg`, i.e. **after** the whole song — so the
   >   take is lost.
   >
   > `d:\` is the correct spelling of the *same directory*: `FileQualifiedFilename`
   > (`File_Win.cpp:53-57`) uses `UsingCD() ? "d:" : HolmesFileShare()` as the console root, and
   > the boot path proves `d:` resolves — `CheckForArchive` (`System.cpp:78-85`) decides
   > `UsingCD()` by `FileGetStat("gen/main_xbox.hdr")`, which qualifies to
   > `d:\gen\main_xbox.hdr` before `GetFileAttributesExA`. `stricmp("d","game") != 0`, so the
   > assert passes.

   If your console *does* have a devkit partition / dev kernel, skip the patch entirely and
   just make sure `devkit:\` exists — try it unpatched first (the title already calls
   `DmMapDevkitDrive()` at startup from `Locale.cpp:179`).

   **Before you burn a song on any of this, probe the drive from the RndConsole** — see
   [the zero-cost drive probe](#drive-spelling-zero-cost-probe) and the
   [ranked drive ladder](#drive-spelling-ranked-ladder).
3. Arm the recorder. **No keyboard is required** — hold **LT + LB** and click the **left
   stick (L3)** to open the on-screen cheat menu, then select
   *"Game: Toggle Song Recording"* with **A**. (If you do have a USB keyboard, `Alt`+`R`
   works too.) Full evidence and the other routes are in
   [§2b Triggering the recording on a console](#2b-triggering-the-recording-on-a-console-no-pc-keyboard).
4. Either way you should see an on-screen `Song recording   TRUE` confirmation (that's the
   `cheat_display show_bool` in the cheat). Arm it *before* starting the song — `sGameRecord`
   is read when the song is set up.
5. Play the song to the end (or fail out) — the file is written on `EndGameMsg`. **Do not
   dashboard out mid-song**, nothing is flushed until then.
6. Retrieve the `.clp` from the game folder (what the title calls `d:\`, what FTP calls e.g.
   `/Hdd1/Games/DanceCentral3`) over FTP (FSD / DashLaunch) or by pulling the USB stick.

### Drive spelling: zero-cost probe

Do this **before** committing to a byte patch or playing a song. It exercises the exact same
code path the recorder uses (`StartXboxRecording` → `StopRecordingNoClear` → `FileStream(kWrite)`
→ `NewFile` → `FileIsLocal` → `AsyncFileWin::_OpenAsync`), takes seconds, and can be repeated for
each candidate drive spelling. `SkeletonClip` exposes the handlers directly
(`SkeletonClip.cpp:285-295`):

```
{new SkeletonClip $probe}
{$probe xbox_start_record "d:\probe.clp"}
{$probe stop_recording}
```

Run it from the on-screen RndConsole (`Esc` with a USB keyboard, or the pad route) or via the
loose-`.dta` bootstrap in [CONSOLE_DTA_EVAL.md](CONSOLE_DTA_EVAL.md).

* **Pass** — no assert, and a small `probe.clp` appears in the game folder over FTP.
* **Fail, illegal drive** — red assert screen reading `File_Win.cpp:36 !strieq(drive, "game")`,
  then the title exits. You used a `game:` spelling.
* **Fail, unwritable/unmapped drive** — red screen reading
  `Recording failed; could not open output file (…)`, then the title exits. The drive name was
  legal but the open failed.

Both failures terminate the title, so expect to relaunch between rungs — but you lose seconds,
not a song.

### Drive spelling: ranked ladder

Every rung below passes the `FileIsLocal` assert. They are ordered by how well the shipped
binary backs them.

| # | String to patch in (slot 1 / slot 2) | Why | Residual risk |
| --- | --- | --- | --- |
| 0 | *(no patch)* `devkit:\%s` | The title calls `DmMapDevkitDrive()` at startup (`Locale.cpp:179`); if your xbdm satisfies it you need no patch at all | Most RGH setups have no devkit partition |
| 1 | `d:\%s` / `d:\%s.clp` | `d:` is DC3's own console root (`File_Win.cpp:57`) and the boot path proves it resolves to the game folder (`CheckForArchive` stats `d:\gen\main_xbox.hdr`) | Read-only when booting from disc — use an extracted folder |
| 2 | `%s` / `%s.clp` (bare relative, no drive) | Also proven at boot: `ArchiveInit` opens `gen/main_xbox.hdr` through `FileStream` → `NewFile` → `CreateFileA` with a *bare relative path*, so the XDK resolves relative paths against the launch directory. Shortest possible string, always fits either slot | Write goes through the CRT `_open` rather than `CreateFileA`; same underlying mapping, but not independently proven for writes |
| 3 | `cache:\%s` / `cache:\%s.clp` | Multi-character drive → `FileIsLocal` is **true**, which additionally rules out the `AsyncFile::New` Holmes branch (`AsyncFile.cpp:64`) | `cache:` is not referenced anywhere in DC3's own code; may not be mounted |
| 4 | `Hdd1:\Games\DanceCentral3\%s` (your literal install path) | Fully explicit, multi-character drive; matches the `--game-path` spelling CONSOLE_DTA_EVAL.md asks you to establish | 12-byte slot 1 cannot hold it — you would have to relocate the string or patch the `lis/addi` pair that loads it |

**If a Holmes/`--host` dev PC is connected**, note that rungs 1 and 2 (`strlen(drive) <= 1`) make
`FileIsLocal` false, and `AsyncFile::New` will then route the *write* to the Holmes host instead
of the console (`AsyncFile.cpp:64`, `UsingHolmes(1) && (mode & FILE_OPEN_WRITE) && !FileIsLocal`).
With no Holmes host, `gHolmesStream` is null and the branch is dead. Rung 3 is immune.

### Route B — DC3Enhanced: an RB3Enhanced-style runtime patch (fallback)

Only needed if the debug XEX cannot be made to boot on your console.

RB3Enhanced (`/home/free/code/milohax/RB3Enhanced`) is the template:

* Build: XDK `cl.exe` → `link.exe -dll -entry:_DllMainCRTStartup` → `imagexex.exe`
  → `RB3Enhanced.dll` (an XEX DLL, `baseaddr 0x84000000`, unencrypted).
* Injection: a separate `RB3ELoader.xex` run as a **DashLaunch plugin** (`plugin1 = Usb:\…`);
  at boot it finds the game and injects the DLL. `DllMain` then hijacks the `bl App::_ct`
  inside `main()` so its `StartupHook` runs before the game constructs itself.
* Hooking primitive: `HookFunction(origAddr, stub, newFn)` in `source/utilities.c` — saves the
  first instruction into an 8-byte trampoline and overwrites it with a `b` (±32 MB range
  checked); plus `POKE_32` / `POKE_BL` for single-instruction patches. Addresses live in a flat
  `include/ports_xbox360.h` table pinned to one executable hash.
* **The drive answer:** RB3E does not rely on anything being pre-mounted — at startup it calls
  `ObCreateSymbolicLink("\\??\\RB3HDD:", "\\Device\\Harddisk0\\Partition1")` (plus
  `RB3USB0..2: → \Device\Mass0..2`) in `source/xbox360_files.c`, then uses plain
  `CreateFile`/`WriteFile`. Its crash dumper writes to `GAME:\` and falls back to `RB3HDD:\`.
  Copy the *symlink* pattern, **not** the `GAME:\` spelling: RB3E can use `GAME:\` because it
  calls Win32 directly, whereas anything routed through DC3's own `NewFile`/`FileIsLocal`
  asserts on a `game` drive (see the warning in [Route A step 2](#route-a--run-the-debug-executable-preferred)).
  A DC3Enhanced that writes the clip itself with raw `CreateFile` is free to use `GAME:\`; one
  that only pokes `sGameRecord` and lets the game's own `WriteClip` run must use a non-`game`
  drive.
* It also has a UDP broadcast channel (`source/net_events.c`, port `0x524E`, `'RB3E'` magic) —
  but with a **255-byte per-packet cap**, and a `RecordedFrame` is ~440 bytes, so a live
  skeleton tee needs either two packets per frame or a private packet type. The underlying
  `net.c` socket helpers have no such limit.

Minimal DC3 hook (debug-XEX addresses from `config/373307D9/symbols.txt`; **these do not
transfer to retail** — the retail image must be decrypted and the addresses re-derived):

| Symbol | Address |
| --- | --- |
| `SkeletonClip::PollRecording(const SkeletonFrame&)` | `0x82DF5378` |
| `SkeletonClip::StartXboxRecording(const char*)` | `0x82DF27B0` |
| `SkeletonClip::StopRecordingNoClear()` | `0x82DF2FA0` |
| `SkeletonClip::WriteClip(FileStream&)` | `0x82DF2B30` |
| `MoveDir::SetupSongRecordClip()` | `0x824FF9B0` |
| `SetupRecordClip(...)` | `0x824FF2E0` |
| `MoveDir::PostUpdate(const SkeletonUpdateData*)` | `0x825051F0` |
| `MoveDir::FinishGameRecord()` | `0x824FF7B8` |
| `MoveDir::sGameRecord` (bool) | `.data 0x82F618C8` |
| `SkeletonFrame::Create(const NUI_SKELETON_FRAME&, int)` | `0x82435A18` |
| `GestureMgr::Poll()` | `0x82428F40` |
| `LiveCameraInput::PollTracking()` | `0x8242FF58` |

Two shapes:

* **Least code:** `POKE` `MoveDir::sGameRecord = 1`, symlink a writable drive, and patch the
  path string. The game's own `WriteClip` then produces a normal `.clp`.
* **Most robust on retail** (where `SetupRecordClip` and the `devkit:` paths are compiled out):
  hook `SkeletonFrame::Create` or `GestureMgr::Poll`, and write the `.clp` yourself — the
  format is fully specified in `parse_skeleton_clip.py`, so a ~60-line emitter reproduces it
  byte-for-byte. This bypasses the game's recorder entirely and is version-robust.

Not verified: whether `RB3ELoader.xex` (not in this repo) can be pointed at title `373307D9`
without changes.

### Route C — standalone Kinect recorder homebrew

If routes A/B stall, a small homebrew XEX using `NuiSkeletonGetNextFrame` can record the same
data. Downside: it is not DC3's camera configuration (tilt, near/seated mode, play-space
prompts) and there is no `song_seconds`, so sync and comparability are weaker. Use only as a
last resort.

---

## 2b. Triggering the recording on a console (no PC keyboard)

Once you are booting `debug.xex` (Route A), `toggle_song_record` exists as a `DataFunc` and the
question is only how to *call* it from the couch. **You do not need a keyboard.**

### Option 1 (recommended) — the on-screen cheat menu, opened with a pad chord

> **Hold `LT` + `LB`, then click the left stick (`L3`).** An on-screen list of every cheat
> appears. Scroll to **`alt r  Game: Toggle Song Recording`**, press **`A`**. Press **`B`** to
> close the menu. Do this *before* starting the song.

DC3 has no `DebugMenu`/`RndDebugUI` class (that Milo-era construct is absent from `src/`
entirely), but it has something better: a real, scrollable, pad-navigable cheat browser that
can invoke **keyboard-section cheats**. The whole chain is confirmed against the shipped data:

* **The chord.** `src/system/utl/Cheats.cpp:308-309` —
  ```cpp
  bool leftShift  = (buttons & (1 << kPad_L1)) && (buttons & (1 << kPad_L2));
  bool rightShift = (buttons & (1 << kPad_R1)) && (buttons & (1 << kPad_R2));
  ```
  `kPad_L2 = 0`, `kPad_L1 = 2`, `kPad_L3 = 9`
  (`orig-assets/extracted/(..)/(..)/system/run/config/macros.dta:169-188`). `kPad_L2` is the
  *trigger*: `src/system/os/Joypad_Xinput.cpp:198-206` maps `state.Gamepad.bLeftTrigger` to
  bit 0. So "left shift" = **LT + LB**, and `CheatProvider` even spells it out in the menu
  header — `"LEFT CHEATS (L1 + L2)"`, `src/system/ui/CheatProvider.cpp:28`.

* **The binding.** `orig-assets/extracted/(..)/(..)/system/run/config/default.dta:218-220` —
  ```
  (kPad_L3
     "Show Cheats"
     {show_cheat_screen system_cheat_screen})
  ```

* **Why that binding is live in DC3.** DC3's own `config/cheats.dta` `(left …)` section only
  defines `kPad_Tri` and `kPad_R2`. But `config/ham_keep.dta` **is** the system config
  (`src/App.cpp:317` → `SystemInit("config/ham_keep.dta")`), and its last line is
  `#merge ../../../system/run/config/default.dta` (`config/ham_keep.dta:158`). `#merge` is
  resolved **at runtime**, not baked (`src/system/obj/DataArray.cpp:564,597`), and
  `DataMergeTags` is a union-by-tag that *appends* tags missing from the destination
  (`src/system/obj/DataUtl.cpp:70-88`). `kPad_L3` (9) is absent from DC3's `left`, so it is
  appended. Verified in the shipped binaries: the `#merge` node is present in
  `config/gen/ham_keep.dtb` and both `"Show Cheats"` strings are present in
  `(..)/(..)/system/run/config/gen/default.dtb`.

* **The screen exists and ships.** `(..)/(..)/system/run/ui/cheat.dta` defines
  `system_cheat_panel` / `system_cheat_screen`; it is loaded by
  `(cheat_init #include ../ui/cheat.dta …)` at `default.dta:414-418`, executed from
  `src/system/ui/UI.cpp:905-906` (`static Message cheat_init("cheat_init"); Handle(...)`).
  The art file is in the ark: `(..)/(..)/system/run/ui/gen/cheat.milo_xbox`.

* **Navigation is pure pad.** `cheat.dta:20-30` handles `BUTTON_DOWN_MSG`
  (`kAction_Cancel` → pop screen, `kAction_ViewModify` → cycle filter) and `cheat.dta:47-51`
  handles `SELECT_MSG` → `{cheat_provider invoke {cheat.lst selected_pos} $user}`.

* **The list includes the *keyboard* cheats.** `src/system/ui/CheatProvider.cpp:18-30` walks
  `SystemConfig("quick_cheats")` and pushes every section, emitting a `"KEYBOARD CHEATS"`
  header. The entry renders as `alt r | Game: Toggle Song Recording`.

* **Selecting one really runs it.** `CheatProvider::Invoke` (`CheatProvider.cpp:126-131`) →
  `CallQuickCheat` (`src/system/utl/Cheats.cpp:151-156`) →
  `CheatsManager::CallCheatScript(true, da, lu, /*b2=*/false)`. With `b2 == false` the
  joypad-type gate at `Cheats.cpp:227-231` is skipped entirely and the script is executed at
  `Cheats.cpp:256`. **This is the key fact: a keyboard-only cheat is invocable from the pad.**

* **Cheats are on.** `(disable_cheats FALSE)` at `default.dta:180`; gate at
  `src/system/utl/Cheats.cpp:391-396`. Key cheats are disabled during boot
  (`src/App.cpp:291`) and re-enabled at `src/App.cpp:763`.

* **The `#ifdef` guards do not remove any of this.** `ham_keep.dta:75-77` wraps the cheats
  include in `#ifndef _SHIP` and `cheats.dta:1` wraps the pad sections in `#ifndef DEMO`. DTA
  `#ifdef`s are evaluated **at runtime** from the macro table, and the only `DataSetMacro`
  calls in the tree are `HX_XBOX` / `HX_WIN` / `HX_NG` plus `-define` argv
  (`src/system/os/System.cpp:433-440`) and `REGION_*` (`src/system/os/PlatformMgr.cpp:137`).
  Neither `_SHIP` nor `DEMO` is ever defined, so both blocks are active.

* **The cheat is in the shipped, precompiled data.** `config/gen/cheats.dtb` is encrypted with
  the engine's own `Rand2` stream cipher (`src/system/utl/BinStream.cpp:227-231` seeds
  `Rand2` from a leading LE int; `src/system/math/Rand2.cpp` is a Lehmer PRNG, each byte
  XORed with `Int() & 0xFF`). Decrypting it shows `toggle_song_record`,
  `toggle_song_record_double` and `Game: Toggle Song Recording` present, matching
  `orig-assets/extracted/config/cheats.dta:1528-1543`.

If the list is long, press the **ViewModify** button to cycle the filter to `game` — the entry
carries `(filters game)` (`cheats.dta:1531`), and `CheatProvider::ApplyFilter`
(`CheatProvider.cpp:137-166`) shows everything under the default `all` filter anyway.

### Option 2 — USB keyboard (`Alt`+`R`). This genuinely works on a real 360.

The keyboard path is **not** Win32-only. `src/system/os/Keyboard_Xbox.cpp:48-50` polls
`XInputGetKeystroke(0xFF, 2, &keystroke)`, and that translation unit is linked into the
shipped debug XEX — `config/373307D9/symbols.txt` lists
`?KeyboardPoll@@YAXXZ = .text:0x825F41F0` and the file-local
`?TranslateVK@?A0x009a559d@@YAHG_N@Z = .text:0x825F4038`. `KeyboardPoll()` is called
unconditionally every frame from `SystemPoll` (`src/system/os/System.cpp:226`), and
`CheatsInit` subscribes the manager to the keyboard and parses the `keyboard` section
(`src/system/utl/Cheats.cpp:397,403`). The `keyboard` section even contains its own
`#ifdef HX_XBOX` block (`config/cheats.dta:34-44`), which only makes sense if it is consumed
on Xbox. So plug in any USB keyboard and press `Alt`+`R`.

With a keyboard you also get the **RndConsole**: `ESC` (`default.dta:226-228` →
`{rnd show_console}`) opens a text prompt that evaluates arbitrary DTA —
`src/system/rndobj/Console.cpp:416-450` does `DataReadString(line)->Execute()`. Typing
`{toggle_song_record}` there is equivalent. There is **no pad binding for the console
anywhere**; it is keyboard-only.

### Option 3 — arm it unconditionally at boot (needs an ark rebuild)

`sGameRecord` is *not* DTA-settable: `src/system/hamobj/MoveDir.h:115-116` declares plain
`static bool`s with no `SYNC_PROP` / `DataMember` / `DataVariable` binding, and the only
mutators are the two **toggle** DataFuncs at `src/lazer/game/Game.cpp:1224-1225`. So there is
no config value to flip — but there *is* a boot script: `config/ham_init.dta`, executed by
`src/system/hamobj/Ham.cpp:172` (`SystemConfig("ham_init")->ExecuteBlock(1)`). Appending
`{toggle_song_record}` there arms recording for every song, since `SetupSongRecordClip()` is
re-consulted at each song reset (`src/system/hamobj/MoveDir.cpp:1201,1209`).

Two obstacles make this worse than Option 1:

1. **You must rebuild the `.dtb`, not the `.dta`.** `CachedDataFile` redirects `.dta` →
   `gen/<base>.dtb` whenever `UsingCD()` (`src/system/obj/DataFile.cpp:584-598`), and the
   shipped `.dtb`s are `Rand2`-encrypted.
2. **The XEX SHA1-checks ark DTBs.** `src/ChecksumData_xbox.cpp` is a baked table (it includes
   `./config/gen/ham_keep.dtb` at line 297), installed at `src/App.cpp:292` and enforced in
   `src/system/obj/DataFile.cpp:528-533` and `:734-741`. Editing a checked `.dtb` in place will
   trip validation.

Related but not a shortcut: `{run <path>}` (`src/system/obj/DataFunc.cpp:1060-1069`, registered
at `:1703`) *will* parse a **loose** `.dta` as text, because `CachedDataFile` only redirects
non-local paths and `FileIsLocal` is true for any drive prefix longer than one char that isn't
`game` (`src/system/os/File_Win.cpp:9-13`, `src/system/os/File.cpp:610-624`). So
`{run "hdd:\rec.dta"}` bypasses the ark — but you still need something to *call* `{run}`, which
puts you back at Option 1 or 2.

### Option 4 — remote trigger over the network (not practical here)

* **xbdm is a dead end.** `debug.xex` imports exactly five xbdm APIs —
  `DmMapDevkitDrive`, `DmGetXboxName`, `DmCaptureStackBackTrace`, `DmGetSystemInfo`,
  `DmIsDebuggerPresent` (`config/373307D9/symbols.txt:161-165`). There is **no**
  `DmRegisterCommandProcessor` anywhere in the binary, so the title registers no remote command
  surface.
* **Holmes (TCP 4544)** is Harmonix's PC link and *does* inject remote keystrokes:
  `HolmesClientPollKeyboard` → `KeyboardSendMsg` (`src/system/os/HolmesKeyboard.cpp:50-65`),
  called from the tail of `KeyboardPoll` (`src/system/os/Keyboard_Xbox.cpp:70`). But it only
  dials out when `UsingCD()` is false (ark header missing or `-no_cd`) or `-host_config` /
  `-host_logging` is passed (`src/system/os/HolmesClient.cpp:402,750-761`), and **no PC-side
  Holmes server exists** in this repo or the sibling decomps — you would have to write one.
* **AppChild (TCP 4543)** is the cleanest remote eval —
  `src/system/os/AppChild.cpp:66-78` literally does `*mStream >> cmd; cmd->Execute(true);` —
  but it requires `-app_child` on the command line plus a resolvable Holmes host
  (`AppChild.cpp:53-57`), i.e. devkit-style launch arguments.
* **OSC (UDP 12346)** carries only float/int values into a name→value map
  (`src/system/utl/OSCMessenger.cpp:15-78`); it cannot call a DataFunc.

### Option 5 (last resort) — patch the executable

**A data byte-flip is not possible.** `sGameRecord` lives in **`.bss`**, not `.data`:
`orig/373307D9/ham_xbox_r.map:91793` puts it at `0009:0005bcc8`, and section 0009's `.bss`
begins at `0009:00058580` (`.data` is `0009:00000680`, length `0x57ee0`). Uninitialized data is
not stored in the image, so there is no byte in `debug.xex` to change. (`symbols.txt` labels it
`.data:0x82F618C8`; the map is the authority here.)

That leaves a **code** patch. The relevant addresses (debug XEX VAs, from
`config/373307D9/symbols.txt`):

| Symbol | VA |
| --- | --- |
| `MoveDir::SetupSongRecordClip()` — reads `sGameRecord` at `MoveDir.cpp:1057` | `0x824FF9B0` (size `0x1F0`) |
| `OnToggleSongRecord(DataArray*)` | `0x82865C10` (size `0x64`) |
| `OnToggleSongRecordDouble(DataArray*)` | `0x82865C78` (size `0x2C`) |
| `MoveDir::sGameRecord` / `sGameRecord2Player` | `.bss` `0x82F618C8` / `0x82F618C9` |

The minimal edit is to neutralise the `sGameRecord` test in `SetupSongRecordClip` so the
`SetupRecordClip(...)` call is always taken. **Caveat before you try this:** the VA→file-offset
mapping in `debug.xex` is **piecewise, not flat**. The `+0x3000` formula used for the two
`devkit:` string patches above is correct for those two (both were located by direct content
search and independently corroborated — `??_C@…toggle_song_record?$AA@` at `.rdata:0x820F40BC`
sits at file `0x000F70BC`, exactly `VA − 0x82000000 + 0x3000`), but applying the same formula in
`.text` around `SetupSongRecordClip` lands roughly `0x8000` bytes off — consistent with XEX
"basic" (zero-run) compression omitting zero blocks from the file. **A code patcher must parse
the XEX basefile block map rather than assume a constant delta.** This was not pinned down; it
is the one item in this section that is unverified.

Given Option 1 works with a stock controller, this route should not be needed.

---

## 3. Filming setup (the video half of the ground truth)

The whole point is a pixel-accurate view from *approximately the Kinect's own viewpoint*, so
MediaPipe sees what the Kinect saw.

**Camera placement — this matters more than camera quality:**

* Put the phone/webcam **as close to the Kinect sensor as physically possible**: same height,
  same aim, directly on top of (or immediately beside) the Kinect. Tape it down. A strip of
  gaffer tape over the phone onto the Kinect's top surface is fine; just don't cover the
  Kinect's IR emitter, IR camera or RGB camera (the three windows across its front), and don't
  block its motorized tilt.
* **Landscape** orientation. Match the Kinect's ~57° horizontal FOV as closely as your camera
  lets you — use the phone's **main (1×) lens**, not ultrawide, and do **not** use any
  "stabilization crop"/"action mode" that changes framing mid-take.
* **1080p, 30 or 60 fps.** 60 is better (less motion blur on fast arm moves), 30 is acceptable.
  Prefer whichever your phone can hold at a *constant* frame rate — avoid "auto fps" / HDR
  video modes that drop frames.
* **Lock exposure, focus and white balance** (on iPhone: long-press to AE/AF-lock; on Android:
  Pro mode, manual ISO/shutter/focus). Autofocus hunting mid-take is the single most common
  ruiner of these captures.
* **Frame the whole body including the feet**, at the Kinect's normal play distance
  (~2.0–3.0 m from the sensor). Leave ~20 cm of headroom and keep the feet in frame at all
  times, including during jumps and lunges. If you have to choose, keep the **feet** — foot/ankle
  Z is exactly what we're trying to validate.
* Room lighting: bright, even, and **constant**. Turn off anything that flickers or auto-dims.
  Avoid strong backlight (window behind the player). The TV's own light changing colour is
  fine and unavoidable.
* If possible, get the TV *out* of frame; if it must be in frame, that's OK, just note it.

**Measure and write down** (this is what lets us check metric scale):

* Distance from the Kinect front face to where the player stands (tape measure, ±5 cm).
* Kinect height off the floor, and roughly its tilt (it auto-tilts; just note "on top of TV" etc.).
* The **player's height** in cm, and shoe type (barefoot / trainers).

---

## 4. Sync procedure

The `.clp` has **no audio and no video**, and the video has no skeleton, so we sync on a motion
event that is unmistakable in both.

Per song:

1. Start the phone recording **first**, and leave it running for the whole song (one video per
   song, or one long video covering several songs — either is fine, just tell us which).
2. Get into position, start the song.
3. **Once the song has actually started** (after the count-in, during the first few seconds of
   music — remember, nothing is recorded before `song_seconds` starts advancing):
   perform **one single sharp jump with both arms thrown straight up overhead**, then land and
   snap the arms straight down. One clean jump, not a sequence.
   * In the skeleton this is a sharp spike in `hip_center.y` plus a large `hand_left.y` /
     `hand_right.y` excursion — trivially detectable.
   * In the video it is an equally obvious vertical body translation.
   * **Do not clap** — claps are audio-only and the clip has no audio.
4. *(Optional but useful)* After the jump, take one step fully **out of the Kinect's view** and
   back in. That produces a clean `is_tracked` false→true transition, giving a second, coarser
   sync landmark and a tracking-loss sample.
5. Play the song normally to the end. **Let it finish** (or fail out to the results screen) —
   the `.clp` is only written when the game ends the song.
6. Between songs, leave the phone rolling if convenient; just do a fresh jump at the start of
   each song.

### Song / move selection

Play **2–3 songs you actually know well** (accuracy of the dance matters less than confident,
full-range movement — hesitant movement produces uninteresting data). Bias toward songs whose
routines include:

* **Big arm extension toward and away from the camera** — punches, pushes, reaching forward,
  arms straight out at the sensor. This is the depth axis where MediaPipe is weakest and where
  we most need ground truth.
* **Foot-heavy moves** — steps, kicks, lunges, stomps, anything where the feet leave the floor
  or slide. This is our known-weak area (see the IK / feet-in-floor work).
* **Turns / partial profile** — moments where one arm or leg occludes the other, so we capture
  `tracking_state == 1` (inferred) joints.
* Ideally one song on a **higher difficulty** (more, faster moves) and one on a lower one
  (cleaner, more separable poses).

---

## 5. What to send back

* **The `.clp` files** — from the game folder, i.e. `d:\` as the title sees it (or wherever your
  patch writes them; see the [drive ladder](#drive-spelling-ranked-ladder)). Filenames look
  like `2117447891~aroundtheworld~h~emilia~pi.clp`; keep them as-is, the name encodes the
  timestamp, song, difficulty, dancer and mode.
* **The video files**, unedited and untranscoded (straight off the phone — do NOT let a
  messaging app re-encode them; use a cable, an SD card, or a file-transfer service).
* **A short note per session**, ideally a text file next to the media:
  * Which video goes with which `.clp` (and which song, in order).
  * **Camera / phone model** and the exact video mode used (e.g. "iPhone 14 Pro, 1080p60,
    main lens, AE/AF locked").
  * **Phone camera FOV** if you know it (horizontal degrees, or the lens's 35 mm-equivalent
    focal length — e.g. "24 mm equiv").
  * How the camera was mounted relative to the Kinect (on top / left / right, offset in cm).
  * **Kinect model** — "Kinect for Xbox 360" model **1414** (original, needs external PSU) vs
    **1473** (slim/E). Printed on the underside label.
  * Measured play distance, Kinect height, player height, footwear.
  * Anything odd that happened: tracking dropped, someone walked through frame, a pet, the
    Kinect re-tilted mid-song, you restarted the song, etc.

Sanity-check each `.clp` before sending — it takes a second:

```bash
python3 tools/pose_corpus/parse_skeleton_clip.py path/to/recording.clp
python3 tools/pose_corpus/parse_skeleton_clip.py path/to/recording.clp --npz recording.npz
```

A good capture shows a plausible frame count (~30 × song length), a high `tracked frames %`,
sensible per-joint ranges (head y ≈ player height, z ≈ 2–3 m), and a `song seconds` span that
matches the song. If `tracked frames` is near 0 % or the joint ranges are absurd, redo it.

---

## 6. Open risks / things still to verify on hardware

1. **Does `debug.xex` boot on an RGH console with the retail disc's ark?** Build dates are one
   day apart (debug map 2012-09-15, retail 2012-09-16) so the data should match, but this is
   untested. If the game asserts on load, that's the first thing to suspect.
2. **Is `xbdm.xex` present/loadable?** `debug.xex` imports it. Without it the module won't
   resolve its imports.
3. **Is the game folder actually writable on your setup, and under which drive spelling?** True
   for an extracted HDD/USB install, false when booting from disc. If writes fail, the debug
   build will `MILO_FAIL` with `Recording failed; could not open output file (…)` — a loud,
   unambiguous signal, but a **fatal** one (`Debug::Modal` → `Exit(1, true)`). Settle this with
   the [zero-cost drive probe](#drive-spelling-zero-cost-probe) before playing a song, and work down
   the [ranked drive ladder](#drive-spelling-ranked-ladder). **Never use a `game:` spelling** — that
   trips `MILO_ASSERT(!strieq(drive, "game"))` in `FileIsLocal` and kills the title.
4. **Does the debug XEX still pass RGH's signature handling after the two-byte-string patch?**
   Dev-signed XEXs are normally not hash-verified on RGH, but if it refuses to launch, rebuild
   the XEX header with `xextool`/`imagexex` instead of a raw byte edit.
5. **Whether the recorder actually arms** — confirm you saw the on-screen
   `Song recording   TRUE` before trusting a session. Diagnostic ladder: if **LT+LB+L3** does
   not open the cheat menu at all, cheats are off (`disable_cheats` in
   `system/run/config/default.dta` — shipped as `FALSE`, so it should be on) or the runtime
   `#merge` of `default.dta` did not bring in the `kPad_L3` binding. If the menu opens but
   *"Game: Toggle Song Recording"* is missing, `config/gen/cheats.dtb` in your ark differs from
   the one analysed here. If the menu opens and the entry is there but selecting it does
   nothing, the problem is downstream in `toggle_song_record` itself. The **LT+LB+L3** chord is
   the single best smoke test — it is the one step verified only by source reading, never on
   hardware.

## References

* `src/system/gesture/SkeletonClip.{h,cpp}` — the recorder and the file format
* `src/system/gesture/BaseSkeleton.h` — joint / bone enums
* `src/system/gesture/Skeleton.h` — `SkeletonFrame`, `SkeletonData`
* `src/system/hamobj/MoveDir.cpp` — `SetupSongRecordClip`, `SetupRecordClip`, `RecordClipName`,
  `PostUpdate`, `FinishGameRecord`
* `src/lazer/game/Game.cpp` — `OnToggleSongRecord`, `Game::ClearState`
* `src/system/utl/Cheats.cpp`, `src/system/os/Keyboard_Xbox.cpp` — the cheat/keyboard path
* `src/system/ui/CheatProvider.cpp` — the pad-navigable cheat menu's list provider
* `orig-assets/extracted/(..)/(..)/system/run/ui/cheat.dta` — `system_cheat_screen` UI
* `orig-assets/extracted/(..)/(..)/system/run/config/default.dta` — `kPad_L3` "Show Cheats"
  binding, merged into DC3's config by `config/ham_keep.dta:158`
* `orig-assets/extracted/config/cheats.dta` — the `Alt+R` binding
* `tools/pose_corpus/parse_skeleton_clip.py` — parser + format spec + selftest
