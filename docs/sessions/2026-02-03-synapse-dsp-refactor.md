# Synapse_dsp.cpp Refactor Session

## Goal

Clean up raw m2c decompiler output in `Synapse_dsp.cpp` into proper C++ with typed members, correct namespaces, and meaningful names.

## Key Constraint

The base object file for this unit is a 4KB stub with NO code sections. Match percentages cannot be verified via objdiff. Validation limited to: build succeeds, object file size stays ~32KB, symbol names match.

## Changes Made

### Fixed namespace ordering
- Changed `namespace Synapse { namespace DSP {` to `namespace DSP { namespace Synapse {`
- Verified mangled symbols match: `?ProcessInPlace@Synapse@1DSP@@` and `??1Synapse@0DSP@@`

### Built proper class definition in `Synapse_dsp.h`
- Added typed members at correct offsets (0x00-0x74, total 0x78 = 120 bytes)
- Named members: `mInputBuffer`, `mDownsampledBuffer`, `mBufferIndex`, `mDefaultPitch`, `mDetectionInterval`, `mPitchDetector`, `mDetectedPitch`, `mPitchConfidence`, `mPitchClarity`, `mPitchThreshold`, `mGain`, `mPeakDetector`, `mChannelBuffers`, `mOutputBuffers`, `mVoices`, `mGranularSynth`, `mTargetPitch`, `mScratchBuffer1`, `mScratchBuffer2`
- Declared all Set*/Get methods from symbols.txt
- Forward-declared `PeakDetector`, `PitchDetector`, `GranularSynth`

### Created `PitchCorrectedVoice.h` with minimal class definition
- Needed because `vector<PitchCorrectedVoice>` requires a complete type
- Size = 0x38 (56 bytes) confirmed by voice iteration stride in ProcessInPlace

### Replaced `this+offset` casts with named member access
- Direct members (`mBufferIndex`, `mDetectedPitch`, etc.) accessed by name
- Vector internals accessed via `VEC_START`/`VEC_FINISH` macros (protected `_M_start`/`_M_finish` require cast-through)
- Kept raw offset access for opaque types (PitchDetector, PeakDetector, GranularSynth fields)

### Cleaned up .cpp
- Removed duplicate inline class definition from .cpp (now in .h)
- Removed stlpmtx_std forward declarations (no longer needed)
- Kept `extern "C"` wrappers as-is (converting to method calls is a separate task)

## Verification

- Build: clean, no warnings or errors
- Obj size: 31805 bytes (unchanged from baseline)
- Symbols: `ProcessInPlace@Synapse@1DSP`, `Synapse@0DSP` destructor both match symbols.txt
- Progress: slight increase (3854040 -> 3854612 code bytes matched)

## Files Modified

- `src/system/synth_xbox/Synapse_dsp.h` - Full class definition with typed members
- `src/system/synth_xbox/Synapse_dsp.cpp` - Refactored ProcessInPlace and destructor
- `src/system/synth_xbox/PitchCorrectedVoice.h` - New minimal class definition (0x38 bytes)

## Future Work

- Convert `extern "C"` wrappers to proper C++ method calls (requires adding method stubs to PeakDetector.h, PitchDetector.h, GranularSynth.h)
- Investigate `merged_OperatorDelete` / `OnlyReturns` in destructor - may be replaceable with `delete` / destructor calls
- Implement missing functions (Set* methods, constructor)
- Replace remaining raw offset access on opaque types with named struct members once those classes are defined
