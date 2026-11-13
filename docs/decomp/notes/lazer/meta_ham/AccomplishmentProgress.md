# Lane notes: `src/lazer/meta_ham/AccomplishmentProgress.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AddAccomplishment@AccomplishmentProgress@@QAA_NVSymbol@@@Z`

`AccomplishmentProgress::AddAccomplishment` -- moved verbatim from `src/lazer/meta_ham/AccomplishmentProgress.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 328-354):

```text
// w8-l: 99.989190 normalized, the only function short of 100% in this unit.
// Two rows, both one stack slot apart:
//   [19] stw  r31, 0x58(r1) (target) vs 0x54 (ours)
//   [21] addi r4,  r1, 0x58 (target) vs 0x54 (ours)
// In the image 0x54 holds only the _M_find key temp; 0x58 is shared by the
// MILO_NOTIFY temp, `award`, and the category/group award temps.  We pool the
// notify temp down into 0x54.  Refuted: introducing `const char *accName =
// s.Str();` before the MILO_NOTIFY was exactly inert (99.989190 unchanged).
// See the note on PlaylistSongProvider::DataSymbol for the shared family.
// w16-e (99.989): dropping the `else` after the !pAcc early return (block
// kept) is inert; an early `if (IsAccomplished(s)) return false;` is worse
// (97.26).
// w21-ar (99.989, same 2 rows [19]/[21]; name_check charges nothing else --
// the target-only MakeString/_M_find/Parent/PostDownload callees are ICF
// folds of byte-identical bodies).  Inert: IsAccomplished spelled
// `find(s) != end()` instead of count(s).  Worse (87.0, block order flips):
// `if (pAcc) { ...; return true; } MILO_NOTIFY(...);` with one shared
// `return false`.
// w21-bn: CLOSED, 99.989 -> 100 (185/185 rows equal).  The `else` after the
// !pAcc early return is gone AND its braces with it, so `category`/`group`
// live in the same scope as the MILO_NOTIFY's block.  Their scope now
// overlaps the notify temp, so MSVC can no longer pool it into 0x54 (the
// IsAccomplished key / `group` slot) and it lands on 0x58 with `award` and
// the assert ints, exactly as in the image.  w16-e's "drop the else, keep
// the block" was inert because the block kept the scope.  Same behaviour.
// Not the outer test: `if (IsAccomplished(s)) return false;` still costs
// 97.3 (block order flips).
```
