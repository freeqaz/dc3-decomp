# Dropped static initializers: the class objdiff cannot score

**dc3-decomp (title 373307D9).** A static whose declaration lost its initializer
lands in our `.obj`'s `.bss` and reads zero at runtime, while the shipped image
defines it in `.data`/`.rdata` with real content. The instruction streams on both
sides are identical, so **objdiff scores these at 100% and always will** — it
scores code and never asks what a static's initial bytes were.

Every fix in this class is a pure behaviour fix with a guaranteed **zero** change
to the match percentage. Do not judge the work by the metric.

## Finding them

```sh
python3 scripts/analysis/bss_initializer_scan.py
```

A symbol we place in a section flagged `IMAGE_SCN_CNT_UNINITIALIZED_DATA` whose
target counterpart holds **nonzero** bytes is a dropped initializer.

The discriminator has to be **content**, not section name. The target objects
under `build/373307D9/obj/` are dtk splits of the shipped image, so *every*
section carries raw bytes — including the one literally named `.bss`. Matching on
the name finds nothing.

## The census: exactly ten, all fixed

| symbol | ours | target | what the wrong value did |
|---|---|---|---|
| `UIComponent::sSelectFrames` | 0 | 10 | |
| `RndMesh::sLastCollide` | 0 | −1 | |
| `gPendingResponse` | `kVersion` | `kInvalidOpcode` | |
| `gEvent`, `gVoiceThread` | `NULL` | `INVALID_HANDLE_VALUE` | |
| `Voice::sHeadsetTarget` | 0 | −1 | |
| `gTempPortraitOffset` | 0.0f | 0.125f | |
| wordwrap `g_uOption` | 0 | 1 | |
| `CharClipDisplay::sZoom` | 0.0f | 1.0f | `16.0f / sZoom` returned ±inf |
| `DxRnd::CopyPostProcess::sCopyPostInited` | false | **true** | guarded block never ran (see below) |
| `TheLocale.mInitialized` (+0x1c) | 0 | **1** | skipped the entire locale load |
| `CSampleXAPOBase<SynapseAPO,…>::m_regProps` | all zero | full struct | APO registered with a null CLSID |

Ten is the whole binary. The scan returns zero hits as of 2026-08-19; if it ever
returns more, someone added a declaration without its initializer.

## Two traps this class sets

**A guard flag that starts `true` is not a latch.** `sCopyPostInited` reads

```cpp
static bool sCopyPostInited = true;
if (sCopyPostInited) {
    sCopyPostInited = true;   // redundant store, not a one-shot
    ...
}
```

Started at `true` the body runs on *every* call; started at `false` it is dead
code. Both compile to the same instructions — `lbz` / `cmplwi` / `beq`, then
`stb r29` inside the taken arm — so nothing but the data bytes distinguishes
"runs always" from "never runs". Read the initial value out of the image before
you reason about a flag's meaning.

**"Uninitialized, matches the original anyway" is usually a missing initializer.**
`Locale::Init` gates the whole locale load on `mInitialized`, and the source
carried a comment asserting that member was an uninitialized read the original
got away with. It is not: the image holds `0x01` at `TheLocale+0x1c`. Any comment
in this codebase claiming a global "happens to be nonzero" is a scan hit that was
rationalised instead of measured.

## Restoring an initializer without moving code

For a scalar, adding `= value` needs no guard and cannot move an instruction.

For a member of a global with a constructor, put it in the member-init list:
MSVC folds the constant into `.data` and leaves only the non-constant stores in
`??__E<name>`, so the dynamic initializer's instruction stream is unchanged.
`Locale() : mInitialized(true) {}` kept `??__ETheLocale` at 8 instructions, all
equal.

Verify all three of: the symbol moved out of `.bss`, its bytes equal the target's,
and `run_objdiff` reports the same mismatch count as before. A rendered "100.0%"
is not byte-identity — compare counts.

## Open: MSVC's static/dynamic split for large aggregates

`m_regProps` (0x42c bytes) is the one place the values now match but the *layout*
does not. The original folds only the leading 0x24 bytes (clsid + the nine chars
of `L"SampleAPO"`) into `.data` and emits a 144-byte
`??__E?m_regProps@…@YAXXZ` that memsets the `FriendlyName` tail, memcpys the
pooled `??_C@_1FA@MJNECBMC@` copyright literal into `CopyrightInfo`, memsets that
tail, and stores the seven trailing `UINT`s.

Our `cl.exe` — the same binary, the same `/O1 /Oi /EHsc /TP` — folds all 0x42c
bytes and emits no dynamic initializer. Falsified so far:

* **not a literal-length threshold** — a 160-character copyright string still folds;
* **not explicit-specialization vs primary-template definition** — both fold.

`src/link_glue.cpp` still `/ALTERNATENAME`s the absent `??__E?m_regProps` for this
and every sibling effect, so recovering the split is worth ~144 B per effect.
