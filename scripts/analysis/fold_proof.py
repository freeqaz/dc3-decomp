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
    PROVEN_MOD_MAP
                  identical only after relocation target names are canonicalised
                  through fold classes the SHIPPED LINKER MAP itself states
                  (--map).  Weaker than PROVEN_FOLD but backed by external ground
                  truth, not by our own inferences -- see below.
    PROVEN_MOD_ALIAS
                  identical only after relocation target names are canonicalised
                  through an ALREADY-ESTABLISHED fold class (--equiv-json).  A
                  strictly WEAKER tier -- see below.
    REFUTED       found on both sides and NOT identical.  The names are not
                  interchangeable; a name charge between them is a real bug.
    UNDECIDABLE   one or both spellings absent from the objects scanned, the
                  bodies are identical but relocation-free (too cheap to prove),
                  or -- with --map -- the definition the linker actually SELECTED
                  is not one our build emits (COMDAT_SELECTION_MISSING, below).

COMDAT SELECTION: WHY "FOUND ON BOTH SIDES" IS NOT ENOUGH
=========================================================
A template instantiation is emitted as a COMDAT into EVERY translation unit that
uses it, and the linker keeps exactly ONE.  If those TUs were compiled with
different flags the copies are not interchangeable, and the copy this tool
happens to index first may not be the copy the linker kept.

Measured on dc3-decomp: `config/373307D9/config.json` gives the `net_xbox` group
`/GS`, so `MakeString<...>` is 116 B with a `__security_cookie` prologue there
and 88 B without one everywhere else.  All seventeen occupants of the fold class
at 0x82563b08 are `net:` objects and the target body is 0x74 = 116 B, so the
linker kept a `net` copy -- but our build emits that particular spelling only
from `obj/DirLoader.obj` and `rndobj/TransAnim.obj`, and comparing an 88 B body
against a 116 B one REFUTED five map-CONFIRMED memberships that are not bugs.

With `--map`, a REFUTED verdict whose map-attributed defining object is absent
from our build is downgraded to UNDECIDABLE / COMDAT_SELECTION_MISSING, naming
the object the linker used and the objects we have.  This can only ever WEAKEN a
verdict -- UNDECIDABLE licenses nothing -- so it cannot fail open.

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

`--map` is that second instrument, and it does not close the loop, because the
shipped map is external to this project: it is what the linker that built the
retail image wrote down.  Names it co-lists at one address ARE one fold class, by
construction.  Canonicalising relocation targets through those classes yields
`PROVEN_MOD_MAP`, which is still weaker than `PROVEN_FOLD` (the two bodies are
identical only up to a fold we did not re-derive) but does not inherit any of our
own guesses.  Worked example on dc3-decomp: `??_7bad_typeid@std@@6B@`,
`??_7bad_cast@std@@6B@` and `??_7__non_rtti_object@std@@6B@` are all 8 all-zero
bytes with two relocations, differing only in which `??_E<class>` deleting
destructor slot 0 names -- and the map co-lists those three destructors at
0x8299dc60.  The vtables therefore fold; the strict name test simply could not
see one level down.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis.coff_bodies import (  # noqa: E402
    IMAGE_REL_PPC_PAIR, data_bodies, function_bodies, iter_objects)

PROVEN = "PROVEN_FOLD"
PROVEN_MOD_MAP = "PROVEN_MOD_MAP"
PROVEN_MOD = "PROVEN_MOD_ALIAS"
REFUTED = "REFUTED"
UNDECIDABLE = "UNDECIDABLE"
VERDICTS = (PROVEN, PROVEN_MOD_MAP, PROVEN_MOD, REFUTED, UNDECIDABLE)

#: ` 0005:00233b08       ?Sym@@... 82563b08 f i net:WebSvcMgr.obj`
_MAP_LINE = re.compile(
    r"^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(?P<name>\S+)\s+"
    r"(?P<addr>[0-9A-Fa-f]{8})\b(?P<rest>.*)$")


