#!/usr/bin/env python3
"""Fold-proof: decide FOLD vs WRONG-CALLEE for a claimed /OPT:ICF merge.

Ported into dc3-decomp from rb3-xenon's `tools/w25_fold_proof.py` on 2026-08-17,
generalised to (a) take every path as an argument -- three sibling decomp trees
(dc3-decomp, rb3-xenon, rb3) share this need and none of them may hardcode
another's title id -- and (b) resolve the two spellings out of DIFFERENT object
files, which the single-object original could not do.

WHAT "FOLD-PROOF" MEANS
======================
objdiff's `functionRelocDiffs=name_check` ruler charges a site when the
relocation there names a different symbol on the two sides.  A charge like

    target calls  ?OnGetOccluded@CamShot@@...
    ours   calls  ?OnSaveFaceAnims@HamDirector@@...

(a real dc3-decomp charge, on `?Handle@HamDirector@@`, gating 9,004 B)
has exactly two explanations, and they call for opposite actions:

  (a) FOLD -- the two are DISTINCT functions with IDENTICAL code that the
      linker's /OPT:ICF merged to one address.  The surviving name in the target
      is arbitrary among the fold class; our source is right and the charge is
      an artifact.  Remedy: an alias entry, so the ruler treats the names as
      equivalent.
  (b) OUR BUG -- the target really calls one function and our source calls a
      different one.  Remedy: fix the source.

THE DECISIVE TEST is /OPT:ICF's own condition, evaluated on OUR OWN objects:
two COMDATs fold iff they are

    * BYTE-identical, AND
    * RELOCATION-SET-identical -- the same relocation offsets, the same
      relocation TYPES, and the same target symbol NAMES.

Byte equality alone is NOT sufficient and this tool never accepts it.  Two `bl`
instructions to different callees are the SAME four bytes (the displacement is
supplied by the relocation), so a byte-only test certifies a wrong-callee bug as
a fold -- which is precisely the failure mode class (b) is.

WHAT THIS CANNOT RULE OUT, STATED PLAINLY
=========================================
Identity of OUR bodies is a fact about OUR build.  If the target's source for F
differed from the target's source for S, the target did not fold them and our F
is simply wrong in a way that coincidentally equals our S.  That is cheapest on
SHORT, relocation-free bodies, so this tool reports body size and relocation
count and REFUSES to certify a zero-relocation body.

THE FAIL-OPEN RISK OF A FABRICATED ALIAS
========================================
An alias installed into `scripts/symbol_aliases.json` tells the ruler "these two
names are the same symbol" FOREVER AND EVERYWHERE.  It does not close a gap --
it stops the gap from being MEASURED.  So an alias installed on a bad proof is
strictly worse than the bug it hides: the bug remains in the shipped code, the
percentage rises, and no later ruler run can ever surface it again.  That is
fail-open, and it is why this tool:

  * refuses on zero-relocation bodies (identity is too cheap to mean anything);
  * refuses when either spelling is missing from our objects (VACUITY GUARD --
    "not found" is not "identical");
  * compares relocation NAMES, not just counts;
  * separates PROVEN_FOLD from UNDECIDABLE instead of collapsing both to "not
    refuted".

A PROVEN_FOLD verdict here is a licence to PROPOSE an alias, with this proof
recorded as its evidence.  It is not a licence to install one silently.

USAGE
=====
    # one pair, resolving both spellings anywhere under our build objects
    python3 scripts/analysis/fold_proof.py \\
        --objects build/373307D9/src \\
        --pair '?OnGetOccluded@CamShot@@IAA?AVDataNode@@PAVDataArray@@@Z' \\
               '?OnSaveFaceanims@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z'

    # a batch of pairs from JSON: [[survivor, folded], ...] or
    # [{"target": s, "base": f, ...}, ...] -- extra keys are echoed through
    python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \\
        --pairs-json /tmp/pairs.json --json-out /tmp/verdicts.json

    # every member of an alias group must equal the group's survivor
    python3 scripts/analysis/fold_proof.py --objects build/373307D9/src \\
        --group SURVIVOR FOLDED1 FOLDED2 ...

VERDICTS
========
    PROVEN_FOLD   byte- AND relocation-set-identical, and the body carries at
                  least one relocation.  /OPT:ICF must merge these.
    PROVEN_MOD_ALIAS
                  identical only after relocation target names are canonicalised
                  through an ALREADY-ESTABLISHED fold class (--equiv-json).  A
                  strictly WEAKER tier -- see below.
    REFUTED       found on both sides and NOT identical.  The names are not
                  interchangeable; a name charge between them is a real bug.
    UNDECIDABLE   one or both spellings absent from the objects scanned, or the
                  bodies are identical but relocation-free (too cheap to prove).

WHY `PROVEN_MOD_ALIAS` EXISTS, AND WHY IT IS A SEPARATE TIER
============================================================
The strict relocation-NAME test is not transitively closed over fold classes
that are already known.  Two template instantiations such as

    operator<<(BinStream&, const list<IKTarget<CharIKHand>>&)
    operator<<(BinStream&, const list<ConstraintSystem<CharBlendBone>>&)

have identical bodies and identical relocation offsets/types, but each calls the
element-type `operator<<` for ITS OWN element type.  Those callees are a fold
class in their own right, so the two bodies really do fold -- yet a name-equality
test refutes them.  Measured on dc3-decomp: 265 of 2,611 map-CONFIRMED body-test
alias memberships are REFUTED by the strict test for exactly this reason.

`--equiv-json` canonicalises each relocation target through the fold classes in
`scripts/symbol_aliases.json` before comparing.  That is sound ONLY to the extent
those classes are sound, so it inherits their evidence rather than adding any:
a bad alias in the input can manufacture a `PROVEN_MOD_ALIAS` here, which would
then be cited to install another alias.  Never let that loop close -- treat
`PROVEN_MOD_ALIAS` as "consistent with a fold", never as an independent proof,
and never chain it into new alias installs without a second instrument (the
linker map, or the target's own bytes).
"""

