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

Gate 5: byte identity of the COMDAT
-----------------------------------
Gates 1-4 are all *map-residency* evidence -- "the retail linker put these
names at one address".  That establishes a fold among RETAIL's spellings.  It
does not establish that OUR `??_8Foo` is the same object as retail's survivor,
and if ours differs the alias papers over a real defect instead of describing a
fold.  The condition `/OPT:ICF` actually tests is byte identity of the COMDAT,
which is directly checkable, so gate 5 checks it: for every member our objects
DEFINE, the COMDAT contents must equal the target survivor's, relocation-
patched fields masked and relocation targets compared by name.  A group with
any mismatching member is refused fail-closed, and so is a group where our
objects define no member at all -- there the fold is asserted over a name this
build never emits, and there is nothing to check it against.

Padding normalisation (`--identity align`, the default)
-------------------------------------------------------
Of the 254 `??_8` COMDATs defined on both sides, 176 are byte-identical, 78 are
exactly `ours + 4 trailing zero bytes` on the target side, and **0 differ in
content**.  Two readings of the 78: dtk's splitter attributing inter-symbol
padding to the vbtable, or a real emission difference in retail.  The
successor discriminates them, because dtk sizes a split symbol by the gap to
the next thing it can name -- so under the first reading padding appears
exactly when the successor's alignment demands it, and under the second it does
not care about the successor at all.  `--padding-evidence` tabulates it and the
table has no exceptions:

    47  flush  / successor is only 4-byte aligned  / our end was off the boundary
   129  flush  / successor needs 8-byte alignment  / our end was on the boundary
    78  padded / successor needs 8-byte alignment  / our end was off the boundary

The first cell is the control the emission reading fails: 47 vbtables whose
last word sits at `addr % 8 == 4` and which are NOT padded, because the datum
after them only wanted 4-byte alignment.  An extra emitted entry would be there
in all 254 regardless.  Two corroborations: every `??_8` COMDAT we emit is
`IMAGE_SCN_ALIGN_4BYTES` (792/792), so the linker never pads a vbtable for the
vbtable's own sake and any padding after one belongs to what follows; and the
target size histogram (12:103, 8:96, 16:36, 24:10, 20:9) is not the uniform
"+1 terminator" shape.  Verdict: **padding, attributed by the splitter**.

So `align` admits a target COMDAT that is our bytes followed by fewer than 8
zero bytes landing the end on an 8-byte boundary, and nothing else.
`--identity strict` requires exact equality and is the conservative control:
it admits 176 of the 254 rather than all 254.

    python3 scripts/retail_map_fold_candidates.py --out <dir>
    python3 scripts/retail_map_fold_candidates.py --out <dir> --emit-map <path>
    python3 scripts/retail_map_fold_candidates.py --out <dir> --identity strict

It does NOT install into `scripts/symbol_aliases.json`; that is
`scripts/install_data_fold_aliases.py`.

⚠ This script can only CREATE a group. Its collision gate drops an address that
already carries one -- 2,038 addresses, the largest non-structural skip bucket
below -- so a class installed before our tree started emitting a spelling, or
before `config/373307D9/symbols.txt` was rewritten, can never be extended or
re-anchored here. That is a real gap and it cost `HttpGet::Poll` a row.
`scripts/retail_map_fold_reconcile.py` is the successor for those two cases; it
imports this module's gates rather than restating them.
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
IMAGE_SCN_LNK_COMDAT = 0x1000
# The alignment the padding evidence is stated against.  Every `??_8` COMDAT we
# emit asks for 4-byte alignment, so any run of padding after one is there for
# the NEXT datum; empirically every padded case lands on an 8-byte boundary.
PAD_ALIGN = 8


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


