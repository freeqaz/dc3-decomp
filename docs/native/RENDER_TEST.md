# Render Test Tool

Standalone tool for testing the native rendering pipeline with programmatic test scenes. Geometry/blend tests need no `.milo` files. Text tests require a font `.milo_xbox` (auto-detected from the DC3 asset library).

## Purpose

Debug and regression-test the WebGPU rendering pipeline by creating minimal, controlled test cases. Instead of loading complex UI screens with hundreds of objects, build exact test scenes where every vertex, color, blend mode, and transform is specified.

## Usage

```bash
cd native/build
ninja render-test

# Render all test cases to a single screenshot
./render-test --output /tmp/render_test.png

# Render at custom resolution
./render-test --output /tmp/render_test.png --width 800 --height 600

# List available test cases
./render-test --list

# Render a specific test case only
./render-test --output /tmp/render_test.png --test alpha_blend

# Use a custom font for text tests
./render-test --output /tmp/render_test.png --font /path/to/font.milo_xbox

# Render the venue+UI composite test (loads a .milo venue scene)
./render-test --output /tmp/venue_ui.png --test venue_with_ui --width 1280 --height 720

# Use a custom venue for the composite test
./render-test --output /tmp/venue_ui.png --test venue_with_ui --venue /path/to/venue.milo_xbox
```

## Test Cases

### Geometry & Color
| Test | Description |
|------|-------------|
| `solid_quads` | Red, green, blue solid quads — baseline mesh + material |
| `vertex_colors` | Quad with per-vertex color interpolation (prelit path) |
| `z_ordering` | Overlapping quads at different depths — draw order correctness |

### Blend Modes
| Test | Description |
|------|-------------|
| `alpha_blend` | Semi-transparent quad over solid — `kBlendSrcAlpha` |
| `additive_blend` | Additive quad over dark background — `kBlendAdd` |
| `multiply_blend` | Multiply quad over bright background — `kBlendMultiply` |

### Text
| Test | Description |
|------|-------------|
| `text_basic` | Simple string rendering — glyph mesh generation |
| `text_clipping` | Text in bounded box — reproduces "OKAY" → "KAY" bug |
| `text_wrap` | Word wrapping — reproduces "Learn m/ore" bug |

### Composite
| Test | Description |
|------|-------------|
| `venue_with_ui` | Venue .milo scene with UI text overlays — tests layered rendering |

### Materials (planned)
| Test | Description |
|------|-------------|
| `textured_quad` | Procedurally generated checkerboard texture |
| `alpha_cutout` | Alpha-tested material (`mAlphaCut`) |

## Architecture

```
native/src/render_test/
├── render_test_main.cpp    # Entry point, CLI, screenshot
├── test_scene.h            # TestScene interface
└── test_scene.cpp          # Programmatic scene construction
```

### How it works

1. Boot engine headless (same init as milo-viewer)
2. Create `RndDir` as the root scene container
3. Create `RndEnviron` with known ambient + lights
4. Create `RndCam` with orthographic-like setup (predictable pixel positions)
5. For each test case, create `RndMesh` + `RndMat` objects with exact properties
6. Render frame: `BeginDrawing()` → iterate drawables → `EndDrawing()`
7. Readback framebuffer → write PNG

### Adding a test case

Each test case is a function that populates an `RndDir` with objects:

```cpp
void BuildSolidQuads(RndDir* dir) {
    // Red quad at (-2, 0)
    RndMat* redMat = Hmx::Object::New<RndMat>();
    redMat->SetName("red_mat", dir);
    redMat->SetColor(1, 0, 0);
    redMat->SetPreLit(true);

    RndMesh* redQuad = Hmx::Object::New<RndMesh>();
    redQuad->SetName("red_quad", dir);
    redQuad->SetMat(redMat);
    SetQuadGeometry(redQuad, -2, 0, 1, 1);  // helper: 4 verts, 2 faces
}
```

## Golden Screenshots

Once the renderer is correct, save golden PNGs to `native/test_data/render_test/`:

```
native/test_data/render_test/
├── solid_quads.golden.png
├── alpha_blend.golden.png
└── ...
```

A future `--compare` mode can diff against goldens and report pixel RMSE.

## Relationship to Existing Tests

- **milo-viewer screenshot tests** (`test_milo_viewer_screenshot.cpp`) — test the full pipeline with real `.milo` assets. Require DC3 asset library.
- **render-test** — tests the renderer in isolation with synthetic scenes. No external assets needed. Faster, more targeted.

Both are complementary: render-test catches renderer regressions, milo-viewer tests catch data loading / object wiring issues.
