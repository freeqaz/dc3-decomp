#!/usr/bin/env python3
"""check_alias_map_soundness -- re-test scripts/symbol_aliases.json with
relocations CHARGED.

WHY
---
``scripts/symbol_aliases.json`` licenses objdiff to treat two differently
spelled symbols as one function.  A FALSE entry makes objdiff forgive a real
call-target difference, so the row scores 100.0 and the defect is invisible to
every ruler in this repo.  Two such defects were real (lane w27-k, 2026-11-02):

  * ``CharClip::EvaluateChannel`` passed ``(char *)offset`` where the image
    passes an int, selecting ``MakeString<char*,int,const char*>`` instead of
    ``MakeString<int,int,const char*>``.  A byte offset reached ``snprintf``
    as a ``%s`` pointer -- a wild read on the failure path.
  * ``CharBonesSamples::Print`` passed ``char *mRawData`` to a ``%x``,
    selecting a different ``FormatString::operator<<`` overload.

Both hid inside ``MakeString@0x823d13d0`` / ``MakeString@0x823e1008``, two
groups whose evidence is the BODY TEST: "retail bytes at the survivor address
are relocation-invariantly identical to our compiled body".  That test is
``shape()`` in decomp-synth's ``tools/revcomp/probes/probe_icf_foldtest.py``:

    if op in (16, 18):
        out.append((op, w & 3))            # branch: AA/LK only, LI dropped
    elif op in D_FORM:
        out.append((op, w & 0x03FF0000))   # D-form: displacement dropped
    else:
        out.append(w)

so a ``bl A`` and a ``bl B`` fingerprint IDENTICALLY, and so do two D-form
words that differ only in their 16-bit immediate/displacement.  The verdict
path is ``if shape(a) == shape(b): hit = (i, j)`` -- the relocations are read
only in the EH-span reconciliation branch, which compares the words our object
does NOT relocate.  **The primary path never looks at what a call points at.**

WHAT THIS SCRIPT DOES
---------------------
For every alias group it compares the target-side body of the ``survivor``
against our compiled body of each ``folded`` spelling, and charges the
relocations: each side's relocated fields are resolved to the SYMBOL NAMES they
name (the body is a COFF object, so the relocation record states them) and the
two operand lists must match word for word.

Three verdicts per (group, member) pair:

  ``AGREE``               masked bodies equal AND resolved operands equal.
  ``MASK_DIVERGENT``      the bodies differ even under masking.  Not this
                          ruler's finding -- but for a body-test group it also
                          contradicts the group's own stated evidence.
  ``RESOLVED_DIVERGENT``  masked bodies equal, resolved operands differ.  THE
                          FAILURE: the two spellings are different functions in
                          our build, so no linker could have folded them, so
                          the alias is not evidence -- it is an erasure.

Non-comparable pairs are DROPS, each with a reason and a count; a group that
cannot be examined is never reported as a pass.

GATING POPULATION
-----------------
Only the groups whose ADMISSION EVIDENCE is the body test are gated.  The
others rest on evidence the body comparison cannot contradict:

  ``retailmap`` / ``retailmap-fn`` / ``retailmap-data``
        ``/OPT:ICF`` folds byte-identical COMDATs and
        ``orig/373307D9/ham_xbox_r.map`` is the linker's own record of which
        names it folded, so two names at one address there ARE one function.
        A resolved-operand disagreement against such a group means OUR build
        did not reproduce a fold the retail linker performed -- a statement
        about our COMDAT contents, not about the alias.
  ``weak-external``
        ``??_E<C>`` is declared by MSVC as an undefined weak external whose
        aux record names ``??_G<C>``.  Structural, emitted by the compiler.
  ``fold-proof`` / ``rdata-const``
        byte- AND relocation-set identical by construction.

Those are still MEASURED and reported (``--notes``), just not gated: a gate
that fails on evidence it cannot refute is a gate that gets routed around.

The tier is decided per MEMBER, not per group, because one body-test group can
mix admittees.  ``OnlyReturns@0x823e3b70`` folds 524 spellings; the retail map
places 521 of them at the survivor's address itself.  A **third** oracle
therefore splits them: a member the retail linker map also places at the
survivor's address is ``body-test+retail-confirmed`` (ground truth, noted only);
a member the linker map does NOT place there rests on the relocation-invariant
body test alone and is GATED.  That split is exactly the population this lane
exists to look at -- OUR spellings the map does not confirm the image emitted,
i.e. the only place a wrong overload can hide -- and it is what separated the
two real defects from the 521 benign members:

    ??$MakeString@PADHPBD@@...                  ABSENT from the retail map
    ??$MakeString@HHPADW4CompressionType@...    ABSENT from the retail map
    ??$MakeString@HHHW4CompressionType@...      0x823e1008  (map-confirmed)

WHAT MAP ABSENCE DOES **NOT** MEAN
----------------------------------
Absence is not evidence that the image never emitted the spelling.  ``/OPT:ICF``
records the SURVIVOR's name at the folded address, so a spelling that was
emitted and folded is expected to be absent from the map too.  Measured
2026-11-02 (lane w27-o): ``MakeString<char[128]>`` and ``char const[7]`` appear
nowhere in ``ham_xbox_r.map`` yet sit at the folded addresses 0x827d40f0 and
0x8292bf68 -- the image did emit them.

The wording matters because a FALSE absence claim is unfalsifiable and gets
cited as fact (it was, in this gate's original merge message).  The GATE itself
is unaffected: it compares the unconfirmed population with relocations charged,
so a map-absent spelling that genuinely folded AGREES and passes.  Absence only
selects what to examine; the body comparison decides.

Corollary for anything that audits this population: key on ADDRESS, not on
name.  A target object's callee name is a fold SURVIVOR too -- the image's call
to ``TerminateMakeString`` is recorded as ``OnlyReturns`` in the split object --
so an audit that filters the target side by name silently drops functions and
reports a smaller, cleaner population than exists.

CANONICALISATION (why a raw count is not the answer)
----------------------------------------------------
Operand names are first run through the map's own fold congruence: an operand
naming any member of any group is replaced by that group's survivor.  Without
it, two spellings that differ only by calling two members of ONE OTHER genuine
fold class read as divergent -- measured here as 130 pairs before canon and 8
after.  Canon is applied to the group under test as well, which is sound: if
A and B really are one function, a call to A and a call to B are the same call
however the group is reached.

DUPLICATE COMDAT COPIES
-----------------------
A template/virtual COMDAT is emitted into every TU that instantiates it, so one
spelling can name several different bodies.  A pair is reported only when NO
compiled copy of the member could have been folded into the survivor's body;
copies that agree are counted, and a name with several DISTINCT copies is
listed under ``ambiguous_copies`` so the reading is never silent about it.

USAGE
-----
    python3 scripts/check_alias_map_soundness.py             # report only
    python3 scripts/check_alias_map_soundness.py --check     # gate: exit 1
    python3 scripts/check_alias_map_soundness.py --json out.json
    python3 scripts/check_alias_map_soundness.py --explain 'MakeString@0x823d13d0'
    python3 scripts/check_alias_map_soundness.py --map /tmp/corrupt.json --check

Exit codes: 0 clean, 1 the check FAILED (a gated group folds two different
bodies), 2 the check could not run (no objects / no map).

DO NOT mass-edit the map from this script.  It reports; a human adjudicates.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from project_root import project_root  # noqa: E402

# --------------------------------------------------------------------------- #
# X360 COFF / PPC
# --------------------------------------------------------------------------- #

SYM_ENT = 18
SYM_CLASS_EXTERNAL = 2
SYM_CLASS_STATIC = 3
SYM_CLASS_LABEL = 6
DT_FUNCTION = 0x20
IMAGE_REL_PPC_PAIR = 0x12

#: Which bits of the containing 32-bit word each PPC COFF relocation patches.
#: Anything absent is masked WHOLE-WORD, the conservative direction.
FIELD_MASK = {
    0x00: 0x00000000,   # ABSOLUTE  -- patches nothing
    0x02: 0xFFFFFFFF,   # ADDR32
    0x03: 0x03FFFFFC,   # ADDR24    (LI)
    0x04: 0x0000FFFF,   # ADDR16
    0x05: 0x0000FFFC,   # ADDR14    (BD)
    0x06: 0x03FFFFFC,   # REL24     (LI)
    0x07: 0x0000FFFC,   # REL14     (BD)
    0x0A: 0xFFFFFFFF,   # ADDR32NB
    0x0B: 0xFFFFFFFF,   # SECREL
    0x0C: 0x0000FFFF,   # SECTION
    0x0F: 0x0000FFFF,   # SECREL16
    0x10: 0x0000FFFF,   # REFHI
    0x11: 0x0000FFFF,   # REFLO
    0x13: 0x0000FFFF,   # SECRELLO
    0x14: 0x0000FFFF,   # SECRELHI
}
_DEFAULT_MASK = 0xFFFFFFFF
REFHI, REFLO = 0x10, 0x11

_NOP = b"\x60\x00\x00\x00"


def _strip_pad(b: bytes) -> bytes:
    while len(b) >= 4 and b[-4:] == _NOP:
        b = b[:-4]
    return b


class Func:
    """One function-typed COFF symbol: its bytes and its relocations."""

    __slots__ = ("name", "obj", "text", "relocs")

    def __init__(self, name, obj, text, relocs):
        self.name = name
        self.obj = obj
        self.text = text
        self.relocs = relocs          # [(offset, type, symbol name)]

    def copy_key(self):
        """Identity of one emitted copy: bytes plus relocation triples."""
        return (self.text, tuple(self.relocs))


def parse_obj(path: Path) -> dict:
    """``{function symbol: Func}`` for one X360 COFF object.

    COFF headers and relocation records are LITTLE-endian even for the
    big-endian PPC target; only the instruction bytes are big-endian.  A
    function's extent ends at the next FUNCTION-typed symbol, never at the next
    symbol of any kind -- MSVC emits ``$M<n>`` branch labels inside a body, and
    a reader that stops at those truncates every function to its first basic
    block (measured: ``CharClip::EvaluateChannel`` read 16 of 244 bytes).
    """
    try:
        data = path.read_bytes()
    except OSError:
        return {}
    if len(data) < 20:
        return {}
    _mach, nsec, _ts, symptr, nsym, opthdr, _ch = struct.unpack_from("<HHIIIHH", data, 0)
    if not symptr or not nsym:
        return {}
    sec_off = 20 + opthdr
    sections = []
    for i in range(nsec):
        b = sec_off + i * 40
        if b + 40 > len(data):
            return {}
        _vs, _va, rsize, rptr, relp, _lp, nrel, _nl, chars = struct.unpack_from(
            "<IIIIIIHHI", data, b + 8)
        sections.append(dict(size=rsize, rawptr=rptr, relptr=relp,
                             nrel=nrel, chars=chars))
    strtab = data[symptr + nsym * SYM_ENT:]

    def sname(raw: bytes) -> str:
        if raw[0:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", raw, 4)[0]
            if off >= len(strtab):
                return ""
            end = strtab.find(b"\0", off)
            return strtab[off:end].decode("latin1")
        return raw.rstrip(b"\0").decode("latin1")

    symbols = []
    i = 0
    while i < nsym:
        b = symptr + i * SYM_ENT
        value, secnum, styp, sclass, naux = struct.unpack_from("<IhHBB", data, b + 8)
        symbols.append(dict(name=sname(data[b:b + 8]), value=value, sec=secnum,
                            typ=styp, cls=sclass))
        symbols.extend([None] * naux)
        i += 1 + naux

    by_sec = collections.defaultdict(list)
    for s in symbols:
        if s is None or s["sec"] <= 0 or s["sec"] > len(sections):
            continue
        by_sec[s["sec"]].append(s)

    out = {}
    for si, syms in by_sec.items():
        sec = sections[si - 1]
        if not (sec["chars"] & 0x20):        # IMAGE_SCN_CNT_CODE
            continue
        rels = []
        for r in range(sec["nrel"]):
            b = sec["relptr"] + r * 10
            if b + 10 > len(data):
                break
            vaddr, symidx, rtyp = struct.unpack_from("<IIH", data, b)
            # IMAGE_REL_PPC_PAIR's VirtualAddress is a displacement, not an
            # address: including it claims an offset that belongs to another
            # word.
            if rtyp == IMAGE_REL_PPC_PAIR:
                continue
            nm = ""
            if symidx < len(symbols) and symbols[symidx] is not None:
                nm = symbols[symidx]["name"]
            rels.append((vaddr, rtyp, nm))
        base = sec["rawptr"]
        raw = data[base:base + sec["size"]] if base else b""
        fnsyms = sorted((s for s in syms if s["typ"] == DT_FUNCTION),
                        key=lambda s: s["value"])
        for k, s in enumerate(fnsyms):
            if not s["name"]:
                continue
            start = s["value"]
            end = fnsyms[k + 1]["value"] if k + 1 < len(fnsyms) else len(raw)
            if end <= start or end > len(raw):
                continue
            text = _strip_pad(raw[start:end])
            if not text:
                continue
            rs = [(o - start, t, n) for (o, t, n) in rels
                  if start <= o < start + len(text)]
            out[s["name"]] = Func(s["name"], path.name, text, rs)
    return out


# --------------------------------------------------------------------------- #
# The two halves of a body
# --------------------------------------------------------------------------- #

def masked_words(f: Func) -> tuple:
    """Instruction stream with every relocated FIELD zeroed.

    Per-type field masks, not the whole word: the bits a relocation does not
    patch are real instruction bits.  This is the half every ruler in this repo
    already compares -- including the alias map's own admission test.
    """
    b = bytearray(f.text)
    for off, typ, _s in f.relocs:
        w = off & ~3
        if w + 4 > len(b):
            continue
        word = int.from_bytes(bytes(b[w:w + 4]), "big")
        word &= ~FIELD_MASK.get(typ, _DEFAULT_MASK)
        b[w:w + 4] = word.to_bytes(4, "big")
    out = _strip_pad(bytes(b))
    return tuple(int.from_bytes(out[i:i + 4], "big")
                 for i in range(0, len(out) - 3, 4))


def resolved_operands(f: Func, canon=None) -> tuple:
    """The relocated half: one entry per LOGICAL operand.

    ``(word index, kind, resolved name)``.  REFHI/REFLO are merged into one
    operand because the 32-bit value is split across two instructions and is
    one operand in the source; an unmatched half stands alone rather than being
    dropped, so the read never loses a site.
    """
    c = canon or (lambda s: s)
    sites = sorted((o, t, s) for (o, t, s) in f.relocs if 0 <= o < len(f.text))
    used, out = set(), []
    for i, (off, typ, sym) in enumerate(sites):
        if i in used:
            continue
        name = c(sym)
        if typ == REFHI:
            lo = None
            for j in range(i + 1, len(sites)):
                if j in used:
                    continue
                _o2, t2, s2 = sites[j]
                if t2 == REFLO and c(s2) == name:
                    lo = j
                    break
            if lo is not None:
                used.add(lo)
                out.append((off // 4, "HL", name, sites[lo][0] // 4))
                continue
        out.append((off // 4, typ, name))
    return tuple(out)


def operand_diff(a, b, limit=6):
    da = {o[0]: o for o in a}
    db = {o[0]: o for o in b}
    out = []
    for w in sorted(set(da) | set(db)):
        if da.get(w) == db.get(w):
            continue
        x, y = da.get(w), db.get(w)
        out.append({"word": w,
                    "target": None if x is None else x[2],
                    "ours": None if y is None else y[2]})
        if len(out) >= limit:
            break
    return out


# --------------------------------------------------------------------------- #
# The map
# --------------------------------------------------------------------------- #

#: Groups admitted on the relocation-invariant body test.  These are the ones a
#: resolved-operand disagreement refutes, so these are the ones the gate holds.
GATED_TIERS = ("body-test",)

#: A member of a body-test group that the retail linker map ALSO places at the
#: survivor's address is confirmed by the linker itself, so a body comparison
#: cannot refute it.  See ``member_tier``.
CONFIRMED_TIER = "body-test+retail-confirmed"

#: Tiers whose evidence is independent of the masked body comparison.  A
#: disagreement here is measured and reported, never gated.
NOTE_TIERS = ("retailmap", "retailmap-fn", "retailmap-data",
              "weak-external", "fold-proof", "rdata-const")


def tier_of(g: dict) -> str:
    name = g.get("name", "")
    ev = g.get("evidence", "")
    if name.startswith("retailmap-data:"):
        return "retailmap-data"
    if name.startswith("retailmap:"):
        return "retailmap-fn"
    if name.startswith("weakalias:"):
        return "weak-external"
    if name.startswith("bodytest:"):
        return "fold-proof"
    if name.startswith("rdata-const-pool:"):
        return "rdata-const"
    if ev.startswith("ICF fold class"):
        return "body-test"
    if ev.startswith("COFF weak external"):
        return "weak-external"
    if ev.startswith("orig/373307D9/ham_xbox_r.map"):
        return "retailmap"
    return "unclassified"


_MAP_RE = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+\S*\s*\S*\s*(\S*)"
)


def load_retail_map(path: Path) -> dict:
    """``{symbol: {address, ...}}`` from the shipped MSVC linker map.

    This is the linker's own record of which names it folded, and it is the one
    oracle here that does not depend on our compiled bodies.  A symbol listed at
    an address is a symbol the linker placed there -- so a body-test group is
    only load-bearing on the members this map does NOT place at the survivor.
    """
    out = collections.defaultdict(set)
    try:
        lines = path.read_text(errors="replace").splitlines()
    except OSError:
        return {}
    for line in lines:
        m = _MAP_RE.match(line)
        if m:
            out[m.group(1)].add(m.group(2).lower())
    return dict(out)


def member_tier(group_tier, member, survivor_addrs, retail):
    """Tier for ONE (group, member) pair.

    A ``body-test`` group can contain members the retail linker map ALSO places
    at the survivor's address -- those were folded by the linker and the body
    comparison cannot refute them (measured: ``OnlyReturns@0x823e3b70`` folds
    524 spellings, 521 of which the map confirms; ``MakeString@0x823e1008``
    confirms ``HHH<CompressionType>`` but NOT the ``HHPAD<CompressionType>``
    spelling our source actually emitted).  Only the unconfirmed ones rest on
    the relocation-invariant body test alone, and only those are gated.
    """
    if group_tier != "body-test":
        return group_tier
    addrs = retail.get(member)
    if addrs and survivor_addrs and (addrs & survivor_addrs):
        return CONFIRMED_TIER
    return group_tier


def load_index(paths):
    """``{symbol: [Func, ...]}`` over the compiled objects.

    Every distinct copy is kept: a COMDAT lands in every instantiating TU.
    """
    idx = collections.defaultdict(list)
    seen = collections.defaultdict(set)
    for p in paths:
        for name, f in parse_obj(p).items():
            k = f.copy_key()
            if k in seen[name]:
                continue
            seen[name].add(k)
            idx[name].append(f)
    return dict(idx)


def _repo_indices(repo: Path):
    cfg = json.loads((repo / "objdiff.json").read_text())
    tgt_paths = [repo / u["target_path"] for u in cfg.get("units", [])
                 if u.get("target_path")]
    base_paths = [repo / u["base_path"] for u in cfg.get("units", [])
                  if u.get("base_path")]
    return load_index(tgt_paths), load_index(base_paths), len(tgt_paths), len(base_paths)


def find_relocation_blind_pair(tgt, base, owner, retail, limit_scan=400000):
    """A (target symbol, our symbol) pair that is IDENTICAL under masking and
    DIFFERENT once relocations resolve -- the exact shape the admission body
    test cannot see.

    Both names must be absent from every existing group, so appending a
    synthetic group for them cannot collide with the map's own canon, and the
    pair must not be retail-map-confirmed (a confirmed fold is ground truth and
    would be filed as a note rather than gated).
    """
    by_sig = collections.defaultdict(list)
    scanned = 0
    for name, copies in base.items():
        if name in owner:
            continue
        for c in copies:
            by_sig[masked_words(c)].append((name, c))
            scanned += 1
            if scanned > limit_scan:
                break
        if scanned > limit_scan:
            break
    for tname, tcopies in tgt.items():
        if tname in owner:
            continue
        taddrs = set(retail.get(tname) or ())
        for t in tcopies:
            ops_t = resolved_operands(t)
            if not ops_t:
                continue
            for name, c in by_sig.get(masked_words(t), ()):
                if resolved_operands(c) == ops_t:
                    continue
                if retail.get(name) and (set(retail[name]) & taddrs):
                    continue
                return tname, name
    return None


def selftest(repo: Path, map_path: Path) -> int:
    """Prove the check can FAIL.

    Two runs, both required:

      1. the CURRENT map must PASS -- if it does not, the check is reporting a
         real finding and a corruption demo would prove nothing;
      2. a copy of the map with ONE synthetic, relocation-blind group appended
         must FAIL, and must name that group.
    """
    import tempfile
    scratch = Path(tempfile.mkdtemp(prefix="alias-soundness-selftest-"))
    print(f"SELFTEST 1/2: the current map must pass (scratch {scratch})")
    clean_json = scratch / "clean.json"
    rc_clean = main(["--repo", str(repo), "--map", str(map_path),
                     "--check", "--json", str(clean_json)])
    if rc_clean != 0:
        print("\nSELFTEST: the CURRENT map already FAILS the check. That is a "
              "finding, not a selftest failure -- adjudicate it and re-run.",
              file=sys.stderr)
        return 1

    aliases = json.loads(map_path.read_text())
    groups = aliases.get("groups") or []
    owner = {}
    for gi, g in enumerate(groups):
        for n in [g.get("survivor")] + list(g.get("folded") or []):
            if n:
                owner.setdefault(n, gi)

    tgt, base, _nt, _nb = _repo_indices(repo)
    retail = load_retail_map(repo / "orig" / "373307D9" / "ham_xbox_r.map")
    found = find_relocation_blind_pair(tgt, base, owner, retail)
    if not found:
        print("SELFTEST VACUOUS: could not find a relocation-blind pair in this "
              "tree to corrupt the map with", file=sys.stderr)
        return 5
    tname, ours = found
    taddr = sorted(retail.get(tname) or ["0x0"])[0]
    corrupt = json.loads(json.dumps(aliases))
    corrupt["groups"].append({
        "name": f"selftest-reloc-blind@{taddr}",
        "address": f"0x{taddr}",
        "survivor": tname,
        "folded": [ours],
        "sites": 1,
        "survivor_candidates": 1,
        # The "ICF fold class" prefix is what puts this in the GATED body-test
        # tier -- the tier the corruption has to exercise.
        "evidence": ("ICF fold class (SELFTEST ONLY) -- a synthetic group over "
                     "a pair measured identical under masking and different on "
                     "its resolved operands. Never admit this to the real map."),
    })
    corrupt_path = scratch / "corrupt.json"
    corrupt_path.write_text(json.dumps(corrupt, indent=1))
    out_json = scratch / "report.json"

    print(f"\nSELFTEST 2/2: a corrupted map folding {ours}\n"
          f"            onto {tname}\n"
          f"            must fail")
    rc_bad = main(["--repo", str(repo), "--map", str(corrupt_path),
                   "--check", "--json", str(out_json)])
    rep = json.loads(out_json.read_text()) if out_json.is_file() else {}
    names = {r["group"] for r in rep.get("failures", [])}
    want = corrupt["groups"][-1]["name"]
    if rc_bad != 1 or want not in names:
        print(f"\nSELFTEST VACUOUS: corrupted map exit {rc_bad} "
              f"(want 1), synthetic group flagged: {want in names} (want True). "
              f"The detector did not fire on a known-bad input.", file=sys.stderr)
        return 5
    print(f"\nSELFTEST OK: clean map exits 0; corrupted map exits 1 and names "
          f"{want}.")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    # From the INVOCATION, never `Path(__file__).resolve()`: the ninja edge runs
    # this from the tree being built, and in a tree whose scripts/ is a symlink
    # into another checkout `resolve()` would quietly re-test THAT tree's
    # objects. See scripts/project_root.py.
    here = project_root(__file__)
    ap.add_argument("--repo", default=str(here),
                    help="project root (default: the tree named by the invocation)")
    ap.add_argument("--map", default=None,
                    help="alias file (default: <repo>/scripts/symbol_aliases.json)")
    ap.add_argument("--check", action="store_true",
                    help="gate mode: exit 1 if any gated group fails")
    ap.add_argument("--notes", action="store_true",
                    help="also print the non-gated tiers' disagreements")
    ap.add_argument("--json", default=None, help="write the full report here")
    ap.add_argument("--explain", default=None,
                    help="print every pair of one group and exit")
    ap.add_argument("--limit", type=int, default=25,
                    help="max rows printed per section (the counts are never cut)")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the check can fail: current map must pass, a "
                         "corrupted copy must fail (exit 5 if vacuous)")
    args = ap.parse_args(argv)

    repo = Path(args.repo)
    map_path = Path(args.map) if args.map else repo / "scripts" / "symbol_aliases.json"
    if args.selftest:
        return selftest(repo, map_path)
    cfg_path = repo / "objdiff.json"
    if not map_path.is_file():
        print(f"CANNOT RUN: no alias map at {map_path}", file=sys.stderr)
        return 2
    if not cfg_path.is_file():
        print(f"CANNOT RUN: no objdiff.json at {cfg_path}", file=sys.stderr)
        return 2

    aliases = json.loads(map_path.read_text())
    groups = aliases.get("groups") or []
    cfg = json.loads(cfg_path.read_text())

    tgt_paths, base_paths = [], []
    for u in cfg.get("units", []):
        if u.get("target_path"):
            tgt_paths.append(repo / u["target_path"])
        if u.get("base_path"):
            base_paths.append(repo / u["base_path"])

    tgt = load_index(tgt_paths)
    base = load_index(base_paths)
    if not tgt or not base:
        print(f"CANNOT RUN: no compiled objects (target={len(tgt)}, "
              f"base={len(base)}); build first", file=sys.stderr)
        return 2

    retail_path = repo / "orig" / "373307D9" / "ham_xbox_r.map"
    retail = load_retail_map(retail_path)
    if not retail:
        print(f"WARNING: no retail linker map at {retail_path}; every member of "
              f"a body-test group is gated on the body comparison alone",
              file=sys.stderr)

    # name -> its group, for the operand canon
    owner = {}
    for gi, g in enumerate(groups):
        for n in [g.get("survivor")] + list(g.get("folded") or []):
            if n:
                owner.setdefault(n, gi)
    survivors = [g.get("survivor") for g in groups]

    def canon(name):
        gi = owner.get(name)
        return name if gi is None else survivors[gi]

    pop = collections.Counter(tier_of(g) for g in groups)
    drops = collections.Counter()
    pair = collections.Counter()
    gated_fail, note_fail, mask_div = [], [], []
    ambiguous = []
    examined = 0

    for g in groups:
        S = g.get("survivor")
        tier = tier_of(g)
        members = [m for m in (g.get("folded") or []) if m]
        if not S:
            drops[f"{tier}: no survivor"] += 1
            continue
        tgt_copies = tgt.get(S)
        if not tgt_copies:
            drops[f"{tier}: survivor not defined in target objects"] += 1
            continue
        have = [m for m in members if m in base]
        if not have:
            drops[f"{tier}: no folded member compiled in our tree"] += 1
            continue
        examined += 1

        survivor_addrs = set(retail.get(S) or ())
        if g.get("address"):
            # The map file writes bare hex; the alias file writes 0x-prefixed.
            survivor_addrs.add(str(g["address"]).lower().removeprefix("0x"))

        for m in have:
            mtier = member_tier(tier, m, survivor_addrs, retail)
            ours = base[m]
            if len(ours) > 1:
                ambiguous.append({"group": g.get("name"), "member": m,
                                  "n_distinct_copies": len(ours)})
            masks_t = {masked_words(t) for t in tgt_copies}
            masks_b = {masked_words(b) for b in ours}
            if not (masks_t & masks_b):
                pair[f"{mtier}: MASK_DIVERGENT"] += 1
                if mtier in GATED_TIERS:
                    mask_div.append({
                        "group": g.get("name"), "address": g.get("address"),
                        "tier": mtier, "survivor": S, "member": m,
                        "diff": [],
                        "survivor_objects": sorted({t.obj for t in tgt_copies}),
                        "member_objects": sorted({b.obj for b in ours}),
                        "target_words": sorted(len(x) for x in masks_t),
                        "our_words": sorted(len(x) for x in masks_b),
                    })
                continue
            # Compare only the copies whose masked half matches, so an
            # unrelated duplicate cannot supply the verdict.
            keep_t = [t for t in tgt_copies if masked_words(t) in masks_b]
            keep_b = [b for b in ours if masked_words(b) in masks_t]
            ops_t = {resolved_operands(t, canon) for t in keep_t}
            ops_b = {resolved_operands(b, canon) for b in keep_b}
            if not any(ops_t) and not any(ops_b):
                pair[f"{mtier}: no relocations either side"] += 1
                continue
            hit = ops_t & ops_b
            if hit:
                pair[f"{mtier}: agree"] += 1
                continue
            pair[f"{mtier}: RESOLVED_DIVERGENT"] += 1
            row = {
                "group": g.get("name"),
                "address": g.get("address"),
                "tier": mtier,
                "survivor": S,
                "member": m,
                "diff": operand_diff(sorted(ops_t)[0], sorted(ops_b)[0]),
                "survivor_objects": sorted({t.obj for t in keep_t}),
                "member_objects": sorted({b.obj for b in keep_b}),
            }
            (gated_fail if mtier in GATED_TIERS else note_fail).append(row)

    total_pairs = sum(pair.values())
    report = {
        "map": str(map_path),
        "repo": str(repo),
        "groups_in_file": len(groups),
        "members_in_file": sum(1 + len(g.get("folded") or []) for g in groups),
        "population_by_tier": dict(pop),
        "groups_examined": examined,
        "drops": dict(drops),
        "pairs": dict(pair),
        "pairs_total": total_pairs,
        "gated_tiers": list(GATED_TIERS),
        "failures": gated_fail,
        "notes": note_fail,
        "mask_divergent_gated": mask_div,
        "ambiguous_copies": ambiguous,
        "target_objects": len(tgt_paths),
        "base_objects": len(base_paths),
        "target_symbols": len(tgt),
        "base_symbols": len(base),
    }

    if args.explain:
        for r in gated_fail + note_fail:
            if r["group"] == args.explain:
                print(json.dumps(r, indent=1))
        if not any(r["group"] == args.explain
                   for r in gated_fail + note_fail):
            print(f"{args.explain}: no resolved-operand divergence recorded")
            print(f"  in the map: "
                  f"{'yes' if any(g.get('name') == args.explain for g in groups) else 'NO'}")
            print(f"  survivor in target objects: "
                  f"{'yes' if any(g.get('name') == args.explain and tgt.get(g.get('survivor')) for g in groups) else 'no'}")
        return 0

    print("ALIAS MAP SOUNDNESS -- relocations charged\n")
    print(f"  map                     {map_path}")
    print(f"  groups in file          {len(groups)}")
    print(f"  members in file         {report['members_in_file']}")
    print(f"  target objects / syms   {len(tgt_paths)} / {len(tgt)}")
    print(f"  base objects   / syms   {len(base_paths)} / {len(base)}")
    print("\nPOPULATION BY EVIDENCE TIER")
    for t, n in pop.most_common():
        mark = "GATED" if t in GATED_TIERS else "note "
        print(f"  {mark}  {n:5d}  {t}")
    print("\nDENOMINATOR CHAIN")
    print(f"  {len(groups):5d}  groups in the map")
    for reason, n in sorted(drops.items()):
        print(f"  {n:5d}  DROP  {reason}")
    print(f"  {examined:5d}  groups examined (survivor + >=1 member found)")
    print(f"  {total_pairs:5d}  (group, member) pairs compared")
    for verdict, n in sorted(pair.items()):
        tag = "FAIL" if verdict.endswith("RESOLVED_DIVERGENT") else "    "
        print(f"  {tag}  {n:5d}  {verdict}")
    if ambiguous:
        print(f"  {len(ambiguous):5d}  pairs where a spelling has >1 distinct "
              f"compiled copy (all copies compared)")

    def show(rows, title):
        print(f"\n{title} ({len(rows)})")
        for r in rows[:args.limit]:
            print(f"  group   {r['group']}  [{r['address']}]")
            print(f"  member  {r['member']}")
            for d in r.get("diff") or []:
                print(f"    word {d['word']:<4} target {d['target']}")
                print(f"                    ours   {d['ours']}")
            if "target_words" in r:
                print(f"    masked body lengths: target {r['target_words']} "
                      f"ours {r['our_words']}")
            print(f"    target obj {', '.join(r['survivor_objects'])}")
            print(f"    ours   obj {', '.join(r['member_objects'])}")
        if len(rows) > args.limit:
            print(f"  ... {len(rows) - args.limit} more (count above is total; "
                  f"use --json for the rest)")

    show(gated_fail, "GATED FAILURES -- body-test evidence refuted")
    if mask_div:
        show(mask_div, "GATED GROUPS WHOSE MASKED BODIES ALREADY DIFFER "
                       "(the stated evidence no longer holds)")
    if args.notes:
        show(note_fail, "NOTES -- ground-truth tiers, not gated")

    if args.json:
        Path(args.json).write_text(json.dumps(report, indent=1))
        print(f"\njson -> {args.json}")

    # Fail closed on provenance the check does not understand: a group whose
    # evidence tier this script cannot name would otherwise be silently
    # un-gated, which is the fail-open direction that lets a bad entry in.
    unknown = pop.get("unclassified", 0)
    if unknown:
        print(f"\nGATED FAILURE -- {unknown} group(s) carry an evidence string "
              f"this check does not classify, so it cannot say whether they are "
              f"gated. Teach tier_of() the new tier rather than ignoring it.")
    failed = bool(gated_fail) or bool(unknown)
    print("\n" + ("CHECK FAILED" if failed else "CHECK PASSED")
          + f": {len(gated_fail)} gated failure(s) over {total_pairs} pairs "
            f"examined; {sum(n for t, n in pop.items() if t in GATED_TIERS)} "
            f"gated groups in the map"
          + (f"; {unknown} unclassified group(s)" if unknown else ""))
    if args.check and failed:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
