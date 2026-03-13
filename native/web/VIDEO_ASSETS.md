# Web Video Assets

The web port should not decode Bink at runtime. Instead, convert `.bik` assets
offline into browser-native `.webm` sidecars and keep the relative path stable.

Example:

```text
orig-assets/extracted/videos/intro.bik
orig-assets/extracted/videos/intro.webm
```

That lets the runtime keep referring to logical `intro.bik` content while the
web path resolver swaps to `intro.webm`.

## Conversion Script

Use [`scripts/web/transcode_bink.py`](/home/free/code/milohax/dc3-decomp/scripts/web/transcode_bink.py):

```bash
python3 scripts/web/transcode_bink.py
```

Useful filters:

```bash
python3 scripts/web/transcode_bink.py --match 'videos/*'
python3 scripts/web/transcode_bink.py --match 'songs/*/*_prev.bik' --jobs 4
python3 scripts/web/transcode_bink.py --match 'world/shared/binks/*' --force
```

## Output

- Sidecars are written next to the source assets by default.
- A manifest is written to `orig-assets/extracted/web-video-manifest.json`.
- Entries include width, height, fps, duration, alpha, audio presence, and the
  generated `.webm` path.

## Encoding Choice

The script uses:

- `libvpx-vp9` for video
- `libopus` for audio when the Bink asset has an audio stream
- `yuva420p` for alpha-bearing sources like song preview clips

`WebM` was chosen instead of animated `WebP` because several Bink assets carry
audio, and the browser can stream `WebM` directly through its native media stack.
