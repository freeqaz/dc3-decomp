# MILO_NOTIFY_ONCE Macro Experiment

**Date**: 2026-02-04
**Status**: REVERTED - caused regressions

## Summary

Attempted to change `MILO_NOTIFY_ONCE` macro from using `DebugNotifyOncer` class to inline implementation. This caused match percentage regressions across multiple functions.

## The Change (DO NOT USE)

```cpp
// OLD (working, higher match %):
#define MILO_NOTIFY_ONCE(...)                                                            \
    {                                                                                    \
        static DebugNotifyOncer _dw;                                                     \
        _dw << MakeString(__VA_ARGS__);                                                  \
    }

// NEW (broken, lower match %):
#define MILO_NOTIFY_ONCE(...)                                                            \
    {                                                                                    \
        static std::list<String> _dw;                                                    \
        const char *_msg = MakeString(__VA_ARGS__);                                      \
        if (AddToStrings(_msg, _dw)) {                                                   \
            TheDebug.Notify(_msg);                                                       \
        }                                                                                \
    }
```

## Regressions Caused

| Function | Before | After | Drop |
|----------|--------|-------|------|
| EndCmd | 100% | 92.3% | -7.7% |
| Normalize | 99.97% | 98.5% | -1.5% |
| SetBone | 98.97% | 95.9% | -3% |
| FracToSample | 96.27% | 95.2% | -1% |
| PlayNormal | 94.53% | 93.2% | -1.3% |

## Key Differences

1. **OLD**: Uses `DebugNotifyOncer` class (contains `std::list<String> mStrings` as member)
2. **NEW**: Uses naked `static std::list<String>` directly

3. **OLD**: `TheDebugNotifier << cc`
4. **NEW**: `TheDebug.Notify(_msg)`

## Conclusion

The original binary was compiled with the `DebugNotifyOncer` class pattern. Do not change this macro without verifying match percentages across all MILO_NOTIFY_ONCE call sites.
