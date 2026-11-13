# Lane notes: `src/lazer/net_ham/ChallengeSystemJobs.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetRows@@YAXAAVJsonConverter@@PBVJsonObject@@AAV?$map@VString@@V?$vector@VChallengeRow@@V?$StlNodeAlloc@VChallengeRow@@@stlpmtx_std@@@stlpmtx_std@@U?$less@VString@@@3@V?$StlNodeAlloc@U?$pair@$$CBVString@@V?$vector@VChallengeRow@@V?$StlNodeAlloc@VChallengeRow@@@stlpmtx_std@@@stlpmtx_std@@@stlpmtx_std@@@3@@stlpmtx_std@@AA_N@Z`

`GetRows` -- moved verbatim from `src/lazer/net_ham/ChallengeSystemJobs.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 290-296):

```text
// w14-e (98.97, 21 rows): the image keeps a 4-byte local at 0x54 that the
// inner compare loop writes with itRows' data pointer (`stw r11, 0x54(r31)`,
// a dead home store) and puts DateTime dt at 0x58.  Spelling the compare as
// rowIt->second[i] != it->second[i] without the two reference locals DOES
// produce a homed store, but of the wrong value and with a 0x140 frame (93.1).
// w18-e (98.97): `rowItRows[i] != it->second[i]` is 98.0 (frame +0x10);
// `rowIt->second[i] != itRows[i]` is 95.7 (homes &rowIt->second instead).
```


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/lazer/net_ham/ChallengeSystemJobs.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

File-level (was lines 362-409; no definition follows it):

```text
// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- ::GetRows, the ChallengeRow
// overload over map<String, vector<ChallengeRow> >, canonical 98.96517 /
// fuzzy 98.64179.  201 instructions and 804 B on BOTH sides (187 equal /
// 13 diff_arg / 1 insert / 1 delete).
// Placed at END OF FILE so that no line above it moves (this TU has no
// MILO_ASSERT at all).
//
// BEHAVIOUR: no divergence.  Both sides build the two element pointers the
// same way (`add r4,r29,itRows._M_start` and `add r3,r29,rowItRows._M_start`
// at idx 173/175, both EQUAL rows), call the same ?9ChallengeRow@@QBA_NABV0@@Z
// and write `bref` the same way.
//
// MECHANISM: the image keeps a 4-byte local at 0x54 that the inner compare
// loop writes with itRows' data pointer -- `stw r11, 0x54(r31)` at idx 174 --
// a DEAD home store: nothing anywhere reads 0x54 as a data pointer.  That
// slot pushes `DateTime dt` to 0x58 (idx 101/110/112) where ours has dt at
// 0x54.  The downstream consequence is one register: the image keeps the
// loop-count quotient in r10 (idx 167/168), so r11 still holds
// itRows._M_start and no reload is needed; we clobber r11 with the quotient
// and must reload it at idx 171/172.
// 10 spellings measured this wave: `i < itRows.size()` 97.90050 (and three
// instructions short), `unsigned int i`, an index hoisted out of the for, and
// `unsigned int timeStamp` declared after dt, all byte-identical; swapping the
// two reference-local declarations byte-identical; `itRows.size() !=
// rowItRows.size()` 98.59204 fuzzy.
//
// THE REFUSAL -- AND THE RULE IT ESTABLISHES, which is the transferable part:
//   `if (itRows[i] != rowItRows[i])` measures canonical 98.97015 / fuzzy
//   98.67164 -- HIGHER ON BOTH RULERS THAN THE BASELINE -- and it is WORSE.
//   The image passes arg3 = rowItRows[i] and arg4 = itRows[i]; that spelling
//   swaps the two arguments of the compare.  The row counts do not move at
//   all (187 equal / 13 diff_arg before AND after), and the register numbers
//   at the two `add` rows stay identical because each is built from
//   `_M_start` on both sides -- but the VALUES in r10 and r11 swap underneath,
//   and the ruler cannot see it.  The score rises only because the
//   pre-existing 13 mismatches RELOCATE: what was a displacement-plus-register
//   charge at idx 171 becomes a register-only charge, and the displacement
//   charge moves onto the insert/delete pair.
//   RULE: a spelling that changes which ARGUMENT carries which value is not
//   adjudicated by the score.  Charges attach to ROWS, so a swap that leaves
//   the row count fixed moves the charge instead of removing it.  Check the
//   argument binding against the listing, and require the equal/diff_arg
//   counts to actually fall -- never merely that the number rose.  This is the
//   same failure as C2RS-BRIDGE's "the canonical metric is not monotone" and
//   "a gain that reorders side-effecting calls is not a gain", seen from a
//   third angle: here the call is in the same place, only its operands moved.
// ---------------------------------------------------------------------------
```
