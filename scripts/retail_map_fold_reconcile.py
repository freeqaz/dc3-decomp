#!/usr/bin/env python3
"""Reconcile the installed ICF alias set with the retail map AND the current split.

The gap this closes
-------------------
`scripts/symbol_aliases.json` records each fold class by NAME. Two of its
producers derived those names from artifacts that have since moved:

  * **the split.** `config/373307D9/symbols.txt` decides which of a fold class's
    spellings dtk stamps on the survivor address, and it has been rewritten
    several times since the alias tiers were installed (`icf-survivor-names`,
    2026-08-19). **380 of the 1,920 addressed groups name a survivor the target
    objects no longer define** — by decomp-synth `validate_groups`' own gate (c)
    those groups are invalid TODAY, and the address can never be re-derived
    because every generator treats an installed address as taken.
  * **our own tree.** A class member is only admitted when our objects emit or
    reference that spelling. Every spelling the decomp started emitting AFTER
    the tier was installed is missing, and nothing adds it: the retail-map
    generator's collision gate skips the whole address
    ("collides with an installed alias group", 2,038 addresses), so it can
    create a group but never extend one. `HttpGet::Poll` hit exactly this —
    typing `mState` as `HttpGet::State` made our object emit
    `MakeString<HttpGet::State>`, which the retail map places at the survivor
    address `0x8255a0a0` alongside the three spellings already admitted, and
    the row stayed charged anyway.

Neither is a disagreement with the evidence. Both are the alias file being a
snapshot of a derivation nobody re-ran.

What this does
--------------
For every address in `orig/373307D9/ham_xbox_r.map` it re-runs the retail-map
tier's four map-residency gates (`scripts/retail_map_fold_candidates.py`'s
module docstring states them; they are re-used from that module, not
reimplemented) and then reconciles the result with what is installed:

  FRESH      no group at that address        -> mint one
  EXTEND     a group exists, members missing -> add them
  RE-ANCHOR  a group exists, its survivor is not the name the TARGET objects
             define at that address -> relabel, so gate (c) passes again

and refuses everything else, counting each refusal.

Gate 5 -- byte identity, and it is the whole point
--------------------------------------------------
An alias group makes two names compare EQUAL as relocation targets across the
whole binary. Admitting one that is not a genuine fold silently forgives real
wrong-callee bugs. Map residency alone does not establish that OUR body for a
spelling is the code the linker folded, so **every name this tool ADDS must
pass the condition `/OPT:ICF` itself tests**: our COMDAT's contents equal the
target survivor's, relocation-patched fields masked and relocation targets
compared by name (modulo folds the same map states -- `reloc_canon`). A name
our objects do not DEFINE is dropped, not admitted on residency: there is no
body to check, so there is no proof. That is stricter than the installed
retail-map tier, deliberately.

The EH-funclet extent, and why it is not a relaxation
-----------------------------------------------------
MSVC emits a function's `__unwind$N` / `__catch$N` funclets into the SAME
`.text` COMDAT as the function, as STATIC symbols; dtk's split names only the
function. Comparing whole sections therefore charged 49 of our 623 candidate
bodies with a tail that is not part of the function at all --
`?Handle@SongSort@@` read as 460 bytes against the target's 316, and the 316
bytes before the funclet were identical with identical relocations. So both
sides are cut at their first EH funclet before comparison. This is measured,
not assumed: the cut fires on 49 of our bodies and **0 of 511 target bodies**,
which is what "dtk names the function only" predicts.

It is not a way to forgive a longer body. `__uninitialized_copy<Label*>` still
REFUSES against `__uninitialized_copy<const SampleMarker*>`: our body is 104
real bytes to the target's 96, its funclet sits above both, and the trim
therefore changes nothing. That row is an independently-identified real
divergence (the target instantiates the STL copy path on `const T*` and we on
`T*`, +8 B, `docs/analysis/icf-fold-pairing-20260821.md`), and it is the
negative control this tool did not have to invent.

Denominator
-----------
Every run prints the full census: map addresses examined, each drop reason with
its count, names offered / admitted / refused / dropped-unproven. A truncated
run is not offered -- there is no `--limit`.

Usage
-----
    python3 scripts/retail_map_fold_reconcile.py --report OUT      # derive only
    python3 scripts/retail_map_fold_reconcile.py --apply           # install
    python3 scripts/retail_map_fold_reconcile.py --check           # 1 if stale
    python3 scripts/retail_map_fold_reconcile.py --uninstall
    python3 scripts/retail_map_fold_reconcile.py --selftest        # negative controls

`--apply` is idempotent: it first REVERTS everything the recorded ledger says a
previous run did, then re-derives from scratch and re-applies. Re-run it after
a rebuild or a `symbols.txt` change and it converges. It never renders the map;
run `scripts/gen_icf_alias_map.py` (or `ninja`) for that.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

# Gates 1-4, the map reader, the COMDAT reader and the byte-identity verdict are
# IMPORTED, never reimplemented: this tool has to admit on the same predicate the
# installed tier was admitted on, or it is a different standard wearing the same
# name.
from retail_map_fold_candidates import (  # noqa: E402
    NOT_A_FOLD, coff_names, comdat_contents, content_verdict, reloc_canon,
    retail_map,
)

REPO = Path(__file__).resolve().parent.parent
ALIAS = REPO / "scripts" / "symbol_aliases.json"
LEDGER_KEY = "retail_map_reconcile"
FRESH_TIER = "retailmap-fn:"

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
EH_FUNCLET = re.compile(r"^(?:__unwind\$|__catch\$)")

# `retail_map_fold_candidates.NOT_A_FOLD` excludes four name shapes from a fold
# class, carried forward from the original installer as "names that share an
# address with the thing they ANNOTATE rather than aliasing it". That reading is
# right for `__unwind$` / `__catch$` / `$L` and WRONG for `??_C@`: a string
# literal is not an annotation on its neighbour, it is a COMDAT the linker pools
# BY CONTENT, so two `??_C@` names at one address are the same bytes -- which is
# precisely a fold. Keeping the exclusion cost `Memcard::GetDisplayName` its last
# row: the target names `math:SHA1.obj`'s narrow empty string and we name
# `os/Memcard_Xbox`'s wide one, `??_C@_01LOCGONAA@` vs `??_C@_11LOCGONAA@`, both
# `00 00`, both at `0x8205f1d0`.
#
# The blast radius is measured, not argued: dropping `??_C@` makes exactly THREE
# map addresses newly reachable binary-wide, of which one survives the member
# gates. It is that small for a structural reason -- the mangled name encodes
# the content, so identical bytes normally give identical NAMES and one symbol;
# two names for one address needs the narrow/wide (`_0`/`_1`) split.
#
# And it cannot forgive a wrong string. Gate 5 requires our COMDAT's bytes to
# equal the target survivor's, so a divergent assert text -- different content,
# therefore a different address, therefore never a candidate -- stays charged.
NOT_A_FOLD_RECONCILE = re.compile(r"^(?:__unwind\$|__catch\$|\$L)")

COMMENT_ADDITION = [
    "  - retail-map RECONCILE (scripts/retail_map_fold_reconcile.py, group",
    "    names 'retailmap-fn:' plus in-place extensions of existing groups):",
    "    the same linker-map address-sharing evidence, re-derived against the",
    "    CURRENT split and the CURRENT tree, because both moved after the",
    "    tiers above were installed. Every name it adds additionally passes a",
    "    byte-identity gate -- our COMDAT equals the target survivor's, relocs",
    "    masked and reloc targets equal by name -- and a name our objects do",
    "    not DEFINE is dropped rather than admitted on residency. The ledger",
    "    in _provenance.retail_map_reconcile makes the change revertible and",
    "    the whole thing re-derivable: --apply reverts, re-derives, re-applies.",
]


# --------------------------------------------------------------------------- #
# EH funclet extent
# --------------------------------------------------------------------------- #
def eh_funclet_extents(path: Path, wanted: set) -> dict:
    """{external name -> bytes of code before the first EH funclet above it}.

    `None` when the section carries no funclet above the symbol, which is the
    "compare the whole COMDAT" case and leaves `comdat_contents` untouched.
    """
    d = path.read_bytes()
    nsec, = struct.unpack_from("<H", d, 2)
    psym, nsym = struct.unpack_from("<II", d, 8)
    if not psym or not nsym:
        return {}
    opt, = struct.unpack_from("<H", d, 16)
    strt = psym + nsym * 18
    rows = []
    i = 0
    while i < nsym:
        rec = d[psym + i * 18: psym + i * 18 + 18]
        if rec[:4] == b"\0\0\0\0":
            off, = struct.unpack_from("<I", rec, 4)
            nm = d[strt + off:d.index(b"\0", strt + off)].decode("latin1")
        else:
            nm = rec[:8].rstrip(b"\0").decode("latin1")
        value, = struct.unpack_from("<I", rec, 8)
        secnum, = struct.unpack_from("<h", rec, 12)
        rows.append((nm, secnum, value, rec[16]))
        i += 1 + rec[17]
    funclets = collections.defaultdict(list)
    for nm, secnum, value, sclass in rows:
        if secnum > 0 and sclass == IMAGE_SYM_CLASS_STATIC and EH_FUNCLET.match(nm):
            funclets[secnum].append(value)
    out = {}
    for nm, secnum, value, sclass in rows:
        if nm in wanted and secnum > 0 and sclass == IMAGE_SYM_CLASS_EXTERNAL:
            above = [v for v in funclets.get(secnum, ()) if v > value]
            out[nm] = (min(above) - value) if above else None
    return out


def cut_at_funclet(body, extent):
    """`(bytes, relocs)` truncated to `extent`; identity when `extent` is None."""
    raw, rel = body
    if extent is None or extent >= len(raw):
        return body
    return raw[:extent], tuple(r for r in rel if r[0] < extent)


# --------------------------------------------------------------------------- #
# reading the tree
# --------------------------------------------------------------------------- #
def read_symbol_sets(repo: Path):
    units = json.loads((repo / "objdiff.json").read_text())["units"]
    used, tgt_def, our_def, tgt_code = set(), set(), set(), set()
    paths = {"target": [], "base": []}
    for u in units:
        tp, bp = u.get("target_path"), u.get("base_path")
        if tp and (repo / tp).exists():
            paths["target"].append(repo / tp)
            c, dd, ref = coff_names(repo / tp)
            tgt_code |= c
            tgt_def |= c | dd
            used |= ref
        if bp and (repo / bp).exists():
            paths["base"].append(repo / bp)
            c, dd, ref = coff_names(repo / bp)
            our_def |= c | dd
            used |= c | dd | ref
    used |= tgt_def
    return paths, used, tgt_def, our_def, tgt_code


def read_bodies(paths, wanted):
    """({name: body} per side, {name: funclet extent} per side, inconsistencies).

    A name defined more than once on one side with DIFFERENT contents has no
    single body to check, so it is recorded and refused rather than resolved.
    """
    def side(files):
        seen = collections.defaultdict(list)
        extents = {}
        for p in files:
            for k, v in comdat_contents(p, wanted).items():
                seen[k].append(v)
            extents.update({k: v for k, v in eh_funclet_extents(p, wanted).items()
                            if k not in extents or v is not None})
        bodies, bad = {}, []
        for k, v in seen.items():
            if any(x != v[0] for x in v):
                bad.append(k)
            else:
                bodies[k] = v[0]
        return bodies, extents, bad
    t, te, tb = side(paths["target"])
    o, oe, ob = side(paths["base"])
    return t, te, tb, o, oe, ob


# --------------------------------------------------------------------------- #
# derivation
# --------------------------------------------------------------------------- #
def derive(repo: Path, identity: str = "align", verbose=True) -> dict:
    paths, used, tgt_def, our_def, tgt_code = read_symbol_sets(repo)
    v2n = retail_map()
    doc = json.loads(ALIAS.read_text())
    doc = revert(doc)          # always derive against the PRISTINE alias set
    groups = doc["groups"]

    by_addr = {}
    dup_addr = set()
    for gi, g in enumerate(groups):
        if not g.get("address"):
            continue
        va = int(str(g["address"]).lower().removeprefix("0x"), 16)
        if va in by_addr:
            dup_addr.add(va)
        by_addr[va] = (gi, g)
    spoken = collections.Counter()
    for g in groups:
        for n in (g["survivor"], *(g.get("folded") or [])):
            spoken[n] += 1

    census = collections.Counter()
    work = []
    for va, raw in sorted(v2n.items()):
        census["map addresses examined"] += 1
        names = [n for n in raw if not NOT_A_FOLD_RECONCILE.match(n)]
        if len(names) < 2:
            census["drop: fewer than two non-annotation names in the map"] += 1
            continue
        members = [n for n in names if n in used]
        if len(members) < 2:
            census["drop: fewer than two members either side uses"] += 1
            continue
        defined = [n for n in members if n in tgt_def]
        if len(defined) != 1:
            census["drop: target objects define %s of the members"
                   % ("none" if not defined else "several")] += 1
            continue
        survivor = defined[0]
        if va in dup_addr:
            census["drop: more than one installed group at the address"] += 1
            continue
        inst = by_addr.get(va)
        if inst is None:
            clash = [n for n in members if spoken[n]]
            if clash:
                census["drop: fresh, but a member is spoken in a group elsewhere"] += 1
                continue
            census["FRESH"] += 1
            work.append({"mode": "fresh", "va": va, "survivor": survivor,
                         "offer": [n for n in members if n != survivor],
                         "gi": None, "reanchor_from": None,
                         "n_map_names_at_addr": len(raw),
                         "kind": "code" if survivor in tgt_code else "data"})
            continue
        gi, g = inst
        have = set([g["survivor"]] + list(g.get("folded") or []))
        offer = [n for n in members if n not in have]
        reanchor = g["survivor"] != survivor
        if not offer and not reanchor:
            census["installed and already complete"] += 1
            continue
        clash = [n for n in offer if spoken[n]]
        if clash:
            census["drop: an offered name is spoken in a group elsewhere"] += 1
            continue
        census["EXTEND" if offer else "RE-ANCHOR only"] += 1
        if offer and reanchor:
            census["  ...of which also RE-ANCHOR"] += 1
        work.append({"mode": "extend", "va": va, "survivor": survivor,
                     "offer": offer, "gi": gi,
                     "reanchor_from": g["survivor"] if reanchor else None,
                     "n_map_names_at_addr": len(raw),
                     "kind": "code" if survivor in tgt_code else "data"})

    # ---- gate 5 ----------------------------------------------------------- #
    wanted = set()
    for w in work:
        wanted.add(w["survivor"])
        wanted |= set(w["offer"])
    tb, te, t_bad, ob, oe, o_bad = read_bodies(paths, wanted)
    canon = None if identity == "strict" else reloc_canon(v2n)

    names = collections.Counter()
    refusals = []
    for w in work:
        ref = tb.get(w["survivor"])
        if ref is None:
            w["admitted"] = []
            w["gate5"] = ("no COMDAT for the survivor in the target objects"
                          if w["survivor"] not in t_bad else
                          "the target objects define the survivor inconsistently")
            names["group refused: " + w["gate5"]] += 1
            continue
        ref = cut_at_funclet(ref, te.get(w["survivor"]))
        w["gate5"] = "ok"
        admitted, checks = [], []
        for m in w["offer"]:
            names["names offered"] += 1
            body = ob.get(m)
            if body is None:
                why = ("our objects define it inconsistently" if m in o_bad
                       else "our objects define no body for this name")
                checks.append([m, "DROP", why])
                names["dropped unproven: " + why] += 1
                continue
            body = cut_at_funclet(body, oe.get(m))
            ok, why = content_verdict(body, ref, w["va"], identity, canon)
            checks.append([m, "ADMIT" if ok else "REFUSE", why])
            names[("admitted: " if ok else "REFUSED by byte identity: ")
                  + why.split(";")[0]] += 1
            if ok:
                admitted.append(m)
            else:
                refusals.append({"address": "0x%08x" % w["va"], "ours": m,
                                 "survivor": w["survivor"], "why": why})
        w["admitted"] = admitted
        w["checks"] = checks

    # A re-anchor is only worth doing if the group ends up holding the
    # target-resident name; otherwise gate (c) still fails and we have relabelled
    # for nothing.
    actions = []
    for w in work:
        if w.get("gate5") != "ok":
            continue
        if w["mode"] == "fresh":
            if not w["admitted"]:
                census["fresh group dropped: no offered name survived gate 5"] += 1
                continue
            actions.append(w)
            continue
        need = w["reanchor_from"] is not None and w["survivor"] in w["offer"]
        if need and w["survivor"] not in w["admitted"]:
            census["re-anchor dropped: the target-resident name failed gate 5"] += 1
            continue
        if not w["admitted"] and w["reanchor_from"] is None:
            continue
        actions.append(w)

    return {
        "identity_mode": identity,
        "census": dict(census),
        "name_census": dict(names),
        "refusals": refusals,
        "actions": actions,
        "n_target_objs": len(paths["target"]),
        "n_base_objs": len(paths["base"]),
    }


# --------------------------------------------------------------------------- #
# apply / revert
# --------------------------------------------------------------------------- #
def revert(doc: dict) -> dict:
    """Undo whatever the recorded ledger says a previous run did.

    Order within `folded` is NOT restored (the lists are re-sorted on apply);
    the member SET is, which is the only thing objdiff or any gate reads.
    """
    led = (doc.get("_provenance") or {}).get(LEDGER_KEY)
    if not led:
        return doc
    doc = json.loads(json.dumps(doc))
    doc["groups"] = [g for g in doc["groups"]
                     if not str(g.get("name", "")).startswith(FRESH_TIER)]
    by_addr = {}
    for g in doc["groups"]:
        if g.get("address"):
            by_addr[int(str(g["address"]).lower().removeprefix("0x"), 16)] = g
    added = {int(k, 16): set(v) for k, v in (led.get("added") or {}).items()}
    reanchored = {int(k, 16): v for k, v in (led.get("reanchored") or {}).items()}
    for va, names in added.items():
        g = by_addr.get(va)
        if g:
            g["folded"] = [n for n in (g.get("folded") or []) if n not in names]
    for va, mv in reanchored.items():
        g = by_addr.get(va)
        if not g:
            continue
        folded = [n for n in (g.get("folded") or []) if n != mv["from"]]
        if mv["to"] not in added.get(va, ()) and mv["to"] not in folded:
            folded.append(mv["to"])
        g["survivor"] = mv["from"]
        g["folded"] = sorted(folded)
    prov = doc.get("_provenance") or {}
    prov.pop(LEDGER_KEY, None)
    doc["_provenance"] = prov
    doc["_comment"] = [c for c in doc.get("_comment", [])
                       if c not in COMMENT_ADDITION]
    return doc


def apply(doc: dict, result: dict) -> dict:
    doc = revert(doc)
    doc = json.loads(json.dumps(doc))
    by_addr = {}
    for g in doc["groups"]:
        if g.get("address"):
            by_addr[int(str(g["address"]).lower().removeprefix("0x"), 16)] = g

    added, reanchored, fresh = {}, {}, []
    for w in sorted(result["actions"], key=lambda w: w["va"]):
        key = "0x%08x" % w["va"]
        ev = (f"orig/373307D9/ham_xbox_r.map: {w['n_map_names_at_addr']} public "
              f"name(s) share {key}; /OPT:ICF folds byte-identical COMDATs, so "
              f"the linker that made the image states this fold set. The target "
              f"objects define exactly one member ({w['survivor']}), which is "
              f"therefore the survivor. Every name added here additionally "
              f"passed the byte-identity gate: our COMDAT equals the survivor's "
              f"with relocation-patched fields masked and relocation targets "
              f"equal by name (both sides cut at their first EH funclet). Names "
              f"our objects do not DEFINE were dropped, not admitted.")
        if w["mode"] == "fresh":
            doc["groups"].append({
                "name": f"{FRESH_TIER}{w['survivor'][:40]}@{key}",
                "address": key,
                "survivor": w["survivor"],
                "folded": sorted(w["admitted"]),
                "n_map_names_at_addr": w["n_map_names_at_addr"],
                "kind": w["kind"],
                "evidence": ev,
            })
            fresh.append(key)
            continue
        g = by_addr[w["va"]]
        folded = set(g.get("folded") or [])
        if w["admitted"]:
            folded |= set(w["admitted"])
            added[key] = sorted(w["admitted"])
        if w["reanchor_from"] is not None:
            folded.discard(w["survivor"])
            folded.add(w["reanchor_from"])
            reanchored[key] = {"from": w["reanchor_from"], "to": w["survivor"]}
            g["survivor"] = w["survivor"]
        g["folded"] = sorted(folded)
        g["reconciled"] = ev

    comment = [c for c in doc.get("_comment", []) if c not in COMMENT_ADDITION]
    anchor = next((i for i, c in enumerate(comment)
                   if c.startswith("Every tier here") or c.startswith("This file holds")),
                  len(comment))
    doc["_comment"] = comment[:anchor] + COMMENT_ADDITION + comment[anchor:]

    prov = doc.get("_provenance") or {}
    prov[LEDGER_KEY] = {
        "tool": "scripts/retail_map_fold_reconcile.py",
        "identity_mode": result["identity_mode"],
        "n_target_objs": result["n_target_objs"],
        "n_base_objs": result["n_base_objs"],
        "added": added,
        "reanchored": reanchored,
        "fresh_groups": fresh,
        "fresh_tier_prefix": FRESH_TIER,
        "n_names_added": sum(len(v) for v in added.values()),
        "n_groups_extended": len(added),
        "n_groups_reanchored": len(reanchored),
        "n_groups_fresh": len(fresh),
        "census": result["census"],
        "name_census": result["name_census"],
        "refused_by_byte_identity": result["refusals"],
        "what": (
            "The alias file records fold classes by NAME, and two of the things "
            "those names were derived from moved afterwards: config/373307D9/"
            "symbols.txt (which spelling dtk stamps on the survivor address) and "
            "our own object set (which spellings we emit). Nothing re-derived "
            "the file, and every generator treats an installed address as taken, "
            "so a class could never be extended or re-anchored. This ledger is "
            "that re-derivation. Re-run --apply after a rebuild or a symbols.txt "
            "change; it reverts itself first, so it converges."),
    }
    doc["_provenance"] = prov
    return doc


def dumps(doc: dict) -> str:
    return json.dumps(doc, indent=1) + "\n"


# --------------------------------------------------------------------------- #
# negative controls
# --------------------------------------------------------------------------- #
def selftest() -> int:
    """Watch every gate REFUSE something before believing it can accept.

    Each case is a fixture, so this runs without a build and cannot be made to
    pass by the tree happening to be in a good state.
    """
    fails = []

    def check(label, cond, detail=""):
        print(f"  {'PASS' if cond else 'FAIL'}  {label}" + (f"  -- {detail}" if detail else ""))
        if not cond:
            fails.append(label)

    print("negative controls (each must REFUSE):")
    canon = reloc_canon({0x1000: ["a_folded", "b_folded"]})
    body = (b"\x38\x60\x00\x01\x4e\x80\x00\x20", ((0, 1, "callee"),))

    ok, why = content_verdict(body, body, 0x82000000, "align", canon)
    check("control positive: identical body ADMITS", ok, why)

    flipped = (b"\x38\x60\x00\x02\x4e\x80\x00\x20", ((0, 1, "callee"),))
    ok, why = content_verdict(flipped, body, 0x82000000, "align", canon)
    check("one flipped instruction byte is REFUSED", not ok, why)

    other = (body[0], ((0, 1, "a_different_callee"),))
    ok, why = content_verdict(other, body, 0x82000000, "align", canon)
    check("a different relocation target is REFUSED", not ok, why)

    foldedA = (body[0], ((0, 1, "a_folded"),))
    foldedB = (body[0], ((0, 1, "b_folded"),))
    ok, why = content_verdict(foldedA, foldedB, 0x82000000, "align", canon)
    check("...unless the MAP ITSELF folds the two callees", ok, why)

    longer = (body[0] + b"\x60\x00\x00\x00" * 2, body[1])
    ok, why = content_verdict(longer, body, 0x82000000, "align", canon)
    check("our body longer than the survivor's is REFUSED", not ok, why)

    check("cut_at_funclet(None) is the identity",
          cut_at_funclet(longer, None) == longer)
    check("cut_at_funclet does not fire above the body",
          cut_at_funclet(longer, 999) == longer)
    cut = cut_at_funclet(longer, len(body[0]))
    check("cut_at_funclet(len) reproduces the untrimmed body exactly",
          cut == body, f"{cut!r}")

    print("structural controls (gates 1-4, on synthetic maps):")
    check("an EH/label annotation is not a fold member",
          all(NOT_A_FOLD_RECONCILE.match(n)
              for n in ("__unwind$1", "__catch$2", "$LN7")))
    check("a real mangled name is not treated as an annotation",
          not NOT_A_FOLD_RECONCILE.match("?Poll@HttpGet@@QAAXXZ"))
    check("a string literal IS fold-eligible here (it is not an annotation)",
          not NOT_A_FOLD_RECONCILE.match("??_C@_01LOCGONAA@?$AA?$AA@")
          and bool(NOT_A_FOLD.match("??_C@_01LOCGONAA@?$AA?$AA@")),
          "diverges from retail_map_fold_candidates.NOT_A_FOLD on purpose")
    empty_narrow = (b"\x00\x00", ())
    other_text = (b"bad assert\x00", ())
    ok, why = content_verdict(empty_narrow, empty_narrow, 0x8205f1d0, "align", canon)
    check("...and the wide/narrow empty string pair ADMITS on bytes", ok, why)
    ok, why = content_verdict(other_text, empty_narrow, 0x8205f1d0, "align", canon)
    check("...but a DIFFERENT string literal is REFUSED", not ok, why)

    print("ledger round-trip:")
    base = {"_comment": ["x"], "_provenance": {},
            "groups": [{"name": "g", "address": "0x82000000",
                        "survivor": "S", "folded": ["F1"]}]}
    res = {"identity_mode": "align", "census": {}, "name_census": {},
           "refusals": [], "n_target_objs": 0, "n_base_objs": 0,
           "actions": [{"mode": "extend", "va": 0x82000000, "survivor": "T",
                        "offer": ["T", "F2"], "admitted": ["T", "F2"],
                        "gi": 0, "reanchor_from": "S", "n_map_names_at_addr": 3,
                        "kind": "code"}]}
    applied = apply(base, res)
    g = applied["groups"][0]
    check("apply re-anchors the survivor to the target-resident name",
          g["survivor"] == "T", g["survivor"])
    check("apply keeps the old survivor as a folded member",
          "S" in g["folded"], str(g["folded"]))
    back = revert(applied)
    check("revert restores the survivor", back["groups"][0]["survivor"] == "S")
    check("revert restores the member set",
          set(back["groups"][0]["folded"]) == {"F1"}, str(back["groups"][0]["folded"]))
    check("revert removes the ledger", LEDGER_KEY not in back["_provenance"])
    check("apply is idempotent", dumps(apply(applied, res)) == dumps(applied))

    print(f"\n{len(fails)} failure(s)")
    return 1 if fails else 0


# --------------------------------------------------------------------------- #
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--identity", choices=("align", "strict"), default="align",
                    help="gate 5 strictness (default: %(default)s)")
    ap.add_argument("--report", help="write the full derivation JSON here")
    ap.add_argument("--apply", action="store_true",
                    help="write the reconciled scripts/symbol_aliases.json")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if --apply would change the alias file")
    ap.add_argument("--uninstall", action="store_true",
                    help="revert the ledger and remove it")
    ap.add_argument("--selftest", action="store_true",
                    help="run the negative controls and exit")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    doc = json.loads(ALIAS.read_text())
    if args.uninstall:
        out = dumps(revert(doc))
        if out == dumps(doc):
            print("nothing installed")
            return 0
        ALIAS.write_text(out)
        print(f"reverted {ALIAS}")
        return 0

    result = derive(REPO, args.identity)
    print(f"{result['n_target_objs']} target objs, {result['n_base_objs']} base objs")
    print("census (map addresses):")
    for k, v in sorted(result["census"].items(), key=lambda kv: -kv[1]):
        print(f"  {v:8}  {k}")
    print("census (names offered to gate 5):")
    for k, v in sorted(result["name_census"].items(), key=lambda kv: -kv[1]):
        print(f"  {v:8}  {k}")
    n_add = sum(len(w["admitted"]) for w in result["actions"])
    print(f"actions: {len(result['actions'])} group(s), {n_add} name(s) admitted, "
          f"{len(result['refusals'])} name(s) refused by byte identity")
    for r in result["refusals"]:
        print(f"  REFUSED {r['address']}  {r['why']}")
        print(f"      ours     {r['ours']}")
        print(f"      survivor {r['survivor']}")

    if args.report:
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(json.dumps(result, indent=1) + "\n")
        print(f"wrote {args.report}")

    if args.check or args.apply:
        new = dumps(apply(doc, result))
        if new == dumps(doc):
            print("alias file already reconciled")
            return 0
        if args.check:
            print("STALE: scripts/symbol_aliases.json disagrees with the "
                  "reconciliation\n  fix: python3 scripts/retail_map_fold_reconcile.py "
                  "--apply && python3 scripts/gen_icf_alias_map.py", file=sys.stderr)
            return 1
        ALIAS.write_text(new)
        print(f"wrote {ALIAS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
