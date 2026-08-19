#!/usr/bin/env python3
"""
icf_pairing_bodytest.py -- decide whether a 0% "we wrote no body at all" row is
a REAL missing implementation or just an ICF NAMING artifact, by comparing bytes.

The class this answers
----------------------
Retail DC3 was linked with MSVC /OPT:ICF, which folds byte-identical COMDATs to a
single address. ``orig/373307D9/ham_xbox_r.map`` lists every fold member at that
one address; dtk's splitter has to pick ONE of those names for
``config/373307D9/symbols.txt``; objdiff pairs functions BY NAME. So when the
name dtk picked is not the one OUR object emits for that address, the row scores
0% even though our bytes are already correct and already there under the fold
partner's name. Worked example:

    ham_xbox_r.map
      0005:00430708  ?Save@FxSendBitCrush@@UAAXAAVBinStream@@@Z    82760708 f  synth:FxSendBitCrush.obj
      0005:00430708  ?Save@FxSendDistortion@@UAAXAAVBinStream@@@Z  82760708 f  synth:FxSendDistortion.obj
    symbols.txt   only the BitCrush name, at .text:0x82760708
    splits.txt    0x82760708 lies in system/synth/FxSendDistortion.cpp's .text range
    our object    FxSendDistortion.obj defines ?Save@FxSendDistortion@@

2026-08-19 sweep over the 840 tier-2 rows: 292 are this shape, 286 of them
witnessed byte-identical here.

What it actually compares
-------------------------
For each candidate row (target defines the symbol, our object does not, and our
object DOES define another member of the same map address's fold set):

  * target side  -- the instruction bytes in ``build/373307D9/asm/<unit>.s``,
    which dtk emits as ``/* ADDR OFF  AA BB CC DD */`` comments.
  * our side     -- the COMDAT section bytes for the peer symbol, straight out of
    the PE-COFF object's section table.

Both sides are then MASKED before comparison, because the two are at different
link stages:

  * ``b`` / ``bc`` (opcodes 18 / 16): the displacement is zeroed, AA and LK kept.
  * every offset carrying a relocation in OUR object: the low 16 bits are zeroed
    on BOTH sides (our object has 0 there plus a reloc entry; the linked target
    has the resolved @ha/@l value).

Limits, stated because they bound what a pass means
---------------------------------------------------
  * Masking branch displacements ENTIRELY means the test cannot see a ``bl`` to a
    DIFFERENT callee. That is a false-positive risk, bounded by the fact that
    /OPT:ICF only folds COMDATs whose relocations resolve identically -- but it
    is not checked here. For a stronger verdict use decomp-synth's
    ``tools/revcomp/probes/probe_icf_foldtest.py``.
  * A mismatch is NOT automatically a bug: several fold sets have more than one
    member in our object, and this picks the first that matches; ``??_E<T>``
    (vector deleting dtor) rows resolve through a COFF weak external to
    ``??_G<T>`` and are a thunk, not the same body.
  * BEWARE THE MASK ASYMMETRY. An earlier version of this script masked
    relocation immediates on our side only and reported 89 false differences out
    of 292 -- including rows that a one-off hand diff then showed differ in
    exactly two ``lis``/``lfs`` immediates, i.e. purely the relocation. Mask both
    sides with the same offset set or the number is meaningless.
  * IT ONLY EVER TESTS ONE INSTANTIATION PER FOLD GROUP -- the one whose name dtk
    happened to write into symbols.txt, in the unit dtk happened to place it in.
    A fold group with 15 members gets ONE probe. ``ObjPtrVec::erase`` has three
    groups (0x823EA0B8 / 0x82706B78 / 0x82848AD0) that differ only in whether
    MSVC inlined ``Set``, and the same TU (world:LightPreset.obj) contributes to
    two of them, so a per-row PASS says nothing about the other members.
  * A 4-byte target body may be an /OPT:ICF BRANCH THUNK (``b OnlyReturns``), not
    a fold member at all. Comparing a real peer body against a thunk always says
    DIFFERENT and always means nothing.

2026-08-19 (second pass): sweeping fake_impl_scan's output rather than
report.json hid rows in two ways at once -- fake_impl_scan pre-filters to
``target_size >= 80`` and ``pct <= 70``, and this script used to return
"not-in-map" for every synthetic dtk label. Use ``--report``; it is a 60-second
sweep of all ~48k scored rows and it is the only population worth quoting.

HOW LITTLE OF THE BINARY THIS ACTUALLY TESTS (measured 2026-08-19 by
``icf_bucket_census.py``, which re-derives these buckets from scratch)
--------------------------------------------------------------------------
Of **48,344** report.json rows, only **373** ever reach the byte comparison:

    30,141  we-define-it        (no artifact to diagnose)
    15,764  no-object           (no built .obj for the unit)
     1,893  no-fold-peer
       373  TESTED   <-- 0.8% of the binary
       163  no-address
        10  addr-not-in-map

So "this class is clean" out of this instrument is a statement about **0.8 %**
of the binary, not about the binary. Quote the denominator whenever you quote a
pass rate.

Of the map-blind rows the second pass recovered: **1,687** report rows carry a
symbol that is absent from ``ham_xbox_r.map``, of which **1,512** are synthetic
``merged_*`` / ``fn_*`` dtk labels. (An earlier draft of this docstring said
"~1,730"; that figure was never derived and should not be re-cited.)

Whole-binary byte-test failures are INSTRUMENT-DEPENDENT, not a fact about the
binary. Three passes over the same tree produced 9, then 17, then -- with the
independent PE/.pdata instrument (``icf_foldcheck_pe.py``) -- 14 real DIFFER
plus a remainder whose target bodies do not decode (4-byte ICF branch thunks and
``$4PPPPPPPM@A@`` vtordisp adjustors, which have no ``.pdata`` entry). Post-fix
on this lane's tree the same instrument reports **8 DIFFER + 16 no-target-body**
out of 373 TESTED. Report the instrument and the tree alongside the number, or
the number means nothing.

Read-only. Reads the built objects, the split asm and the retail map; writes
nothing.

Usage:
    python3 scripts/analysis/icf_pairing_bodytest.py                 # sweep the tier-2 pool
    python3 scripts/analysis/icf_pairing_bodytest.py --scan JSON     # a fake_impl_scan.py output
    python3 scripts/analysis/icf_pairing_bodytest.py --one SYMBOL --unit default/system/...
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def load_obj(path):
    """Parse a PE-COFF object: section table (with reloc pointers) + defined symbols."""
    d = open(path, "rb").read()
    machine, nsec, ts, ptr, nsym, osz, ch = struct.unpack_from("<HHIIIHH", d, 0)
    hdr = 20 + osz
    secs = []
    for i in range(nsec):
        r = d[hdr + i * 40 : hdr + i * 40 + 40]
        nm = r[0:8].rstrip(b"\x00").decode("latin1")
        vsz, va, rawsz, rawptr, relptr = struct.unpack_from("<IIIII", r, 8)
        nrel = struct.unpack_from("<H", r, 32)[0]
        secs.append(dict(name=nm, rawptr=rawptr, rawsz=rawsz, relptr=relptr, nrel=nrel))
    strtab = ptr + nsym * 18
    syms = {}
    i = 0
    while i < nsym:
        r = d[ptr + i * 18 : ptr + i * 18 + 18]
        nm = r[0:8]
        if nm[0:4] == b"\x00\x00\x00\x00":
            o = struct.unpack_from("<I", nm, 4)[0]
            e = d.index(b"\x00", strtab + o)
            s = d[strtab + o : e].decode("latin1")
        else:
            s = nm.rstrip(b"\x00").decode("latin1")
        value, secnum, typ, sclass, naux = struct.unpack_from("<IhHBB", r, 8)
        if secnum > 0 and s not in syms:
            syms[s] = (secnum, value)
        i += 1 + naux
    return d, secs, syms


def our_body(path, sym):
    """Bytes of `sym`'s COMDAT in `path`, plus the set of relocated offsets inside it."""
    d, secs, syms = load_obj(path)
    if sym not in syms:
        return None
    secnum, value = syms[sym]
    s = secs[secnum - 1]
    body = d[s["rawptr"] + value : s["rawptr"] + s["rawsz"]]
    relocs = set()
    for i in range(s["nrel"]):
        va = struct.unpack_from("<I", d, s["relptr"] + i * 10)[0]
        if va >= value:
            relocs.add(va - value)
    return body, relocs


