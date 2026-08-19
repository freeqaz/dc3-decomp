#!/usr/bin/env python3
"""check_stub_shadow — post-link gate for weak-stub shadowing in the native port.

Problem this exists to catch
----------------------------
`native/src/engine_stubs_generated.cpp` defines *weak* placeholder bodies for
symbols the native port has not recovered.  A `weak` stub can legally shadow a
real `weak_odr` body (templates, `inline` functions, implicitly-instantiated
members): the linker takes whichever definition it sees first and is free to
pick the stub.  The native link additionally passes
`-Wl,--unresolved-symbols=ignore-all` and `-Wl,--allow-multiple-definition`, so
neither a missing symbol nor a duplicate one produces any diagnostic.

Consequence: a function can be fully recovered in the decomp, compiled into a
real object file, and still be *dead* in `dc3-native` because the stub won.  A
confirmed instance was `NuiTransformSkeletonToDepthImage` (stub `_stub_fn_111`),
where every consumer read uninitialised out-parameters.

What this script does
---------------------
1. Reads the *actual link line* for the target out of `build.ninja` (not a
   guess, not a source scan) and expands it into the full set of input objects
   and static-archive members.
2. `nm --defined-only` on the stub object gives the set of stub-provided
   symbols.
3. For every other link input, records which of those symbols it also defines.
4. Disassembles / inspects the **final linked executable** for each duplicated
   symbol and decides which body the linker actually chose, by fingerprinting
   the stub bodies (they reference `dc3::gStubTraceEnabled` /
   `dc3::StubTraceHit`, or are a bare `xor eax,eax; ret`) and by comparing
   `st_size` against every candidate definition.

Exit status
-----------
0  no stub symbol is bound in the final binary while a real definition exists
1  at least one SHADOWED symbol (this is the gate failing)
2  usage / environment error

Run manually:

    python3 scripts/native/check_stub_shadow.py --build-dir native/build

`--json` emits a machine-readable report.  `--all` also lists stub symbols that
are the only definition (informational, never a failure).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass, field

STUB_OBJ_HINT = "engine_stubs_generated.cpp.o"

# Symbol types nm reports for a *definition* we care about.
CODE_TYPES = set("Tt")
DATA_TYPES = set("DdBbRrGgSs")
WEAK_TYPES = set("WwVv")


def die(msg: str) -> None:
    print(f"check_stub_shadow: {msg}", file=sys.stderr)
    sys.exit(2)


def run(cmd: list[str]) -> str:
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0 and not p.stdout:
        return ""
    return p.stdout


# ---------------------------------------------------------------------------
# 1. link inputs, straight out of build.ninja
# ---------------------------------------------------------------------------

def link_inputs(build_dir: str, target: str) -> tuple[list[str], list[str]]:
    """Return (object_files, archive_files) for `target`'s link edge."""
    ninja = os.path.join(build_dir, "build.ninja")
    if not os.path.exists(ninja):
        die(f"{ninja} not found — configure the native build first")
    src = open(ninja, encoding="utf-8", errors="replace").read()

    m = re.search(rf"^build {re.escape(target)}: (\S+) (.*?)(?=\n[^ ])", src,
                  re.M | re.S)
    if not m:
        die(f"no link edge for target {target!r} in {ninja}")
    body = m.group(2)
    # strip the "| implicit || order-only" tail — implicit deps are not inputs
    body = re.split(r"\s\|\|?\s", body)[0]
    inputs = [tok for tok in body.split() if tok]

    # The LINK_LIBRARIES variable of the edge carries external archives
    edge = src[m.start():]
    edge = edge[:edge.find("\nbuild ", 1) if edge.find("\nbuild ", 1) > 0 else len(edge)]
    libs = re.search(r"^\s+LINK_LIBRARIES = (.*)$", edge, re.M)
    if libs:
        for tok in shlex.split(libs.group(1)):
            if tok.endswith(".a"):
                inputs.append(tok)

    objs, archives = [], []
    for tok in inputs:
        path = tok if os.path.isabs(tok) else os.path.join(build_dir, tok)
        path = os.path.normpath(path)
        if tok.endswith(".o") and os.path.exists(path):
            objs.append(path)
        elif tok.endswith(".a") and os.path.exists(path):
            archives.append(path)
    return objs, archives


