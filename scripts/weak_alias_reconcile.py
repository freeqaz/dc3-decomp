#!/usr/bin/env python3
"""Reconcile the installed ICF alias set with MSVC's DECLARED weak aliases.

What this evidence is
---------------------
MSVC emits the vector deleting destructor `??_E<Class>` as an **undefined weak
external** whose auxiliary record names its default resolution -- `??_G<Class>`
-- with characteristics 2, `IMAGE_WEAK_EXTERN_SEARCH_ALIAS`. The linker resolves
one to the other, so a call to either is a call to the same address, and the
compiler states that in our own object file. That is evidence class 2 of the
three `scripts/symbol_aliases.json` carries, and it is the only one that can
speak about a name our objects never DEFINE: the weak external is a reference,
so a body test reads it as "cannot adjudicate" and the retail-map tier's gate 5
drops it as "our objects define no body for this name". The aux record reads it
as "the compiler already told you".

Why it had to be re-derivable
-----------------------------
The tier was minted once, 2026-08-10, by a run script that lives outside this
repo (`<decomp-bench>/archive/runs/dc3-placeholder-adjudication-20260810/
scripts/weak_alias_classes.py` + `install_weak_aliases.py`). Its COFF reader is
carried over here verbatim -- the admitting predicate is not reimplemented -- but
its installer is not: that one REWRITES `scripts/symbol_aliases.json` wholesale
from `validate_groups`' accepted set, which silently drops any group that fails
validation today, and the file has since grown 123 groups from three other
producers and two in-place lanes. This tool is additive and ledgered instead,
the same shape as `scripts/retail_map_fold_reconcile.py`.

Re-running the 2026-08-10 generator against the CURRENT tree derives 886 groups
where 962 are installed, because `config/373307D9/symbols.txt` has been
rewritten since (`icf-survivor-names`, 2026-08-19) and gate (c) -- the target
must define exactly one member -- now fails for names it then passed. A
regenerate-and-replace would therefore LOSE 76 groups that are not wrong, only
re-derived from a moved artifact. Hence: additive only.

The evidence standard, and what is refused
------------------------------------------
An aux record proves the two names RESOLVE together. It does not, on its own,
say which address they resolve to, and `scripts/gen_icf_alias_map.py` renders an
MSVC-format map keyed on the survivor's address and SKIPS an addressless group.
So a pair is only admitted when `orig/373307D9/ham_xbox_r.map` -- the shipped
linker's own map -- places BOTH names at the SAME address. Every other shape is
counted and refused:

  * map gives the two names DIFFERENT addresses  -> a real divergence, refused;
  * only one of the two is in the map            -> unanchorable, refused;
  * neither is in the map                        -> the class never reached the
                                                    image; a LEAD, refused;
  * the two are already in two DIFFERENT groups  -> would merge two adjudicated
                                                    classes, refused fail-closed;
  * the weak name is declared with two different
    defaults across our objects                  -> not a function, refused.

Direction is not ours to choose. The survivor is the spelling the TARGET objects
define at that address (`scripts/target_symbol_map.json`, which is what dtk
stamped from `symbols.txt`), because decomp-synth `validate_groups`' gate (c)
requires the target to name the survivor and nothing else.

What an alias group does NOT do -- measured, because it was assumed
-------------------------------------------------------------------
An alias group makes two names compare EQUAL as RELOCATION TARGETS. It does
NOT pair two differently-spelled SYMBOLS, so it cannot rescue a function that
reads 0.0% because the target defines the ICF survivor's spelling and our
object defines the folded one. `??_EGainEffect@@UAAPAXI@Z` (100 B, GainEffect)
and `?Process@?$CSampleXAPOBase@VHeadsetPlaybackEffect@@...` (132 B) read 0.0%
before this tier reached them and 0.0% after.

The control that settles it: binary-wide, **118 functions / 8,172 B sit at
`match_percent_normalized == 0.0` while their name IS already a member of an
alias group** -- including two of the four `??_E` rows this lane was pointed at
(`??_E?$CSampleXAPOBase@VCompressionEffect@@UParams@1@@ATG@@MAAPAXI@Z`,
`??_E?$StandardEffect@VCompressionEffect@@@@UAAPAXI@Z`), which were members
long before it ran. Membership is not the mechanism.

The mechanism that would pair them exists in the fork -- `diff::
reconcile_global_byte_matches` in `objdiff-core/src/diff/mod.rs`, which DOES
consult `SymbolEquivalences` for pairing -- but it is gated on
`objdiff-cli report generate --global-byte-eq`, and this project's report edge
does not pass it (`grep -c global-byte-eq build.ninja` = 0). So the headline
cannot move for this row class no matter how complete the alias set is. What
the alias set is for is the RELOCATION charge: the harvest that installed this
ledger took the reloc-name-charged population from 386 rows to 383 (-288 B,
`scripts/analysis/reloc_name_gate.py`), 0 rows entering and 0 scores dropping.
Record the fold because it is true; do not expect a percentage for it.

Denominator
-----------
Every run prints the full census: weak SEARCH_ALIAS records read, and every
bucket with its count. There is no `--limit`; a truncated sweep is not offered,
because its silence about the unexamined would read as absence.

Usage
-----
    python3 scripts/weak_alias_reconcile.py --report OUT   # derive only
    python3 scripts/weak_alias_reconcile.py --apply        # install
    python3 scripts/weak_alias_reconcile.py --check        # exit 1 if stale
    python3 scripts/weak_alias_reconcile.py --uninstall
    python3 scripts/weak_alias_reconcile.py --selftest     # negative controls

`--apply` is idempotent: it first REVERTS everything the recorded ledger says a
previous run did, then re-derives from scratch and re-applies. It never renders
the map; run `scripts/gen_icf_alias_map.py` (or `ninja`) for that.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ALIAS = REPO / "scripts" / "symbol_aliases.json"
TARGET_SYMBOL_MAP = REPO / "scripts" / "target_symbol_map.json"
RETAIL_MAP = REPO / "orig" / "373307D9" / "ham_xbox_r.map"
OBJDIFF = REPO / "objdiff.json"
LEDGER_KEY = "weak_alias_reconcile"
FRESH_TIER = "weakalias:"

EVIDENCE = (
    "COFF weak external, IMAGE_WEAK_EXTERN_SEARCH_ALIAS: our own object "
    "declares this name as an undefined weak external whose auxiliary record "
    "names the group member it resolves to (characteristics 2), i.e. the "
    "compiler states the alias. Anchored additionally on "
    "orig/373307D9/ham_xbox_r.map, which places both names at the same "
    "address; a pair the map splits across two addresses is refused. The "
    "retail-map tier cannot reach this name -- the weak external is a "
    "REFERENCE, so our objects define no body and its byte-identity gate 5 "
    "drops it unproven. Re-derive with scripts/weak_alias_reconcile.py."
)

IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105
IMAGE_WEAK_EXTERN_SEARCH_ALIAS = 2

# `<decomp-bench>/archive/runs/dc3-placeholder-adjudication-20260810/scripts/
# weak_alias_classes.py`, verbatim. The predicate that admits this tier is not
# reimplemented here; only the install plumbing around it is new.
MAP_LINE = re.compile(
    r"^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s")


def weak_aliases(raw: bytes) -> dict:
    """{weak symbol name -> the name it is declared to resolve to}."""
    if len(raw) < 20:
        return {}
    symptr = struct.unpack_from("<I", raw, 8)[0]
    nsym = struct.unpack_from("<I", raw, 12)[0]
    if not symptr or not nsym:
        return {}
    strtab = raw[symptr + 18 * nsym:]

    def nm(off):
        b = raw[off:off + 8]
        if b[:4] == b"\0\0\0\0":
            so = struct.unpack_from("<I", b, 4)[0]
            end = strtab.find(b"\0", so)
            return strtab[so:end].decode("latin1")
        return b.rstrip(b"\0").decode("latin1")

    out, i = {}, 0
    while i < nsym:
        off = symptr + 18 * i
        if off + 18 > len(raw):
            break
        sec = struct.unpack_from("<h", raw, off + 12)[0]
        cls, naux = raw[off + 16], raw[off + 17]
        if cls == IMAGE_SYM_CLASS_WEAK_EXTERNAL and sec == 0 and naux:
            aux = off + 18
            tag = struct.unpack_from("<I", raw, aux)[0]
            chars = struct.unpack_from("<I", raw, aux + 4)[0]
            if chars == IMAGE_WEAK_EXTERN_SEARCH_ALIAS and tag < nsym:
                out[nm(off)] = nm(symptr + 18 * tag)
        i += 1 + naux
    return out


# --------------------------------------------------------------------------- #
# inputs
# --------------------------------------------------------------------------- #
def read_weak_records() -> tuple:
    """({weak -> default}, {weak -> {defaults seen}}, {weak -> [units]})."""
    units = json.loads(OBJDIFF.read_text())["units"]
    alias, conflict, where = {}, collections.defaultdict(set), {}
    n_objs = 0
    for u in units:
        bp = u.get("base_path")
        if not bp:
            continue
        p = REPO / bp if not Path(bp).is_absolute() else Path(bp)
        if not p.exists():
            continue
        n_objs += 1
        for w, d in weak_aliases(p.read_bytes()).items():
            conflict[w].add(d)
            alias[w] = d
            where.setdefault(w, []).append(u["name"])
    return alias, conflict, where, n_objs


def read_retail_map() -> dict:
    """{symbol -> {addresses}} from the shipped MSVC linker map."""
    addrs = collections.defaultdict(set)
    for line in RETAIL_MAP.read_text(errors="replace").splitlines():
        m = MAP_LINE.match(line)
        if m:
            addrs[m.group(1)].add("0x" + m.group(2).lower())
    return addrs


def read_target_names() -> dict:
    """{address -> the name dtk stamped there} -- gate (c)'s survivor oracle."""
    out = {}
    for a, n in json.loads(TARGET_SYMBOL_MAP.read_text()).items():
        if isinstance(a, str) and a.startswith("0x") and isinstance(n, str):
            out.setdefault(a.lower(), n)
    return out