def comdat_contents(path: Path, wanted):
    """{name: (masked bytes, reloc triples)} for the COMDATs `wanted` names own.

    Only sections carrying `IMAGE_SCN_LNK_COMDAT` and exactly one external
    definition are read -- anything else is not a COMDAT in the sense
    `/OPT:ICF` folds, and taking a whole shared section as one symbol's
    contents would compare the wrong bytes.

    Relocation-patched fields are masked to zero and the relocations are
    returned as (offset, type, target name).  A data COMDAT holding a pointer
    has that word written at link time, so its raw 4 bytes differ between any
    two objects while the COMDAT is still foldable iff the reloc TARGETS agree;
    comparing the unmasked word would reject every folded pointer table, and
    comparing the masked word alone would accept two tables pointing at
    different things.

    The extent runs from the symbol's Value to the end of the section, not
    from the section start.  MSVC emits a vftable COMDAT as the complete-object
    locator pointer followed by the slots and puts `??_7` at Value=4 (5,220 of
    5,220 in our objects), while dtk's split puts it at 0 (2,863 of 2,863).
    Taking the section from 0 on both sides compared our 12-byte
    `[??_R4 ptr][slot][slot]` against the target's 8-byte `[slot][slot]` and
    called a matching vftable a content mismatch.
    """
    d = path.read_bytes()
    nsec, = struct.unpack_from("<H", d, 2)
    psym, nsym = struct.unpack_from("<II", d, 8)
    if not psym or not nsym:
        return {}
    opt, = struct.unpack_from("<H", d, 16)
    sh = 20 + opt
    sec = []
    for s in range(nsec):
        b = sh + s * 40
        size, praw, prel = struct.unpack_from("<III", d, b + 16)
        nrel, = struct.unpack_from("<H", d, b + 32)
        chars, = struct.unpack_from("<I", d, b + 36)
        sec.append((size, praw, prel, nrel, chars))
    strt = psym + nsym * 18
    idx, defs, n_ext = {}, [], collections.Counter()
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
        idx[i] = nm
        if secnum > 0 and rec[16] == IMAGE_SYM_CLASS_EXTERNAL:
            n_ext[secnum] += 1
            if nm in wanted:
                defs.append((nm, secnum, value))
        i += 1 + rec[17]

    out = {}
    for nm, secnum, value in defs:
        size, praw, prel, nrel, chars = sec[secnum - 1]
        if not chars & IMAGE_SCN_LNK_COMDAT or n_ext[secnum] != 1:
            continue
        if value >= size:
            continue
        raw = bytearray(d[praw:praw + size]) if praw else bytearray(size)
        rel = []
        for r in range(nrel):
            va, si = struct.unpack_from("<II", d, prel + r * 10)
            ty, = struct.unpack_from("<H", d, prel + r * 10 + 8)
            for k in range(va, min(va + 4, len(raw))):
                raw[k] = 0
            if va >= value:
                rel.append((va - value, ty, idx.get(si, "?")))
        out[nm] = (bytes(raw[value:]), tuple(sorted(rel)))
    return out


