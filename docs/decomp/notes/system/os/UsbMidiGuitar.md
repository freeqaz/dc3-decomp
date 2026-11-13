# Lane notes: `src/system/os/UsbMidiGuitar.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@UsbMidiGuitar@@SAXXZ`

`UsbMidiGuitar::Poll` -- moved verbatim from `src/system/os/UsbMidiGuitar.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 60-65):

```text
// w8-l: re-measured at 99.993570 normalized, still the only function short of
// 100% in this unit (8/9).  Confirmed NOT a behavioural bug: the image loads
// pgRaw[0xb] before pgRaw[0xc] and we load them the other way round, but the
// three terms are summed into the same value either way -- the byte loads are
// independent and every arithmetic instruction matches.  The three refutations
// recorded below still stand.
```

Inside the body (was lines 182-203):

```text
                // The three program-change bits are the top bit of bytes
                // 0xa/0xb/0xc (mProgramChangeBit0..2, the MSB of each
                // accelerometer byte). The target loads them 0xb, 0xc, 0xa; we
                // load 0xc, 0xb, 0xa -- the only two rows left in this function
                // (plus the two rlwinm forms that follow from them). Both sides
                // associate as (b + c) + a and emit the same two adds, so the
                // residual is purely which of the two byte loads is scheduled
                // first, and nothing said in source moves it.
                //
                // Measured inert (w3 lane): swapping the two terms,
                // parenthesising as a + (b + c).
                // Measured inert (w7-m): splitting into three accumulator
                // statements (`int pc = b; pc += c; pc += a;`) -- the lever that
                // pins /fp:fast float association does nothing for integers here.
                // Measured WORSE (w7-m, 99.994 -> 99.5): the named-bitfield
                // spelling `(mProgramChangeBit1 << 1) + (mProgramChangeBit2 << 2)
                // + mProgramChangeBit0`. `<< 2` still fuses into one rlwinm but
                // `<< 1` does not, costing srwi+slwi; the load order does NOT
                // change, which is what rules the term order out as the cause.
                // w18-d (99.994, same 4 rows): `bool : 1` program-change bits with
                // the named-field sum fuses all three rlwinm but still loads 0xc
                // first (98.5 with locals, 99.5 inline) -- reverted.
```
