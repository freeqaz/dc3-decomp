#!/usr/bin/env python3
"""Is a register-only diff a pure register PERMUTATION, or a hidden operand swap?

WHY THIS EXISTS
===============
The canonical ruler (`match_percent_normalized`) forgives register permutation,
so a row whose only differing typed arguments are registers costs nothing there.
That also forgives an operand SWAP: `fsubs f0, f13, f0` vs `fsubs f0, f0, f13`
is a register-only row, scores identically to a harmless rename, and computes a
different value.  Argument-register swaps at a call are the same shape.  Reading
the rows one by one cannot tell the two apart; following the values can.

WHAT IT DOES
============
Diffs each function with `--include-instructions` (the report's own ruler, via
`scripts/analysis/ruler.py`) and walks the 1:1-aligned target/base streams with
value numbering: every definition mints a value on each side and pairs them;
every USE must find the same pairing.  Parameters arrive pre-paired by register
name, so a parameter swap is caught too.  Commutative opcodes (add/or/fadds/
fmuls, and the two multiplicands of an fma -- see COMMUT; fsel is NOT
commutative) and indexed-address operands (`lwzx rD, rA, rB`) may pair crossed.  At each `bl`, argument registers holding values defined on both
sides must agree (CALLARG? -- a dead leftover beyond the callee's arity also
lands here, so check the callee's signature).  Branch destinations are compared
function-relative (BRANCHDEST: a jump-threading / tail-merge choice, or a real
control-flow difference -- read the listing).

LIMITS (it is a lead generator, not a prover)
=============================================
  * Linear scan in address order: at a join the fall-through state is carried,
    so a flag on an instruction that is a branch target is labelled JOIN? and
    may be spurious.  Two independent moves emitted in the opposite order
    (`mr r4,r7; mr r6,r8` vs `mr r6,r8; mr r4,r31`) also flag; both are
    adjudicated by reading the listing.
  * It does not model memory: a value stored and reloaded is a fresh pair.
  * It assumes the streams align 1:1 (canonical-100 rows do).  Insert/delete
    rows are reported as UNALIGNED and the rest of the walk is best-effort.
  * Relocation NAMES are not its business -- see reloc_name_gate.py.  Rows here
    are counted by class (REG / SYM / BRANCHDEST / OTHER) so a name row is not
    silently mixed into a "register-only" verdict.

USAGE
=====
    python3 scripts/analysis/regperm_equivalence.py --project . \
        --unit default/system/rnddx9/Cam '?ProjectZ@DxCam@@UAAIM@Z'
    python3 scripts/analysis/regperm_equivalence.py --project . --list funcs.tsv
        (funcs.tsv: `<unit>\t<symbol>` per line)
    python3 scripts/analysis/regperm_equivalence.py --selftest

`--selftest` runs in-memory fixtures and REQUIRES the planted operand swap,
parameter swap and call-argument swap to be flagged and the pure rename and
commutative reorder to pass; it exits 1 if any control misbehaves.

Written by lane p2-reloc (2026-11-14) to adjudicate the 94 canonical-100 /
fuzzy<100 rows: 73 were pure register permutation, 0 were operand swaps.
"""
import argparse
import collections
import json
import subprocess
import sys
from pathlib import Path

COMMUT = {"add", "add.", "fadds", "fadd", "fmuls", "fmul", "mullw", "and", "or", "xor",
          "nand", "nor", "eqv", "vaddfp", "vmulfp", "vand", "vor", "vxor", "vmaxfp", "vminfp",
          "addc", "adde", "mulhw", "mulhwu", "and.", "or.", "xor.", "mullw.", "fmadd", "fmadds",
          "fmsubs", "fmsub", "fnmsubs", "fnmadds", "fnmsub", "fnmadd", "vmaddfp", "vnmsubfp",
          "vmulfp128", "vaddfp128", "vmaddfp128", "vmaddcfp128", "vnmsubfp128", "vand128", "vor128",
          "vxor128", "vmaxfp128", "vminfp128"}
# fma-family entries above: only the first two sources (the multiplicands) swap,
# which is exactly what check() tries.
VOLATILE = {f"r{i}" for i in [0] + list(range(3, 13))} | {f"f{i}" for i in range(0, 14)} | \
    {f"cr{i}" for i in (0, 1, 5, 6, 7)} | {"ctr"} | {f"v{i}" for i in range(0, 20)}