def load_map(path):
    """Parse an MSVC linker map into its two facts about fold classes.

    Returns ``(canon, defining_obj)``:

    ``canon``  name -> canonical representative of the set of names the map
               places at ONE address.  /OPT:ICF folds byte-identical COMDATs and
               the map co-lists every folded name at the survivor's address, so
               this partition is the linker's own, not ours.
    ``defining_obj``
               name -> BASENAME of the object the linker took the definition
               from (`net:WebSvcMgr.obj` -> `WebSvcMgr.obj`).  A name emitted as
               a COMDAT into many TUs has exactly one entry here: the copy that
               survived.  Used by the COMDAT_SELECTION_MISSING guard.
    """
    by_addr = collections.defaultdict(list)
    defining_obj = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        m = _MAP_LINE.match(line)
        if not m:
            continue
        name, addr = m.group("name"), m.group("addr").lower()
        by_addr[addr].append(name)
        tail = m.group("rest").split()
        if tail and tail[-1].lower().endswith(".obj"):
            defining_obj[name] = tail[-1].rsplit(":", 1)[-1]
    canon = {}
    for names in by_addr.values():
        if len(names) < 2:
            continue
        rep = min(names)
        for n in names:
            canon[n] = rep
    return canon, defining_obj


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

    def __init__(self, roots, verbose=False, keep_pair_relocs=False,
                 include_data=False):
        self.roots = [str(r) for r in roots]
        self.keep_pair_relocs = keep_pair_relocs
        self.include_data = include_data
        self.by_name: dict[str, list] = collections.defaultdict(list)
        self.kind: dict[str, str] = {}
        self.n_objects = 0
        self.n_slices = 0
        self.n_data_slices = 0
        t0 = time.time()
        readers = [("code", function_bodies)]
        if include_data:
            readers.append(("data", data_bodies))
        for obj in iter_objects(roots):
            self.n_objects += 1
            try:
                for kind, reader in readers:
                    for name, body, relocs, _entry in reader(obj):
                        self._add(obj, kind, name, body, relocs,
                                  keep_pair_relocs)
            except Exception as exc:                      # malformed .obj
                if verbose:
                    print(f"  ! skipped {obj}: {exc}", file=sys.stderr)
        self.build_seconds = time.time() - t0
        if verbose:
            print(f"index: {self.n_objects} objects, {self.n_slices} slices, "
                  f"{len(self.by_name)} distinct symbols, "
                  f"{self.build_seconds:.1f}s", file=sys.stderr)

    def _add(self, obj, kind, name, body, relocs, keep_pair_relocs):
        # A type-18 PAIR record's "VirtualAddress" is a DISPLACEMENT, not an
        # offset, so the `v <= o < end` slice filter in coff_bodies can keep it
        # on one side and drop it on the other for two genuinely identical
        # bodies.  It also carries no information the REFHI/REFLO record it
        # belongs to does not already carry -- same target symbol, same site.
        # Drop it by default so it cannot manufacture a false REFUTED.
        if kind == "data":
            self.n_data_slices += 1
        self.kind.setdefault(name, kind)
        self.n_slices += 1
        self.by_name[name].append((str(obj), body, tuple(sorted(
            (o, ty, tn) for (o, tn, ty) in relocs
            if keep_pair_relocs or ty != IMAGE_REL_PPC_PAIR))))

    def get(self, name):
        return self.by_name.get(name, [])


#: A data COMDAT this small (or all-zero) coincides too easily to mean anything.
CHEAP_DATA_BYTES = 8