from __future__ import annotations

import argparse
import collections
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis.coff_bodies import (  # noqa: E402
    IMAGE_REL_PPC_PAIR, function_bodies, iter_objects)

PROVEN = "PROVEN_FOLD"
PROVEN_MOD = "PROVEN_MOD_ALIAS"
REFUTED = "REFUTED"
UNDECIDABLE = "UNDECIDABLE"
VERDICTS = (PROVEN, PROVEN_MOD, REFUTED, UNDECIDABLE)


def load_equivalences(path):
    """symbol name -> canonical fold-class representative, from an alias JSON.

    Accepts dc3-decomp's `scripts/symbol_aliases.json` shape:
    ``{"groups": [{"survivor": str, "folded": [str, ...]}, ...]}``.
    """
    data = json.loads(Path(path).read_text())
    groups = data.get("groups", data) if isinstance(data, dict) else data
    canon = {}
    for g in groups:
        if not isinstance(g, dict):
            continue
        survivor = g.get("survivor")
        if not survivor:
            continue
        canon[survivor] = survivor
        for f in g.get("folded") or []:
            canon[f] = survivor
    return canon


def canonicalise(relocs, canon):
    """Rewrite each relocation's target name to its fold-class representative."""
    if not canon:
        return relocs
    return tuple(sorted((o, ty, canon.get(tn, tn)) for (o, ty, tn) in relocs))


class BodyIndex:
    """symbol name -> list of (obj_path, body_bytes, normalised relocs).

    Built once over a set of object roots and reused for every pair.  A symbol
    defined in more than one object (COMDATs are emitted into every TU that uses
    them) keeps every definition; a pair proves only when SOME definition on
    each side matches.
    """

    def __init__(self, roots, verbose=False, keep_pair_relocs=False):
        self.roots = [str(r) for r in roots]
        self.keep_pair_relocs = keep_pair_relocs
        self.by_name: dict[str, list] = collections.defaultdict(list)
        self.n_objects = 0
        self.n_slices = 0
        t0 = time.time()
        for obj in iter_objects(roots):
            self.n_objects += 1
            try:
                for name, body, relocs, _entry in function_bodies(obj):
                    self.n_slices += 1
                    # A type-18 PAIR record's "VirtualAddress" is a DISPLACEMENT,
                    # not an offset, so the `v <= o < end` slice filter in
                    # coff_bodies can keep it on one side and drop it on the
                    # other for two genuinely identical bodies.  It also carries
                    # no information the REFHI/REFLO record it belongs to does
                    # not already carry -- same target symbol, same site.  Drop
                    # it by default so it cannot manufacture a false REFUTED.
                    self.by_name[name].append(
                        (str(obj), body, tuple(sorted(
                            (o, ty, tn) for (o, tn, ty) in relocs
                            if keep_pair_relocs or ty != IMAGE_REL_PPC_PAIR))))
            except Exception as exc:                      # malformed .obj
                if verbose:
                    print(f"  ! skipped {obj}: {exc}", file=sys.stderr)
        self.build_seconds = time.time() - t0
        if verbose:
            print(f"index: {self.n_objects} objects, {self.n_slices} slices, "
                  f"{len(self.by_name)} distinct symbols, "
                  f"{self.build_seconds:.1f}s", file=sys.stderr)

    def get(self, name):
        return self.by_name.get(name, [])


