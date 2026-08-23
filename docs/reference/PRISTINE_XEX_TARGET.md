# The match target is now the pristine debug XEX (2026-08-23)

`orig/` is gitignored, so this file is the only in-repo record of what image
the target objects are split from. Read it before touching anything in
`orig/373307D9/`.

## What changed and why

From the project's start until 2026-08-23, `orig/373307D9/default.xex` was a
**hand-patched** copy of the Sep 16 2012 final debug build — 130 bytes modified
across 4 regions, apparently to run the game off a devkit HDD (`UPDATE:` device
paths rewritten to `D:`, plus one function stubbed). Every copy the repo ever
had (`orig-assets/debug.xex`, `orig-assets/default_debug.xex`) is that same
patched variant; no in-repo provenance records who patched it or when.

The pristine image surfaced on 2026-08-22 in the hiddenpalace.org "Dance
Central 3 (September 16th, 2012 Final Debug Build)" archive. Same build (map
timestamp `505565e0`, identical `ham_xbox_r.map` modulo CRLF), unpatched. It is
now the split input, because matching decompiled source against patched bytes
is matching against something the compiler never emitted.

## The images

| file | sha1 | size | role |
|---|---|---|---|
| `orig/373307D9/default.xex` | `07d92deed845cae6ff3c18a50c6a9b0e84f5f74d` | 16,891,904 | **pristine** — split input (`config/373307D9/config.yml` `object:`) |
| `orig/373307D9/default_patched_devkit.xex` | `9975673b8ba30696f7c5f5e07b547599da6c5cee` | 16,887,808 | patched devkit variant, kept for reference/boot fallback |
| `orig/373307D9/ham_xbox_r.exe` | `7cae3d8cd8cdbb08b020caa0528269f12ea05aef` | 17,283,584 | PE extracted from the pristine XEX (dtk writes it on every split) |
| `orig/373307D9/ham_xbox_r_patched_devkit.exe` | `d4c17a00627ad8b0950735767df3fda72c5752b1` | 17,283,584 | PE of the patched variant |

`orig-assets/debug.xex` and `orig-assets/default_debug.xex` are additional
copies of the **patched** image (`9975673b…`) and were left as found.

Off-site copy of the source archive:
`b2://halo-protos/archives/hiddenpalace/Dance Central 3 (September 16th, 2012 Final Debug Build).zip`
(readable via the `decomp-cold-mount.service` mount at `~/mnt/decomp-cold`).

## The 130-byte patch, exactly

PE file offsets in `ham_xbox_r*.exe`; VAs are image base `0x82000000` +
section mapping.

| PE offset | VA | unit | pristine | patched |
|---|---|---|---|---|
| `0x7fbe4` | `0x8207fbe4` | os:ContentMgr_Xbox (rdata) | `"UPDATE:"` | `"D:"` + zero-fill |
| `0x141b2c`–`0x141bed` | `0x82141b2c`… | nuiapi:nuiruntime (4 strings) | `"update:\…"` | `"D:\…"` + zero-fill |
| `0x1badb8` | `0x821badb8` | nuispeech:xspeechapi | `"UPDATE:\nuisp%d"` | `"D:\nuisp%d"` |
| `0x5d73f0` | `0x825e29f0` | os:PlatformMgr | `7d8802a6` (`mflr r12`) | `4e800020` (`blr`) |

The last row is the first instruction of
`?SetDiskError@PlatformMgr@@QAAXW4DiskError@@@Z`. With the patched image as
target, `SetDiskError` was permanently capped at 98.145% and unit
`system/os/PlatformMgr` at 41/42 matched — the decomp source was correct and
the target was wrong. The raw XEXs also differ by 4,096 bytes of container
size; the payload delta is exactly these 130 bytes.

## What the re-split changed (measured 2026-08-23)

`dtk xex split` against the pristine image rewrote exactly 4 of 2,223 target
objects — `system/os/ContentMgr_Xbox.obj`, `system/os/PlatformMgr.obj`,
`xdk/nuiapi/nuiruntime.obj`, `xdk/nuispeech/xspeechapi.obj` — and left the
other 2,219 byte-identical. Splitting is deterministic; any target-obj diff
traces to the input image.

