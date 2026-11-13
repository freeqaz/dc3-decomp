#!/usr/bin/env python3
"""Byte-identity gate for comment-only source edits (lane-note moves).

WHY THIS EXISTS
===============
Moving a comment block out of a source file shifts every line below it.  That
is only inert if no line number reaches the binary, and in this tree some do:
`__LINE__` is passed explicitly by src/system/synth_xbox/SynthSample.cpp
(PhysicalFreeTracked), oggvorbis/misc.h, the stlport/curl debug allocators, and
anything that pastes it into a symbol name; `#line` directives (CharBones.cpp,
CharBonesSamples.cpp, Memory_Xbox.cpp, several dsp/ files) restate it.  The
2026-11-13 pilot also saw a comment-only move change MatAnim.obj's COMDAT
*section order*, so a whole-file hash says "changed" even when every symbol is
identical -- a file hash is the wrong ruler in both directions.

So this compares what objdiff and the linker actually consume, per symbol:

  * every non-section symbol defined in every base object
    (build/<v>/src/**.obj): storage class, type, section name +
    characteristics + COMDAT selection, its byte range, and the relocations
    inside that range as (offset-in-range, type, target), where a target is the
    symbol's NAME, or for a section-symbol target the target section's name and
    content hash (so a reordered section table is not a difference, a changed
    string pool is);
  * the multiset of sections themselves (same normalisation, order-free);
  * the whole of report.json, unit by unit (measures, sections, and every
    function's size / fuzzy / normalized / metadata).

Any difference, in any unit, is a refusal.  Undefined behaviour of "it still
scores 100" is not accepted: a line number baked into a MILO_FAIL string moves
no score and is still a binary change.

USAGE
=====
    # in the tree being edited, after a full `ninja`, BEFORE the edit
    scripts/notes/verify_comment_move.py snapshot --out ~/tmp/notes-gate/base

    # after the edit (runs `ninja` itself unless --no-build)
    scripts/notes/verify_comment_move.py compare --before ~/tmp/notes-gate/base

`compare` also refuses to be vacuous: every .cpp the working tree changed since
the snapshot's HEAD must map to an object that was rewritten AFTER the
snapshot (ninja actually recompiled it), and report.json must be newer than the
snapshot.  A gate that compared yesterday's objects with yesterday's objects
would always pass.

Exit codes: 0 identical   1 DIFFERENT (refuse the move)   2 could not verify
"""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from project_root import project_root  # noqa: E402

VERSION = "373307D9"
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080


class CoffError(Exception):
    pass


def _h(b):
    return hashlib.sha256(b).hexdigest()[:20]


def parse_coff(data):
    """Return (sections, symbols) of a classic (non-bigobj) COFF object."""
    if len(data) < 20:
        raise CoffError("truncated header")
    machine, nsec, _ts, symoff, nsym, opt, _ch = struct.unpack_from("<HHIIIHH", data, 0)
    if machine == 0 and nsec == 0xFFFF:
        raise CoffError("bigobj COFF is not supported")
    strtab_off = symoff + nsym * 18
    strtab = data[strtab_off:] if symoff else b""

    def strname(raw):
        if raw[:1] == b"/" and raw[1:].rstrip(b"\0").isdigit():
            o = int(raw[1:].rstrip(b"\0"))
            return strtab[o : strtab.index(b"\0", o)].decode("latin-1")
        return raw.rstrip(b"\0").decode("latin-1")

    sections = []
    off = 20 + opt
    for i in range(nsec):
        (raw, _vs, _va, size, ptr, rptr, _lptr, nrel, _nl, ch) = struct.unpack_from(
            "<8sIIIIIIHHI", data, off + i * 40
        )
        name = strname(raw)
        if ch & IMAGE_SCN_LNK_NRELOC_OVFL and nrel == 0xFFFF:
            nrel = struct.unpack_from("<I", data, rptr)[0]
            rstart = rptr + 10
            nrel -= 1
        else:
            rstart = rptr
        relocs = [struct.unpack_from("<IIH", data, rstart + k * 10) for k in range(nrel)]
        content = b"" if (ch & IMAGE_SCN_CNT_UNINITIALIZED_DATA or not ptr) else data[ptr : ptr + size]
        sections.append({"name": name, "size": size, "ch": ch, "data": content,
                         "relocs": relocs, "sel": None})

    symbols = []  # index-aligned with the raw table; aux slots are None
    i = 0
    while i < nsym:
        p = symoff + i * 18
        raw, value, secnum, typ, scl, naux = struct.unpack_from("<8sIhHBB", data, p)
        if raw[:4] == b"\0\0\0\0":
            o = struct.unpack_from("<I", raw, 4)[0]
            name = strtab[o : strtab.index(b"\0", o)].decode("latin-1")
        else:
            name = raw.rstrip(b"\0").decode("latin-1")
        sym = {"name": name, "value": value, "sec": secnum, "type": typ, "cls": scl,
               "is_section": False}
        if (scl == IMAGE_SYM_CLASS_STATIC and naux >= 1 and value == 0 and secnum > 0
                and secnum <= len(sections) and sections[secnum - 1]["name"] == name):
            sym["is_section"] = True
            aux = data[p + 18 : p + 36]
            sections[secnum - 1]["sel"] = aux[14]
        symbols.append(sym)
        symbols.extend([None] * naux)
        i += 1 + naux
    return sections, symbols


