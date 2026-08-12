#!/usr/bin/env python3
"""Derive `scripts/target_symbol_map.json` from `config/<BUILD_ID>/symbols.txt`.

WHY THIS EXISTS
---------------
`scripts/target_symbol_map.json` is 69,132 committed entries that no in-repo
script regenerated. Three commits have touched it (366e8c71e installed it,
1d730cba2 fixed the global `operator delete`, 391d1b080 named 407 addresses the
splitter had only numbered) and each edited it by hand or by an ad-hoc script
that was not kept. A file nothing can rebuild is a file nobody can audit, and
the thing being audited here is a gate on the ADMISSION predicate.

So: this reproduces it, and prices the widening that has been proposed.

WHAT IT PROVES (`--verify`, measured 2026-08-12 at dc3 16b5c31b4)
-----------------------------------------------------------------
The committed map IS `symbols.txt` filtered to `type:function`, to within a
diff small enough to read:

    symbols.txt              211,852 rows = 69,307 function
                                          + 142,256 object
                                          +     289 label
    committed map             69,132 entries, 0 duplicate VAs, 0 duplicate names
    derived (type:function)   69,307
    only in derived              175   the Bink/RAD runtime block at 0x82EE-0x82EF
                                       (`BinkOpen`, `rrMutexCreate`, ...)
    only in committed              0
    value disagreements            2   0x829A2760 sprintf, 0x829A1AD0 _snprintf
                                       -- the map is NAMED and symbols.txt still
                                       has the splitter's `fn_...` placeholder

Those 177 rows are the whole provenance gap. Neither direction is a defect of
this script; both are recorded so the next reader does not have to rediscover
them.

WHAT IT DOES NOT DO
-------------------
It does not write `scripts/target_symbol_map.json`. `--emit` takes an explicit
output path and refuses that one. The widening is NOT installed here, and this
script is not a licence to install it -- see WIDENING below.

WIDENING: PRICED, NOT INSTALLED
-------------------------------
The blindness is real. decomp-synth's `symbol_equivalences.validate_groups`
gate (a) -- "survivor is a value in target_symbol_map.json" -- refuses 102 of
the 106 `retailmap-data:` fold groups installed 2026-08-12, on gate (a) and
nothing else, because all 102 survivors are `??_8` vbtables and the map holds
0 data names. Measured against the target objects' own COFF `Type` field:
69,130 of the map's names are typed FUNCTION there, 0 are typed anything else.

And the blindness is EXPENSIVE, re-measured here rather than inherited. Render
the map with and without the 102 `kind: data` groups, pinned objdiff-cli-B, one
-o path per arm with the .cache purged, complete-function SETS compared:

    name_check   41.940052% (28,668 complete)  ->  42.635746% (28,862)
                 +194 complete functions, +0.695694 pp, 0 LOST
    none         43.730507% (29,182 complete)  ->  43.730507% (29,182)
                 +0, 0 LOST -- the control, and it behaves

The `none` null is the proof the gain is real rather than a scorer artefact:
`none` ignores relocation names, so a name-equivalence tier MUST be invisible
to it, and it is, to the byte. The `name_check` gain is what the 102 groups
buy, and it is what decomp-synth's grader is currently declining to see.

The figure carried into this lane was +198. Measured here it is +194, over
`fuzzy_match_percent == 100` at dc3 16b5c31b4 on pinned binary B. The 4-row gap
is not chased; the number to quote is the one with the ruler and tree named
beside it.

The data IS derivable -- `--tier` prices three of them below, and the 102
survivors are all present in `symbols.txt` as `type:object` in `.rdata` at
exactly the address the alias group records (0 mismatches). Widening is the
correct repair of the blindness.

`--cross-check-retail-map` confirms that against a SECOND and fully independent
witness -- `orig/373307D9/ham_xbox_r.map`, the shipped MSVC linker map, which
symbols.txt was not derived from. All 102 survivors appear there at exactly the
address the alias group records, 102/102. So the data half of the widening does
not rest on the splitter alone.

(That mode is worth running for its own sake. It is how you see that
symbols.txt's `merged_*` spellings ARE the ICF classes: at 0x82331448 the
splitter writes `merged_ObjRefConcreteGetObj` and the retail map publishes
three `?GetObj@?$ObjRefConcrete@...` instantiations at that one address.)

It is still an OWNER CALL, because absence-from-this-map is a MEANING to five
consumers, not a lookup miss:

  1. `symbol_equivalences.validate_groups` gate (a) -- widening RETRACTS 102
     refusals and loosens the admission predicate. That is the point, and it
     is a grader change.
  2. `tools/vast/jobs/progress-history/terminal_witness.py` -- pins the grader
     as sha256(`coff_ppc.py`) + sha256(`symbol_equivalences.py`) and computes
     `grader_consistent` from those two. THE MAP IS NOT HASHED. A widening
     moves graded witnessed/refuted counts while every provenance field still
     reports the grader unchanged. This is the one that should decide
     sequencing.
  3. `scripts/grind/agent_tools._health_probe_target` -- probes Ghidra with the
     MEDIAN of the sorted map, specifically to prove attribution is exercised
     before a graded run may start. At tier `all` the median moves from a real
     function to a `.pdata` row, and the gate either goes vacuous or blocks
     every run. `agent_tools.py` names the hazard in its own comment --
     "Function rows only -- data symbols would poison containment" -- but that
     guard sits on a `symbols.txt` fallback that is dead now dc3 ships a map.
  4. `scripts/grind/agent_tools._tool_resolve_address` -- absence is rendered
     to the model verbatim as "not in the identified-symbol map (still
     anonymous)". At tier `all`, `.pdata` packed every 8 bytes answers nearly
     every containment query instead.
  5. `tools/il_witness/reloc_repair.decide` -- absence is `UNDECIDED,
     repair=False`. Presence flips it to `ADDR_IDENTITY, repair=True`. Lookup
     miss and deliberate refusal are the same line of code there.

Consumers 3 and 4 are why `--tier` is not a boolean -- but the tiers do NOT
buy a way out, and that is a finding in its own right:

    tier      entries   102 data survivors   median (agent_tools' probe)
    committed  69,132     0   gate (a) blind  0x827CA420 ??6FormatString@@QAAAAV0@H@Z
    function   69,307     0   gate (a) blind  0x827CE020 ?compare@FixedString@@QBAHIIPBD@Z
    data      126,166   102   gate (a) sees   0x825B58F8 __unwind$109631
    all       211,563   102   gate (a) sees   0x8230AEC0 pdata@8230AEC0

No tier that unblinds gate (a) leaves the median on a real function. So
consumer 3 has to be repaired in `agent_tools`, not dodged by choosing a
narrower widening.

And the reason it cannot be dodged is worth stating on its own, because it is
true TODAY, before any widening: **20,649 of the committed map's 69,132 entries
-- 29.87% -- are `__unwind$NNNNNN` blobs.** The splitter emits them as
`type:function` in `.text`, and the target objects' COFF symbol tables type
them `IMAGE_SYM_DTYPE_FUNCTION` too, so nothing downstream can tell them from
code. `_health_probe_target` is therefore already running a ~30% chance of
probing Ghidra with an unwind record and calling it "a REAL function start";
it currently lands on a real one by luck, and ANY edit to this file -- the
407-address rename of 391d1b080 was one -- can move it. Widening makes a latent
hazard certain, which is different from creating it.

If the widening lands, the same change should carry: the map's sha256 into
`terminal_witness`'s grader record, the function-only filter restored inside
`agent_tools._load_symbol_maps` rather than left on the dead fallback, and a
rewrite of `scripts/install_data_fold_aliases.py`'s `_comment`, which ships a
claim ("0 of 69,132 names is a data symbol") that widening makes false.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
COMMITTED_REL = "scripts/target_symbol_map.json"

# `Name = .section:0xVA; // type:<t> size:0x<n> scope:<s> [data:<d>]`
ROW = re.compile(r"^\s*(?P<name>\S+)\s*=\s*(?P<sec>[\w.$]+):"
                 r"0x(?P<va>[0-9A-Fa-f]+)\s*;\s*//\s*(?P<rest>.*)$")
TYPE = re.compile(r"type:(\w+)")

# Address-derived spellings the splitter minted because it had no name. Kept
# verbatim from `symbol_equivalences.PLACEHOLDER_RE`, which is the gate that
# refuses them (gate (d)) -- if the two ever disagree the map would carry names
# a downstream gate then rejects, which is worse than not carrying them.
PLACEHOLDER = re.compile(r"^(?:fn|lbl|jumptable|func)_[0-9A-Fa-f]{6,}$")

# `.pdata` is exception-unwind bookkeeping emitted PER FUNCTION, not a datum any
# source spelling references. It is 41% of the object rows and it is what moves
# `agent_tools`' median probe off a real function start. No fold group has ever
# named one.
BOOKKEEPING_SECTIONS = {".pdata"}

TIERS = {
    "function": "type:function only -- reproduces the committed map",
    "data": "function + REAL data (drops address-derived placeholders and "
            ".pdata bookkeeping) -- the tier that unblinds gate (a)",
    "all": "function + every type:object row, unfiltered -- priced, not "
           "recommended; see this module's WIDENING note, consumers 3 and 4",
}


def parse_symbols(path: Path) -> dict:
    """`{kind: {"0xva": name}}` plus per-row section, from a splitter map."""
    rows: dict = {"function": {}, "object": {}, "label": {}, "other": {}}
    section: dict = {}
    n_rows = n_unparsed = 0
    dup_va: Counter = Counter()
    for line in path.read_text(errors="replace").splitlines():
        if not line.strip():
            continue
        m = ROW.match(line)
        if not m:
            n_unparsed += 1
            continue
        n_rows += 1
        t = TYPE.search(m.group("rest"))
        kind = t.group(1) if t else "other"
        if kind not in rows:
            kind = "other"
        va = "0x" + m.group("va").lower()
        if va in rows[kind]:
            dup_va[va] += 1
        rows[kind][va] = m.group("name")
        section[m.group("name")] = m.group("sec")
    return {"rows": rows, "section": section, "n_rows": n_rows,
            "n_unparsed": n_unparsed, "dup_va": dict(dup_va)}


def build(parsed: dict, tier: str) -> dict:
    """The map a given tier would ship, as `{"0xva": name}`."""
    if tier not in TIERS:
        raise ValueError(f"unknown tier {tier!r}; choose from {sorted(TIERS)}")
    out = dict(parsed["rows"]["function"])
    if tier == "function":
        return out
    sec = parsed["section"]
    for va, name in parsed["rows"]["object"].items():
        if tier == "data":
            if PLACEHOLDER.match(name):
                continue
            if sec.get(name) in BOOKKEEPING_SECTIONS:
                continue
        out[va] = name
    return out


def diff(committed: dict, derived: dict) -> dict:
    """Every way the two disagree, with examples. Never a bare count."""
    c = {k.lower(): v for k, v in committed.items()
         if isinstance(k, str) and k.lower().startswith("0x")
         and isinstance(v, str)}
    d = {k.lower(): v for k, v in derived.items()}
    only_c = sorted(set(c) - set(d))
    only_d = sorted(set(d) - set(c))
    disagree = sorted((k, c[k], d[k]) for k in set(c) & set(d) if c[k] != d[k])
    dup_names_c = [n for n, k in Counter(c.values()).items() if k > 1]
    dup_names_d = [n for n, k in Counter(d.values()).items() if k > 1]
    return {
        "n_committed": len(c), "n_derived": len(d),
        "n_only_in_committed": len(only_c), "n_only_in_derived": len(only_d),
        "n_value_disagreements": len(disagree),
        "only_in_committed": [{"va": k, "name": c[k]} for k in only_c[:20]],
        "only_in_derived": [{"va": k, "name": d[k]} for k in only_d[:20]],
        "value_disagreements": [{"va": k, "committed": a, "derived": b}
                                for k, a, b in disagree[:20]],
        # A name at two VAs has no deterministic inverse, and
        # `reloc_repair.load_address_map` inverts this file. Report it here
        # rather than let a consumer discover it.
        "n_duplicate_names_committed": len(dup_names_c),
        "n_duplicate_names_derived": len(dup_names_d),
        "duplicate_names_derived": sorted(dup_names_d)[:20],
    }


# MSVC map publics: ` 0001:00000000       <name>   82000600  f i <obj>`
PUBLIC = re.compile(r"^\s*\d{4}:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{8})\s+(\S.*)?$")


def cross_check_retail(repo: Path, parsed: dict, path: str) -> dict:
    """Agree `symbols.txt` against the SHIPPED linker map, VA by VA.

    An independent witness matters here because everything else in this file
    comes from one source. `symbols.txt` is the dtk splitter's output; the
    retail map is what the MSVC linker itself published in 2012, and the
    widening's whole case is that the data addresses in the first are real.

    Disagreement at a VA is EXPECTED and is not an error: the retail map is
    where the ICF folds are visible, so an address the splitter had to give one
    synthetic `merged_*` name is an address the linker published several real
    ones at. Counted, exampled, never reconciled away.
    """
    if path == "AUTO":
        cands = sorted((repo / "orig").glob("*/*.map"))
        if len(cands) != 1:
            return {"status": "unresolved",
                    "reason": f"cannot pick a retail map automatically: {cands}"}
        mp = cands[0]
    else:
        mp = Path(path)
    if not mp.is_file():
        return {"status": "unresolved", "reason": f"no such map: {mp}"}
    by_va: dict = {}
    n_pub = 0
    for line in mp.read_text(errors="replace").splitlines():
        m = PUBLIC.match(line)
        if not m:
            continue
        n_pub += 1
        by_va.setdefault("0x" + m.group(2).lower(), set()).add(m.group(1))

    def agree(rows: dict) -> dict:
        present = absent = name_eq = 0
        ex = []
        for va, name in rows.items():
            names = by_va.get(va)
            if names is None:
                absent += 1
                continue
            present += 1
            if name in names:
                name_eq += 1
            elif len(ex) < 8:
                ex.append({"va": va, "symbols_txt": name,
                           "retail_map": sorted(names)[:3]})
        return {"n": len(rows), "va_present_in_retail_map": present,
                "va_absent": absent, "name_agrees": name_eq,
                "name_disagrees": present - name_eq, "examples": ex}

    return {"status": "checked", "map": str(mp.name), "n_publics": n_pub,
            "n_distinct_vas": len(by_va),
            "function_rows": agree(parsed["rows"]["function"]),
            "object_rows": agree(parsed["rows"]["object"])}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--symbols", default=None,
                    help="default <repo>/config/<BUILD_ID>/symbols.txt (globbed)")
    ap.add_argument("--committed", default=str(REPO / COMMITTED_REL))
    ap.add_argument("--tier", default="function", choices=sorted(TIERS),
                    help="; ".join(f"{k}: {v}" for k, v in TIERS.items()))
    ap.add_argument("--emit", default=None,
                    help="write the derived map here. Refuses the committed "
                         "path: widening is an owner call, not a flag.")
    ap.add_argument("--price-all-tiers", action="store_true",
                    help="diff every tier against the committed map and stop")
    ap.add_argument("--cross-check-retail-map", nargs="?", const="AUTO",
                    default=None, metavar="PATH",
                    help="second witness: agree symbols.txt against the shipped "
                         "MSVC linker map (default orig/<BUILD_ID>/*.map)")
    a = ap.parse_args(argv)

    if a.symbols:
        spath = Path(a.symbols)
    else:
        cands = sorted((REPO / "config").glob("*/symbols.txt"))
        if len(cands) != 1:
            print(f"cannot pick a symbols.txt automatically: {cands}",
                  file=sys.stderr)
            return 2
        spath = cands[0]

    parsed = parse_symbols(spath)
    committed = json.loads(Path(a.committed).read_text())
    kinds = {k: len(v) for k, v in parsed["rows"].items()}
    report = {
        "symbols_txt": str(spath.relative_to(REPO)),
        "committed": str(Path(a.committed).name),
        "symbols_rows": parsed["n_rows"],
        "symbols_unparsed": parsed["n_unparsed"],
        "symbols_by_kind": kinds,
        "symbols_duplicate_vas": parsed["dup_va"],
    }

    tiers = sorted(TIERS) if a.price_all_tiers else [a.tier]
    report["tiers"] = {}
    # `agent_tools._health_probe_target` probes Ghidra with the MEDIAN of the
    # sorted map and calls it "a REAL function start". Report it per tier so a
    # future widener sees what it is about to point that gate at.
    def probe_median(m: dict) -> dict:
        if not m:
            return {}
        keys = sorted(m, key=lambda k: int(k, 16))
        k = keys[len(keys) // 2]
        n = m[k]
        return {"va": k, "name": n,
                "is_unwind_blob": n.startswith("__unwind$"),
                "is_pdata_row": n.startswith("pdata@")}

    committed_norm = {k.lower(): v for k, v in committed.items()
                      if isinstance(k, str) and k.lower().startswith("0x")
                      and isinstance(v, str)}
    report["committed_probe_median"] = probe_median(committed_norm)
    report["committed_unwind_blobs"] = sum(
        1 for v in committed_norm.values() if v.startswith("__unwind$"))
    for t in tiers:
        m = build(parsed, t)
        report["tiers"][t] = {"desc": TIERS[t], "n_entries": len(m),
                              "probe_median": probe_median(m),
                              **diff(committed, m)}

    if a.cross_check_retail_map:
        report["retail_map"] = cross_check_retail(
            REPO, parsed, a.cross_check_retail_map)

    if a.emit:
        out = Path(a.emit).resolve()
        if out == (REPO / COMMITTED_REL).resolve():
            print("refusing to overwrite " + COMMITTED_REL + ": widening is an "
                  "owner call and five consumers read absence as meaning. "
                  "See this module's docstring.", file=sys.stderr)
            return 3
        m = build(parsed, a.tier)
        out.parent.mkdir(parents=True, exist_ok=True)
        # Two-space-free, one entry per line: the committed file's own shape, so
        # a real diff of a real widening stays readable.
        out.write_text("{\n" + ",\n".join(
            f'"{k}": {json.dumps(v)}' for k, v in sorted(m.items())) + "\n}\n")
        report["emitted"] = {"path": str(out), "tier": a.tier, "n": len(m)}

    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