# --------------------------------------------------------------------------- #
# derivation
# --------------------------------------------------------------------------- #
def derive(doc: dict) -> dict:
    alias, conflict, where, n_objs = read_weak_records()
    mapaddr = read_retail_map()
    tgt_at = read_target_names()

    grp_of, addr_of_group = {}, {}
    for g in doc["groups"]:
        for n in (g["survivor"], *g.get("folded", [])):
            grp_of[n] = g
        if g.get("address"):
            addr_of_group[id(g)] = g["address"].lower()

    census = collections.Counter()
    extends, fresh, leads, refusals = [], [], [], []

    for weak, default in sorted(alias.items()):
        if len(conflict[weak]) > 1:
            census["refused: weak name declared with two different defaults"] += 1
            refusals.append({"weak": weak, "why": "two defaults",
                             "defaults": sorted(conflict[weak])})
            continue
        gw, gd = grp_of.get(weak), grp_of.get(default)
        if gw is not None and gw is gd:
            census["already in one alias group"] += 1
            continue
        if gw is not None and gd is not None:
            census["refused: the two names are in two DIFFERENT groups"] += 1
            refusals.append({"weak": weak, "default": default,
                             "why": "would merge two adjudicated classes",
                             "groups": [gw["name"], gd["name"]]})
            continue
        aw, ad = mapaddr.get(weak, set()), mapaddr.get(default, set())
        if not aw and not ad:
            census["refused (LEAD): neither name is in the retail map"] += 1
            leads.append({"weak": weak, "default": default,
                          "units": where.get(weak, [])[:4],
                          "why": "no address in orig/373307D9/ham_xbox_r.map, so "
                                 "nothing anchors a group and gen_icf_alias_map "
                                 "would skip it"})
            continue
        if not (aw & ad):
            if aw and ad:
                census["refused: the map gives them DIFFERENT addresses"] += 1
                refusals.append({"weak": weak, "default": default,
                                 "why": "real divergence, not a fold",
                                 "weak_addrs": sorted(aw),
                                 "default_addrs": sorted(ad)})
            else:
                census["refused: only one of the two is in the retail map"] += 1
                refusals.append({"weak": weak, "default": default,
                                 "why": "unanchorable",
                                 "weak_addrs": sorted(aw),
                                 "default_addrs": sorted(ad)})
            continue

        addr = sorted(aw & ad)[0]
        host = gw or gd
        if host is not None:
            if addr_of_group.get(id(host)) != addr:
                census["refused: host group is anchored at another address"] += 1
                refusals.append({"weak": weak, "default": default,
                                 "why": "group address disagrees with the map",
                                 "group": host["name"],
                                 "group_address": addr_of_group.get(id(host)),
                                 "map_address": addr})
                continue
            missing = weak if gw is None else default
            census["EXTEND an installed group"] += 1
            extends.append({"group": host["name"], "address": addr,
                            "add": missing, "declared_by": weak,
                            "resolves_to": default})
            grp_of[missing] = host
            continue

        survivor = tgt_at.get(addr)
        if survivor not in (weak, default):
            census["refused: target names neither spelling at that address"] += 1
            refusals.append({"weak": weak, "default": default, "address": addr,
                             "why": "gate (c): the target must define exactly "
                                    "one member and it must be the survivor",
                             "target_names": survivor})
            continue
        folded = default if survivor == weak else weak
        census["FRESH group"] += 1
        fresh.append({"name": f"{FRESH_TIER}{survivor}@{addr}",
                      "survivor": survivor, "address": addr,
                      "folded": [folded], "evidence": EVIDENCE})
        grp_of[survivor] = grp_of[folded] = fresh[-1]

    return {"n_base_objs": n_objs, "n_weak_records": len(alias),
            "census": dict(census), "extends": extends, "fresh": fresh,
            "leads": leads, "refusals": refusals}


