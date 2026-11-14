"""Execute a xenia-disassembled Xenos pixel shader (ALU + tfetch) for one pixel.

  emu = Ucode(path, literals16)   # literals: c252..c255 from the blob's table
  out = emu.run(regs={0: (u,v,0,0), 1: wpos, ...}, consts={0: (...), ...},
                tex={0: (r,g,b,a), 1: ...})     # tfetch returns tex[n] (coords ignored)
  out -> dict of exported regs ('oC0': np.array(4))

Semantics: every ALU instruction (vector op + co-issued scalar op) reads all
sources before writing; '_prev' scalar ops use the previous instruction's
scalar result ('ps'); two-component scalar ops (maxs/mins/muls/adds/subs) read
src.swz[0], src.swz[1]; log/exp are base 2.
"""
import math
import re

import numpy as np

C = "xyzw"


def _sat(v):
    return np.clip(v, 0.0, 1.0)


SCALAR_OPS = {"log", "exp", "rsq", "sqrt", "rcp", "maxs", "mins", "muls", "adds", "subs",
              "mulsc", "addsc", "subsc", "frcs", "floors"}


class Ucode:
    def __init__(self, path, literals):
        self.lit = {}
        nreg = len(literals) // 4
        for i in range(nreg):
            self.lit[256 - nreg + i] = np.array(literals[i * 4:i * 4 + 4], dtype=np.float64)
        self.ins = []   # list of (kind, vec(op, args) | None, sca(op,args) | None)
        cur = None
        for line in open(path):
            line = line.rstrip()
            m = re.match(r"/\*\s*([\d.]+)\s*\*/\s+(.*)$", line)
            cont = re.match(r"\s+\+\s+(.*)$", line)
            if m:
                body = m.group(2).strip()
                if body.split()[0] in ("exec", "exece", "alloc", "cnop", "cexec", "cexece"):
                    continue
                op, _, args = body.partition(" ")
                toks = [t.strip() for t in args.split(",")]
                base = op[:-4] if op.endswith("_sat") else op
                if op.startswith("tfetch"):
                    cur = ["tex", (op, toks), None]
                elif base in SCALAR_OPS or base.endswith("_prev"):
                    cur = ["alu", ("nop", []), (op, toks)]
                else:
                    cur = ["alu", (op, toks), None]
                self.ins.append(cur)
            elif cont and cur is not None:
                body = cont.group(1).strip()
                op, _, args = body.partition(" ")
                cur[2] = (op, [t.strip() for t in args.split(",")])

    def _read(self, tok):
        neg = tok.startswith("-")
        if neg:
            tok = tok[1:]
        absv = False
        m = re.match(r"r_abs\[(\d+)\]\.?(\w*)$", tok)
        if m:
            name, swz, absv = "r" + m.group(1), m.group(2), True
        else:
            name, _, swz = tok.partition(".")
        swz = swz or "xyzw"
        swz = (swz + swz[-1] * 4)[:4]
        if name.startswith("c"):
            n = int(name[1:])
            base = self.lit[n] if n in self.lit else self.c.get(n, np.zeros(4))
        else:
            base = self.r.setdefault(name, np.zeros(4))
        v = np.array([base[C.index(ch)] for ch in swz], dtype=np.float64)
        if absv:
            v = np.abs(v)
        if neg:
            v = -v
        return v

    def _write(self, dst, val, sat):
        name, _, mask = dst.partition(".")
        mask = mask or "xyzw"
        if sat:
            val = _sat(val)
        comps = [(i, float(val[i])) for i, ch in enumerate(mask[:4]) if ch != "_"]
        return name, comps

    def run(self, regs, consts, tex, trace=None):
        self.r = {f"r{k}": np.array(v, dtype=np.float64) for k, v in regs.items()}
        self.c = {k: np.array(v, dtype=np.float64) for k, v in consts.items()}
        ps = 0.0
        for kind, vec, sca in self.ins:
            if kind == "tex":
                op, toks = vec
                tf = int(toks[2][2:])
                t = np.array(tex[tf], dtype=np.float64)
                name, _, mask = toks[0].partition(".")
                tgt = self.r.setdefault(name, np.zeros(4)).copy()
                for i, ch in enumerate((mask or "xyzw")[:4]):
                    if ch != "_":
                        tgt[i] = t[C.index(ch)] if ch in C else (0.0 if ch == "0" else 1.0)
                self.r[name] = tgt
                if trace is not None:
                    trace.append({k: v.copy() for k, v in self.r.items()})
                continue
            writes = []
            op, toks = vec
            sat = op.endswith("_sat")
            b = op[:-4] if sat else op
            if b != "nop":
                a = [self._read(t) for t in toks[1:]]
                if b == "mul":
                    v = a[0] * a[1]
                elif b == "add":
                    v = a[0] + a[1]
                elif b == "mad":
                    v = a[0] * a[1] + a[2]
                elif b == "max":
                    v = np.maximum(a[0], a[1])
                elif b == "min":
                    v = np.minimum(a[0], a[1])
                elif b == "mov":
                    v = a[0]
                elif b == "dp3":
                    v = np.full(4, float(np.dot(a[0][:3], a[1][:3])))
                elif b == "dp4":
                    v = np.full(4, float(np.dot(a[0], a[1])))
                elif b == "dp2add":
                    v = np.full(4, float(np.dot(a[0][:2], a[1][:2]) + a[2][0]))
                elif b == "frc":
                    v = a[0] - np.floor(a[0])
                elif b == "floor":
                    v = np.floor(a[0])
                else:
                    raise ValueError("vector op " + op)
                writes.append(self._write(toks[0], v, sat))
            new_ps = ps
            if sca:
                op, toks = sca
                sat = op.endswith("_sat")
                b = op[:-4] if sat else op
                if b.endswith("_prev"):
                    x = self._read(toks[1])[0]
                    bb = b[:-5]
                    s = {"muls": x * ps, "adds": x + ps, "maxs": max(x, ps), "mins": min(x, ps)}[bb]
                elif b in ("maxs", "mins", "muls", "adds", "subs"):
                    x = self._read(toks[1])
                    s = {"maxs": max(x[0], x[1]), "mins": min(x[0], x[1]), "muls": x[0] * x[1],
                         "adds": x[0] + x[1], "subs": x[0] - x[1]}[b]
                elif b in ("mulsc", "addsc", "subsc"):
                    x, y = self._read(toks[1])[0], self._read(toks[2])[0]
                    s = {"mulsc": x * y, "addsc": x + y, "subsc": x - y}[b]
                else:
                    x = self._read(toks[1])[0]
                    if b == "rsq":
                        s = math.inf if x == 0 else 1.0 / math.sqrt(x)
                    elif b == "sqrt":
                        s = math.sqrt(x)
                    elif b == "log":
                        s = -math.inf if x == 0 else math.log2(x)
                    elif b == "exp":
                        s = 2.0 ** x
                    elif b == "rcp":
                        s = math.inf if x == 0 else 1.0 / x
                    elif b == "frcs":
                        s = x - math.floor(x)
                    else:
                        raise ValueError("scalar op " + op)
                if sat:
                    s = min(max(s, 0.0), 1.0)
                new_ps = s
                dst = toks[0]
                if dst.partition(".")[2].strip("_"):
                    writes.append(self._write(dst, np.full(4, s), False))
            for name, comps in writes:
                tgt = self.r.setdefault(name, np.zeros(4))
                for i, x in comps:
                    tgt[i] = x
            ps = new_ps
            if trace is not None:
                trace.append({k: v.copy() for k, v in self.r.items()})
        return {k: v for k, v in self.r.items() if k.startswith("o")}
