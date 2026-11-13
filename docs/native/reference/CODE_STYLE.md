# Native port code style

Rules for writing native-port code: new code under `native/`, and native-only
code that has to live in the decomp `src/` tree.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

There are two kinds of native code, and they follow different rules:

- **Decomp code in `src/`** is the game. Its structure, inheritance and naming
  are dictated by the original binary. Native-only changes there are
  constrained by the PPC build (section 1).
- **New code in `native/`** (viewer, tools, platform layer, tests) is ours.
  It follows sections 2-8. The viewer (`native/src/viewer/`) is the worked
  example and is cited below.

## 1. Rules for native-only code in the decomp tree (`src/`)

- **Guard every native-only change with `#ifdef HX_NATIVE`.** A guarded block
  is invisible to the PPC compiler. An unguarded new inline body, virtual
  declaration or operator change in a header shifts MSVC's inlining and
  codegen across every TU that includes it. The rule table and the mechanism
  are in [docs/decomp/TECHNICAL_NOTES.md](../../decomp/TECHNICAL_NOTES.md#header-edits-shift-inlining-tu-wide-2026-03-10-salvaged-2026-08-17).
  The two PCH headers, `obj/Object.h` and `os/Debug.h`, reach every TU.
- **Keep a native body as close to the image body as you can, and prefer none
  at all.** Every decomp instrument (objdiff, `report.json`, unicorn) measures
  only the non-native branch. A `#ifdef HX_NATIVE` replacement body is code no
  tool checks. See
  [docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md](../../decomp/patterns/native-shadow-bodies-are-unmeasured.md).
- **Fix engine bugs at the root, with a unit test first.** Do not add a
  per-callsite workaround that papers over a mis-decompiled function.
- **Keep members `protected`/`private`** unless DWARF or an assert confirms
  they are public. For outside access, add a getter/setter, or a `friend` for
  closely related types (e.g. `Foo` and `FooHandle`).
- **Be careful with `MILO_ASSERT()` and `OBJ_MEM_OVERLOAD`.** Their contents
  affect codegen (`__LINE__`, string pools), so test any change to them.

## 2. Composition over inheritance

The Milo engine's inheritance chains (`Hmx::Object` → `RndTransformable` →
`RndDrawable` → `RndMesh`) belong to the game and must be preserved. New native
code does not need to copy that pattern.

**For a closed set of types, prefer a `std::variant` over a virtual base class:**

```cpp
// native/src/viewer/ViewerCapture.h
using ViewerMode = std::variant<ScreenshotMode, VideoMode, InteractiveMode>;

// native/src/viewer/milo_viewer.cpp
ViewerMode mode = SelectMode(cfg);
int rc = std::visit(overloaded{ /* one lambda per mode */ }, mode);
```

A variant gives value semantics, no heap allocation, and exhaustive visiting.
Use inheritance only where the set of types is genuinely open.

**Compose behaviour from small structs that each do one thing**
(`native/src/viewer/ViewerAnimation.h`):

- `BlinkState`: a blink timer with `Advance(dt)` / `Weight()`
- `AnimState`: frame-based prop animation
- `CharAnimState`: beat-based clip playback; it owns a `BlinkState`

A blink timer can be tested without a GPU.

## 3. Functional core, imperative shell

Keep computation separate from effects. The core is self-contained state and
math, with no file I/O, GPU calls or global mutation. The shell is the thin
layer that does I/O and wires things together; in the viewer that is `main()`
in `milo_viewer.cpp`.

Engine globals (`TheRnd`, `TheTaskMgr`) cannot be avoided. Isolate them. For
example, `CharAnimState::AdvanceBeat` is the one method that drives `TheTaskMgr`.

## 4. State is a value

Prefer a struct passed explicitly over scattered `static` globals. State held
in a struct can be snapshotted for deterministic replay, diffed in a test,
passed by const-ref to guarantee the renderer does not mutate it, and printed
in one line.

Known compromise: `gAnim` and `gOrbitCam` are globals because the GLFW
callbacks need them.

## 5. Linear initialization

A setup sequence should read as sequential calls in one function, not a
pipeline of abstractions. `milo_viewer.cpp` `main()` runs:

1. `ViewerConfig::Parse`
2. engine init
3. `ViewerScene::Load`
4. clip/viseme wiring, kept inline
5. `AnimState::ScanScene`
6. `ViewerScene::AutoFrameCamera`
7. `ViewerScene::SetupSyntheticLights`
8. `SelectMode` → `std::visit`

Each step owns its error handling and logging. Inherently sequential wiring
stays inline: extracting it moves the code without simplifying it.

## 6. Explicit over implicit

- Pass dependencies as arguments rather than reaching for globals. Six
  parameters beats a god-object.
- Name things after what they are.
- Do not let a `[&]` lambda outlive its scope; it silently captures everything.

## 7. Deduplicate by giving the method to the struct that owns the state

When logic is copy-pasted across modes, move it into a method on the struct
that owns the state. Do not move it into a free utility with eight parameters.
In the viewer, the blink-weight ramp lives once in `BlinkState::Weight()`, and
the face-servo poll lives once in `CharAnimState::PollFace()`.

## 8. Pragmatism

The rules serve testability and readability. Purity is not the goal.

- Three similar lines you can read top to bottom beat a template you have to
  chase through four files.
- Code you can step through in a debugger beats code that looks elegant in
  review.

## Sources

- `native/src/viewer/{milo_viewer.cpp,ViewerCapture.h,ViewerAnimation.h,ViewerScene.h}`
- `CLAUDE.md` § Code Style
- [docs/decomp/TECHNICAL_NOTES.md](../../decomp/TECHNICAL_NOTES.md),
  [docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md](../../decomp/patterns/native-shadow-bodies-are-unmeasured.md)
- Archived: [plans/custom-graphics-engine/PHILOSOPHY.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/PHILOSOPHY.md).
  Its module map listed `CharTwistSolver` as a viewer module. The viewer no
  longer calls it: twist bones come from the real `CharUpperTwist` /
  `CharForeTwist` pollables via `Character::Poll`. The copy that links is in
  `../milo-native-engine/src/char/`. Its `computeBlinkWeight` example does not
  exist.