BYTE_RE = re.compile(r"/\* [0-9A-F]+ [0-9A-F]+  ((?:[0-9A-F]{2} ){3}[0-9A-F]{2}) \*/")


ASM_HDR_RE = re.compile(r"^#\s+\.\w+:0x[0-9A-Fa-f]+\s+\|\s+0x([0-9A-Fa-f]{8})\s+\|\s+size:")


def asm_addresses(asmpath):
    """symbol -> link address, read out of the `# .text:0x.. | 0xADDR | size:`
    comment dtk emits above each `.fn`.

    This exists because ``ham_xbox_r.map`` only knows real mangled names. dtk
    writes SYNTHETIC labels (``merged_<addr>``, ``merged_ObjPtrVecErase``,
    ``fn_<addr>``) into symbols.txt for folded and EH-funclet bodies, and those
    are absent from the map -- so a map-only lookup silently drops every one of
    them. In this tree that is **1,512** synthetic labels inside **1,687** rows
    whose symbol is not in the map at all, and one of them
    (``merged_ObjPtrVecErase``) is a real ObjPtrVec::erase fold group.

    ORDER OF THE TWO DEFECTS, because the lane that found them got it backwards:
    this one -- the map lookup returning "not-in-map" -- fires FIRST and is why
    ``merged_ObjPtrVecErase`` looked clean. Only once the address resolves does
    the second defect (``target_body`` matching ``.fn "NAME"`` only, while dtk
    emits synthetic labels BARE -- the load-bearing census figure is that the
    QUOTED count is 0, in every sample) become reachable at all. Fixing the
    quoting alone would have changed nothing.
    """
    m = {}
    if not os.path.exists(asmpath):
        return m
    pend = None
    for line in open(asmpath, errors="replace"):
        h = ASM_HDR_RE.match(line)
        if h:
            pend = h.group(1).lower()
            continue
        if line.startswith(".fn ") and pend:
            m[line[4:].split(", ")[0].strip().strip('"')] = pend
        pend = None
    return m