ARGREGS = [f"r{i}" for i in range(3, 11)] + [f"f{i}" for i in range(1, 14)]


def is_store(op):
    return op.startswith("st") or op.startswith("dcb") or op in ("stvx", "stvx128", "stvewx", "stvewx128")


def is_cmp(op):
    return op.startswith("cmp") or op.startswith("fcmp") or op.startswith("vcmp") and False


def regs(args):
    return [a["value"] for a in args if a.get("type") == "Register"]


class Side:
    def __init__(self, tag):
        self.tag = tag
        self.st = {}
        self.n = 0

    def get(self, r):
        if r not in self.st:
            self.st[r] = f"{self.tag}:undef:{r}:{self.n}"
            self.n += 1
        return self.st[r]

    def define(self, r, idx):
        v = f"{self.tag}:{idx}:{r}"
        self.st[r] = v
        return v


def check(rec):
    T, B = Side("T"), Side("B")
    fwd, bwd = {}, {}
    # entry: pre-pair every register by name (parameters arrive in the same regs)
    flags = []
    for r_ in [f"r{i}" for i in range(32)] + [f"f{i}" for i in range(32)] + [f"v{i}" for i in range(128)]:
        T.st[r_] = f"T:init:{r_}"; B.st[r_] = f"B:init:{r_}"

    def pair(tv, bv):
        if tv.startswith("T:init:") and bv.startswith("B:init:") and tv[7:] != bv[7:]:
            return False
        if tv in fwd:
            return fwd[tv] == bv
        if bv in bwd:
            return bwd[bv] == tv
        fwd[tv] = bv
        bwd[bv] = tv
        return True

    def ival(v):
        return int(v, 0) if isinstance(v, str) else int(v)
    t0 = b0 = None
    for ins in rec["instructions"]:
        if t0 is None and ins.get("target"):
            t0 = ival(ins["target"]["address"])
        if b0 is None and ins.get("base"):
            b0 = ival(ins["base"]["address"])
    branch_targets = set()
    for ins in rec["instructions"]:
        x = ins.get("target") or {}
        for a in x.get("typed_args", []) or []:
            if a.get("type") == "BranchDest":
                branch_targets.add(ival(a["value"]))

    for ins in rec["instructions"]:
        t = ins.get("target") or {}
        b = ins.get("base") or {}
        if not t or not b:
            flags.append(("UNALIGNED", ins.get("index"), ins.get("match_type")))
            continue
        op = t["opcode"]
        if op != b["opcode"]:
            flags.append(("OPCODE", ins["index"], t["opcode"], b["opcode"]))
            continue
        ta, ba = t.get("typed_args") or [], b.get("typed_args") or []
        for x, y in zip(ta, ba):
            if x.get("type") == "BranchDest":
                if ival(x["value"]) - t0 != ival(y["value"]) - b0:
                    flags.append(("BRANCHDEST", ins["index"], t["args"], b["args"],
                                  hex(ival(x["value"]) - t0), hex(ival(y["value"]) - b0)))
        tr, br = regs(ta), regs(ba)
        if len(tr) != len(br):
            flags.append(("NREG", ins["index"], t["args"], b["args"]))
            continue
        try:
            addr = int(t["address"], 16)
        except Exception:
            addr = None
        at_join = addr in branch_targets
        # determine dests / sources
        dests, srcs = [], list(range(len(tr)))
        if op in ("bl",):
            # call: check argument registers (warning-level)
            for r_ in ARGREGS:
                if r_ in T.st and r_ in B.st:
                    tv, bv = T.st[r_], B.st[r_]
                    if fwd.get(tv) != bv and not (tv not in fwd and bv not in bwd):
                        flags.append(("CALLARG?", ins["index"], r_, t["args"]))
            for r_ in VOLATILE:
                T.st.pop(r_, None)
                B.st.pop(r_, None)
            pair(T.define("r3", ins["index"]), B.define("r3", ins["index"]))
            pair(T.define("f1", ins["index"]), B.define("f1", ins["index"]))
            continue
        if op.startswith("b") and op not in ("bl",):
            srcs = list(range(len(tr)))
        elif is_store(op) or op.startswith("mt") or op in ("tw", "twi", "tdi", "td"):
            srcs = list(range(len(tr)))
        elif op.startswith("cmp") or op.startswith("fcmp"):
            if tr and tr[0].startswith("cr"):
                dests, srcs = [0], list(range(1, len(tr)))
            else:
                srcs = list(range(len(tr)))
                dests = []
        elif op.startswith("vcmp"):
            dests, srcs = [0], list(range(1, len(tr)))
        else:
            if tr:
                dests, srcs = [0], list(range(1, len(tr)))
            # update forms write the base register too
            if (op.endswith("u") or op.endswith("ux")) and op[0] == "l" and len(tr) >= 2:
                dests.append(len(tr) - 1)
        if op in ("stwu", "stdu", "stfsu", "stfdu") and len(tr) >= 2:
            dests = [len(tr) - 1]
        # check sources
        tv = [T.get(tr[i]) for i in srcs]
        bv = [B.get(br[i]) for i in srcs]
        ok = True
        # snapshot pairing for rollback attempts
        trial = list(zip(tv, bv))

        def try_pairs(pairs):
            saved_f, saved_b = dict(fwd), dict(bwd)
            for a_, b_ in pairs:
                if not pair(a_, b_):
                    fwd.clear(); fwd.update(saved_f); bwd.clear(); bwd.update(saved_b)
                    return False
            return True

        if not try_pairs(trial):
            ok = False
            if op in COMMUT and len(trial) >= 2:
                sw = [trial[0], trial[1]]
                # swap first two sources' base sides
                alt = [(trial[0][0], trial[1][1]), (trial[1][0], trial[0][1])] + trial[2:]
                if try_pairs(alt):
                    ok = True
            idx_mem = op.endswith("x") and (op[0] in "ls") and not op.endswith("ux") and op not in ("extsw", "extsh", "extsb")
            if not ok and idx_mem and len(trial) >= 2:
                alt = trial[:-2] + [(trial[-2][0], trial[-1][1]), (trial[-1][0], trial[-2][1])]
                if try_pairs(alt):
                    ok = True
        if not ok:
            flags.append(("JOIN?" if at_join else "SRC", ins["index"], t["address"], op, t["args"], "|", b["args"]))
        # crs implicitly
        for i in dests:
            pair(T.define(tr[i], ins["index"]), B.define(br[i], ins["index"]))
        if op.endswith(".") or op in ("andi.", "andis."):
            pair(T.define("cr0", ins["index"]), B.define("cr0", ins["index"]))
        if op.startswith("cmp") and not (tr and tr[0].startswith("cr")):
            pair(T.define("cr0", ins["index"]), B.define("cr0", ins["index"]))
    return flags