def _identity_is_cheap(kind, body):
    """Is byte-identity of a relocation-free `kind` COMDAT uninformative?

    For CODE, always yes: every unimplemented `{ return 0; }` stub in the tree
    compiles to the same 16 relocation-free bytes, so identity carries no
    information about whether the TARGET folded the two.  This is the guard that
    keeps `?Handle@HamDirector@@`'s pair honest.

    For DATA there is no stub analogue -- a string literal or a vtable that is
    byte-identical to another really is what /OPT:ICF folds on -- so identity is
    informative EXCEPT on tiny or all-zero COMDATs, where coincidence is likely.
    """
    if kind != "data":
        return True
    return len(body) < CHEAP_DATA_BYTES or not any(body)


def _first_word_diffs(a, b, limit=8):
    n = min(len(a), len(b))
    return [i for i in range(0, n, 4) if a[i:i + 4] != b[i:i + 4]][:limit]


def _selection_missing(index, name, defining_obj):
    """The map's chosen definition of `name`, if our build does not emit it.

    Returns ``(map_obj, our_objs)`` when the linker took `name` from an object
    whose basename is not among the objects that define it in our tree, else
    ``None``.  See "COMDAT SELECTION" in the module docstring: comparing a copy
    the linker discarded against a copy it kept is not a test of anything.
    """
    want = defining_obj.get(name)
    if not want:
        return None
    ours = {Path(o).name for (o, _b, _r) in index.get(name)}
    if want in ours:
        return None
    return want, sorted(ours)


