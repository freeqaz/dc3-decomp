# Session: JobMgr + FftIpp Fixes (2026-02-03)

## JobMgr CancelJob: 78.8% → 99.8%

Fixed three real bugs in `CancelJob` (src/system/utl/JobMgr.cpp):

1. **Loop structure**: Changed `for` to `while` — target does a pre-check before entering the loop body, which `while` generates correctly.

2. **Inner loop was wrong**: Old code had `for (it2 = begin(); it2 != it; ++it2) (*it2)->Start()` — iterating from front to erased position. Target only calls `Start()` on the **single next job** returned by `erase()`. Semantically correct: if you cancelled the front job, just start the new front.

3. **`frontID` not `curID`**: Old code read `job->ID()` (the found job's ID, always == id — no-op check). Target reads `mJobQueue.front()->ID()` — checking whether the *front* job was the one cancelled before deciding to start the next.

Remaining 0.2% gap: ICF merged `MakeString<int>` — unfixable linker artifact.

## JobMgr Poll: 98% (confirmed at limit)

Single dead `mr r10, r11` instruction in target. All 50 of our instructions match. Compiler register allocation artifact.

## FftIpp.cpp Build Fix

- `s32 temp_size = malloc(...)` → `void *temp = malloc(...)` (type error)
- `s32` return type → `int` (mangling: `s32` is `signed long` = `J`, target expects `int` = `H`)
- Moved `fft_matrix_inverse_columnwise` from FftIpp.cpp to FFT.cpp (target address 0x82E4E6B8 falls in FFT.cpp's .text range per splits.txt)

## fft_matrix_inverse_columnwise: 19.6% — Needs Major Work

- **Target**: 1160 bytes (290 instructions), heavy VMX128 vector operations
- **Current impl**: 256 bytes — just the final column-wise FFT loop
- **Missing**: Entire matrix transpose with twiddle factors, VMX128 permute/multiply operations, sin/cos calls, dual FFTComplex calls per iteration
- **Location**: `src/system/synth_xbox/FFT.cpp` (bottom of file)
- **Symbol**: `?fft_matrix_inverse_columnwise@@YAHPAMJ0@Z`
- **Target address**: 0x82E4E6B8, size 0x488
- **Companion**: `fft_matrix_forward_columnwise` exists above it in FFT.cpp (also complex VMX128 code)
- **Key externals**: `FFTComplex`, `sin`, `cos`, VMX constants (`__vmx_3f800000bf800000...`, `__vmx_00000000...`), float constants (`__real_4000000000000000`, `__real_3f800000`, `__real_40490fdb`)

## Files Changed

- `src/system/utl/JobMgr.cpp` — CancelJob rewrite
- `src/system/utl/JobMgr.h` — restored from clobbered state (staged removal reverted)
- `src/system/synth_xbox/FftIpp.cpp` — removed misplaced function, fixed build
- `src/system/synth_xbox/FFT.cpp` — added fft_matrix_inverse_columnwise skeleton