def content_verdict(ours, tgt, va, mode, canon=None):
    """Does our COMDAT satisfy /OPT:ICF's own test against the survivor's?

    Returns (ok, reason).  `mode` is "strict" (exact equality) or "align"
    (exact equality, or our bytes followed by alignment slack -- see module
    docstring for the measurement that licenses the slack).

    `canon` (optional) renames a relocation TARGET to a fold-class id before
    the comparison; see `reloc_canon`.  Two spellings the retail linker put at
    one address are one pointer in the image, so a relocation to either is the
    same relocation -- refusing the class because the two sides SPELL that
    pointer differently would refuse a fold on the strength of a second fold
    the same map states.  Only names the map itself buckets together are ever
    equated; anything else still has to match by name, so the gate stays
    fail-closed.  `strict` mode does not use it.

    Extent relaxation (`align` only).  dtk's splitter sizes a target symbol by
    the gap to the next name it can resolve, so an unnamed datum following the
    survivor lands INSIDE the survivor's carved COMDAT -- the same attribution
    the module docstring establishes for trailing padding, except the tail here
    is live content rather than zeros.  It is distinguishable from real extra
    content the cheap way: the tail carries its own RELOCATIONS.  When it does,
    our COMDAT is compared over our own extent only (bytes, plus every target
    relocation landing inside it).  When the tail has no relocations the old
    zero-padding rule applies unchanged, so the 176/254-vs-254 measurement the
    `align` default rests on is untouched.
    """
    ob, orel = ours
    tb, trel = tgt
    c = canon or (lambda n: n)

    def norm(rel):
        return tuple((off, ty, c(nm)) for off, ty, nm in rel)

    orel_n, trel_n = norm(orel), norm(trel)
    note = ("; relocation targets equal modulo an ICF fold the retail map "
            "states" if orel != trel and orel_n == trel_n else "")
    n = len(ob)
    if orel_n == trel_n:
        if ob == tb:
            return True, "identical" + note
        if mode == "strict":
            return False, "contents differ"
    elif mode == "strict":
        return False, "relocation targets differ"

    if mode == "strict":
        return False, "contents differ"
    if len(tb) <= n or tb[:n] != ob:
        return False, ("relocation targets differ" if orel_n != trel_n
                       else "contents differ")

    if any(off >= n for off, _, _ in trel_n):
        # Live tail: the splitter attributed a following, unnamed datum to the
        # survivor. Compare over our extent.
        if tuple(r for r in trel_n if r[0] < n) != orel_n:
            return False, "relocation targets differ"
        return True, (f"identical over our {n}-byte extent{note}; the target "
                      f"COMDAT continues for {len(tb) - n} more relocated "
                      f"byte(s) dtk's splitter attributed to the survivor")

    if orel_n != trel_n:
        return False, "relocation targets differ"
    pad = len(tb) - n
    if any(tb[n:]):
        return False, "target is longer and the extra bytes are not zero"
    if pad >= PAD_ALIGN:
        return False, f"{pad} trailing zeros is more than alignment slack"
    if (va + len(tb)) % PAD_ALIGN:
        return False, "trailing zeros do not land on an alignment boundary"
    return True, f"identical modulo {pad} bytes of alignment padding{note}"


def reloc_canon(v2n):
    """name -> fold-class id, for names the retail map buckets with another.

    A name alone at its address maps to ITSELF, so a name in a fold class never
    compares equal to one outside it.
    """
    fold = {}
    for va, names in v2n.items():
        real = [n for n in names if not NOT_A_FOLD.match(n)]
        if len(real) > 1:
            for n in real:
                fold[n] = f"#icf@{va:08x}"
    return lambda nm: fold.get(nm, nm)


SYMBOLS = REPO / "config" / "373307D9" / "symbols.txt"
SYM_LINE = re.compile(
    r"^(\S+) = (\S+):0x([0-9A-Fa-f]+); // type:(\S+) size:0x([0-9A-Fa-f]+)")