def prove_pair(index, survivor, folded, canon=None, map_canon=None,
               defining_obj=None):
    """Return a verdict dict for one claimed fold of `folded` into `survivor`."""
    canon = canon or {}
    map_canon = map_canon or {}
    defining_obj = defining_obj or {}
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

    def _same_mod(a, b, table):
        return bool(table) and canonicalise(a, table) == canonicalise(b, table)

    best = None
    for sobj, sbody, srel in sdefs:
        for fobj, fbody, frel in fdefs:
            same_bytes = sbody == fbody
            same_rels = srel == frel
            same_rels_map = same_rels or _same_mod(srel, frel, map_canon)
            same_rels_mod = same_rels or _same_mod(srel, frel, canon)
            score = (same_bytes and same_rels, same_bytes and same_rels_map,
                     same_bytes and same_rels_mod, same_bytes, same_rels)
            cand = (score, sobj, sbody, srel, fobj, fbody, frel)
            if best is None or cand[0] > best[0]:
                best = cand
    (_score, sobj, sbody, srel, fobj, fbody, frel) = best
    same_bytes = sbody == fbody
    same_rels = srel == frel
    same_rels_map = same_rels or _same_mod(srel, frel, map_canon)
    same_rels_mod = same_rels or _same_mod(srel, frel, canon)
    rec.update({
        "kind": index.kind.get(survivor) or index.kind.get(folded) or "code",
        "survivor_obj": sobj, "folded_obj": fobj,
        "survivor_size": len(sbody), "folded_size": len(fbody),
        "survivor_relocs": len(srel), "folded_relocs": len(frel),
        "same_bytes": same_bytes, "same_relocs": same_rels,
        "same_relocs_mod_map": same_rels_map,
        "same_relocs_mod_alias": same_rels_mod,
    })
    kind = rec.get("kind", "code")
    if same_bytes and (same_rels_map or same_rels_mod):
        if not srel and _identity_is_cheap(kind, sbody):
            rec["verdict"] = UNDECIDABLE
            rec["reason"] = (
                "bodies identical but ZERO relocations -- an unimplemented stub "
                "in our tree compiles to this too, so identity here is CHEAP "
                "and proves nothing"
                if kind == "code" else
                f"data COMDATs identical but trivial ({len(sbody)} B, no "
                f"relocations, all-zero={not any(sbody)}) -- too cheap to prove")
        elif same_rels:
            rec["verdict"] = PROVEN
            rec["reason"] = (f"byte- AND relocation-set-identical "
                             f"({len(sbody)} B, {len(srel)} relocations) => "
                             f"/OPT:ICF must merge them")
        else:
            # The map is external ground truth, the alias file is our own
            # inference, so prefer the map class when both would bridge.
            via_map = same_rels_map
            rec["verdict"] = PROVEN_MOD_MAP if via_map else PROVEN_MOD
            differing = sorted(
                (o, tn, fn) for ((o, _t, tn), (_o2, _t2, fn)) in zip(srel, frel)
                if tn != fn)
            rec["alias_bridged_relocs"] = [[o, a, b] for (o, a, b) in differing[:16]]
            rec["reason"] = (
                f"byte-identical ({len(sbody)} B, {len(srel)} relocations) and "
                f"relocation-set-identical ONLY after {len(differing)} target "
                f"name(s) were canonicalised through "
                + ("fold classes the SHIPPED LINKER MAP co-lists at one address "
                   "-- weaker than PROVEN_FOLD, but external evidence"
                   if via_map else
                   "an existing fold class -- WEAKER tier, inherits that "
                   "class's evidence"))
        return rec

    # Before calling this a real difference: did we even compare the two COMDATs
    # the linker compared?  See "COMDAT SELECTION" in the module docstring.
    for side, name in (("survivor", survivor), ("folded", folded)):
        miss = _selection_missing(index, name, defining_obj)
        if miss:
            map_obj, our_objs = miss
            rec["verdict"] = UNDECIDABLE
            rec["selection_missing"] = {"side": side, "name": name,
                                        "map_obj": map_obj, "our_objs": our_objs}
            rec["reason"] = (
                f"COMDAT_SELECTION_MISSING: the linker took {side} "
                f"{name[:60]} from {map_obj}, which our build does not emit it "
                f"from (we have it in {', '.join(our_objs[:4]) or 'nothing'}); "
                f"copies from different TUs need not be interchangeable, so the "
                f"two bodies compared are not the two the linker compared")
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
    ap.add_argument("--map",
                    help="shipped MSVC linker map (dc3: "
                         "orig/373307D9/ham_xbox_r.map). Enables the "
                         "PROVEN_MOD_MAP tier -- relocation targets are "
                         "canonicalised through fold classes the LINKER states "
                         "-- and the COMDAT_SELECTION_MISSING guard, which "
                         "downgrades a REFUTED to UNDECIDABLE when the "
                         "definition the linker selected is not one we emit.")
    ap.add_argument("--include-data", action="store_true",
                    help="also index DATA COMDATs (string literals, vtables, "
                         "fp constants, local statics). /OPT:ICF folds those "
                         "too and a large share of name charges name them.")
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
    map_canon, defining_obj = load_map(args.map) if args.map else ({}, {})
    if args.map:
        print(f"linker map: {len(map_canon)} names in multi-name fold classes, "
              f"{len(defining_obj)} name->object attributions from {args.map} "
              f"(PROVEN_MOD_MAP tier + COMDAT_SELECTION_MISSING guard enabled)")
    index = BodyIndex(args.objects, verbose=args.verbose,
                      keep_pair_relocs=args.keep_pair_relocs,
                      include_data=args.include_data)

    recs = []
    for s, f in pairs:
        rec = prove_pair(index, s, f, canon, map_canon, defining_obj)
        if (s, f) in extras:
            rec.update(extras[(s, f)])
        recs.append(rec)
        if not args.quiet:
            print_pair(rec)
            print()

    counts = collections.Counter(r["verdict"] for r in recs)
    print(f"index: {index.n_objects} objects, {index.n_slices} slices "
          f"({index.n_data_slices} data), {len(index.by_name)} distinct symbols "
          f"({index.build_seconds:.1f}s)")
    print(f"pairs: {len(recs)}   " + "  ".join(
        f"{k}={counts.get(k, 0)}" for k in VERDICTS))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(recs, indent=1))
        print(f"wrote {args.json_out}")
    return 0 if counts.get(REFUTED, 0) == 0 and counts.get(UNDECIDABLE, 0) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
