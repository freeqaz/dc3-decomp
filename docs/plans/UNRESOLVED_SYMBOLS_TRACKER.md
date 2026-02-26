# Unresolved Symbols Tracker

**Goal:** Drop `/FORCE:UNRESOLVED` from linker flags so all symbols resolve cleanly.

**Initial state (2026-02-26):** 726 unique unresolved symbols across 1017 LNK2001/LNK2019 errors.

**After data stubs (2026-02-26):** 85 unique unresolved symbols across 310 errors. **88% reduction.**

**COMPLETE (2026-02-26):** 0 unresolved symbols, 0 errors. `/FORCE:UNRESOLVED` dropped from linker flags. **100% resolution.**

## What We Did: Data Stub Approach

The root cause: Config B replaces split .obj with decomp .obj for Matching units. Split .objs export data with `lbl_*` names, decomp .objs export the same data with C++ COMDAT names. Cross-references from non-Matching split .objs use `lbl_*` names → unresolved.

**Fix:** `scripts/create_data_stubs.py` creates "data-stub" .obj files from split .objs:
1. Read each Matching unit's split .obj (COFF format)
2. Strip code sections (.text) — keep only data sections (.data, .rdata, .bss)
3. Write minimal COFF .obj with only data sections + `lbl_*` symbols
4. Link data stubs alongside decomp .objs

`tools/project.py` modified to auto-include `build/373307D9/data/*.obj` when the data stub exists.

**Results:** 299 data-stub .obj files, 19,925 data symbols. All 577 `lbl_*`, 3 `jumptable_*`, and 28 `__real@*` symbols resolved.

## Remaining: 85 Unique Unresolved

| Category | Count | Fix Strategy | Status |
|----------|-------|-------------|--------|
| `__unwind$*` EH records | 17 | Need split .obj contribution or stubs | TODO |
| Synth360/Synth methods | 14 | Audio subsystem — undecomped | TODO |
| CXAPOBase/XAPO SDK methods | 8 | Xbox audio SDK — need .lib | TODO |
| LEAPFX/NUISPEECH methods | 3 | Kinect audio — need .lib | TODO |
| STL template instantiations | 8 | vector push_back/deallocate/dtor | TODO |
| UIPanel virtual thunks | 5 | vtordisp thunks — need correct decomp | TODO |
| ObjRefConcrete\<MoveDir\> | 1 | Template instantiation | TODO |
| DSP::Synapse::PitchDetector | 3 | Audio DSP — need .lib or stubs | TODO |
| FxSend360 methods | 5 | Audio effects — undecomped | TODO |
| AutoGlitchReport::EndExternal | 1 | Timer subsystem | TODO |
| UILabel::Terminate | 1 | UI cleanup | TODO |
| CheatsManager global | 1 | Need decomp source or stub | TODO |
| ReverbConvertI3DL2ToNative | 1 | XAudio2 utility | TODO |
| LocalePanel::Entry dtor | 1 | Template instantiation | TODO |
| LevelData ctor | 1 | Audio level data | TODO |
| StandardEffect ctor | 1 | Audio template | TODO |
| CriticalSection dtor | 1 | Threading | TODO |
| aligned_vector dtor | 1 | Memory allocator | TODO |
| dynamic initializer | 1 | Static init for XAPO params | TODO |
| Misc STL destroy_range | 1 | Template instantiation | TODO |
| _poll_mapping_P | 0 | (resolved by data stubs) | DONE |

### Fix Strategies for Remaining

**Group A: Audio subsystem (~30 symbols)**
Synth360, FxSend360, XAPO, LEAPFX, DSP::PitchDetector — all from Xbox audio SDK. Options:
1. Link XDK audio .lib files (xaudio2.lib, xapofx.lib)
2. Create stubs in link_glue.cpp
3. Decomp the audio functions (significant effort)

**Group B: __unwind$ EH records (17 symbols)**
These are compiler-generated exception handler records. They come from functions in split .objs whose exception handlers reference code in Matching units. Options:
1. Include __unwind$ symbols in data stubs (currently filtered out)
2. Create stub EH entries

**Group C: Template instantiations (~12 symbols)**
STL vector/allocator specializations and ObjRef templates. Options:
1. Add explicit template instantiations in decomp source
2. Stub in link_glue.cpp

**Group D: UIPanel virtual thunks (5 symbols)**
Virtual dispatch thunks with vtordisp adjustments. Requires correct class hierarchy in decomp.

**Group E: Misc (gCheatsManager, UILabel::Terminate, etc.)**
Individual globals and methods. Stub or decomp.

## Config

Current linker flags in `config/373307D9/config.json`:
```json
"/FORCE:MULTIPLE"
```

`/FORCE:UNRESOLVED` has been removed. `/FORCE:MULTIPLE` retained for LNK4006 COMDAT duplicates (809 warnings, all harmless duplicate COMDATs).

## Build Integration

```bash
# Generate data stubs (run after dtk split, before ninja link)
python3 scripts/create_data_stubs.py

# Data stubs are auto-included by project.py when present at:
# build/373307D9/data/{unit_path}.obj
```

## Related Docs

- [CLEAN_LINK_PROJECT.md](CLEAN_LINK_PROJECT.md) — overall link project plan
- [JEFF_LINK_LIMITATIONS.md](../sessions/JEFF_LINK_LIMITATIONS.md) — jeff limitations