def row_classes(rec):
    """Count non-equal rows by what differs: REG / SYM / BRANCHDEST / OTHER."""
    c = collections.Counter()
    for i in rec.get("instructions", []) or []:
        mt = i.get("match_type")
        if mt == "equal":
            continue
        if mt != "diff_arg":
            c["OTHER"] += 1
            continue
        kinds = set()
        for x, y in zip((i.get("target") or {}).get("typed_args", []) or [],
                        (i.get("base") or {}).get("typed_args", []) or []):
            if x.get("value") != y.get("value") or x.get("type") != y.get("type"):
                kinds.add(x.get("type"))
        if "Symbol" in kinds:
            c["SYM"] += 1
        elif "BranchDest" in kinds:
            c["BRANCHDEST"] += 1
        elif kinds and kinds <= {"Register", "Opaque"}:
            c["REG"] += 1
        else:
            c["OTHER"] += 1
    return c


def diff_records(proj, by_unit):
    sys.path.insert(0, str(proj))
    from scripts.analysis import ruler as ruler_mod
    rk = ruler_mod.graded_ruler(proj)
    cli = str(proj / "bin/objdiff-cli")
    out = {}
    for uname, syms in by_unit.items():
        cmd = [cli, "diff", "-p", str(proj), "-u", uname, "--batch", "-f", "json", "-o", "-",
               "--include-instructions"] + rk.args
        r = subprocess.run(cmd, capture_output=True, text=True, input="\n".join(syms) + "\n")
        txt = r.stdout.strip()
        try:
            j = json.loads(txt) if txt else []
            recs = j if isinstance(j, list) else [j]
        except json.JSONDecodeError:
            recs = [json.loads(x) for x in txt.splitlines() if x.strip()]
        for rec in recs:
            if rec.get("symbol") in syms:
                out[(uname, rec["symbol"])] = rec
    return out


