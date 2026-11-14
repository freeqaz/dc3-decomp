"""Expand xenia ucode disassembly (ALU + tfetch) into per-component
assignments with swizzles resolved, so the maths can be read directly.

  python3 ucode_expand.py shader.ps.txt [literal floats...]

Semantics (Xenos):
  vector op dst.mask, a.swz, b.swz[, c.swz]   -- per written component
  scalar co-issue: '+ op dst.mask, src.swz'   -- one result, broadcast to the mask
    xxx_prev uses ps = the previous instruction's scalar result
    maxs/muls/adds/subs a.b two components of one source
    mulsc/addsc/subsc c, r: constant op register
  every instruction reads its sources before it writes.
"""
import re
import sys

COMP = "xyzw"
lit = [float(x) for x in sys.argv[2:]]
LIT = {}
for i, v in enumerate(lit):
    LIT[f"c{252 + i // 4}.{COMP[i % 4]}"] = v


def fmt(v):
    return ("%g" % v)


def src(tok, comp_index):
    """tok like -r14.wyzz / c252.yyyy / r_abs[1].w; return string for component."""
    neg = tok.startswith("-")
    if neg:
        tok = tok[1:]
    absv = False
    m = re.match(r"r_abs\[(\d+)\]\.(\w+)$", tok)
    if m:
        reg, swz = "r" + m.group(1), m.group(2)
        absv = True
    else:
        reg, _, swz = tok.partition(".")
        if not swz:
            swz = "xyzw"
    swz = (swz + swz[-1] * 4)[:4]
    c = swz[comp_index]
    name = f"{reg}.{c}"
    if name in LIT:
        s = fmt(LIT[name] * (-1 if neg else 1))
        return s
    s = name
    if absv:
        s = f"|{s}|"
    if neg:
        s = "-" + s
    return s


def mask_comps(dst):
    reg, _, m = dst.partition(".")
    if not m:
        m = "xyzw"
    return reg, [i for i, ch in enumerate(m[:4]) if ch != "_"]


ps = "ps?"
n = 0
for line in open(sys.argv[1]):
    line = line.rstrip()
    m = re.match(r"/\*\s*([\d.]+)\s*\*/\s+(.*)$", line)
    cont = re.match(r"\s+\+\s+(.*)$", line)
    if m:
        body = m.group(2).strip()
        if body.startswith(("exec", "alloc", "cnop", "exece")):
            continue
        n = m.group(1)
        prev_ps = ps
        ps_set = False
        out = []
    elif cont:
        body = cont.group(1).strip()
    else:
        continue
    parts = body.split(None, 1)
    op, args = parts[0], parts[1] if len(parts) > 1 else ""
    toks = [t.strip() for t in args.split(",")]
    sat = op.endswith("_sat")
    base = op[:-4] if sat else op

    def S(e):
        return f"sat({e})" if sat else e

    if base.startswith("tfetch"):
        reg, comps = mask_comps(toks[0])
        swz = toks[0].partition(".")[2] or "xyzw"
        for i in comps:
            ch = swz[i]
            print(f"[{n}] {reg}.{COMP[i]} = {toks[2]}({toks[1]}).{ch}")
        continue
    if cont is None:
        # vector op
        reg, comps = mask_comps(toks[0])
        a = toks[1:]
        for i in comps:
            if base in ("dp3", "dp4", "dp2add"):
                k = {"dp3": 3, "dp4": 4, "dp2add": 2}[base]
                terms = [f"{src(a[0], j)}*{src(a[1], j)}" for j in range(k)]
                e = " + ".join(terms)
                if base == "dp2add":
                    e += f" + {src(a[2], 0)}"
            elif base == "mul":
                e = f"{src(a[0], i)} * {src(a[1], i)}"
            elif base == "add":
                e = f"{src(a[0], i)} + {src(a[1], i)}"
            elif base == "mad":
                e = f"{src(a[0], i)} * {src(a[1], i)} + {src(a[2], i)}"
            elif base == "max":
                e = f"max({src(a[0], i)}, {src(a[1], i)})"
            elif base == "min":
                e = f"min({src(a[0], i)}, {src(a[1], i)})"
            elif base == "mov":
                e = f"{src(a[0], i)}"
            else:
                e = f"{base}({', '.join(src(t, i) for t in a)})"
            print(f"[{n}] {reg}.{COMP[i]} = {S(e)}")
    else:
        reg, comps = mask_comps(toks[0])
        a = toks[1:]
        if base.endswith("_prev"):
            b2 = base[:-5]
            sym = {"muls": "*", "adds": "+", "subs": "-"}.get(b2)
            e = f"{src(a[0], 0)} {sym} {prev_ps}" if sym else f"{b2}({src(a[0], 0)}, {prev_ps})"
        elif base in ("maxs", "mins", "muls", "adds", "subs"):
            x0, x1 = src(a[0], 0), src(a[0], 1)
            if base == "maxs":
                e = f"max({x0}, {x1})" if x0 != x1 else x0
            elif base == "mins":
                e = f"min({x0}, {x1})"
            else:
                e = f"{x0} {dict(muls='*', adds='+', subs='-')[base]} {x1}"
        elif base in ("mulsc", "addsc", "subsc"):
            x0, x1 = src(a[0], 0), src(a[1], 0)
            e = f"{x0} {dict(mulsc='*', addsc='+', subsc='-')[base]} {x1}"
        elif base in ("rsq", "sqrt", "log", "exp", "rcp", "frc", "floor"):
            e = f"{base}({src(a[0], 0)})"
        else:
            e = f"{base}({', '.join(src(t, 0) for t in a)})"
        e = S(e)
        label = f"ps{n}"
        if comps:
            for i in comps:
                print(f"[{n}]   {reg}.{COMP[i]} = {e}   (=: {label})")
        else:
            print(f"[{n}]   {label} = {e}")
        ps = label