def _target(symbols, sections, idx):
    s = symbols[idx] if 0 <= idx < len(symbols) else None
    if s is None:
        return ("bad", idx)
    if s["is_section"]:
        sec = sections[s["sec"] - 1]
        return ("sec", sec["name"], _h(sec["data"]))
    if s["sec"] <= 0:
        return ("ext", s["name"])
    return ("sym", s["name"])


def fingerprint_object(path):
    """{'symbols': {name: [fp, ...]}, 'sections': [fp, ...]} for one object."""
    return fingerprint_object_bytes(Path(path).read_bytes())


def fingerprint_object_bytes(data):
    sections, symbols = parse_coff(data)
    sec_fps = []
    for s in sections:
        rel = sorted((va, typ, _target(symbols, sections, si)) for va, si, typ in s["relocs"])
        sec_fps.append(_h(repr((s["name"], s["size"], s["ch"], s["sel"], _h(s["data"]), rel)).encode()))
    sec_fps.sort()

    by_sec = {}
    for s in symbols:
        if s is None or s["is_section"] or s["sec"] <= 0:
            continue
        by_sec.setdefault(s["sec"], []).append(s)
    syms = {}
    for secnum, lst in by_sec.items():
        sec = sections[secnum - 1]
        starts = sorted({s["value"] for s in lst})
        for s in lst:
            lo = s["value"]
            later = [v for v in starts if v > lo]
            hi = later[0] if later else sec["size"]
            body = sec["data"][lo:hi]
            rel = sorted((va - lo, typ, _target(symbols, sections, si))
                         for va, si, typ in sec["relocs"] if lo <= va < hi)
            fp = _h(repr((s["cls"], s["type"], sec["name"], sec["ch"], sec["sel"],
                          hi - lo, _h(body), rel)).encode())
            syms.setdefault(s["name"], []).append(fp)
    for v in syms.values():
        v.sort()
    return {"symbols": syms, "sections": sec_fps}


def _fp_one(args):
    root, rel = args
    try:
        return rel, fingerprint_object(os.path.join(root, rel)), None
    except Exception as e:  # noqa: BLE001 -- reported, never swallowed
        return rel, None, f"{type(e).__name__}: {e}"


def objects(root):
    base = Path(root) / "build" / VERSION / "src"
    return sorted(str(p.relative_to(root)) for p in base.rglob("*.obj"))


def fingerprint_tree(root, jobs):
    rels = objects(root)
    out, errors = {}, {}
    with ProcessPoolExecutor(max_workers=jobs) as ex:
        for rel, fp, err in ex.map(_fp_one, [(str(root), r) for r in rels], chunksize=8):
            if err:
                errors[rel] = err
            else:
                out[rel] = fp
    return out, errors


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                          text=True, check=True).stdout


def unit_objects(root):
    """source_path -> base object path, from objdiff.json."""
    cfg = json.loads((Path(root) / "objdiff.json").read_text())
    m = {}
    for u in cfg["units"]:
        sp = (u.get("metadata") or {}).get("source_path")
        if sp and u.get("base_path"):
            m[sp] = u["base_path"]
    return m


def cmd_snapshot(a, root):
    if not a.no_build:
        r = subprocess.run(["ninja"], cwd=root)
        if r.returncode != 0:
            print("CANNOT SNAPSHOT: ninja failed", file=sys.stderr)
            return 2
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    fps, errors = fingerprint_tree(root, a.jobs)
    if errors:
        for k, v in errors.items():
            print(f"unparseable: {k}: {v}", file=sys.stderr)
        return 2
    rep = Path(root) / "build" / VERSION / "report.json"
    meta = {"head": git(root, "rev-parse", "HEAD").strip(), "time": t0,
            "objects": len(fps), "root": str(root)}
    (out / "fingerprints.json").write_text(json.dumps(fps))
    (out / "report.json").write_bytes(rep.read_bytes())
    (out / "meta.json").write_text(json.dumps(meta, indent=1))
    nsym = sum(len(v["symbols"]) for v in fps.values())
    print(f"snapshot: {len(fps)} objects, {nsym} symbols, HEAD {meta['head'][:10]} -> {out}")
    return 0


