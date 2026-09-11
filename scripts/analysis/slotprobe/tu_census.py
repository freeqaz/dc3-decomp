#!/usr/bin/env python3
"""Compile TUs with the exact ninja command line + /FAs, then report, per function,
which named stack locals SHARE a frame offset (our build), cross-referenced with
report.json so a 100%-matched function with a shared slot proves the TARGET shares too.

usage: tu_census.py <worktree> <unit-rel-path-without-ext> ...
"""
import json, os, re, subprocess, sys

wt = sys.argv[1]
units = sys.argv[2:]
outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tu")
os.makedirs(outdir, exist_ok=True)

rep = json.load(open(os.path.join(wt, "build/373307D9/report.json")))
pct = {}
for u in rep["units"]:
    for f in u.get("functions", []):
        pct[f["name"]] = (f.get("match_percent_normalized", 0.0), int(f["size"]))

SLOT = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\$([0-9]+) = (-?\d+)\s*;\s*size = (\d+)")
PROC = re.compile(r"^(\S+)\s+PROC\s+NEAR")

for unit in units:
    base = os.path.basename(unit)
    obj = f"build/373307D9/{unit}.obj"
    cmd = subprocess.run(["ninja", "-t", "commands", obj], cwd=wt, capture_output=True, text=True).stdout
    cl = [l for l in cmd.splitlines() if "cl.exe" in l][-1]
    asm = f"{outdir}/{base}.asm"
    cl2 = cl.replace("/showIncludes", f"/FAs /Fa{asm}")
    cl2 = re.sub(r"/Fo\S+", f"/Fo{outdir}/{base}.obj", cl2)
    cl2 = re.sub(r"&& \S*python\S* \S*obj_build_metadata_patcher\.py.*$", "", cl2)
    r = subprocess.run(cl2, shell=True, cwd=wt, capture_output=True, text=True)
    if not os.path.exists(asm):
        print(f"## {unit}: NO LISTING\n{r.stdout[-500:]}\n{r.stderr[-500:]}")
        continue
    # parse: slot lines precede each PROC
    pending = []
    funcs = []  # (name, slots)
    for line in open(asm, errors="ignore"):
        m = SLOT.match(line)
        if m:
            pending.append((m.group(1), int(m.group(3)), int(m.group(4))))
            continue
        m = PROC.match(line)
        if m:
            funcs.append((m.group(1), pending))
            pending = []
    print(f"## {unit}: {len(funcs)} PROCs")
    for name, slots in funcs:
        if not slots:
            continue
        byoff = {}
        for v, off, sz in slots:
            byoff.setdefault(off, []).append((v, sz))
        shared = {off: vs for off, vs in byoff.items() if len(vs) > 1}
        p, sz = pct.get(name, (None, None))
        tag = "100%" if p is not None and p >= 100.0 else (f"{p:.1f}%" if p is not None else "n/a")
        if shared:
            desc = "; ".join(f"0x{off:x}: " + ",".join(f"{v}[{s}]" for v, s in vs) for off, vs in sorted(shared.items()))
            print(f"  SHARED  {tag:>6} {name}  {desc}")
        else:
            print(f"  distinct {tag:>6} {name}  ({len(slots)} named slots)")
