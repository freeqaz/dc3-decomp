# Lane notes: `src/system/synth/Utl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CacheWav@@YAPBDPBDAAW4CacheResourceResult@@@Z`

`CacheWav` -- moved verbatim from `src/system/synth/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 53-82):

```text
    // RESIDUAL (7 rows, 93.3%): retail branches here --
    //   cmpwi r3,0 / ble / li r29,0
    // -- while MSVC if-converts our assignment into a branchless
    //   subic / srwi / subfze / and.
    // Measured NOT fixable by: dropping the `cacheResult` temp (inert), or
    // rewriting as two returns `if (>0) return nullptr; return sCacheWavBuf;`
    // (90.7%, 17 rows -- MSVC then duplicates the ~String call instead of
    // sharing the epilogue, which retail does share).
    // Also NOT fixable by giving the `if` an explicit empty first arm
    // (`if ((int)result <= 0) {} else { dst = nullptr; }`).  Retail's `ble` at
    // 0x82768AF4 lands on a block that holds nothing but `b` at 0x82768B08, so
    // an empty else-arm looked like the shape that produced it -- it is
    // byte-inert here (93.3%, the same 7 rows).  The arm-placement lever that
    // fixed ReadEditorDirDead does not reach an if-conversion decision.
    // w13-e, also inert (93.3%, same 7 rows): `while (r > 0) { dst = 0; break; }`,
    // `do { if (r <= 0) break; dst = 0; } while (0)`, a switch on the test, an
    // `else dst = sCacheWavBuf;` arm, and the ternary.  Every one if-converts.
    // w21-bc (93.3, same 7 rows; stopped). Standalone cl.exe probe, ~40
    // spellings. The ONLY shape that stops the if-conversion is a third arm:
    //   if ((int)result > 0) dst = nullptr;
    //   else if ((int)result < 0) dst = sCacheWavBuf;   // a no-op: dst already is
    // -> 98.6 in the real build, 2 rows left (retail's `ble` lands on an empty
    // `b` trampoline at 0x82768B08 we do not emit). NOT shipped: the extra arm
    // is an invented no-op statement; flagged to the coordinator instead.
    // Also inert here (all if-convert or duplicate ~String): strcpy() for the
    // copy loop (byte-identical loop, kept the original spelling), a block-
    // scoped String, `bool failed`, `0 < result`, `>= kCacheUnknownExtension`,
    // `!= kCacheUnnecessary && != kCacheSuccessful` (different code), an
    // if (!UsingCD()) { ... } wrapper with one return, `ret = 0; if (<= 0)
    // ret = dst;` (bgt + mr: different), for(;;)/break, goto to a label.
```
