# Xenia as an oracle for the native port

**Status:** proposal, 2026-09-30. Nothing here is built yet. The inventory in
§1 was taken read-only from `xenia` @ `frag-alloc-trace` `aa337a177` and this
repo @ `64f20a202`, and every claim in it carries a file reference in the survey
it came from.

**Goal.** Record "authoritative" states from the *original* `debug.xex`
running under our Xenia fork, and check that the native port reproduces them.
This is the second dynamic-testing track. The first — making the native
engine itself bug-free (routes, asserts, gameplay gates) — continues in
parallel and is covered in §6.

---

## 1. What Xenia actually gives us today

**The honest summary: Xenia reaches `game_screen` with an animating dancer,
but the menu flow, song clock, pause state, audio and Kinect input around
that dancer are all driven by host code, not by the game.** A timeline
recorded from a Xenia gameplay run is therefore *not* a recording of what the
game does. It records what the game computes *inside conditions the host
forces*.

### Patches on the original layout (≈90 guest modifications plus runtime host writes)

| Perturbed subsystem | How |
|---|---|
| Kinect / NUI / gesture input | 56 NUI and 3 `CXbcImpl` function overrides. The skeleton is a hardcoded 20-joint constant pose (`emulator.cc:3217-3238`). |
| UI flow and transitions | The nav bridge force-calls `GotoScreen` along attract→…→`game_screen` on NUI-call-count thresholds, and force-enters or force-completes transitions by writing `UIManager` +0x2C/+0x48/+0x4C. The input driver also forces the attract→title jump. |
| Song selection | "LoadSong repair" sets `'ymca'` whenever the song is empty. |
| Pause / wait state | An **unpause nudge** writes `mWaitState`, `GamePanel+0xF8`, `mRealTime` and `mPaused`. `HandleWait+0x90` and `Game::PauseForSkeletonLoss` are patched. |
| Song clock / beat | **Two independent host beat drives**, each hardcoded to 120 BPM and each with its own accumulator, both write the TaskMgr timelines (`emulator.cc:4376-4452`, `nop_input_driver.cc:423-583`). |
| Audio | `HamAudio::IsReady+0x70`→1, `XMAHALAllocateContexts` patched. Audio is never genuinely ready. |
| Anim selection | `HamDirector::SongAnim` is forced to EXPERT. |
| Movies, splash, save/content, speech | `BinkMovieImpl::Ready`, `MoviePanel::IsLoaded`, 4× Splash, `SaveLoadManager::Activate`, `SpeechMgr::Grammar::Unload` |
| File-I/O completion | `CDReadDone`=1, `ContentMgr::RefreshDone`=1, `io_force_synchronous_completion` |
| Error semantics | The `Debug::Fail` thread spin returns instead of spinning. |

**No patch touches:** the DTA interpreter, `ObjectDir` and merge logic,
Char/CharClip/`RndPropAnim` evaluation, skinning, `HamIKEffector` (unless
the telemetry code caves are on), `math/`, Rnd draw submission, and the
allocator. **Those are the oracle-grade subsystems.** Their *inputs* are
still synthetic.

### Other facts that constrain the design

- **Gameplay has not been re-verified on Xenia since the 2026-06-09
  bring-up.** Four DC3 commits since then (manifest load, XCU protect, MMIO
  gating, content wipe) were only boot-checked, with runs of about 20 s on
  `--gpu=null`. The branch the docs name, `headless-vulkan-linux`, no longer
  exists; its lineage is now xenia `main`.
- **Hidden config dependency.** The NUI block needs `--stub_nui_functions=true`.
  The documented command line omits it, and it works only because the
  *shared* `~/.local/share/Xenia/xenia.config.toml` sets it — a file the RB3
  session last wrote.
- **Nothing makes a run reproducible.** Nav thresholds, script delays, the
  beat drives and the 33 ms NUI timeout are all tied to wall clock or to
  thread scheduling.
- **Observation channels:**
  - frames (`--dump_frames_path`, indexed by VdSwap)
  - GDB-RSP with breakpoints, but no memory or register writes
  - `DC3:IK` log lines
  - `--dc3_runtime_telemetry_enable` JSONL (boot milestones only)
  - **milo-trace `.mtr`** per-call capture (`cpu_flags.cc:38-78`)
  - **`processor->Execute`**, used at 20 sites in `emulator.cc`, all on the
    skeleton worker thread and never on the main thread

  There is no HTTP server and no DTA channel.
