# Diagnostic toolkit + environment gotchas

Everything here was validated during the bug-1 investigation. The gotchas cost real
hours — read them first.

## Building

```bash
cd native/build && ninja dc3-native      # incremental; only touched files recompile
```
Compiler is Clang (x86_64). A single-file change (e.g. `UI.cpp`) → ~1 file + link,
seconds. Editing a shared header rebuilds more.

## Running headless + screenshot-at-frame (the reliable capture method)

`dc3-native` renders with WebGPU/Vulkan → **needs GPU → skip the sandbox**. In a
Bash tool call set `dangerouslyDisableSandbox: true`. Subagents do the same in their
own Bash calls — they are NOT GPU-blocked.

Capture PNGs at specific frames in one self-terminating run:

```bash
cd native/build
rm -rf ./_shots && mkdir -p ./_shots
env MILO_HEADLESS=1 DC3_FAST_BOOT=1 DC3_SHOW_SPLASH=0 \
    MILO_INPUT_SCRIPT=/abs/path/scripts/dc3-input-flows/to-choose-mode.txt \
    MILO_SCREENSHOT_DIR=./_shots MILO_SCREENSHOT_FRAMES=200,320 \
    MILO_MAX_FRAMES=340 \
    ./dc3-native > ./_run.log 2>&1
```
- Writes `_shots/frame_00200.png`, `frame_00320.png`. `MILO_MAX_FRAMES` bounds the
  run so it exits cleanly (set it just past your last capture frame).
- **Write screenshots to a repo path** (e.g. `native/build/_shots`), NOT `/tmp`:
  sandbox-disabled and sandboxed shells see *different* `/tmp`, so a PNG written to
  `/tmp` under `dangerouslyDisableSandbox` is invisible to a later normal Read. Repo
  paths are bind-mounted and always visible. Then `Read` the PNG to view it.
- No window/HTTP needed; screenshot works in pure headless.

### Live HTTP debug server (optional)
`DC3_HTTP=1 DC3_HTTP_PORT=9090` then `curl localhost:9090/api/{health,screen,screenshot,...}`,
`/api/screen/wait/<screen>`, `/api/settings?fovScale=…`. Docs:
`docs/tools/HTTP_DEBUG_SERVER.md`. Fine for interactive poking; screenshot-at-frame
is more reliable for scripted A/B.

## Input flows (scripts/dc3-input-flows/)

`boot-to-main.txt`, `to-choose-mode.txt`, `to-song-select.txt`, `betteroffalone.txt`
(full boot→gameplay), `ymca.txt`, etc. Each waits on screens (`wait_screen X`) and
injects buttons (`+N confirm`). Use `betteroffalone.txt` to reach gameplay.

## Instrumentation pattern (zero PPC/decomp impact)

Gate every diagnostic behind `HX_NATIVE` **and** an env var, print to stderr:

```cpp
#ifdef HX_NATIVE
if (getenv("DC3_TEXT_DIAG")) {
    static int n = 0;
    if (n++ < 60) fprintf(stderr, "DC3_TEXT_DIAG ...\n");
}
#endif
```
`HX_NATIVE` is undefined in the PPC/decomp build, so this never touches match%.
Rate-limit with a static counter. Revert with `git checkout -- <file>` when done
(these files had no other pending edits).

Useful things to instrument:
- `RndText::DrawShowing` (src/system/rndobj/Text.cpp): per-drawn-text ascii, mesh
  verts/faces, `mesh->Name()` (empty ⇒ isTextMesh), material + `GetDiffuseTex()`,
  `mesh->WorldXfm()`, and **`RndCam::Current()->WorldToScreen(worldVert, screen)`**
  to see where a glyph lands in screen space ([0,1], 0.5 = centre; >1 or <0 =
  off-screen). This is how bug 1 was pinned to projection.
- `WgpuRnd::DrawMeshImmediate` (engine `src/platform/Mesh_Wgpu.cpp`): per-mesh pass
  (`CurrentSampleCount`/`CurrentPassHasDepth`), pipeline non-null, DrawIndexed
  reached, material uniform flags.

## Camera / projection knobs (env vars, no rebuild)

- `MILO_UI_CAM_MODE = default | original | z_hack | rotate_hack` — selects the UI
  camera mode (see bug 1). `z_hack` re-applies the Z=387 shift.
- `MILO_CAM_FOV_SCALE` (>1 zooms in / narrows FOV, <1 widens), `MILO_CAM_NEAR`,
  `MILO_CAM_FAR`, `MILO_CAM_ASPECT`, `MILO_CAM_FORWARD`, `MILO_CAM_HEIGHT`,
  `MILO_CAM_LATERAL`, `MILO_CAM_DEBUG`. Defined in
  `native/src/platform/NativeSettings.h`.
- `MILO_SIMPLE_RENDER=1` — forces prelit + minimal material processing (isolates
  material vs geometry issues).

## Engine A/B via worktree (when you must test a different engine SHA)

The DC3 native build consumes the sibling `../milo-native-engine` (soft pin
`MILO_ENGINE_PIN` in `native/CMakeLists.txt`; build uses engine HEAD regardless).

```bash
git -C ../milo-native-engine worktree add --detach /home/free/code/milohax/_engine_pin <SHA>
cd native
cmake -S . -B build-enginepin -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DMILO_ENGINE_PATH=/home/free/code/milohax/_engine_pin \
  -DDawn_DIR=/home/free/code/milohax/dc3-decomp-deps/dawn/lib/cmake/Dawn
cd build-enginepin && ninja dc3-native
# clean up: git -C ../milo-native-engine worktree remove --force /home/free/code/milohax/_engine_pin
```
Caveat: an old engine SHA may not compile against current DC3 headers (the engine
`#include`s DC3 game headers). `FxSendNative.cpp` was the skew point — copy the
current working-tree version into the worktree to compile.

## GOTCHAS that will waste your time

- **`pkill -f dc3-native` kills your own shell.** The pattern matches the *current*
  Bash command line (which contains "dc3-native"), so `pkill -f dc3-native` sends
  SIGTERM to the very command running it → exit ~143/144, empty output. Don't use
  it. Bound runs with `MILO_MAX_FRAMES` instead; they exit on their own.
- **Foreground `sleep` is blocked by the harness** (exit 144). Don't `sleep` to wait
  for a run; use `MILO_MAX_FRAMES`-bounded foreground runs, or `run_in_background` +
  poll a log file, or Monitor.
- **`grep -c` returns empty in this shell** (ugrep quirk) — it prints nothing instead
  of a count, which reads as "no matches" and misleads. Use `awk '/pat/'`,
  `fgrep pat`, or `grep pat | wc -l`.
- **`/tmp` differs between sandbox modes** — see the screenshot note above. Write to
  repo paths.
- A `dangerouslyDisableSandbox` run and a normal run do not share `/tmp`; keep a
  whole capture+read cycle inside repo-relative paths.
