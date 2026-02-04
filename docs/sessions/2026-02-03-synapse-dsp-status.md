# Synapse_dsp Status Report

Unit: `default/system/synth_xbox/Synapse_dsp`

## Implemented (3 functions, all 100%)

| Function | Size | Match |
|----------|------|-------|
| `ProcessInPlace` | 0x360 | 100% |
| `~Synapse` | 0xFC | 100% |
| `_M_insert_overflow` (vector<float*>) | - | 100% |

## Unimplemented (45 functions)

### Trivial SetVoice* one-liners (8 functions, 8-24 bytes each)

These delegate directly to PitchCorrectedVoice methods or write to GranularSynth fields:

| Function | Size | Notes |
|----------|------|-------|
| `SetVoiceTargetNote(uint, float)` | 0x14 | Writes to `mVoices[idx].field_0x4` |
| `SetVoiceGain(uint, float)` | 0x18 | Writes to `mGranularSynth->voices[idx].field_0x4` |
| `SetVoiceEnabled(uint, bool)` | 0x08 | Tail-calls `GranularSynth::SetVoiceEnabled` |
| `SetVoiceTransposition(uint, float)` | 0x10 | Calls `PitchCorrectedVoice::SetTransposition` |
| `SetVoiceAmount(uint, float)` | 0x10 | Calls `PitchCorrectedVoice::SetAmount` |
| `SetVoiceProximityEffect(uint, float)` | 0x10 | Calls `PitchCorrectedVoice::SetProximityEffect` |
| `SetVoiceProximityFocus(uint, float)` | 0x10 | Calls `PitchCorrectedVoice::SetProximityFocus` |
| `GranularSynth::SetVoiceEnabled(uint, bool)` | 0x88 | Standalone, not a one-liner |

### Smoothing methods (2 functions, 160 bytes each)

| Function | Size | Notes |
|----------|------|-------|
| `SetAttackSmoothing(float)` | 0xA0 | Calls `Time2IirA`, loops over voices calling `PitchCorrectedVoice::SetAttackSmoothing` |
| `SetReleaseSmoothing(float)` | 0xA0 | Same pattern, calls merged `PitchCorrectedVoice::SetReleaseSmoothing` |

### Constructor (1 function)

| Function | Size | Notes |
|----------|------|-------|
| `Synapse(float)` | 0x654 | Large, initializes all members, creates sub-objects |

### Anonymous namespace helper (1 function)

| Function | Size | Notes |
|----------|------|-------|
| `Time2IirA` | - | `?Time2IirA@?A0xa7b3dd7d@@YAMMM@Z` - static helper |

### STL template instantiations (~25 functions)

Vector boilerplate for `vector<PitchCorrectedVoice>`, `vector<vector<float>>`, `vector<float*>`:
- allocate/deallocate for StlNodeAlloc specializations (5)
- _Vector_base destructors (2)
- vector destructors (2)
- _M_erase, _M_fill_insert, _M_fill_insert_aux, _M_insert_overflow_aux (6)
- _M_clear_after_move (2)
- resize (2)
- __destroy_range_aux, _Destroy_Range (3)
- __uninitialized_fill_n, __uninitialized_copy (3)

### scoped_ptr destructors (3 functions, 0x40 bytes each)

- `scoped_ptr<PitchDetector>::~scoped_ptr`
- `scoped_ptr<PeakDetector>::~scoped_ptr`
- `scoped_ptr<GranularSynth>::~scoped_ptr`
- `scoped_ptr<Biquad>::~scoped_ptr`

## Symbols

All symbols for this unit are already in `config/373307D9/symbols.txt`. No additions needed.

## Code Quality Notes

The implemented ProcessInPlace and ~Synapse use:
- `VEC_START`/`VEC_FINISH` macros for raw vector pointer access (load-bearing for match)
- `extern "C"` with opaque `void*` for calls to untyped helper classes
- Magic offsets (0x30, 0x04, 0x0C, etc.) into GranularSynth, PeakDetector, PitchCorrectedVoice

These are ugly but achieve 100% match. Cleaning them up would require fully typing GranularSynth, PeakDetector, etc. - a larger effort that could be done alongside implementing the unimplemented functions.

## Implementation Priority

1. **SetVoice* one-liners** - trivial, quick wins
2. **SetAttackSmoothing / SetReleaseSmoothing** - medium, clear pattern from Ghidra
3. **Time2IirA** - needed by smoothing methods
4. **scoped_ptr destructors** - small, templated
5. **Constructor** - large but mechanical
6. **STL instantiations** - come for free once types are properly used
