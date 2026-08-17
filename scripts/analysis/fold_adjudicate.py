#!/usr/bin/env python3
"""Adjudicate relocation-NAME charges: PROVEN_FOLD vs REFUTED vs UNDECIDABLE.

Chains the three instruments this repo has for a `functionRelocDiffs=name_check`
name charge, in increasing order of what they can conclude:

  1. THE SHIPPED LINKER MAP (`orig/373307D9/ham_xbox_r.map`).  /OPT:ICF folds
     byte-identical COMDATs, and the map co-lists every folded name at the
     surviving address -- 0x82901c78 carries thirteen.  Two charged names
     sharing an address there ARE a fold, stated by the linker that made the
     image.  Two names at DIFFERENT addresses are NOT.  This is the strongest
     instrument and it is checked first.

  2. THE FOLD PROOF on our own objects (`scripts/analysis/fold_proof.py`), for
     the pairs the map cannot adjudicate because one or both names are absent
     from it.  Byte- AND relocation-set-identity is /OPT:ICF's own condition.

  3. STRUCTURAL CLASSIFICATION of the mangled names, for the pairs instrument 2
     must decline -- which is most of them, because a name the TARGET spells and
     our build does not emit cannot be looked up in our objects at all.  This
     stage does not prove folds; it REFUTES them, by showing the two names are
     the same entity under a different source spelling.

WHY STAGE 3 CARRIES THE BUCKET
==============================
A charged pair whose target-side name is absent from the map looks, at first,
like the hard case.  On dc3-decomp it is mostly the EASY case, because the
absent names are compiler-generated symbols for FUNCTION-LOCAL STATICS, and
their mangling encodes source structure:

    ?front@?4??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A   (target)
    ?front@?3??SyncWaypoint@ClipCollide@@IAAXXZ@4VSymbol@@A   (ours)

Same variable, same enclosing function, different SCOPE INDEX -- MSVC's counter
over the lexical scopes and local statics of the enclosing function.  These two
names can never be a fold: they are the same object, and the index differs
because our source has a different number of preceding scopes or statics.  The
charge is a faithful report of a real structural difference.

The classes this stage separates, all REFUTED (never folds):

    LOCAL_STATIC_SCOPE_SKEW    same variable + same enclosing function, scope
                               index differs -> our scope/static COUNT is wrong
    LOCAL_STATIC_RENAME        same enclosing + same scope, variable NAME
                               differs -> rename the local, one-line fix
    LOCAL_STATIC_SCOPE_RENAME  both differ
    LOCAL_STATIC_KIND_CHANGE   `??_B` guard on one side, `?$S` counter on the
                               other -> different local-static shape
    STRING_LITERAL             two `??_C@` literals -> the decoded text differs
                               (usually a `__FILE__` path)
    ANON_NS_HASH               same symbol under two anonymous-namespace hashes
    SIGNATURE_DIFF             same qualified name, different mangled signature
    IMMEDIATE_VS_SYMBOL        the target relocates, we emit a bare immediate
    DIFFERENT_SYMBOL           genuinely unrelated names -- the residue

USAGE
=====
    python3 scripts/analysis/fold_adjudicate.py \\
        --pairs /tmp/census_pairs.json \\
        --objects build/373307D9/src --include-data \\
        --map orig/373307D9/ham_xbox_r.map \\
        --equiv-json scripts/symbol_aliases.json \\
        --markdown docs/analysis/<date>-fold-adjudication.md

`--pairs` is the output of `scripts/analysis/name_charge_census.py
--pairs-json`.  Nothing here writes `scripts/symbol_aliases.json`: PROVEN_FOLD
rows are PROPOSED, with their proof, for whoever owns that file.  See
`fold_proof.py`'s docstring for why an alias installed on a bad proof is worse
than the bug it hides.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis import fold_proof as fp  # noqa: E402
from scripts.analysis.name_charge_census import (  # noqa: E402
    load_map_index, map_verdict)

#: `?<var>@?<scope>` or `?<var>@?<scope>@` immediately before the enclosing `??`.
SCOPE_TAIL = re.compile(r"^(.*)@\?([0-9A-Z]+)@?$")
#: `??_B<scope>` -- the local-static initialisation guard has no `@` separator.
GUARD_TAIL = re.compile(r"^(\?\?_B)\?([0-9A-Z]+)@?$")
#: `?$S<n>` -- MSVC's per-function local-static counter object.
SCOUNT = re.compile(r"^\?\$S\d+$")
ANON = re.compile(r"\?A0x[0-9a-f]{8}")
STRING_LIT = re.compile(r"^\?\?_C@_[0-9A-Z]+@[0-9A-Z]+@(.*)@$")
#: `merged_<hex>` -- an ICF class dtk names by its ADDRESS, which the map can
#: resolve.  `merged_Returns1` / `merged_SetObjConcrete` are NOT this: they are
#: named by SHAPE and resolve to nothing.
MERGED_ADDR = re.compile(r"^merged_[0-9a-fA-F]{8}$")
#: dtk's shape-derived placeholders for fold classes with no surviving name.
SYNTHETIC_TARGET = re.compile(r"^(OnlyReturns|Returns\w*|merged_[0-9a-fA-F]+|"
                              r"fn_[0-9a-fA-F]{8}|lbl_[0-9a-fA-F]{8}|merged_)")


def split_local_static(name):
    """(var_part, scope, enclosing) for a function-local compiler symbol.

    MSVC encodes these as ``?<var>@<scope>??<enclosing>@<type>``, with the scope
    written ``?N`` for small N and ``?AA@`` for large.  Returns None when the
    name is not of that shape.
    """
    if not isinstance(name, str):
        return None
    i = name.rfind("??")
    if i <= 0:
        return None
    # A TEMPLATE enclosing function is written `???$Name@...`, so rfind lands on
    # the second `?` of the three and the prefix keeps a stray `?`.  Back up one
    # character when that is what happened, or every template-scoped local
    # static falls through to DIFFERENT_SYMBOL.
    if i >= 1 and name[i - 1] == "?" and name[i + 2:i + 3] == "$":
        i -= 1
    prefix, rest = name[:i], name[i + 2:]
    for rx in (GUARD_TAIL, SCOPE_TAIL):
        m = rx.match(prefix)
        if m:
            return m.group(1), m.group(2), rest
    return None


#: A mangled function ends at the first `XZ` (void params) or `@Z`; whatever
#: follows is the local object's own storage/type decoration (`@4VSymbol@@A`,
#: `@4IA`, `@57`) and must not take part in "same enclosing function?".
FUNC_END = re.compile(r"^(.*?(?:X|@)Z)")


def strip_type_suffix(enclosing):
    """The enclosing function's mangling, without the local's decoration."""
    m = FUNC_END.match(enclosing)
    return m.group(1) if m else enclosing


#: `?<leaf>@<owner>@@<storage><type>` -- a data symbol's leaf name and owner.
DATA_SYM = re.compile(r"^\?([^@?][^@]*)@(.*)$")
#: An anonymous namespace scope, either MSVC's hashed form or dtk's bare `?A@`.
ANON_SCOPE = re.compile(r"\?A(?:0x[0-9a-f]{8})?@")


def leaf_of(name):
    """The leaf identifier of a simple data symbol, or None."""
    m = DATA_SYM.match(name) if isinstance(name, str) else None
    return m.group(1) if m else None


def classify_data_naming(target, base, symbols_txt=None):
    """Sub-classes for two spellings of the SAME data object.

    None of these is a fold: they are one object under two naming conventions,
    and which side is wrong differs per class.
    """
    tl, bl = leaf_of(target), leaf_of(base)

    # An unmangled name is what MSVC emits for an INTERNAL-LINKAGE data symbol.
    # The target side then cannot have come from the shipped image's public
    # names -- it is a spelling the decomp's own split config invented.
    for mangled, plain, side in ((target, base, "ours"), (base, target, "target")):
        if isinstance(plain, str) and isinstance(mangled, str) \
                and not plain.startswith("?") and leaf_of(mangled) == plain:
            if symbols_txt is not None and mangled in symbols_txt:
                return ("SPLIT_CONFIG_NAMING",
                        f"{side} emits the unmangled internal-linkage label "
                        f"`{plain}`; the other spelling is synthesised in "
                        f"config/.../symbols.txt and is not in the linker map")
            return ("C_LINKAGE_SKEW",
                    f"`{plain}` has C linkage on one side and C++ linkage on "
                    f"the other")

    if tl and bl and tl == bl:
        ta, ba = bool(ANON_SCOPE.search(target)), bool(ANON_SCOPE.search(base))
        if ta != ba:
            return ("ANON_NS_PRESENCE",
                    f"`{tl}` is in an unnamed namespace on the "
                    f"{'target' if ta else 'our'} side only")
        if ta and ba:
            return ("ANON_NS_HASH",
                    f"`{tl}`: same unnamed namespace, different hash")
        return ("STORAGE_CLASS_SKEW",
                f"`{tl}` has a different owner scope: "
                f"`{target.split('@@')[0]}` vs `{base.split('@@')[0]}`")
    return None


#: Structural classes that state WHY nothing can decide, not what the answer is.
UNDECIDABLE_CLASSES = frozenset({
    "DIFFERENT_SYMBOL", "ICF_SYNTHETIC_TARGET_NAME",
})


def classify_structural(target, base, symbols_txt=None):
    """A REFUTED sub-class for a pair the map and the fold proof both decline."""
    if not isinstance(base, str) or not isinstance(target, str):
        return "IMMEDIATE_VS_SYMBOL", "target relocates a symbol; we emit a bare immediate"

    if MERGED_ADDR.match(target) or MERGED_ADDR.match(base):
        return ("ICF_MERGED_TARGET",
                "the target side is an ICF class; look the address up in the "
                "linker map and check whether our callee is among its occupants")

    # dtk names a trivial target function by its SHAPE, not its identity, when
    # the fold class has no surviving public name.  Such a name exists in no
    # source and in no object, so neither the map nor the fold proof can ever
    # resolve it -- the pair stays UNDECIDABLE by construction, and saying so is
    # the honest answer.
    if not target.startswith("?") and SYNTHETIC_TARGET.match(target):
        return ("ICF_SYNTHETIC_TARGET_NAME",
                f"the target side is dtk's shape-derived placeholder "
                f"`{target}` for a fold class with no surviving public name; "
                f"it names no symbol, so nothing can look it up")

    ts, bs = split_local_static(target), split_local_static(base)
    if ts and bs and strip_type_suffix(ts[2]) == strip_type_suffix(bs[2]):
        tvar, tscope, _ = ts
        bvar, bscope, _ = bs
        guard_t = tvar.startswith("??_B") or bool(SCOUNT.match(tvar))
        guard_b = bvar.startswith("??_B") or bool(SCOUNT.match(bvar))
        if guard_t and guard_b and (tvar.startswith("??_B") != bvar.startswith("??_B")):
            return ("LOCAL_STATIC_KIND_CHANGE",
                    f"local-static shape differs: {tvar} vs {bvar} in the same function")
        same_var = tvar == bvar or (bool(SCOUNT.match(tvar)) and bool(SCOUNT.match(bvar)))
        if same_var and tscope != bscope:
            return ("LOCAL_STATIC_SCOPE_SKEW",
                    f"same local static, scope index ?{tscope} vs ?{bscope} -- our "
                    f"enclosing function has a different scope/static count")
        if not same_var and tscope == bscope:
            return ("LOCAL_STATIC_RENAME",
                    f"same scope ?{tscope}, local named {tvar} in the target and "
                    f"{bvar} in ours -- rename the local")
        return ("LOCAL_STATIC_SCOPE_RENAME",
                f"local {tvar}@?{tscope} vs {bvar}@?{bscope} in the same function")

    mt, mb = STRING_LIT.match(target), STRING_LIT.match(base)
    if mt and mb:
        return ("STRING_LITERAL",
                f"string literal text differs: {mt.group(1)[:48]!r} vs "
                f"{mb.group(1)[:48]!r}")

    if ANON.search(target) and ANON.search(base) \
            and ANON.sub("?A0x", target) == ANON.sub("?A0x", base):
        return ("ANON_NS_HASH",
                "same symbol under two anonymous-namespace hashes")

    dn = classify_data_naming(target, base, symbols_txt)
    if dn:
        return dn

    if target.startswith("??$") != base.startswith("??$") \
            and target.lstrip("?").split("@")[0].lstrip("$") \
            == base.lstrip("?").split("@")[0].lstrip("$"):
        return ("TEMPLATE_VS_PLAIN",
                "one side is a template specialisation, the other a plain "
                "overload of the same operator/function")

    tq, bq = target.split("@@", 1)[0], base.split("@@", 1)[0]
    if tq == bq and target != base:
        return ("SIGNATURE_DIFF",
                "same qualified name, different mangled signature")

    return ("DIFFERENT_SYMBOL", "names are not related by any known spelling rule")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pairs", required=True,
                    help="name_charge_census.py --pairs-json output")
    ap.add_argument("--objects", action="append", required=True)
    ap.add_argument("--include-data", action="store_true")
    ap.add_argument("--map", help="MSVC linker map for stage 1")
    ap.add_argument("--equiv-json", help="alias JSON for the PROVEN_MOD_ALIAS tier")
    ap.add_argument("--symbols-txt",
                    help="dtk split symbol config (config/<title>/symbols.txt). "
                         "Lets the classifier tell a name the SPLIT invented "
                         "from a name the shipped image actually carries.")
    ap.add_argument("--only-map-verdict",
                    help="restrict to pairs the census gave this map verdict "
                         "(e.g. NOT_IN_MAP)")
    ap.add_argument("--json-out")
    ap.add_argument("--markdown")
    args = ap.parse_args(argv)

    pairs = json.loads(Path(args.pairs).read_text())
    if args.only_map_verdict:
        pairs = [p for p in pairs if p.get("map_verdict") == args.only_map_verdict]
    print(f"pairs in: {len(pairs)}")

    index = fp.BodyIndex(args.objects, include_data=args.include_data)
    print(f"index: {index.n_objects} objects, {index.n_slices} slices "
          f"({index.n_data_slices} data), {len(index.by_name)} symbols "
          f"({index.build_seconds:.1f}s)")
    canon = fp.load_equivalences(args.equiv_json) if args.equiv_json else {}
    mapidx = load_map_index(args.map) if args.map else None
    byaddr = None
    if mapidx:
        byaddr = collections.defaultdict(set)
        for name, addrs in mapidx.items():
            for a in addrs:
                byaddr[a].add(name)
    symbols_txt = None
    if args.symbols_txt:
        symbols_txt = {ln.split("=", 1)[0].strip()
                       for ln in Path(args.symbols_txt).read_text().splitlines()
                       if "=" in ln}
        print(f"split config: {args.symbols_txt} ({len(symbols_txt)} names)")

    out = []
    for p in pairs:
        t, b = p.get("target"), p.get("base")
        rec = dict(p)
        rec["map"] = map_verdict(mapidx, t, b) if (
            mapidx and isinstance(t, str) and isinstance(b, str)) else "NO_MAP"
        proof = fp.prove_pair(index, t, b, canon) if (
            isinstance(t, str) and isinstance(b, str)) else {
                "verdict": fp.UNDECIDABLE, "reason": "base side is not a symbol"}
        rec["proof"] = proof["verdict"]
        rec["proof_reason"] = proof["reason"]
        for k in ("survivor_size", "survivor_relocs", "survivor_obj",
                  "folded_obj", "kind"):
            if k in proof:
                rec[k] = proof[k]

        if rec["map"] == "MAP_CONFIRMS_FOLD":
            rec["verdict"], rec["why"] = "PROVEN_FOLD", "linker map co-lists both names at one address"
        elif rec["map"] == "MAP_REFUTES_FOLD":
            rec["verdict"], rec["why"] = "REFUTED", "linker map places the two names at different addresses"
        elif proof["verdict"] in (fp.PROVEN, fp.PROVEN_MOD):
            rec["verdict"], rec["why"] = "PROVEN_FOLD", proof["reason"]
            rec["tier"] = proof["verdict"]
        elif proof["verdict"] == fp.REFUTED:
            rec["verdict"], rec["why"] = "REFUTED", "our COMDATs for the two names differ: " + proof["reason"]
            rec["structural"] = classify_structural(t, b, symbols_txt)[0]
        else:
            klass, why = classify_structural(t, b, symbols_txt)
            rec["structural"] = klass
            if klass == "ICF_MERGED_TARGET" and byaddr:
                # `merged_<hex>` IS decidable: the map lists the fold class's
                # occupants at that address.  Our callee is either among them or
                # it is not, and there is no third answer.
                addr = (t if t.startswith("merged_") else b).split("_", 1)[1].lower()
                occ = byaddr.get(addr.lstrip("0").rjust(8, "0")) or byaddr.get(addr)
                ours = b if t.startswith("merged_") else t
                if occ is None:
                    rec["verdict"], rec["why"] = "UNDECIDABLE", (
                        f"the ICF class at 0x{addr} is not in the linker map")
                elif ours in occ:
                    rec["verdict"], rec["why"] = "PROVEN_FOLD", (
                        f"our callee is one of the {len(occ)} names the map "
                        f"lists at 0x{addr}")
                else:
                    rec["verdict"], rec["why"] = "REFUTED", (
                        f"the map lists {len(occ)} names at 0x{addr} and our "
                        f"callee is NOT among them: "
                        + ", ".join(sorted(occ)[:4]))
            elif klass in UNDECIDABLE_CLASSES:
                rec["verdict"], rec["why"] = "UNDECIDABLE", why
            else:
                rec["verdict"], rec["why"] = "REFUTED", why
        out.append(rec)

    rows = collections.defaultdict(lambda: {"size": 0, "verdicts": set()})
    for r in out:
        key = (r.get("unit"), r.get("row"))
        rows[key]["size"] = r.get("row_size", 0)
        rows[key]["verdicts"].add(r["verdict"])

    def row_verdict(v):
        for k in ("REFUTED", "UNDECIDABLE", "PROVEN_FOLD"):
            if k in v:
                return k
        return "?"

    print()
    print(f"{'PAIRS':<8}{'verdict':<16}{'n':>6}")
    for k, n in collections.Counter(r["verdict"] for r in out).most_common():
        print(f"{'':<8}{k:<16}{n:>6}")
    print()
    print(f"{'ROWS':<8}{'verdict':<16}{'rows':>6}{'bytes':>9}")
    rc, rb = collections.Counter(), collections.Counter()
    for key, meta in rows.items():
        v = row_verdict(meta["verdicts"])
        rc[v] += 1
        rb[v] += meta["size"]
    for k, n in rc.most_common():
        print(f"{'':<8}{k:<16}{n:>6}{rb[k]:>9}")
    print()
    print("REFUTED sub-classes (pairs):")
    for k, n in collections.Counter(
            r.get("structural", "our-COMDATs-differ") for r in out
            if r["verdict"] == "REFUTED").most_common():
        print(f"    {n:>4}  {k}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(out, indent=1))
        print(f"\nwrote {args.json_out}")
    if args.markdown:
        Path(args.markdown).write_text(render_markdown(out, rows, row_verdict))
        print(f"wrote {args.markdown}")
    return 0


def render_markdown(out, rows, row_verdict):
    L = []
    rc, rb = collections.Counter(), collections.Counter()
    for meta in rows.values():
        v = row_verdict(meta["verdicts"])
        rc[v] += 1
        rb[v] += meta["size"]
    L.append("| class | rows | bytes | pairs |")
    L.append("|---|---:|---:|---:|")
    pc = collections.Counter(r["verdict"] for r in out)
    for k in ("PROVEN_FOLD", "REFUTED", "UNDECIDABLE"):
        L.append(f"| {k} | {rc.get(k, 0)} | {rb.get(k, 0)} | {pc.get(k, 0)} |")
    L.append("")
    L.append("## REFUTED sub-classes (pairs)")
    L.append("")
    L.append("| sub-class | pairs |")
    L.append("|---|---:|")
    for k, n in collections.Counter(
            r.get("structural", "our-COMDATs-differ") for r in out
            if r["verdict"] == "REFUTED").most_common():
        L.append(f"| {k} | {n} |")
    for verdict in ("PROVEN_FOLD", "UNDECIDABLE", "REFUTED"):
        sel = [r for r in out if r["verdict"] == verdict]
        if not sel:
            continue
        L.append("")
        L.append(f"## {verdict} — {len(sel)} pairs")
        L.append("")
        for r in sorted(sel, key=lambda r: -r.get("row_size", 0)):
            L.append(f"- **{r.get('row_size', 0)} B** `{r.get('unit')}` "
                     f"`{r.get('row')}`")
            L.append(f"  - target `{r.get('target')}`")
            L.append(f"  - ours   `{r.get('base')}`")
            L.append(f"  - {r.get('structural', '')} — {r.get('why')}")
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    sys.exit(main())