- **Input.** Native flow files do not replay faithfully. `+N` means frames on
  native but N×50 ms on Xenia. `cancel` silently becomes a no-op. Absolute-frame
  lines are rejected. Xenia's `wait_screen` does not check "not in transition".
  Separately, the nav bridge advances screens whatever the script says.
- **Precedent.** The only Xbox-vs-native comparison ever made — the feet-in-floor
  "toes Xbox-exact" — compared *distributions* binned by pelvis-Z band, because
  nothing aligned the two runs in time. The `PoseDumpCanMatchGoldenWithTolerance`
  golden is **native self-regression**, not Xbox truth (its `source_milo` is a
  local `milo-viewer --pose-dump`).

### What the dc3 repo already has on the receiving side

- **State Diff** (`tools/state_diff/`, `docs/tools/STATE_DIFF.md`) is already
  a differential engine. DTA probe specs are compiled to paged evals and produce
  a canonical `schema: 2` snapshot. Normalisation is per tolerance class,
  volatile fields are elided, findings are ranked, and there is a measured noise
  floor. It has `NativeHttpTarget`, `ConsoleTarget` and `ReplayTarget`.
  **`ConsoleTarget` adapts any object exposing `eval(script)` /
  `eval_batch(scripts)`, run on the title's main thread.** That is the whole
  seam a Xenia target needs.
- Native `/api/dta/eval`, `/api/pose/target`, `DC3_TEL` (per-frame
  `key=value`), `milo-viewer --pose-dump` (timing-free bone JSON for one clip at
  a beat), `synthetic_kinect.py` (DC3_20 skeleton packets over
  `DC3_POSE_SOCKET`), and `native/scripts/compare_pose_json.py`.
- The unicorn runner (orig PPC vs *our PPC*, function at a time) and
  `../milo-trace` (Xenia `.mtr` captures replayed through Unicorn). Neither
  touches the native x86 build.

---

## 2. Design principle: compare what the game computes, not what the host drives

Rank candidate comparisons by how much of the *authoritative* side is
genuinely the original code:

| Tier | Comparison | Authority | Timing needed | Cost |
|---|---|---|---|---|
| **A** | **Evaluator oracle:** call the game's own functions from a quiescent point with controlled inputs (load a .milo, dump its object graph, evaluate a clip at beat *b*, run an IK solve on a given pose, evaluate a DTA expression) | High: patch-free subsystems, host-chosen inputs | None | Medium: one Xenia feature |
| **B** | **Golden vectors:** milo-trace per-call captures of pure functions (math, clip eval, IK) turned into native gtest fixtures | High for leaf/pure functions | None | Medium |
| **C** | **Timeline diff:** same flow and same skeleton stream on both sides; per-beat snapshots of bones, director and scoring | Medium, *after* the clock and input are made fixed-step and identical | Beat/song-ms alignment | High |
| **D** | **Visual:** frames at aligned beats, perceptual diff | Low, but a good smoke test | Beat alignment | Low once C exists |

**Tier A first.** It sidesteps every weak point in §1: no clock, no flow, no
input. It exercises exactly the subsystems no patch touches. And State Diff
already consumes it unchanged.

---

## 3. Tier A — a DTA channel into the original binary

### 3.1 Mechanism

Give Xenia a transport satisfying State Diff's console contract:

1. **Run on the guest main thread, at a safe point.** Hook a function the
   main loop calls once per frame and drain a request queue there. **Do not**
   reuse the skeleton-worker `processor->Execute` pattern: the 2026-06-02 work
   measured that calling `LoadMgr::Poll` from the NUI thread races the main
   thread's own poll and crashes in `DataArray::Node`. Any object-graph read
   off the main thread is the same race.
2. **Evaluate with the game's own interpreter:** parse the script, then
   `DataArray::Execute` each command, wrapped the way `RndConsole` does
   (`MILO_TRY`/`MILO_CATCH`, `Console.cpp:434-444`) so a bad probe prints
   `Script error` instead of faulting. Emit results with the `=> ` marker and
   `!! refused` / `!! parse error` sentinels, so `split_results()` in
   `tools/console/dc3_eval.py` works unchanged.
3. **Transport:** a unix socket or a spool directory. The contract is only
   `eval` and `eval_batch`.