# --------------------------------------------------------------------------- #
# install / revert
# --------------------------------------------------------------------------- #
def revert(doc: dict) -> int:
    """Undo whatever the recorded ledger says a previous run did."""
    led = doc.get("_provenance", {}).get(LEDGER_KEY)
    if not led:
        return 0
    undone = 0
    by_name = {g["name"]: g for g in doc["groups"]}
    for e in led.get("extends", []):
        g = by_name.get(e["group"])
        if g and e["add"] in g.get("folded", []):
            g["folded"] = [n for n in g["folded"] if n != e["add"]]
            rec = [r for r in g.get("weak_alias_extended", [])
                   if r.get("added") != e["add"]]
            if rec:
                g["weak_alias_extended"] = rec
            else:
                g.pop("weak_alias_extended", None)
            undone += 1
    minted = {f["name"] for f in led.get("fresh", [])}
    if minted:
        before = len(doc["groups"])
        doc["groups"] = [g for g in doc["groups"] if g["name"] not in minted]
        undone += before - len(doc["groups"])
    doc["_provenance"].pop(LEDGER_KEY, None)
    return undone


def apply(doc: dict, res: dict) -> None:
    by_name = {g["name"]: g for g in doc["groups"]}
    for e in res["extends"]:
        g = by_name[e["group"]]
        if e["add"] in g.setdefault("folded", []):
            continue
        # Insert in sorted position when the list is already sorted. Not
        # cosmetic: `retail_map_fold_reconcile` writes its `folded` lists
        # sorted and re-attaches foreign members sorted, so appending here
        # would leave the two tools disagreeing on ORDER while agreeing on the
        # member set -- and its `--check` compares the rendered file, so it
        # would report STALE forever. Revert removes by value either way.
        if g["folded"] == sorted(g["folded"]):
            g["folded"] = sorted(g["folded"] + [e["add"]])
        else:
            g["folded"].append(e["add"])
        # Recorded in the ledger too, so `retail_map_fold_reconcile.apply` can
        # re-attach it verbatim when it re-mints a group it owns, instead of
        # fabricating evidence it has no standing to write.
        e["group_record"] = {
            "added": e["add"], "declared_by": e["declared_by"],
            "resolves_to": e["resolves_to"], "evidence": EVIDENCE,
            "what_would_drop_it": (
                "our objects ceasing to emit the IMAGE_WEAK_EXTERN_SEARCH_ALIAS "
                "record, or the retail map ceasing to place both names at "
                + e["address"])}
        g.setdefault("weak_alias_extended", []).append(e["group_record"])
    # Appended, never re-sorted: `revert` has to be an exact inverse, and the
    # selftest asserts apply-then-revert is a fixed point of the whole file.
    doc["groups"].extend(res["fresh"])
    doc["_provenance"][LEDGER_KEY] = {
        "tool": "scripts/weak_alias_reconcile.py",
        "n_base_objs": res["n_base_objs"],
        "n_weak_records": res["n_weak_records"],
        "census": res["census"],
        "extends": res["extends"],
        "fresh": [f["name"] for f in res["fresh"]],
        "fresh_tier_prefix": FRESH_TIER,
        "n_names_added": len(res["extends"]) + sum(
            1 + len(f["folded"]) for f in res["fresh"]),
        "leads_not_installed": res["leads"],
        "what": (
            "Evidence class 2, re-derived against the CURRENT tree. MSVC "
            "declares ??_E<Class> as an undefined weak external whose aux "
            "record names ??_G<Class> (characteristics 2, "
            "IMAGE_WEAK_EXTERN_SEARCH_ALIAS); the pair is admitted only when "
            "orig/373307D9/ham_xbox_r.map ALSO places both names at one "
            "address. The retail-map tier structurally cannot reach these "
            "names: a weak external is a reference, our objects define no "
            "body, and its byte-identity gate drops them unproven. "
            "--apply reverts this ledger, re-derives and re-applies.")}