Note the patched bytes reached the **runnable** build through two of these:
`nuiruntime.obj`/`xspeechapi.obj` link directly from `build/…/obj/`, and the
`build/…/data/` supplement stubs are carved from the split objects. The linked
`default.exe` therefore now carries `update:`/`UPDATE:` device paths where it
previously carried `D:` ones. PlatformMgr/ContentMgr code was never affected —
those units link from decomp-compiled `src/` objects, which always had the
pristine behavior (`ContentMgr_Xbox.h`'s `ContentPath` returns `"UPDATE:"`).

## Link rot found on the way (fixed 2026-08-23, same branch)

The first relink after the re-split failed with 37 unresolved externals. None
traced to the image swap (the 4 changed target objects export identical symbol
tables); all traced to `config/373307D9/symbols.txt` renames made after the
last successful link, with three downstream consumers never updated:

1. **Stripped names.** Commit `7274dd67a` renamed 1,427 symbols to `fn_*`,
   including `sprintf` (0x829A2760) and `_snprintf` (0x829A1AD0) — the split
   LIBCMT objects stopped exporting the names every caller uses. Restoring
   the names in symbols.txt does NOT stick: `dtk xex split` rewrites
   symbols.txt on every run and reverted both renames within one split
   (measured 2026-08-23 — the stamp hash came back identical to pre-edit).
   The 7274dd67a strip has the same signature and was likely a committed
   write-back, not a deliberate edit. Worked around with
   `/ALTERNATENAME:sprintf=fn_829A2760` (and `_snprintf`) in link_glue; the
   durable fix is in jeff, not this repo.
2. **ICF fold groups.** New source (vtable sweep, XAPO base cleanup) references
   names that fold to a shared body in the original (`OnSetParameters`×12 →
   0x82E44240, trivial stubs → `OnlyReturns` at 0x823E3B70, etc.). Only one
   name can live at a fold address, so the rest need `/ALTERNATENAME` aliases —
   added to `src/link_glue.cpp`, each with its fold-address evidence. The five
   `??$`-mangled BinStream `operator<<` instantiations cannot be aliased and
   got compiled specializations instead (fold body 0x82793CA8: write the name).
3. **Renamed statics.** Commit `391d1b080` named curl's `initialized`
   (0x82F63AF8), which moved it out of the `lbl_*` re-export path in
   `scripts/create_data_stubs.py`; the easy.c data stub's reloc dangled.
   Parked on `__link_glue_zero` with a note — the durable fix is teaching the
   stub generator to re-export renamed statics.

Also: `RtlDeleteCriticalSection` is not in the original import table and the
original emitted no `~CriticalSection` at all — the decomp destructor's call is
an invention with nothing to bind to (aliased to noop); `getenv` folded to the
return-0 group in the original (aliased to `curl_getenv`, which returns 0).

## If boot regresses under xenia

The `blr` stub suppressed `PlatformMgr::SetDiskError`; the devkit `D:` paths
redirected content lookups. If a rebuilt XEX hits a disk-error halt or a
missing `update:` device:

1. Do **not** re-patch the split input — the match target stays pristine.
2. Apply runtime patches on the xenia side (manifest/POKE), or add an opt-in
   patch step to `scripts/build/build_xex.py`, which already takes
   `--original-xex orig/373307D9/default.xex` as the container template.
3. The patched image remains at `orig/373307D9/default_patched_devkit.xex` for
   byte-level reference.

## Regenerating after an image change

```sh
rm -f build/373307D9/config.json && ninja build/373307D9/config.json
# data stubs + obj patch chain read the split outputs but are stamp-guarded:
rm -f build/373307D9/{data_stubs,anon_ns_patched,dynamic_init_patched,guard_patched,bool_mangle_patched,atexit_scope_patched,objs_patched_verified}.stamp
ninja build/373307D9/report.json
```

`scripts/verify_split_current.py` records `xex_size` in
`build/373307D9/split_inputs.stamp` (now 16,891,904); a stale stamp fails
`--check` until the split re-runs.