def compare_reports(before, after):
    diffs = []
    bu = {u["name"]: u for u in before.get("units", [])}
    au = {u["name"]: u for u in after.get("units", [])}
    for name in sorted(set(bu) | set(au)):
        if name not in bu or name not in au:
            diffs.append(f"report unit {name}: present only {'after' if name in au else 'before'}")
            continue
        b, a_ = bu[name], au[name]
        for key in sorted(set(b) | set(a_)):
            if key == "functions":
                bf = {f["name"]: f for f in b.get("functions", [])}
                af = {f["name"]: f for f in a_.get("functions", [])}
                for fn in sorted(set(bf) | set(af)):
                    if bf.get(fn) != af.get(fn):
                        diffs.append(f"report {name}: function {fn}: {bf.get(fn)} -> {af.get(fn)}")
            elif b.get(key) != a_.get(key):
                diffs.append(f"report {name}: {key} differs")
    if before.get("measures") != after.get("measures"):
        diffs.append("report: global measures differ")
    return diffs


def compare_fps(before, after):
    diffs, nsym = [], 0
    for rel in sorted(set(before) | set(after)):
        if rel not in before or rel not in after:
            diffs.append(f"{rel}: object present only {'after' if rel in after else 'before'}")
            continue
        b, a_ = before[rel], after[rel]
        nsym += len(a_["symbols"])
        for name in sorted(set(b["symbols"]) | set(a_["symbols"])):
            if b["symbols"].get(name) != a_["symbols"].get(name):
                what = ("missing after" if name not in a_["symbols"] else
                        "new after" if name not in b["symbols"] else "bytes/relocations differ")
                diffs.append(f"{rel}: symbol {name}: {what}")
        if b["sections"] != a_["sections"]:
            diffs.append(f"{rel}: section multiset differs")
    return diffs, nsym


def cmd_compare(a, root):
    snap = Path(a.before)
    try:
        meta = json.loads((snap / "meta.json").read_text())
        before = json.loads((snap / "fingerprints.json").read_text())
        rep_before = json.loads((snap / "report.json").read_text())
    except (OSError, ValueError) as e:
        print(f"CANNOT VERIFY: snapshot unreadable: {e}", file=sys.stderr)
        return 2
    if not a.no_build:
        r = subprocess.run(["ninja"], cwd=root)
        if r.returncode != 0:
            print("CANNOT VERIFY: ninja failed (the edit does not even build)", file=sys.stderr)
            return 2

    # --- vacuity: the objects of the edited sources must really be new ---------
    changed = [l for l in git(root, "diff", "--name-only", meta["head"], "--", "src").split()
               if l]
    u2o = unit_objects(root)
    stale = []
    rebuilt = 0
    for sp in changed:
        if not sp.endswith((".cpp", ".c", ".cc")):
            continue
        obj = u2o.get(sp)
        if obj is None:
            continue  # not a unit (e.g. #included .cpp); its includer is covered by the sweep
        p = Path(root) / obj
        if not p.exists() or p.stat().st_mtime < meta["time"]:
            stale.append(obj)
        else:
            rebuilt += 1
    rep_path = Path(root) / "build" / VERSION / "report.json"
    if rep_path.stat().st_mtime < meta["time"] and changed:
        stale.append(str(rep_path.relative_to(root)) + " (report not regenerated)")
    if stale:
        print("CANNOT VERIFY (vacuous): these outputs predate the snapshot, so the edit "
              "was never compiled:", file=sys.stderr)
        for s in stale[:20]:
            print(f"  {s}", file=sys.stderr)
        return 2

    after, errors = fingerprint_tree(root, a.jobs)
    if errors:
        for k, v in errors.items():
            print(f"unparseable: {k}: {v}", file=sys.stderr)
        return 2
    diffs, nsym = compare_fps(before, after)
    diffs += compare_reports(rep_before, json.loads(rep_path.read_text()))
    touched_units = [u2o[s] for s in changed if s in u2o]
    if diffs:
        print(f"DIFFERENT: {len(diffs)} difference(s) -- refuse this move.")
        for d in diffs[: a.max_list]:
            print(f"  {d}")
        if len(diffs) > a.max_list:
            print(f"  ... {len(diffs) - a.max_list} more")
        return 1
    tsyms = sum(len(after[o]["symbols"]) for o in touched_units if o in after)
    print(f"byte-identical: {len(after)} objects / {nsym} symbols compared; "
          f"{len(changed)} changed source file(s), {rebuilt} recompiled unit(s) "
          f"carrying {tsyms} symbols; report.json identical "
          f"({len(rep_before.get('units', []))} units)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", help="tree to act on (default: from the invocation path)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snapshot")
    s.add_argument("--out", required=True)
    s.add_argument("--no-build", action="store_true")
    c = sub.add_parser("compare")
    c.add_argument("--before", required=True)
    c.add_argument("--no-build", action="store_true")
    c.add_argument("--max-list", type=int, default=60)
    a = ap.parse_args(argv)
    root = project_root(__file__, a.repo, levels=2)
    return cmd_snapshot(a, root) if a.cmd == "snapshot" else cmd_compare(a, root)


if __name__ == "__main__":
    sys.exit(main())
