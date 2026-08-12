#!/usr/bin/env python3
"""Rank the wrong-reference half of the residency split by what it would BUY.

`split_reloc_residency.py` says which charges are folds and which are genuine
references to the wrong symbol.  This ranks the genuine half.  The ranking key
is not the site count: it is **solo-completable functions** -- functions whose
ONLY remaining `name_check` charge, across every lane, is this one pair.  Those
are the functions that go complete the moment the pair is repaired.  A pair with
20 sites spread over functions that are also charged by
`local_static_scope_ordinal` buys nothing until that lane moves too.
"""
import collections
import json
import sys
from pathlib import Path

DEFECT = {"both_mapped_diff_addr", "survivor_mapped_ours_unmapped",
          "neither_mapped", "ambiguous_multi_addr",
          "ours_mapped_survivor_unmapped"}

# Hand-recorded outcomes.  A pair leaves this list only by being fixed or by
# being shown to cost bytes; a blank note means nobody has looked yet.
NOTES = {
 ("?sSuperClassMap@ObjectDir@@2V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A",
  "?sSuperClassMap@@3V?$map@U?$pair@VSymbol@@V1@@stlpmtx_std@@_NU?$less@U?$pair@VSymbol@@V1@@stlpmtx_std@@@2@V?$StlNodeAlloc@U?$pair@$$CBU?$pair@VSymbol@@V1@@stlpmtx_std@@_N@stlpmtx_std@@@2@@stlpmtx_std@@A"):
   "TRIED, REVERTED: declaring the std::map member in Dir.h costs "
   "HamStoreProvider fn_82980E8C at `none` (100 -> 99.9), at either placement; "
   "a 4-blank-line control probe at the same point lost nothing, so it is the "
   "declaration. Needs a forward-declared or type-erased member.",
 ("?kArkBlockSize@@3HB", "?kArkBlockSize@@3HA"):
   "TRIED, REVERTED: `extern const int` in Archive.cpp lets MSVC fold the "
   "divisor and costs Archive::GetArkfileNumBlocks at `none`. Retail divides, "
   "so retail's DEFINITION is in another TU -- find it, do not add const here.",
 ("?gPhysicalType@?A0x2be09a71@@3PBDB", "?gPhysicalType@?A0x2be09a71@@3PADA"):
   "NEEDS JUDGEMENT: `PBDB` is `const char *const`, but PhysMemTypeTracker "
   "assigns to it at Memory_Xbox.cpp:338/347. Either retail had a second "
   "variable or those writes live elsewhere.",
 ("?DeleteChecksum@FileStream@@AAAXXZ", "?DeleteChecksum@BufStream@@QAAXXZ"):
   "LIKELY A FOLD, NOT A DEFECT: two same-named methods on unrelated classes "
   "with identical bodies; ours is simply the spelling /OPT:ICF discarded. "
   "Needs a body test, not a source edit.",
 ("??3@YAXPAX@Z", "??3DataArray@@SAXPAX@Z"):
   "NEEDS JUDGEMENT: retail calls the GLOBAL operator delete where we call the "
   "class-scoped POOL_OVERLOAD one. Mirror image of rb3-xenon's biggest pair. "
   "`??3@YAXPAX@Z` sits at three addresses in the map, so no alias is minted.",
}


def subclass(t, b):
    """How far apart the two spellings are, structurally.

    `same_qualified_name` is the mechanically fixable shape: identical
    identifier and scope, differing only in the access / cv / type decoration
    MSVC appends -- so retail's own symbol states the repair.  Anything else
    names a different entity and needs someone to decide which.
    """
    ht, hb = t.split("@@", 1)[0], b.split("@@", 1)[0]
    if ht == hb:
        return "same_qualified_name"
    lt = ht.lstrip("?").split("@")[0]
    lb = hb.lstrip("?").split("@")[0]
    if lt == lb:
        return "same_identifier_other_scope"
    return "different_identifier"


def main():
    all_sites, classified, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    per_fn = collections.defaultdict(list)
    for line in open(all_sites):
        r = json.loads(line)
        per_fn[(r["unit"], r["func"])].append(r)

    pair = collections.defaultdict(
        lambda: {"sites": 0, "fns": set(), "solo": set(),
                 "units": set(), "buckets": set(), "addr": {}})
    for line in open(classified):
        r = json.loads(line)
        if r["bucket"] not in DEFECT:
            continue
        k = (r["target"], r["base"])
        d = pair[k]
        d["sites"] += 1
        d["fns"].add((r["unit"], r["func"]))
        d["units"].add(r["unit"])
        d["buckets"].add(r["bucket"])
        if not [x for x in per_fn[(r["unit"], r["func"])]
                if (x["target"], x["base"]) != k]:
            d["solo"].add((r["unit"], r["func"]))

    allpairs = set(pair)
    cycles = sorted({tuple(sorted(p)) for p in allpairs if (p[1], p[0]) in allpairs})

    rows = []
    for (t, b), d in pair.items():
        rows.append({
            "target": t, "base": b,
            "bucket": sorted(d["buckets"])[0],
            "sites": d["sites"],
            "functions": len(d["fns"]),
            "solo_completable": len(d["solo"]),
            "units": sorted(d["units"]),
            "charged_functions": sorted(f for _u, f in d["fns"]),
            "two_cycle": (b, t) in allpairs,
            "subclass": subclass(t, b),
            "note": NOTES.get((t, b), ""),
        })
    rows.sort(key=lambda r: (-r["solo_completable"], -r["functions"], -r["sites"]))
    out.write_text(json.dumps({
        "generated_by": "scripts/build_wrongref_worklist.py",
        "totals": {
            "pairs": len(rows),
            "charged_functions": len(set().union(*[pair[k]["fns"] for k in pair])),
            "solo_completable_functions":
                len(set().union(*[pair[k]["solo"] for k in pair])),
            "two_cycles": len(cycles),
        },
        "two_cycles": [{"a": a, "b": b} for a, b in cycles],
        "pairs": rows,
    }, indent=1))
    print(f"{len(rows)} pairs, {len(cycles)} two-cycles -> {out}")


if __name__ == "__main__":
    main()