**Zero-new-Xenia-code variant worth trying first.** `dc3_eval.py`'s **file
transport** already drives the *real console* with no binary modification: it
pushes a loose `.dta`, `RndConsole` runs `{run "…\p.dta"}` on one keypress, and
the script writes its answers to a file. Under Xenia, the `devkit:` device is
a host directory (`--devkit_root`), so an FTP-less local backend plus a way to
deliver the keypress might give a working channel in an afternoon. The open
question is how `RndConsole` receives its trigger on Xenia; that is the first
thing to measure.

### 3.2 Which state to boot into

Pick the **least-patched stable state** that has the subsystem loaded:

- **Evaluator probes** (asset load, clip eval, IK, DTA semantics): boot to
  `title_screen` or the main menu and stop driving the flow. There, all the
  gameplay-state patches are idle: the unpause nudge, the beat drives, and
  the SongAnim-expert patch has not been exercised.
- Then **have the probe do the work**: `{new ObjectDir}`, load a character
  .milo, set a clip and a frame, read back. The host chooses every input.

### 3.3 First probes (all timing-free)

1. **Loader:** load each character, venue and song .milo and dump
   `(class, name, type props)` for every object. This compares the native
   DirLoader and every `Load()` against the original. A mis-decompiled `Load`
   — the `CharUpperTwist::Load` and `RndFlare::Load` class — shows up as a
   field diff. The whole asset corpus is the test set.
2. **Clip evaluation:** for each clip and beat grid `b ∈ {0, 0.25, …}`, set the
   frame and read the bone local transforms. ⚠ State Diff notes that raw
   matrices and bone/skinning matrices are *not* propsynced. Add a
   `world_xfm_raw`-style reader on both sides: natively through the HTTP
   server, in Xenia by reading guest memory at the object's address. This is
   the one place where a small shared, non-DTA helper is justified.
3. **DTA semantics:** a battery of expressions (string, math, array and
   symbol functions, `iterate`, message dispatch). Cheap, and it would have
   caught `DataNode::Equal` and the `SortNodes` stride.
4. **Object behaviour:** `RndMorph::SetFrame`, `RndPropAnim` at a frame,
   `HamCamShot` rewind — the handler-driven state that shadow bodies and
   mis-decomps have repeatedly broken this month.

### 3.4 Recorded goldens: the product

- A run produces a **capture bundle**: the snapshot JSON plus a provenance
  manifest:
  - the xex sha256
  - the xenia binary's **xxh3** (not the commit stamp — see
    `pattern_provenance_needs_a_content_hash`)
  - the full cvar set, **explicit on the command line, never the shared toml**
  - the **list of active guest patches**, each with the subsystem it perturbs
  - the probe spec hash
  - the boot state
- **The differ refuses to compare a field in a subsystem the manifest marks as
  perturbed.** This makes the §1 table machine-enforced rather than folklore.
  It is the same discipline as `callee_gate.ensure_current_scan()`: a
  finding from a patched instrument is not a finding.
- Commit bundles to a corpus (small JSON, content-addressed). The **native
  side then runs against the corpus with no Xenia at all**: a gtest or ctest
  label `XeniaGolden.*` compares a live native capture to the stored golden.
  That makes Xbox faithfulness a CI property instead of a manual session.
- Record each golden **twice from independent boots** and keep only fields that
  agree. This is the Xenia-side noise floor, the same way State Diff measured
  native's.

---

## 4. Tier B — golden vectors from milo-trace

`--milo_trace_enable` already records per-call entry registers, memory windows,
returns and write deltas during a real run (`x64_emitter.cc:452`). For
**pure or leaf functions whose inputs are value types** (`math/`, `Vector3`/
`Transform` ops, clip sampling, IK solve steps, `RandomFloat`), convert
captures into native gtest vectors: decode each argument by type from our
headers, call the native function, and compare the outputs. This generalises
`native/tests/test_rand_seed.cpp:148`, the one existing case of an
orig-derived constant feeding a native test.

Limits:
- Anything touching an object graph needs layout translation (LP64, 16-byte
  `Vector3`), so it goes to Tier A instead.
- milo-trace's own W5 note caps trustworthy non-leaf capture at 4 of 37.

Leaf-only is the honest scope.

---

## 5. Tier C — timeline diff, and what must change first

Only worth building after Tier A proves the channel. The prerequisites are all
Xenia-side determinism work:

1. **One clock, fixed-step, keyed to the song.** Replace both 120 BPM beat
   drives with a single host clock that advances song-ms by a fixed step per
   guest frame, using the song's real tempo map, which the guest already has
   loaded. Native already has `DC3_FAST_TIME` (exactly 1/120 s per frame,
   `LiveInput.cpp:19-33`). Make the two steps equal.