def _first_word_diffs(a, b, limit=8):
    n = min(len(a), len(b))
    return [i for i in range(0, n, 4) if a[i:i + 4] != b[i:i + 4]][:limit]


def prove_pair(index, survivor, folded, canon=None):
    """Return a verdict dict for one claimed fold of `folded` into `survivor`."""
    canon = canon or {}
    sdefs = index.get(survivor)
    fdefs = index.get(folded)
    rec = {
        "survivor": survivor,
        "folded": folded,
        "survivor_defs": len(sdefs),
        "folded_defs": len(fdefs),
    }
    if not sdefs or not fdefs:
        rec["verdict"] = UNDECIDABLE
        missing = []
        if not sdefs:
            missing.append("survivor")
        if not fdefs:
            missing.append("folded")
        rec["reason"] = (f"VACUITY GUARD: no COMDAT for {'/'.join(missing)} in "
                         f"the objects scanned -- 'not found' is not 'identical'")
        return rec

    best = None
    for sobj, sbody, srel in sdefs:
        for fobj, fbody, frel in fdefs:
            same_bytes = sbody == fbody
            same_rels = srel == frel
            same_rels_mod = same_rels or (
                canonicalise(srel, canon) == canonicalise(frel, canon))
            score = (same_bytes and same_rels, same_bytes and same_rels_mod,
                     same_bytes, same_rels)
            cand = (score, sobj, sbody, srel, fobj, fbody, frel)
            if best is None or cand[0] > best[0]:
                best = cand
    (_score, sobj, sbody, srel, fobj, fbody, frel) = best
    same_bytes = sbody == fbody
    same_rels = srel == frel
    same_rels_mod = same_rels or (
        canonicalise(srel, canon) == canonicalise(frel, canon))
    rec.update({
        "survivor_obj": sobj, "folded_obj": fobj,
        "survivor_size": len(sbody), "folded_size": len(fbody),
        "survivor_relocs": len(srel), "folded_relocs": len(frel),
        "same_bytes": same_bytes, "same_relocs": same_rels,
        "same_relocs_mod_alias": same_rels_mod,
    })
    if same_bytes and same_rels_mod:
        if not srel:
            rec["verdict"] = UNDECIDABLE
            rec["reason"] = ("bodies identical but ZERO relocations -- an "
                             "unimplemented stub in our tree compiles to this "
                             "too, so identity here is CHEAP and proves nothing")
        elif same_rels:
            rec["verdict"] = PROVEN
            rec["reason"] = (f"byte- AND relocation-set-identical "
                             f"({len(sbody)} B, {len(srel)} relocations) => "
                             f"/OPT:ICF must merge them")
        else:
            rec["verdict"] = PROVEN_MOD
            differing = sorted(
                (o, tn, fn) for ((o, _t, tn), (_o2, _t2, fn)) in zip(srel, frel)
                if tn != fn)
            rec["alias_bridged_relocs"] = [[o, a, b] for (o, a, b) in differing[:16]]
            rec["reason"] = (
                f"byte-identical ({len(sbody)} B, {len(srel)} relocations) and "
                f"relocation-set-identical ONLY after {len(differing)} target "
                f"name(s) were canonicalised through an existing fold class -- "
                f"WEAKER tier, inherits that class's evidence")
        return rec

    rec["verdict"] = REFUTED
    why = []
    if not same_bytes:
        why.append(f"bytes differ ({len(sbody)} vs {len(fbody)} B"
                   + (f", first differing words at {_first_word_diffs(sbody, fbody)}"
                      if len(sbody) == len(fbody) else "") + ")")
        rec["first_word_diffs"] = _first_word_diffs(sbody, fbody)
    if not same_rels:
        sset = {(o, t, n) for (o, t, n) in srel}
        fset = {(o, t, n) for (o, t, n) in frel}
        only_s = sorted(sset - fset)
        only_f = sorted(fset - sset)
        why.append(f"relocation sets differ ({len(srel)} vs {len(frel)})")
        rec["relocs_only_survivor"] = [[o, t, n] for (o, t, n) in only_s[:16]]
        rec["relocs_only_folded"] = [[o, t, n] for (o, t, n) in only_f[:16]]
    rec["reason"] = "; ".join(why)
    return rec