def target_body(asmpath, sym):
    """Instruction bytes of `sym` from dtk's split asm listing."""
    if not os.path.exists(asmpath):
        return None
    # dtk quotes mangled names but emits synthetic labels (merged_*, fn_*) bare.
    heads = ('.fn "%s"' % sym, ".fn %s," % sym)
    out = []
    inside = False
    for line in open(asmpath, errors="replace"):
        if line.startswith(heads):
            inside = True
            continue
        if inside and line.startswith(".endfn"):
            break
        if inside:
            m = BYTE_RE.match(line.strip())
            if m:
                out.append(bytes.fromhex(m.group(1).replace(" ", "")))
    return b"".join(out) if out else None


def mask(b, relocs):
    """Zero branch displacements and relocated low-16 immediates. Apply to BOTH sides
    with the SAME `relocs` set -- see the module docstring."""
    a = bytearray(b)
    for i in range(0, len(a) & ~3, 4):
        w = struct.unpack_from(">I", a, i)[0]
        op = w >> 26
        if op in (16, 18):  # bc / b: keep opcode + AA + LK, drop the displacement
            struct.pack_into(">I", a, i, w & 0xFC000003)
        elif i in relocs:
            struct.pack_into(">I", a, i, w & 0xFFFF0000)
    return bytes(a)


def read_map(project):
    """address -> [symbol], and symbol -> (address, flags, contributing obj)."""
    addr2 = collections.defaultdict(list)
    sym2rec = {}
    pat = re.compile(
        r"^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f\s*i?)\s+(\S+)\s*$"
    )
    path = os.path.join(project, "orig", "373307D9", "ham_xbox_r.map")
    for line in open(path, errors="replace"):
        m = pat.match(line)
        if not m:
            continue
        _, _, sym, addr, flags, obj = m.groups()
        sym2rec.setdefault(sym, (addr, flags.strip(), obj))
        addr2[addr].append(sym)
    return addr2, sym2rec


