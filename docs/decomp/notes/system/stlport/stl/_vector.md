# Lane notes: `src/system/stlport/stl/_vector.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `_VECTOR_IMPL::size()`

`_VECTOR_IMPL::size` -- moved verbatim from `src/system/stlport/stl/_vector.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `_VECTOR_IMPL::size` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 180-201):

```text
  // w7-bk (2026-09-14): REFUTED -- do not "fix" the (0x0,0x4) OFFSET_SWAP here.
  // Four near-perfect callers (SuperEasyRemixer::LoadAllVariants 99.57,
  // SuperEasyRemixer::SaveSuperEasyMoveParents 98.44, ChallengeSystemJobs GetRows
  // 98.97, MoveGraph::FindVariantPair 98.10) show the image loading _M_start (+0)
  // before _M_finish (+4) inside this inlined size(), while the spelling below makes
  // MSVC load _M_finish first. Forcing _M_start into a named local was measured
  // binary-wide (full ninja, all 48,365 rows, report.json match_percent_normalized,
  // threshold 0.001) and is a large NET LOSS -- the extra local changes register
  // pressure at every one of the thousands of inlined size() sites:
  //   baseline                                                  31171 matched fns / 5,513,112 B / 48.4675%
  //   (a) pointer __s = _M_start; return size_type(_M_finish-__s);
  //                                        UP 4 / DOWN 388  ->  30851 matched fns / 5,281,516 B / 46.4315%
  //   (c) const pointer __s = _M_start, __f = _M_finish; return size_type(__f-__s);
  //                                        UP 4 / DOWN 720  ->  30554 matched fns / 5,205,912 B / 45.7668%
  // Neither spelling helped the four target callers meaningfully: (a) moved
  // LoadAllVariants +0.0084 and cost ChallengeSystemJobs GetRows -0.01; (c) crossed
  // SaveSuperEasyMoveParents to 100.0 but pushed LoadAllVariants DOWN to 99.16 and
  // broke 720 other rows (?RMaxSort@MessageTimer 100.0 -> 0.91). The load order is
  // decided by MSVC scheduling per call site, not by how size() is spelled here --
  // note LoadAllVariants already emits _M_start-first at its OTHER size() site
  // (offsets 0x2c/0x30) with the original spelling. Lane w7-bg separately refuted
  // the call-site form (`i < v.end() - v.begin()`, measured worse at 98.46).
```