def print_pair(rec, indent=""):
    v = rec["verdict"]
    mark = f"{v:<16}"
    print(f"{indent}[{mark}] {rec['survivor'][:88]}")
    print(f"{indent}              vs {rec['folded'][:88]}")
    if "survivor_size" in rec:
        print(f"{indent}    survivor {rec['survivor_size']:>6} B / "
              f"{rec['survivor_relocs']:>3} rel   {rec.get('survivor_obj','')}")
        print(f"{indent}    folded   {rec['folded_size']:>6} B / "
              f"{rec['folded_relocs']:>3} rel   {rec.get('folded_obj','')}")
    print(f"{indent}    {rec['reason']}")
    for key, label in (("relocs_only_survivor", "only in survivor"),
                       ("relocs_only_folded", "only in folded  ")):
        for (o, t, n) in rec.get(key, []):
            print(f"{indent}      {label}: +0x{o:04x} type=0x{t:02x} -> {str(n)[:70]}")
    for (o, a, b) in rec.get("alias_bridged_relocs", []):
        print(f"{indent}      alias-bridged: +0x{o:04x} {str(a)[:60]}")
        print(f"{indent}                              {str(b)[:60]}")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Prove or refute a claimed /OPT:ICF fold from our own objects.")
    ap.add_argument("--objects", action="append", required=True,
                    help="object file or directory to scan (repeatable). For "
                         "dc3-decomp our build objects are build/373307D9/src")
    ap.add_argument("--pair", nargs=2, action="append", default=[],
                    metavar=("SURVIVOR", "FOLDED"),
                    help="one claimed fold (repeatable)")
    ap.add_argument("--group", nargs="+", default=None,
                    metavar="SYMBOL",
                    help="SURVIVOR FOLDED... -- every member must equal the first")
    ap.add_argument("--pairs-json",
                    help="JSON list of [survivor, folded] or "
                         "{'target':..,'base':..} records")
    ap.add_argument("--equiv-json",
                    help="alias JSON (e.g. scripts/symbol_aliases.json) whose "
                         "fold classes canonicalise relocation target names. "
                         "Enables the WEAKER PROVEN_MOD_ALIAS tier -- read the "
                         "module docstring before citing one.")
    ap.add_argument("--keep-pair-relocs", action="store_true",
                    help="include IMAGE_REL_PPC_PAIR records in the relocation "
                         "set (they are displacements, not offsets, and are "
                         "dropped by default -- see BodyIndex)")
    ap.add_argument("--json-out", help="write verdict records here")
    ap.add_argument("--quiet", action="store_true",
                    help="summary counts only")
    ap.add_argument("--verbose", action="store_true",
                    help="report index build progress on stderr")
    args = ap.parse_args(argv)

    pairs = [tuple(p) for p in args.pair]
    extras = {}
    if args.group:
        survivor, folded = args.group[0], args.group[1:]
        if not folded:
            ap.error("--group needs a survivor and at least one folded spelling")
        pairs += [(survivor, f) for f in folded]
    if args.pairs_json:
        raw = json.loads(Path(args.pairs_json).read_text())
        for item in raw:
            if isinstance(item, dict):
                s = item.get("target") or item.get("survivor")
                f = item.get("base") or item.get("folded")
                extras[(s, f)] = {k: v for k, v in item.items()
                                  if k not in ("target", "base", "survivor",
                                               "folded")}
            else:
                s, f = item[0], item[1]
            pairs.append((s, f))
    if not pairs:
        ap.error("nothing to prove: pass --pair, --group or --pairs-json")

    canon = load_equivalences(args.equiv_json) if args.equiv_json else {}
    if canon:
        print(f"equivalences: {len(canon)} names canonicalised from "
              f"{args.equiv_json} (PROVEN_MOD_ALIAS tier enabled)")
    index = BodyIndex(args.objects, verbose=args.verbose,
                      keep_pair_relocs=args.keep_pair_relocs)

    recs = []
    for s, f in pairs:
        rec = prove_pair(index, s, f, canon)
        if (s, f) in extras:
            rec.update(extras[(s, f)])
        recs.append(rec)
        if not args.quiet:
            print_pair(rec)
            print()

    counts = collections.Counter(r["verdict"] for r in recs)
    print(f"index: {index.n_objects} objects, {index.n_slices} function slices, "
          f"{len(index.by_name)} distinct symbols ({index.build_seconds:.1f}s)")
    print(f"pairs: {len(recs)}   " + "  ".join(
        f"{k}={counts.get(k, 0)}" for k in VERDICTS))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(recs, indent=1))
        print(f"wrote {args.json_out}")
    return 0 if counts.get(REFUTED, 0) == 0 and counts.get(UNDECIDABLE, 0) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