def verdict_for(project, unit, symbol, addr2, sym2rec):
    rel_unit = unit.replace("default/", "", 1)
    objp = os.path.join(project, "build/373307D9/src", rel_unit + ".obj")
    asmp = os.path.join(project, "build/373307D9/asm", rel_unit + ".s")
    rec = sym2rec.get(symbol)
    addr = rec[0] if rec else asm_addresses(asmp).get(symbol)
    if not addr:
        return "no-address", None
    if not os.path.exists(objp):
        return "no-object", None
    try:
        d, secs, syms = load_obj(objp)
    except Exception as e:
        return "unreadable-object", None
    if symbol in syms:
        return "we-define-it", None
    peers = [p for p in addr2.get(addr, []) if p != symbol and p in syms]
    if not peers:
        return ("no-fold-peer" if addr2.get(addr) else "addr-not-in-map"), None
    tb = target_body(asmp, symbol)
    if tb is None:
        return "no-target-asm", None
    for p in peers:
        got = our_body(objp, p)
        if not got:
            continue
        body, relocs = got
        body = body[: len(tb)]
        if len(body) == len(tb) and mask(body, relocs) == mask(tb, relocs):
            return "BYTE-IDENTICAL", p
    return "DIFFERENT", peers[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", default=REPO)
    ap.add_argument(
        "--scan",
        default=os.path.join(os.environ.get("TMPDIR", "/tmp"), "fake_impl_scan.json"),
        help="fake_impl_scan.py --out JSON to sweep (default /tmp/fake_impl_scan.json)",
    )
    ap.add_argument(
        "--report",
        action="store_true",
        help="sweep EVERY function in build/373307D9/report.json instead of the "
        "fake_impl_scan output (which is pre-filtered to target>=80B, pct<=70)",
    )
    ap.add_argument("--one", help="a single mangled symbol instead of a sweep")
    ap.add_argument("--unit", help="unit for --one, e.g. default/system/synth/FxSendDistortion")
    ap.add_argument("--verbose", action="store_true", help="print every row, not just the tally")
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    addr2, sym2rec = read_map(project)

    if args.one:
        if not args.unit:
            ap.error("--one needs --unit")
        v, peer = verdict_for(project, args.unit, args.one, addr2, sym2rec)
        print("%-16s %s" % (v, args.one))
        if peer:
            print("        peer: %s" % peer)
        return

    if args.report:
        # The HONEST population: every function objdiff scored, not just the
        # rows fake_impl_scan surfaced. fake_impl_scan defaults to
        # --min-target-size 80 / --max-pct 70, so sweeping its output can only
        # ever see a subset -- it cannot, for instance, see the 4-byte
        # `b OnlyReturns` ICF thunks or anything above 70%.
        rep = json.load(open(os.path.join(project, "build/373307D9/report.json")))
        rows = [
            dict(unit=u["name"], symbol=f["name"], target_size=int(f.get("size", 0)))
            for u in rep["units"]
            for f in (u.get("functions") or [])
            if f.get("name")
        ]
    else:
        rows = [
            r
            for r in json.load(open(args.scan))["fakes"]
            if r["our_pct"] == 0.0 and r["our_real_insns"] == 0
        ]
    tally = collections.Counter()
    interesting = []
    for r in rows:
        v, peer = verdict_for(project, r["unit"], r["symbol"], addr2, sym2rec)
        tally[v] += 1
        if v in ("no-address", "no-object", "we-define-it", "no-fold-peer",
                 "addr-not-in-map", "unreadable-object"):
            continue
        if args.verbose or v != "BYTE-IDENTICAL":
            interesting.append((r["target_size"], v, r["unit"], r["symbol"], peer))
    # Print EVERY bucket, including the ones we skip. A verdict of "the class is
    # clean" is only as good as the size of the population that never got tested,
    # so the skips are part of the result, not noise to be filtered out.
    print("rows considered: %d" % sum(tally.values()))
    for k, n in tally.most_common():
        print("  %6d  %s" % (n, k))
    if interesting:
        print("\n-- rows to look at --")
        for sz, v, unit, sym, peer in sorted(interesting, reverse=True):
            print("  %5d %-15s %s" % (sz, v, unit))
            print("        %s" % sym)
            print("        peer: %s" % peer)


if __name__ == "__main__":
    main()