# ---------------------------------------------------------------------------
# 2/3. symbol tables
# ---------------------------------------------------------------------------

@dataclass
class Defn:
    where: str          # object path (or "archive(member)")
    sym_type: str       # nm letter
    size: int


@dataclass
class Sym:
    name: str
    stub: Defn
    reals: list[Defn] = field(default_factory=list)


def nm_defined(path: str) -> list[tuple[str, str, int]]:
    """[(name, type_letter, size)] for defined symbols in an object/archive."""
    out = run(["nm", "-S", "--defined-only", "--no-demangle", path])
    res = []
    cur_member = ""
    for line in out.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            cur_member = line[:-1]
            continue
        parts = line.split()
        if len(parts) == 3:              # addr type name
            _, t, name = parts
            size = 0
        elif len(parts) == 4:            # addr size type name
            _, sz, t, name = parts
            try:
                size = int(sz, 16)
            except ValueError:
                size = 0
        else:
            continue
        # Local symbols (lowercase nm letters) cannot participate in cross-TU
        # shadowing at all — `.L.str`, static functions, etc.  Skipping them
        # removes ~99% of the raw symbol volume and all of the false pairs.
        if t.islower():
            continue
        res.append((name, t, size, cur_member))
    return res


def collect(build_dir: str, target: str, verbose: bool):
    objs, archives = link_inputs(build_dir, target)
    stub_objs = [o for o in objs if o.endswith(STUB_OBJ_HINT)]
    if not stub_objs:
        die(f"stub object ({STUB_OBJ_HINT}) is not in {target}'s link line")

    syms: dict[str, Sym] = {}
    for so in stub_objs:
        for name, t, size, _ in nm_defined(so):
            if t in "Uun":
                continue
            syms[name] = Sym(name, Defn(so, t, size))

    if verbose:
        print(f"  link inputs: {len(objs)} objects, {len(archives)} archives",
              file=sys.stderr)
        print(f"  stub object defines {len(syms)} symbols", file=sys.stderr)

    for path in objs + archives:
        if path in stub_objs:
            continue
        for name, t, size, member in nm_defined(path):
            s = syms.get(name)
            if s is None or t in "Uun":
                continue
            where = f"{path}({member})" if member else path
            s.reals.append(Defn(where, t, size))
    return syms, objs, archives


# ---------------------------------------------------------------------------
# 4. which body did the linker actually pick?
# ---------------------------------------------------------------------------

STUB_MARKERS = ("gStubTraceEnabled", "StubTraceHit", "_ZN3dc313StubTraceHitEPKc",
                "_ZN3dc317gStubTraceEnabledE")


def binary_symbols(binary: str) -> dict[str, tuple[int, int, str]]:
    """name -> (addr, size, type) for defined symbols in the linked binary."""
    out = run(["nm", "-S", "--defined-only", "--no-demangle", binary])
    res = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            addr, size, t, name = parts
            try:
                res[name] = (int(addr, 16), int(size, 16), t)
            except ValueError:
                pass
        elif len(parts) == 3:
            addr, t, name = parts
            try:
                res[name] = (int(addr, 16), 0, t)
            except ValueError:
                pass
    return res


def disasm(binary: str, addr: int, size: int) -> str:
    size = max(size, 16)
    return run(["objdump", "-d", "--no-show-raw-insn",
                f"--start-address=0x{addr:x}",
                f"--stop-address=0x{addr + size:x}", binary])


def looks_like_stub(text: str) -> bool:
    if any(m in text for m in STUB_MARKERS):
        return True
    # bare `xor %eax,%eax ; ret` (or `ret` alone) with nothing else
    body = [l.split("\t", 1)[-1].strip()
            for l in text.splitlines() if re.match(r"^\s+[0-9a-f]+:", l)]
    body = [b for b in body if b and not b.startswith("nop")]
    if not body:
        return False
    return all(re.match(r"^(xor\s+%eax,%eax|ret|retq|xor\s+%rax,%rax)$", b)
               for b in body) and len(body) <= 3


