#!/usr/bin/env python3
"""Retail-map ICF fold classes the installed alias set cannot see: DATA COMDATs.

Background
----------
`scripts/symbol_aliases.json` already carries a `retail_map_classes` evidence
tier (615 groups, installed 2026-08-10): `/OPT:ICF` folds byte-identical
COMDATs, so names sharing an address in `orig/373307D9/ham_xbox_r.map` are the
fold set, stated by the linker that made the image.  Its installer
(`bench-bank/archive/runs/dc3-retail-map-authority-20260810/scripts/map_classes.py`)
admits an address only when

  1. the map carries >= 2 non-annotation names there, and
  2. >= 2 of them are names our objects reference or define, and
  3. every member is a name one side actually uses, and
  4. the TARGET objects define exactly one member -- that one is the survivor.

Gate 4 is the substantive one: it is what distinguishes a fold (one surviving
spelling) from an over-merge, and it is what picks a deterministic canonical
name.  The installer evaluates 2 and 4 with `coff_ppc.parse(...).funcs`, i.e.
**function COMDATs only**.  Every DATA COMDAT the linker folded is therefore
invisible to it -- not refused on evidence, not seen at all.

What this script does
---------------------
Re-runs exactly those four gates with the symbol read widened from function
COMDATs to the whole COFF external-definition table.  No gate is relaxed, no
class-size cap is raised, no name shape is trusted: the annotation exclusion
(`__unwind$` / `__catch$` / `$L` / `??_C@`) is carried over verbatim and gate 4
still has to find exactly one target-resident member or the address is dropped.

    python3 scripts/retail_map_fold_candidates.py --out <dir>
    python3 scripts/retail_map_fold_candidates.py --out <dir> --emit-map <path>

It does NOT install into `scripts/symbol_aliases.json`.  Whether a data-COMDAT
fold class is admissible on this evidence tier is an owner call; this produces
the candidate set and the measurement so that call has a number attached.
"""
import argparse
import collections
import json
import re
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAP = REPO / "orig" / "373307D9" / "ham_xbox_r.map"
ALIAS = REPO / "scripts" / "symbol_aliases.json"

MAP_LINE = re.compile(
    r"^ ([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(\S.*)$")
# Carried over verbatim from map_classes.py: names that share an address with
# the thing they annotate rather than aliasing it.
NOT_A_FOLD = re.compile(r"^(?:__unwind\$|__catch\$|\$L|\?\?_C@)")

IMAGE_SYM_CLASS_EXTERNAL = 2


IMAGE_SCN_CNT_CODE = 0x20


def coff_names(path: Path):
    """(code defs, data defs, relocation targets) for one COFF object.

    Whole symbol table, both code and data -- this is the single difference
    from the installed generator, which sees function COMDATs only.  The
    code/data split is RECORDED so the report can say what a candidate is; it
    is never used as an admission gate, because ICF folds both.
    """
    d = path.read_bytes()
    nsec, = struct.unpack_from("<H", d, 2)
    psym, nsym = struct.unpack_from("<II", d, 8)
    if not psym or not nsym:
        return set(), set(), set()
    opt, = struct.unpack_from("<H", d, 16)
    sh = 20 + opt
    is_code = {}
    for s in range(nsec):
        chars, = struct.unpack_from("<I", d, sh + s * 40 + 36)
        is_code[s + 1] = bool(chars & IMAGE_SCN_CNT_CODE)
    strt = psym + nsym * 18
    syms, cdef, ddef = [], set(), set()
    i = 0
    while i < nsym:
        rec = d[psym + i * 18: psym + i * 18 + 18]
        if rec[:4] == b"\0\0\0\0":
            off, = struct.unpack_from("<I", rec, 4)
            end = d.index(b"\0", strt + off)
            nm = d[strt + off:end].decode("latin1")
        else:
            nm = rec[:8].rstrip(b"\0").decode("latin1")
        secnum, = struct.unpack_from("<h", rec, 12)
        sclass = rec[16]
        naux = rec[17]
        syms.append((i, nm))
        if secnum > 0 and sclass == IMAGE_SYM_CLASS_EXTERNAL:
            (cdef if is_code.get(secnum) else ddef).add(nm)
        for k in range(naux):
            syms.append((i + 1 + k, None))
        i += 1 + naux
    idx = {i: nm for i, nm in syms}

    refs = set()
    for s in range(nsec):
        base = sh + s * 40
        prel, = struct.unpack_from("<I", d, base + 24)
        nrel, = struct.unpack_from("<H", d, base + 32)
        for r in range(nrel):
            si, = struct.unpack_from("<I", d, prel + r * 10 + 4)
            nm = idx.get(si)
            if nm:
                refs.add(nm)
    return cdef, ddef, refs