def _ins(idx, addr, op, args):
    typed = []
    for a in args:
        if isinstance(a, int):
            typed.append({"type": "Signed", "value": a})
        else:
            typed.append({"type": "Register", "value": a})
    return {"index": idx, "address": hex(addr), "opcode": op,
            "args": ", ".join(map(str, args)), "typed_args": typed}


def _fixture(base_rows):
    tgt = [("lfs", ["f0", 0, "r3"]), ("lfs", ["f31", 4, "r3"]),
           ("fadds", ["f12", "f0", "f31"]), ("fsubs", ["f1", "f0", "f31"]),
           ("mr", ["r4", "r5"]), ("bl", []), ("blr", [])]
    rec = {"instructions": []}
    for n, ((top, targs), (bop, bargs)) in enumerate(zip(tgt, base_rows)):
        rec["instructions"].append({
            "index": n, "match_type": "equal" if (top, targs) == (bop, bargs) else "diff_arg",
            "target": _ins(n, 0x100 + 4 * n, top, targs),
            "base": _ins(n, 0x800 + 4 * n, bop, bargs)})
    return rec


def selftest():
    rename = [("lfs", ["f31", 0, "r3"]), ("lfs", ["f0", 4, "r3"]),
              ("fadds", ["f12", "f31", "f0"]), ("fsubs", ["f1", "f31", "f0"]),
              ("mr", ["r4", "r5"]), ("bl", []), ("blr", [])]
    cases = {
        "pure rename (must pass)": (rename, False),
        "commutative fadds reorder (must pass)":
            ([*rename[:2], ("fadds", ["f12", "f0", "f31"]), *rename[3:]], False),
        "operand swap in fsubs (must flag)":
            ([*rename[:3], ("fsubs", ["f1", "f0", "f31"]), *rename[4:]], True),
        "parameter swap r3->r4 (must flag)":
            ([("lfs", ["f31", 0, "r4"]), *rename[1:]], True),
        "call argument from wrong param (must flag)":
            ([*rename[:4], ("mr", ["r4", "r6"]), *rename[5:]], True),
    }
    bad = 0
    for name, (rows, want_flag) in cases.items():
        flags = check(_fixture(rows))
        got = bool(flags)
        ok = got == want_flag
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {len(flags)} flag(s) {flags[:2]}")
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--project", default=".")
    ap.add_argument("--unit", help="unit name for the positional symbols")
    ap.add_argument("--list", help="TSV of `<unit>\\t<symbol>` lines")
    ap.add_argument("--json-out")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("symbols", nargs="*")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    funcs = []
    if a.list:
        funcs += [tuple(l.rstrip("\n").split("\t")[:2]) for l in open(a.list) if l.strip()]
    if a.symbols:
        if not a.unit:
            ap.error("positional symbols need --unit")
        funcs += [(a.unit, s) for s in a.symbols]
    if not funcs:
        ap.error("nothing to check (give --list, or --unit and symbols)")
    by_unit = collections.defaultdict(list)
    for u, s in funcs:
        by_unit[u].append(s)
    recs = diff_records(Path(a.project).resolve(), by_unit)
    tally, dump = collections.Counter(), {}
    for u, s in funcs:
        rec = recs.get((u, s))
        if rec is None:
            print(f"NO-RECORD   {s}")
            tally["NO-RECORD"] += 1
            continue
        flags = check(rec)
        rc = row_classes(rec)
        verdict = "EQUIVALENT" if not flags else "FLAGGED"
        tally[verdict] += 1
        print(f"{verdict:11s} {s}  fuzzy={rec.get('fuzzy_match_percent')} rows={dict(rc)}")
        for f in flags[:12]:
            print("            ", *f)
        dump[s] = {"unit": u, "verdict": verdict, "rows": dict(rc),
                   "fuzzy": rec.get("fuzzy_match_percent"), "flags": [list(map(str, f)) for f in flags]}
    print(f"\n{len(funcs)} requested: " + ", ".join(f"{k} {v}" for k, v in sorted(tally.items())))
    if a.json_out:
        json.dump(dump, open(a.json_out, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