2. **One input stream.** Teach `Dc3NuiSequencerExtern` to consume the
   `DC3_POSE_SOCKET` DC3_20 packet stream `synthetic_kinect.py` already serves,
   so both sides see the *same* skeleton sequence. Gesture detectors and
   scoring are guest code: with identical skeleton input, scoring becomes
   comparable even though NUI is overridden.
3. **Flow.** Either make the native flow-file semantics Xenia's (frames,
   `cancel`, not-in-transition waits), or skip the flow entirely by booting
   straight into a song through a Tier A DTA call. The second is far more
   robust.
4. **Key every sample by (song, beat)**, never by frame. Per beat, capture
   bone world transforms, `HamDirector` state, the current move and clip, and
   per-player score and rating. Diff with State Diff's tolerance classes.

Until 1–3 land, a Xenia gameplay timeline stays a distribution-level
comparison, like the feet work was. Say so in any finding drawn from one.

---

## 6. The native bug-finding track (continuing)

Separate from Xenia. Ordered by recent yield:

1. **Shadow-body audit, runtime-verified.** `#ifdef HX_NATIVE` bodies were
   the richest vein on 2026-09-30: ObjPtrVec erase, the HamNavList handler,
   `ReplaceNode`, the scoring guards, the `NgRnd::Offscreen` stub and the
   `VorbisReader` CTR endianness. A "legitimate adaptation" verdict now needs
   runtime evidence. Tier A turns many of these into direct A/B checks: the
   same probe, native body vs the original.
2. **ASan (and UBSan) route harvests.** `-DENABLE_ASAN=ON` exists
   (`native/CMakeLists.txt:41`, `docs/native/TESTING.md:76`) but is not part
   of any route run. Running `native_assert_harvest.py`'s four routes under
   an ASan build, in a private build dir, is cheap and hunts exactly the
   lifetime class this session kept finding by hand (RndGroup double free,
   ControlOwner parking).
3. **Content-pinning flow tests.** After the ymca-was-Starships finding, every
   flow test should assert the content it claims to exercise.

---

## 7. Proposed first steps (in order)

1. **Re-establish a DC3 Xenia baseline in an isolated xenia worktree** (xenia
   has `scripts/setup_worktree.sh`). The RB3 session is live on
   `frag-alloc-trace`, so do not touch its tree or the shared toml.
   - Make every cvar explicit, with a private config dir.
   - Re-verify boot → `game_screen` on current xenia `main`.
   - Record the patch manifest.

   **Exit:** a reproducible command and a manifest file.
2. **Spike the channel.** Try the file-transport variant (§3.1) first, then the
   main-thread request queue. **Exit:** `{+ 1 2}` and `{object_list …}`
   evaluated inside the original binary through `ConsoleTarget`.
3. **First golden:** the loader probe over a handful of character .milos,
   captured twice, diffed against native. Every disagreement gets
   adjudicated against the target listing. Expect the first few to be
   *instrument* findings, and treat them as such.
4. **Productionise:** the corpus, the manifest-aware refusal in the differ,
   and the `XeniaGolden.*` native tests.
5. **Then** Tier B leaf vectors and Tier C clock/input unification.

Open question for the owner: whether the Xenia DC3 work should live on a
dedicated long-lived xenia branch (for example `dc3-oracle`), so that RB3
bring-up commits on a shared branch cannot silently change the oracle. The
provenance xxh3 detects that, but it does not prevent it.

---

## 8. Outcome of steps 1–2 (2026-10-01)

**The channel exists.** The xenia branch `dc3-oracle` (`cf4011322`,
`cc37b22af`, worktree `/home/free/tmp/xenia-dc3-oracle`, unpushed) adds
`--dc3_dta_channel=<unix socket>`, off by default.

- It overrides `HolmesClientPollKeyboard`, which `SystemPoll` calls once per
  frame on the guest main thread.
- It evaluates with the game's own `DataReadString` and `DataArray::Execute`.
- It traps a failing command back into the channel and replies
  `=> !! refused: script error: …`.

The dc3 side is merge `27b49234d`, consumed through
`make_target("xenia:<socket>")` or `dc3_eval.py -T xenia --socket <path>`.
Measured inside `debug.xex`: `{+ 1 2}` → `3`;
`{size {object_list main Object FALSE}}` → `702`.

**Corrections to §1**, measured by the spike (details in the xenia branch's
`docs/dc3-oracle/BASELINE.md`):

