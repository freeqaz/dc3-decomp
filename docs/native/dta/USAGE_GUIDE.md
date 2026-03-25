# DTA Overlay Usage Guide

Practical guide to adding native-only features via the DTA overlay system. For the design and architecture, see [OVERLAY_ENGINE.md](OVERLAY_ENGINE.md).

## Quick Start

To add a new overlay file:

1. Extract the original from the archive (if not already in `orig-assets/extracted/`):
   ```bash
   # Files are at their archive-relative path, e.g.:
   cat orig-assets/extracted/ui/options/options_gameplay.dta
   ```

2. Copy to the overlay directory at the same path:
   ```bash
   mkdir -p native/dta/ui/options/
   cp orig-assets/extracted/ui/options/options_gameplay.dta native/dta/ui/options/
   ```

3. Edit the overlay copy. The engine picks it up automatically — no rebuild needed for DTA-only changes.

4. To revert, delete the overlay file. The engine falls through to the archive original.

## Current Overlays

| Overlay File | What It Does |
|---|---|
| `native/dta/ui/options/options_gameplay.dta` | Adds "Camera Blend" toggle to Gameplay Settings menu |

## Adding a Settings Toggle (Walkthrough)

The Camera Blend toggle is the reference implementation. Here's the pattern for adding more native-only settings.

### 1. Add the handler to NativeProfileMgrStub

In `src/App.cpp`, the `NativeProfileMgrStub` class handles DTA queries from settings panels. Add a getter and toggler:

```cpp
// In NativeProfileMgrStub::Handle()
if (sym == "get_my_setting") return DataNode(NativeSettings::Get().mySetting ? 1 : 0);
if (sym == "toggle_my_setting") {
    NativeSettings::Get().mySetting = !NativeSettings::Get().mySetting;
    return DataNode(0);
}
```

### 2. Add the setting to NativeSettings

In `native/src/platform/NativeSettings.h`:

```cpp
struct NativeSettings {
    bool mySetting = true;  // default value
    // ...
};
```

### 3. Add locale strings

In `src/App.cpp`, in the MagnuStrings injection block (inside `#ifdef HX_NATIVE`), grow the DataArray and add entries:

```cpp
DataArray *nativeLocale = new DataArray(N);  // increase N
// ... existing entries ...
DataArray *myLabel = new DataArray(2);
myLabel->Node(0) = DataNode(Symbol("option_my_setting"));
myLabel->Node(1) = DataNode("My Setting Label");
nativeLocale->Node(idx) = DataNode(myLabel, kDataArray);
myLabel->Release();
// ... similarly for option_my_setting_desc ...
```

MagnuStrings are checked first by `Locale::Localize()`, English-only, and don't require modifying any locale `.dta` files.

### 4. Add to the options DTA overlay

In `native/dta/ui/options/options_gameplay.dta`, add to these four sections:

**`update_provider`** — append the nav item after the provider loads:
```dta
{[provider] append_nav_item}
{[provider]
   set_label
   {- {[provider] num_data} 1}
   option_my_setting}
{[provider]
   set
   (nav_items
      {- {[provider] num_data} 1}
      checkbox)
   1}
```

**`NAV_SELECT_MSG`** — handle selection:
```dta
(option_my_setting
   {profile_mgr toggle_my_setting}
   {$this update_all}
   skip_select_anim)
```

**`update_checks`** — sync checkbox state:
```dta
{[provider]
   set_checked
   option_my_setting
   {profile_mgr get_my_setting}}
```

**`update_description`** — show description on highlight:
```dta
(option_my_setting
   {description.lbl set text_token option_my_setting_desc})
```

### 5. Wire up the setting

Use `NativeSettings::Get().mySetting` wherever the feature's behavior is controlled (e.g. in a render pass, game loop, etc.).

## DTA Syntax Reference

Quick reference for patterns used in overlay files:

| Pattern | Meaning |
|---|---|
| `{[provider] append_nav_item}` | Add empty nav item to end of provider |
| `{[provider] num_data}` | Get current item count (UIListProvider handler) |
| `{[provider] set_label IDX SYMBOL}` | Set label for item at index |
| `{[provider] set (nav_items IDX checkbox) VAL}` | Set checkbox mode (0=none, 1=unchecked, 2=checked) |
| `{[provider] set_checked SYMBOL BOOL}` | Set checkbox state by label symbol |
| `{[provider] set_enabled SYMBOL BOOL}` | Enable/disable item by label symbol |
| `{profile_mgr HANDLER}` | Call NativeProfileMgrStub handler |
| `{description.lbl set text_token SYMBOL}` | Set description label to locale token |

## How Overlay Detection Works

At startup:
1. `NativeDetectDataDir()` finds the game data (typically `orig-assets/`)
2. `NativeDetectOverlayDir()` looks for `native/dta/` relative to the data dir or repo root
3. Both run before `SystemPreInit()` and archive initialization

The overlay dir is optional — if not found, the engine logs a message and continues normally.

## Testing

```bash
# Build
cmake --build native/build --target dc3-native -- -j$(nproc)

# Run from repo root (overlay dir auto-detected)
./native/build/dc3-native

# Verify overlay is picked up (check startup log)
# Should see: "DC3 Native: overlay dir=orig-assets/../native/dta"

# Negative test: rename overlay, verify original behavior
mv native/dta/ui/options/options_gameplay.dta native/dta/ui/options/options_gameplay.dta.bak
./native/build/dc3-native
# Menu should show original 5 items, no camera blend toggle
mv native/dta/ui/options/options_gameplay.dta.bak native/dta/ui/options/options_gameplay.dta
```

## Caveats

- Overlay files replace the **entire** archive file, not individual sections. Keep them in sync with the game version.
- Only works for files loaded via `NewFile()`. Binary `.milo` files use separate load paths.
- DTA changes don't require a C++ rebuild, but locale string changes (MagnuStrings) do.
- The `{- {[provider] num_data} 1}` pattern for dynamic indexing works because `num_data` is evaluated at runtime after `append_nav_item` has already grown the list.