def render_census(res: dict) -> str:
    w = max((len(k) for k in res["census"]), default=0)
    lines = [f"{res['n_base_objs']} base objs, "
             f"{res['n_weak_records']} weak SEARCH_ALIAS records",
             "census (weak records):"]
    for k, v in sorted(res["census"].items(), key=lambda kv: -kv[1]):
        lines.append(f"  {v:8d}  {k:<{w}}")
    lines.append(f"actions: {len(res['extends'])} extend, "
                 f"{len(res['fresh'])} fresh, "
                 f"{len(res['leads'])} lead(s) reported not installed, "
                 f"{len(res['refusals'])} refused")
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# selftest -- negative controls
# --------------------------------------------------------------------------- #
def selftest() -> int:
    ok = True

    def check(name, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {name}")
        ok = ok and cond

    print("selftest: the reader must be able to say NO")
    # 1. a COFF with no weak externals yields nothing
    check("empty/short buffer -> {}", weak_aliases(b"") == {})
    # 2. characteristics != SEARCH_ALIAS is not an alias
    real = None
    units = json.loads(OBJDIFF.read_text())["units"]
    for u in units:
        bp = u.get("base_path")
        p = (REPO / bp) if bp else None
        if p and p.exists() and weak_aliases(p.read_bytes()):
            real = p
            break
    check("some base object does carry a SEARCH_ALIAS record", real is not None)
    if real is not None:
        raw = bytearray(real.read_bytes())
        symptr = struct.unpack_from("<I", raw, 8)[0]
        nsym = struct.unpack_from("<I", raw, 12)[0]
        i, flipped = 0, False
        while i < nsym:
            off = symptr + 18 * i
            cls, naux = raw[off + 16], raw[off + 17]
            sec = struct.unpack_from("<h", raw, off + 12)[0]
            if cls == IMAGE_SYM_CLASS_WEAK_EXTERNAL and sec == 0 and naux:
                struct.pack_into("<I", raw, off + 18 + 4, 1)  # SEARCH_NOLIBRARY
                flipped = True
            i += 1 + naux
        check("flipping characteristics 2 -> 1 empties the read",
              flipped and weak_aliases(bytes(raw)) == {})
    doc = json.loads(ALIAS.read_text())
    res = derive(doc)

    # 3. INJECTED negative control: a weak record whose declared default the map
    #    places at a DIFFERENT address must be refused, not admitted. Without
    #    this the address gate is never observed failing, and a gate you have
    #    not watched refuse is not a gate.
    mapaddr = read_retail_map()
    two = [(n, sorted(a)[0]) for n, a in mapaddr.items() if len(a) == 1]
    a1 = next(x for x in two if not x[0].startswith("__"))
    a2 = next(x for x in two if x[1] != a1[1] and not x[0].startswith("__"))
    real_reader = globals()["read_weak_records"]
    try:
        globals()["read_weak_records"] = lambda: (
            {a1[0]: a2[0]}, {a1[0]: {a2[0]}}, {a1[0]: ["synthetic"]}, 1)
        neg = derive(json.loads(ALIAS.read_text()))
        check("a map-split pair is REFUSED, not admitted",
              not neg["extends"] and not neg["fresh"]
              and neg["census"].get(
                  "refused: the map gives them DIFFERENT addresses") == 1)
        # positive control on the same injection path, so the refusal above is
        # not just "derive() admits nothing under injection"
        globals()["read_weak_records"] = lambda: (
            {a1[0]: a1[0]}, {a1[0]: {a1[0]}}, {a1[0]: ["synthetic"]}, 1)
        pos = derive(json.loads(ALIAS.read_text()))
        check("the injection path can reach an admit/already-grouped verdict",
              not pos["census"].get(
                  "refused: the map gives them DIFFERENT addresses"))
    finally:
        globals()["read_weak_records"] = real_reader
    # 4. every admitted name must be at the group's own address in the map
    mapaddr = read_retail_map()
    bad = [e for e in res["extends"] if e["address"] not in mapaddr.get(e["add"], set())]
    check("every EXTEND name is in the map at the group's address", not bad)
    badf = [f for f in res["fresh"]
            if f["address"] not in mapaddr.get(f["survivor"], set())
            or any(f["address"] not in mapaddr.get(n, set()) for n in f["folded"])]
    check("every FRESH member is in the map at the group's address", not badf)
    # 5. the --apply cycle (revert -> derive -> apply) must be a fixed point of
    #    the installed file, and revert must be an exact inverse of apply. Both
    #    directions, because either one alone can pass on a no-op.
    import copy
    d2 = copy.deepcopy(doc)
    before = json.dumps(d2, sort_keys=True)
    revert(d2)
    apply(d2, derive(d2))
    check("the --apply cycle is a fixed point of the installed file",
          json.dumps(d2, sort_keys=True) == before)

    d3 = copy.deepcopy(doc)
    revert(d3)
    stripped = json.dumps(d3, sort_keys=True)
    res3 = derive(d3)
    check("reverting an installed ledger actually removes something",
          bool(res3["extends"] or res3["fresh"]))
    apply(d3, res3)
    revert(d3)
    check("apply-then-revert returns to the reverted file",
          json.dumps(d3, sort_keys=True) == stripped)
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", metavar="OUT")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()

    doc = json.loads(ALIAS.read_text())

    if a.uninstall:
        n = revert(doc)
        ALIAS.write_text(json.dumps(doc, indent=1) + "\n")
        print(f"reverted {n} ledgered change(s); wrote {ALIAS}")
        return 0

    if a.apply:
        revert(doc)
    res = derive(doc)
    print(render_census(res))
    for e in res["extends"]:
        print(f"  EXTEND {e['address']}  {e['group']}")
        print(f"         + {e['add']}")
    for f in res["fresh"]:
        print(f"  FRESH  {f['address']}  {f['survivor']}")
        for n in f["folded"]:
            print(f"         + {n}")
    for ld in res["leads"]:
        print(f"  LEAD   {ld['weak']} -> {ld['default']}  ({ld['units'][:2]})")

    if a.check:
        stale = bool(res["extends"] or res["fresh"])
        print("STALE" if stale else "current")
        return 1 if stale else 0

    if a.report:
        Path(a.report).write_text(json.dumps(res, indent=1) + "\n")
        print(f"wrote {a.report}")
        return 0

    if a.apply:
        apply(doc, res)
        ALIAS.write_text(json.dumps(doc, indent=1) + "\n")
        print(f"wrote {ALIAS}")
        return 0

    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