def verdict(binary: str, sym: Sym, bsyms) -> tuple[str, str]:
    """Returns (verdict, evidence)."""
    ent = bsyms.get(sym.name)
    if ent is None:
        return "ABSENT", "symbol not present in the linked binary"
    addr, size, t = ent
    if t in DATA_TYPES or sym.stub.sym_type in DATA_TYPES:
        # data symbol: decide by size
        if sym.stub.size and size == sym.stub.size and \
           all(d.size != size for d in sym.reals):
            return "SHADOWED", f"data size 0x{size:x} matches stub, not real defs"
        return "OK", f"data size 0x{size:x}"
    text = disasm(binary, addr, size)
    if looks_like_stub(text):
        head = "; ".join(
            l.split("\t", 1)[-1].strip()
            for l in text.splitlines() if re.match(r"^\s+[0-9a-f]+:", l))[:200]
        return "SHADOWED", f"0x{addr:x} size 0x{size:x}: {head}"
    return "OK", f"0x{addr:x} size 0x{size:x} does not match the stub fingerprint"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default="native/build")
    ap.add_argument("--target", default="dc3-native")
    ap.add_argument("--binary", default=None,
                    help="linked executable (default: <build-dir>/<target>)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="also list stub symbols with no competing definition")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    build_dir = os.path.abspath(args.build_dir)
    binary = args.binary or os.path.join(build_dir, args.target)
    if not os.path.exists(binary):
        die(f"{binary} not found — build {args.target} first")

    syms, objs, archives = collect(build_dir, args.target, args.verbose)
    bsyms = binary_symbols(binary)

    dup = {n: s for n, s in syms.items() if s.reals}
    report = {"target": args.target, "binary": binary,
              "n_link_objects": len(objs), "n_link_archives": len(archives),
              "n_stub_symbols": len(syms), "n_with_real_definition": len(dup),
              "shadowed": [], "ok": [], "absent": [], "solo": []}

    for name, s in sorted(dup.items()):
        v, ev = verdict(binary, s, bsyms)
        rec = {"symbol": name,
               "demangled": run(["c++filt", name]).strip() or name,
               "stub_type": s.stub.sym_type,
               "real_defs": [{"where": d.where, "type": d.sym_type,
                              "size": d.size} for d in s.reals],
               "evidence": ev}
        report[{"SHADOWED": "shadowed", "OK": "ok",
                "ABSENT": "absent"}[v]].append(rec)

    if args.all:
        for name, s in sorted(syms.items()):
            if not s.reals:
                report["solo"].append({"symbol": name,
                                       "demangled": run(["c++filt", name]).strip() or name,
                                       "stub_type": s.stub.sym_type})

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"link inputs      : {len(objs)} objects, {len(archives)} archives")
        print(f"stub symbols     : {len(syms)}")
        print(f"  also defined elsewhere : {len(dup)}")
        print(f"  SHADOWED (stub won)    : {len(report['shadowed'])}")
        print(f"  OK (real body won)     : {len(report['ok'])}")
        print(f"  not in binary          : {len(report['absent'])}")
        for rec in report["shadowed"]:
            print(f"\n!! SHADOWED {rec['demangled']}")
            print(f"   mangled : {rec['symbol']}")
            print(f"   evidence: {rec['evidence']}")
            for d in rec["real_defs"]:
                print(f"   real def: [{d['type']}] size 0x{d['size']:x} {d['where']}")
        if args.all and report["solo"]:
            print(f"\n-- {len(report['solo'])} stub symbols with no competing "
                  f"definition (informational)")
            for rec in report["solo"]:
                print(f"   {rec['demangled']}")

    return 1 if report["shadowed"] else 0


if __name__ == "__main__":
    sys.exit(main())
