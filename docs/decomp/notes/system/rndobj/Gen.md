# Lane notes: `src/system/rndobj/Gen.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetFrame@RndGenerator@@UAAXMM@Z`

`RndGenerator::SetFrame` -- moved verbatim from `src/system/rndobj/Gen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 191-195):

```text
            // w25-gh: instFrame and life are named, as in rb3's spelling.  Each
            // adds one lowering temp ahead of the `frame + mRateGenHigh` add
            // below; two of them move the mRateGenHigh load's base temp to a sid
            // that is 0 mod 4, so its sort key (0x10008) drops below frame's
            // (0x10060) and the add is emitted `fadds f0,f31,f0` as the image.
```