def retail_map():
    v2n = collections.defaultdict(list)
    seen = set()
    with MAP.open(errors="latin1") as fh:
        for ln in fh:
            m = MAP_LINE.match(ln.rstrip("\n"))
            if not m:
                continue
            nm, va = m.group(3), int(m.group(4), 16)
            if nm in seen:
                continue
            seen.add(nm)
            v2n[va].append(nm)
    return v2n


def render_msvc_map(groups):
    out = ["; CANDIDATE retail-map fold classes over DATA COMDATs.",
           "; NOT INSTALLED -- generated by scripts/retail_map_fold_candidates.py",
           "; Evidence: address-sharing in orig/373307D9/ham_xbox_r.map.",
           ";", "; Address                        Publics by Value"]
    for g in groups:
        va = g["address"].removeprefix("0x").upper().rjust(8, "0")
        out.append(f"; --- {g['name']} @ {va} ---")
        for n in [g["survivor"], *g["folded"]]:
            out.append(f" 0001:00000000       {n:60} {va}  f i icf_aliases.synthetic")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--emit-map")
    ap.add_argument("--restrict-to",
                    help="JSON pair table; keep only addresses it charges")
    args = ap.parse_args()

    units = json.loads((REPO / "objdiff.json").read_text())["units"]
    used, tgt_def, our_def = set(), set(), set()
    tgt_code = set()
    nt = nb = 0
    for u in units:
        tp, bp = u.get("target_path"), u.get("base_path")
        if tp and (REPO / tp).exists():
            nt += 1
            c, dd, ref = coff_names(REPO / tp)
            tgt_code |= c
            tgt_def |= c | dd
            used |= ref
        if bp and (REPO / bp).exists():
            nb += 1
            c, dd, ref = coff_names(REPO / bp)
            our_def |= c | dd
            used |= c | dd | ref
    used |= tgt_def
    print(f"{nt} target objs ({len(tgt_def)} defs, {len(tgt_code)} code), "
          f"{nb} base objs ({len(our_def)} defs), {len(used)} names in play")

    v2n = retail_map()
    doc = json.loads(ALIAS.read_text())
    spoken = {n for g in doc["groups"]
              for n in (g["survivor"], *g.get("folded", []))}
    have_addr = {int(str(g["address"]).removeprefix("0x"), 16)
                 for g in doc["groups"] if g.get("address")}

    keep = None
    if args.restrict_to:
        keep = set()
        for d in json.load(open(args.restrict_to)):
            keep |= {int(a, 16) for a in
                     set(d["target_addr"]) & set(d["base_addr"])}

    skipped = collections.Counter()
    groups = []
    for va, raw in sorted(v2n.items()):
        if keep is not None and va not in keep:
            continue
        names = [n for n in raw if not NOT_A_FOLD.match(n)]
        if len(names) < 2:
            skipped["fewer than two non-annotation names"] += 1
            continue
        members = [n for n in names if n in used]
        if len(members) < 2:
            skipped["fewer than two members our objects use"] += 1
            continue
        defined = [n for n in members if n in tgt_def]
        if len(defined) != 1:
            skipped["target defines %s of the members"
                    % ("none" if not defined else "several")] += 1
            continue
        if va in have_addr or any(n in spoken for n in members):
            skipped["collides with an installed alias group"] += 1
            continue
        survivor = defined[0]
        folded = sorted(set(members) - {survivor})
        # Code vs data is recorded, never used as a gate: ICF folds both.
        kind = "code" if survivor in tgt_code else "data"
        groups.append({
            "name": f"retailmap-data:{survivor[:40]}@0x{va:08x}",
            "address": f"0x{va:08x}",
            "survivor": survivor,
            "folded": folded,
            "n_map_names_at_addr": len(raw),
            "kind": kind,
            "evidence": (f"orig/373307D9/ham_xbox_r.map: {len(raw)} public "
                         f"name(s) share 0x{va:08x}; /OPT:ICF folds "
                         f"byte-identical COMDATs, so the linker that made the "
                         f"image states this fold set. Members restricted to "
                         f"the {len(members)} name(s) our or the target objects "
                         f"use; the target objects define exactly one of them "
                         f"({survivor}), which is therefore the survivor."),
        })

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "candidate_groups.json").write_text(json.dumps(groups, indent=1))
    (out / "skipped.json").write_text(json.dumps(dict(skipped), indent=1))
    print(f"{len(groups)} candidate groups / "
          f"{sum(1 + len(g['folded']) for g in groups)} names")
    print(f"  largest class: {max((len(g['folded']) + 1 for g in groups), default=0)}")
    for k, v in skipped.most_common():
        print(f"  skipped {v:6}  {k}")
    if args.emit_map:
        Path(args.emit_map).write_text(render_msvc_map(groups))
        print(f"wrote {args.emit_map}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
