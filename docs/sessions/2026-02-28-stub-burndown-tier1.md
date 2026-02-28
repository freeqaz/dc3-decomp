# Stub Burndown — Tier 1 Results (2026-02-28)

## Summary

Resolved all 33 Tier 1 (single-stub unit) functions from the stub burndown plan.

**Progress: 94.4% → 97.5% COMPLETE** (+1,560 functions, +3.1pp)

## Key Finding

29 of 33 "reset" stubs were **false negatives** — the source implementations already existed and matched 100%. The reset script marked them as workable because they had ALTERNATENAME stubs in `link_glue.cpp`, but the actual source code was already compiled and matching. Running `run_recon` on each confirmed the match and re-reported them as COMPLETE.

## Actual New Code (4 functions)

| Function | Match | Notes |
|----------|-------|-------|
| `UsbMidiKeyboard::GetSustain` | 100% | Moved from inline const to out-of-line non-const |
| `UILabel::LabelStyle::~LabelStyle` | 100% | Empty dtor, fixed `ObjPtr` → `ObjDirPtr` in header |
| `FileMergerSort::operator()` | 95.7% AT_LIMIT | Addr reloc + r24↔r28 regswap |
| `pow(float, int)` | 82.8% AT_LIMIT | Float register swaps f1↔f13, f12↔f13 |

## Files Modified

- `src/system/os/UsbMidiKeyboard.h` — `GetSustain` declaration change
- `src/system/os/UsbMidiKeyboard.cpp` — `GetSustain` implementation
- `src/system/ui/UILabel.h` — `ObjPtr<UILabelDir>` → `ObjDirPtr<UILabelDir>`
- `src/system/ui/UILabel.cpp` — `LabelStyle::~LabelStyle() {}`
- `src/system/char/FileMergerOrganizer.cpp` — `FileMergerSort::operator()`
- `src/system/midi/MidiReader.cpp` — `pow(float, int)` implementation

## Implication for Remaining Tiers

Most of the 733 "reset" stubs likely already match from existing source. The batch was reset because they had ALTERNATENAME entries, but the source TUs were already compiling the correct symbols. **Running `batch_check` or `run_recon` on Tier 2-4 units should auto-resolve the majority without any code changes.**