def padding_evidence(prefix="??_8"):
    """Is the target side's extra tail alignment padding, or real content?

    The discriminator is the SUCCESSOR.  dtk sizes a split symbol by the gap to
    the next thing it can name, so any inter-symbol alignment padding the
    linker inserted lands inside the preceding symbol's carved COMDAT.  If that
    is what the tail is, then padding must appear exactly when the successor's
    alignment demands it and never otherwise; if instead retail's compiler
    emitted an extra entry, padding would be uncorrelated with the successor.
    This tabulates that correlation over every `prefix` symbol defined on both
    sides -- see the module docstring for the reading.
    """
    units = json.loads((REPO / "objdiff.json").read_text())["units"]
    ours, tgts = {}, {}
    for u in units:
        for key, store in (("base_path", ours), ("target_path", tgts)):
            p = u.get(key)
            if not p or not (REPO / p).exists():
                continue
            for nm, v in comdat_contents(REPO / p, _AnyWith(prefix)).items():
                store[nm] = v

    syms = []
    for ln in SYMBOLS.open(errors="latin1"):
        m = SYM_LINE.match(ln)
        if m:
            syms.append((m.group(2), int(m.group(3), 16), m.group(1),
                         int(m.group(5), 16)))
    syms.sort()
    rows, tab = [], collections.Counter()
    for i, (sect, addr, nm, size) in enumerate(syms):
        if nm not in tgts or nm not in ours:
            continue
        osz = len(ours[nm][0])
        j = i + 1
        while j < len(syms) and syms[j][:2] == (sect, addr):
            j += 1
        nxt = syms[j] if j < len(syms) and syms[j][0] == sect else None
        pad = len(tgts[nm][0]) - osz
        row = {"name": nm, "addr": f"0x{addr:08x}", "our_size": osz,
               "target_size": size, "pad": pad,
               "our_end_mod_align": (addr + osz) % PAD_ALIGN,
               "target_end_mod_align": (addr + size) % PAD_ALIGN,
               "next": nxt[2] if nxt else None,
               "next_addr_mod_align": (nxt[1] % PAD_ALIGN) if nxt else None,
               "gap_to_next": (nxt[1] - addr) if nxt else None}
        rows.append(row)
        tab[("padded" if pad else "flush",
             "successor needs %d-byte alignment" % PAD_ALIGN
             if nxt and nxt[1] % PAD_ALIGN == 0 else "successor is only "
             "%d-byte aligned" % (nxt[1] % PAD_ALIGN) if nxt else "no successor",
             "our end was already on the boundary"
             if (addr + osz) % PAD_ALIGN == 0 else "our end was off the "
             "boundary")] += 1
    return rows, tab