- **Error semantics are perturbed for the whole run, not just the spin.**
  The `Debug::Fail` thread-spin patch returns with `mFailing` still set, from
  a boot-time `BinkMovieImpl::Ready called in the wrong thread` failure. So
  every later `MILO_FAIL` or assert, on every thread, falls through silently.
  *State the game builds outside the channel is suspect.* The channel clears
  the flag only for the duration of a probe.
- **Xenia cannot catch guest C++ exceptions.** A throw reaches a stub that
  raises `SIGTRAP`, so `MILO_TRY`/`MILO_CATCH` (and `RndConsole`) do not work.
  This also rules out the file-transport variant of §3.1, which additionally
  needs a keyboard device (`flags=40000002 → DEVICE_NOT_CONNECTED`) and a
  writable game directory.
- The listed cvars do not exist on xenia `main`; that behaviour is hardcoded
  on, for every title. The unpause nudge is not DC3-gated. Calibration,
  `Movie::Poll` and a dummy audio driver patch were missing from the table.
- **Baseline reproducibility depends on load.** A null-GPU boot reached
  `game_screen` 4 of 4 times at load ~20, and 0 of 9 at load 100–220 (4 of
  those hung during boot, 3 with no channel attached). Vulkan failed 2 of 2.
  One crash under load was the predicted race: the main thread faulted in
  `ObjectDir::FindObject` while the host navigation code was calling engine
  functions from the skeleton worker thread.

**Step 3 recommendation (unchanged in spirit, sharper in scope):**
1. Boot to `title_screen` and have the probe load the asset itself into a new
   `ObjectDir`, then dump it with probes scoped to that dir. The navigation
   code, beat drives and song repair cannot reach that scope.
2. Capture on a quiet box, twice from separate boots.

Separately, fix the `Debug::Fail` patch so it no longer leaves `mFailing`
set, and take a fresh baseline after that fix, before trusting any
game-built state.

---

## 9. Milestone: the song clock and the asserts are the game's own (2026-10-02)

xenia main `abed23403` (pushed). Of the perturbations listed in §1, these
are now **gone**:

- **Both 120 BPM host beat drives**, the unpause nudge, `HamAudio::IsReady`,
  `HandleWait`, the `XMAHALAllocateContexts` stub and the dummy audio driver.
  The game unpauses itself (`Game::PostWaitStart`), and the song clock is
  driven by real XMA contexts through the paced nop driver. With the drives
  on, song end landed anywhere from 189 to 213 s. Now it lands at
  198.2–201.5 s over 5/5 runs, even at load 103.
- **The latched `mFailing`.** The faithful `Debug::Fail` spin is restored,
  and menu automation runs on the guest **main** thread
  (`--dc3_headless_autonav`). A tripwire reports every main-thread FAIL.
  Six FAILs the latch had hidden are recorded as findings in xenia
  `docs/fork/dc3/BASELINE.md`.
- **Movie/Bink/Splash/boot gates** (10 hacks). `BinkMovieSys::Init`'s stub
  itself caused the song_select "preview.tmov" FAIL.

Two core Xenia bugs were fixed along the way:
- Overrides were bypassed by every indirect (vtable) call.
- The MMIO handler livelocked on a 128-bit `stvx128`, which is how the XMA
  HAL kicks its contexts.

**What is still perturbed, so the differ must still refuse it:**
- Kinect/NUI: 56 SDK overrides with a constant pose. Retiring them needs a
  NUI HLE device.
- Calibration patches.
- `SongAnim → EXPERT`.
- `ContentMgr::RefreshDone` and `SaveLoadManager::Activate`.
- The autonav itself: it presses buttons, but on the main thread.
- The headless null-GPU / capture path. Use `--headless_inline_render` for
  frames, because deferred replay produces artifacts (xenia
  `docs/fork/gpu/`).

**Consequence for Tier C (§5).** Prerequisite 1, a single clock keyed to the
song, is now satisfied by the game itself. Prerequisites 2 (a shared
skeleton stream) and 3 (flow) remain. Each hack now has an id, so a golden's
patch manifest can list exactly which ones were active (`--dc3_disable_hacks`
and the logged gate lines).

Related dc3 fixes this wave:
- `add619a39` and `2b92f12ab` made our *rebuilt* xex boot under Xenia with
  0 traps, down from 627.
- 149 of the 173 decomp-pack hacks were defects in our own image, not in
  Xenia.
