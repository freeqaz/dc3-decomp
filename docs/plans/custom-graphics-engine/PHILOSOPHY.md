# Native Port: Design Philosophy

Guiding principles for the DC3 native port codebase. These apply to the viewer,
the engine runtime, and any new tools we build.

## 1. Composition Over Inheritance

The Milo engine already has deep inheritance hierarchies (`Hmx::Object` ->
`RndTransformable` -> `RndDrawable` -> `RndMesh`). We inherit that — it's the
game's architecture, and the decomp must preserve it.

New code we write does not need to follow that pattern.

**Prefer structs with data + free functions over class hierarchies.**

```cpp
// NO — virtual dispatch for 3 known modes is ceremony
struct ViewerMode { virtual void Run(ViewerScene&) = 0; };
struct ScreenshotMode : ViewerMode { ... };

// YES — std::variant for a closed set of types
using ViewerMode = std::variant<ScreenshotMode, VideoMode, InteractiveMode>;

std::visit(overloaded{
    [&](ScreenshotMode& m) { RunScreenshot(m, scene, anim, charAnim, cam, cfg); },
    [&](VideoMode& m)      { RunVideo(m, scene, anim, charAnim, cam, cfg); },
    [&](InteractiveMode& m){ RunInteractive(m, scene, anim, charAnim, cam, cfg); },
}, mode);
```

**Why:** Inheritance works when you need open extension (plugin systems, new
entity types at runtime). Our viewer modes, render steps, and tool pipelines
are *closed* — the set of types is known at compile time. `std::variant` gives
value semantics (no heap, no vtable indirection, exhaustive visiting) and makes
the state machine explicit.

**Compose behavior from small, focused structs:**

```cpp
// Real code from ViewerAnimation.h:
struct BlinkState  { float timer, phase; void Advance(float dt); float Weight() const; };
struct AnimState    { float currentFrame, speed; bool paused; void ScanScene(...); };
struct CharAnimState { CharClip* clip; BlinkState blink; void AdvanceBeat(...); void PollFace(); };
```

Each struct does one thing. Testing a blink timer doesn't require a GPU.

## 2. Functional Core, Imperative Shell

Separate *computation* from *effects*.

The **core** is pure functions and self-contained state machines — no file I/O,
no GPU calls, no global mutation. The **shell** is the thin layer that does
I/O and wires things together.

```cpp
// Core: pure, testable, no side effects
float BlinkState::Weight() const;              // triangle ramp from phase
void  AnimState::ScanScene(ObjectDir*, cfg);   // populate from dir
float computeBlinkWeight(float phase, float duration);

// Shell: imperative, does I/O (milo_viewer.cpp main())
ViewerScene scene;
scene.Load(absPath, cfg);
gAnim.ScanScene(baseScene, cfg);
ViewerMode mode = SelectMode(cfg);
int rc = std::visit(..., mode);
```

**In practice:** Not everything can be pure (we're calling into a game engine
with global state like `TheRnd`, `TheTaskMgr`, `gWgpuRnd`). The goal isn't
purity for its own sake — it's pushing side effects to the edges so the middle
is understandable. `BlinkState` and `AnimState` are pure value types.
`CharAnimState::AdvanceBeat()` calls `TheTaskMgr` — that's the imperative
shell leaking in, but it's isolated to one method.

## 3. Value Types for State

Prefer value-type structs over mutable globals.

```cpp
// NO — scattered globals, implicit coupling
static float gBlinkTimer;
static float gBlinkPhase;
static float gLastAnimBeat;
static bool gAnimPaused;

// YES — state is a value, passed explicitly
struct CharAnimState {
    BlinkState blink;       // composed, not inherited
    float lastBeat = 0.0f;
    float lastSeconds = 0.0f;
};
```

When state is a struct, you can:
- Snapshot it for deterministic replay (video mode)
- Diff it for regression testing
- Pass it by const-ref to guarantee no mutation in render
- Print it in one `printf` for debugging

**Pragmatic exception:** `gAnim` and `gOrbitCam` are globals because GLFW
key callbacks need them and `glfwSetWindowUserPointer` migration is a separate
task. This is a known compromise, not a forgotten principle.

