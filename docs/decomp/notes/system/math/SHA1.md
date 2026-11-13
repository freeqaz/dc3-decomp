# Lane notes: `src/system/math/SHA1.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/math/SHA1.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

File-level (was lines 11-15; nearest following definition: `Sha1Bswap32`):

```text
// w22-a01: retail is Dominik Reichl's CSHA1, whose ROL32 is MSVC's _rotl
// intrinsic under _MSC_VER (rb3-xenon c0254f3cf took Transform to 100 with this
// spelling, the SHABLK operand order below and natural a..e / pState[0..4]
// order).  _rotl and the shift-or idiom compute the same 32-bit rotate; the
// intrinsic only changes MSVC's register allocation.  Native keeps shift-or.
```


## `?Transform@CSHA1@@AAAXPAIPBE@Z`

`CSHA1::Transform` -- moved verbatim from `src/system/math/SHA1.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 60-67):

```text
// w22-a01 (2026-10-08): 61.99 -> 100 (1464/1464 rows equal) with rb3-xenon's
// shape -- the _rotl intrinsic above, SHABLK operand order, natural a..e and
// pState[0..4] order.  This REFUTES the earlier "floor certificate" (w8-o)
// that put the 57 extra `mr` copies down to unreachable register allocation:
// they come from the intrinsic.  Behaviour was already exact (unicorn, 2026-08-19:
// SHA-1("abc") = a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d on both sides);
// the native arm was re-checked on a host harness against the FIPS 180 vectors
// ("abc", "", the 448-bit message, one million 'a').
```
