# Lane notes: `src/system/hamobj/HamDriver.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@HamDriver@@UAAXXZ`

`HamDriver::Poll` -- moved verbatim from `src/system/hamobj/HamDriver.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 101-106):

```text
    // NOTE (w7-av): the old "bootstrap" hack that used to live here -- one
    // forced mLayers.Eval(1.0f) when mLayers.mWeight was still 0 -- existed
    // only to compensate for the mis-decompiled guard below (see the NOTE on
    // the `if`).  The guard now reads CharWeightable::Weight(), which the ctor
    // does initialise, so there is nothing left to bootstrap and the forced
    // Eval would just fire every frame for every idle driver.
```

Inside the body (was lines 108-122):

```text
    // NOTE (w7-av): BEHAVIOURAL FIX.  The gate and the Eval() argument are
    // CharWeightable::Weight() (== mWeightOwner->mWeight), NOT mLayers.mWeight.
    // Image, 0x824BB61C..0x824BB630:
    //     lwz  r11, -0x8(r3)          ; mWeightOwner  (CharWeightable + 0xc)
    //     lfs  f1, 0x8(r11)           ; ->mWeight     (CharWeightable + 0x8)
    //     lfs  f0, __real@00000000
    //     fcmpu cr6, f1, f0 / ble
    // and f1 is then live, unclobbered, into the `bctrl` at 0x824BB648 -- the
    // LayerArray::Eval call takes that same value.  Only afterwards does the
    // image read the layer weight, at 0x824BB654 `lfs f13, 0x24(r31)`, for
    // ScaleDown's 1.0f - w.  That ordering is the proof: mLayers.mWeight is
    // LayerArray::Eval's *output* (it opens with `mWeight = 0;`), so gating on
    // it made the driver poll off its own previous-frame result -- Eval could
    // never restart once the accumulator reached 0, and the weight actually
    // set on the driver was ignored.
```