## 4. Linear Initialization

The viewer's setup is a linear sequence: parse args -> init GPU -> load scene ->
find character -> set up animation -> configure camera -> dispatch mode. Express
this as sequential calls in main(), not a pipeline of abstractions.

```cpp
// Real code from milo_viewer.cpp main():
ViewerConfig cfg = ViewerConfig::Parse(argc, argv);
// ... engine init ...
ViewerScene scene;
scene.Load(absPath, cfg);
// ... char/clip/viseme setup (imperative, stays inline) ...
gAnim.ScanScene(baseScene, cfg);
scene.AutoFrameCamera(gOrbitCam, cam, cfg);
scene.SetupSyntheticLights(cfg);
ViewerMode mode = SelectMode(cfg);
int rc = std::visit(overloaded{...}, mode);
```

Not a pipe operator library. Just functions with clear inputs and outputs,
called in sequence. Each function owns its error handling and logs its own
status. The 150-line char/clip/viseme setup stays inline because it's
inherently sequential — extracting it to a method just moves it, doesn't
simplify it.

## 5. Explicit Over Implicit

- Pass dependencies as arguments, don't reach for globals
- Name things after what they *are*, not what they *do to*
- If a function needs 6 parameters, that's fine — it's better than hiding
  them behind a god-object or global state
- `[&]` lambda captures are code smell when the lambda outlives the current
  scope — they silently bind everything in sight. The refactor eliminated
  all scope-escaping lambdas (`drawFrame`, `advanceCharAnim`, etc.)

## 6. Deduplication Through Composition

When code is copy-pasted across modes, extract the common logic into a method
on the struct that owns the state — not into a utility function with 8
parameters.

```cpp
// Before: blink weight computed identically in 3 places
float half = blinkDuration * 0.5f;
blinkWeight = (blinkPhase < half) ? blinkPhase / half : (blinkDuration - blinkPhase) / half;

// After: one method, three call sites
float w = charAnim.blink.Weight();

// Before: face servo poll duplicated in advanceCharAnim + screenshot direct-pose
if (faceServo) {
    if (!charEyes) { faceServo->SetProceduralBlinkWeight(blinkWeight); }
    faceServo->ApplyProceduralWeights();
    faceServo->Poll();
}

// After: one method
charAnim.PollFace();
```

The key: the *struct owns the state*, so the method doesn't need parameters
for state it already has. This is composition's payoff — `CharAnimState`
composes `BlinkState`, and `PollFace()` uses `blink.Weight()` internally.

## 7. Pragmatism

These principles serve the code, not the other way around.

- The Milo engine has globals (`TheRnd`, `TheTaskMgr`). We use them where
  we must, and isolate them where we can.
- Perfect purity isn't the goal. *Testability and readability* are.
- If the simplest correct solution is 3 similar lines of code, don't
  abstract it. Three similar lines you can read top-to-bottom beats a
  clever template you have to chase through 4 files.
- Game dev rewards code you can step through in a debugger over code that
  looks elegant in a code review.
- A 450-line main file with clear linear flow is a better outcome than an
  80-line main() that delegates to 15 functions you have to jump between.
  The char/clip/viseme setup is imperative wiring — it reads better inline.

## Module Map

After the refactor, the viewer is organized as:

```
milo_viewer.cpp     — Entry point: engine init, char setup, mode dispatch
ViewerArgs          — CLI config struct + parser (ViewerConfig)
ViewerCamera        — OrbitCamera struct, mouse/scroll callbacks (gOrbitCam)
ViewerAnimation     — AnimState, CharAnimState, BlinkState, PoseMeshesWithFacing
ViewerScene         — Scene lifecycle, mesh visibility, env lookup, drawing
ViewerCapture       — RunScreenshot, RunVideo, RunInteractive + mode variant
ViewerPoseDump      — JSON bone pose dump
CharTwistSolver     — Twist bone solvers (CharUpperTwist, CharNeckTwist, etc.)
```

Dependencies flow downward: Capture depends on Animation + Scene + Camera.
Animation depends only on engine types. Scene depends only on engine types.
No circular dependencies between viewer modules.