class _AnyWith:
    """Stand-in for `comdat_contents`' `wanted` set: every name with a prefix."""

    def __init__(self, prefix):
        self.prefix = prefix

    def __contains__(self, nm):
        return nm.startswith(self.prefix)


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
    ap.add_argument("--identity", choices=("align", "strict", "off"),
                    default="align",
                    help="gate 5: how our COMDAT bytes must match the "
                         "survivor's. 'align' tolerates trailing alignment "
                         "slack, 'strict' does not, 'off' reproduces the "
                         "pre-gate-5 candidate set for comparison")
    ap.add_argument("--identity-report",
                    help="write the per-name byte comparison here")
    ap.add_argument("--ignore-installed-prefix", metavar="PREFIX",
                    help="treat groups already in symbol_aliases.json whose "
                         "name starts with PREFIX as absent when checking for "
                         "collisions. An installer that owns a tier must pass "
                         "its own prefix or the second run finds every group "
                         "it installed itself and refuses all of them, which "
                         "reads as the class evaporating")
    ap.add_argument("--padding-evidence", nargs="?", const="??_8",
                    metavar="PREFIX",
                    help="tabulate padded-vs-flush against the successor's "
                         "alignment for every PREFIX symbol defined on both "
                         "sides, print it, and exit; this is the measurement "
                         "that licenses --identity align")
    args = ap.parse_args()

    if args.padding_evidence:
        rows, tab = padding_evidence(args.padding_evidence)
        print(f"{len(rows)} {args.padding_evidence} symbol(s) defined on both "
              f"sides")
        for k, v in sorted(tab.items()):
            print(f"  {v:4}  {' / '.join(k)}")
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / "padding_evidence.json").write_text(json.dumps(rows, indent=1))
        print(f"wrote {out / 'padding_evidence.json'}")
        return 0

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
    installed = [g for g in doc["groups"]
                 if not (args.ignore_installed_prefix
                         and g.get("name", "").startswith(
                             args.ignore_installed_prefix))]
    if len(installed) != len(doc["groups"]):
        print(f"ignoring {len(doc['groups']) - len(installed)} installed "
              f"{args.ignore_installed_prefix!r} group(s) when checking "
              f"collisions")
    spoken = {n for g in installed
              for n in (g["survivor"], *g.get("folded", []))}
    have_addr = {int(str(g["address"]).removeprefix("0x"), 16)
                 for g in installed if g.get("address")}

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
            # NOT a refusal on evidence -- the address is simply out of this
            # script's reach, and 2,038 addresses land here. 511 of them are
            # incomplete or stale, not complete: see
            # scripts/retail_map_fold_reconcile.py, which extends and
            # re-anchors instead of skipping.
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

    # ---- gate 5: byte identity of the COMDAT -------------------------------
    # Second pass: only the candidate members are read, so the whole
    # 153k-definition symbol space never has to be held as bytes.
    refused, checks = [], []
    canon = None if args.identity == "strict" else reloc_canon(v2n)
    if args.identity != "off":
        wanted = {n for g in groups for n in (g["survivor"], *g["folded"])}
        ours_c, tgt_c, conflict = {}, {}, set()
        for u in units:
            for key, store in (("base_path", ours_c), ("target_path", tgt_c)):
                p = u.get(key)
                if not p or not (REPO / p).exists():
                    continue
                for nm, v in comdat_contents(REPO / p, wanted).items():
                    if nm in store and store[nm] != v:
                        conflict.add(nm)
                    store[nm] = v
        print(f"gate 5: read {len(ours_c)} of our / {len(tgt_c)} target "
              f"candidate COMDATs, {len(conflict)} name(s) defined "
              f"inconsistently within one side")

        kept = []
        for g in groups:
            va = int(g["address"], 16)
            surv = tgt_c.get(g["survivor"])
            members = [g["survivor"], *g["folded"]]
            bad, ok_names, pad_names = [], [], []
            if surv is None:
                bad.append((g["survivor"],
                            "target defines it outside a single-external "
                            "COMDAT section, so there are no fold bytes"))
            else:
                for m in members:
                    if m in conflict:
                        bad.append((m, "our own objects define it with two "
                                       "different contents"))
                        continue
                    mine = ours_c.get(m)
                    if mine is None:
                        continue          # not defined by us: nothing to check
                    ok, why = content_verdict(mine, surv, va, args.identity,
                                              canon)
                    checks.append({"group": g["name"], "member": m,
                                   "ok": ok, "reason": why})
                    (ok_names if ok else bad).append(
                        m if ok else (m, why))
                    if ok and why.startswith("identical modulo"):
                        pad_names.append(m)
            if not bad and not ok_names:
                bad.append(("(none)", "our objects define no member of this "
                                      "class, so nothing checks the fold"))
            if bad:
                refused.append({**g, "refused_by": "byte identity",
                                "reasons": [{"member": m, "why": w}
                                            for m, w in bad]})
                continue
            pad = (f" {len(pad_names)} of them modulo trailing alignment "
                   f"padding dtk's splitter attributed to the survivor"
                   if pad_names else "")
            g["byte_identity"] = {
                "mode": args.identity,
                "members_we_define": sorted(ok_names),
                "padded": sorted(pad_names),
            }
            g["evidence"] += (
                f" Gate 5 (byte identity, the condition /OPT:ICF actually "
                f"tests): our objects define {len(ok_names)} member(s) of this "
                f"class and every one has COMDAT contents equal to the target "
                f"survivor's with relocation-patched fields masked and "
                f"relocation targets equal by name;{pad}.")
            kept.append(g)
        groups = kept

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "candidate_groups.json").write_text(json.dumps(groups, indent=1))
    (out / "skipped.json").write_text(json.dumps(dict(skipped), indent=1))
    (out / "refused_by_byte_identity.json").write_text(
        json.dumps(refused, indent=1))
    if args.identity_report:
        Path(args.identity_report).write_text(json.dumps(checks, indent=1))
    if args.identity != "off":
        why = collections.Counter(r["reasons"][0]["why"] for r in refused)
        print(f"  gate 5 refused {len(refused)} group(s):")
        for k, v in why.most_common():
            print(f"    {v:4}  {k}")
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
