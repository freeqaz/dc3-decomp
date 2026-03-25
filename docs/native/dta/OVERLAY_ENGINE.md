# DTA Overlay Engine

The native port uses a file overlay system to patch game DTA files without modifying the original game assets. This replaces in-place patching or migration scripts with a transparent, zero-risk mechanism.

## How It Works

### The Problem

DC3's data-driven behavior (menus, settings, UI flow, object schemas) is defined in `.dta` text files packed inside `.ark` archives. The native port needs to modify some of these files to add features like settings toggles, but users should never have to manually patch their extracted game assets.

### The Solution

A `native/dta/` directory in the repo mirrors the archive's path structure. When the engine opens a file, it checks the overlay directory first. If a matching file exists there, the engine reads from disk instead of the archive.

```
Engine requests: "ui/options/options_gameplay.dta"
  1. Check: does native/dta/ui/options/options_gameplay.dta exist on disk?
  2. YES → read from disk (overlay file)
  3. NO  → read from .ark archive (original game file)
```

### File Loading Path

The intercept happens in two functions in `native/src/platform/File_Native.cpp`:

**`FileIsLocal(filename)`** — determines whether a file is on disk or in the archive.

```
FileIsLocal("ui/options/options_gameplay.dta")
  → check overlay dir for this path
  → file exists → return true  (engine reads from disk)
  → file missing → return false (engine reads from .ark)
```

When `FileIsLocal` returns `true`, `NewFile()` sets the local flag (`mode |= 0x10000`) and creates an `AsyncFile` (disk read) instead of an `ArkFile` (archive read).

**`FileQualifiedFilename(filename)`** — resolves relative paths to absolute paths.

```
FileQualifiedFilename("ui/options/options_gameplay.dta")
  → overlay exists → "/path/to/repo/native/dta/ui/options/options_gameplay.dta"
  → no overlay    → "/path/to/data/ui/options/options_gameplay.dta"
```

### Full Flow

```
NewFile("ui/options/options_gameplay.dta", READ)
  → FileIsLocal() checks overlay dir
  → overlay exists: mode |= 0x10000 (local flag set)
  → UsingCD() is true, but mode has local flag → skip ArkFile
  → AsyncFile::New() reads from the resolved overlay path
  → engine gets our modified DTA, completely transparent
```

## Directory Structure

```
native/dta/
  ui/
    options/
      options_gameplay.dta    ← overrides archive's ui/options/options_gameplay.dta
  config/
    some_config.dta           ← overrides archive's config/some_config.dta
```

Paths mirror the archive structure exactly. Only files that need modification are present — everything else falls through to the archive.

## Properties

- **Zero user action** — overlays are bundled with the native port. No scripts to run, no assets to patch.
- **Original assets untouched** — the `.ark` archive is never modified. Removing an overlay file restores original behavior.
- **Git-tracked changes** — overlay files live in the repo under `native/dta/`. Every modification is a normal git diff.
- **Per-file granularity** — each overlay replaces one archive file completely. No partial patching or merge logic.
- **Transparent to engine code** — all other systems (DTA parser, UI panels, loaders) see no difference. They call `NewFile()` and get file contents back.

## Limitations

- Overlays replace the entire file, not individual sections. If the original DTA changes (different game version), the overlay must be updated to match.
- Only works for files loaded through `NewFile()`. Binary `.milo` files loaded through the archive's direct read path may need separate handling.
- The overlay directory path must be set before any DTA files are loaded during engine init.

## Adding a New Overlay

1. Find the original file in `orig-assets/extracted/` (reference copy of the archive contents).
2. Copy it to `native/dta/` at the same relative path.
3. Make your modifications.
4. Build and test — the engine picks it up automatically.

## Comparison to Alternatives

| Approach | User action | Risk | Tracking | DTA parser needed |
|----------|-------------|------|----------|-------------------|
| **File overlay** | None | Zero | Git history | No |
| In-place migration | Run script | Corrupts originals | Version file | Yes |
| Runtime TypeDef patching | None | Fragile C++ code | In source | No |

The overlay approach was chosen for safety and simplicity. It requires the least infrastructure and poses no risk to user assets.
