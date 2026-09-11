#!/usr/bin/env python3
"""Compile one TU with the exact ninja command + /FAs and print the full
name$NNNN = offset slot table for the named PROC(s).

usage: s2_slots.py <worktree> <unit-rel-path-without-ext> <proc-substring> [...]
"""
import os, re, subprocess, sys

wt = sys.argv[1]
unit = sys.argv[2]
wanted = sys.argv[3:]
outdir = "/tmp/slotprobe_tu"
os.makedirs(outdir, exist_ok=True)

SLOT = re.compile(r"^([A-Za-z_][A-Za-z0-9_$]*)\$([0-9]*) = (-?\d+)\s*;\s*size = (\d+)")
PROC = re.compile(r"^(\S+)\s+PROC\s+NEAR")

base = os.path.basename(unit)
obj = f"build/373307D9/{unit}.obj"
cmd = subprocess.run(["ninja", "-t", "commands", obj], cwd=wt, capture_output=True, text=True).stdout
cl = [l for l in cmd.splitlines() if "cl.exe" in l][-1]
asm = f"{outdir}/{base}.asm"
if os.path.exists(asm):
    os.remove(asm)
cl2 = cl.replace("/showIncludes", f"/FAs /Fa{asm}")
cl2 = re.sub(r"/Fo\S+", f"/Fo{outdir}/{base}.obj", cl2)
cl2 = re.sub(r"&& \S*python\S* \S*obj_build_metadata_patcher\.py.*$", "", cl2)
r = subprocess.run(cl2, shell=True, cwd=wt, capture_output=True, text=True)
if not os.path.exists(asm):
    print(f"## {unit}: NO LISTING\n{r.stdout[-800:]}\n{r.stderr[-800:]}")
    sys.exit(1)
pending = []
for line in open(asm, errors="ignore"):
    m = SLOT.match(line)
    if m:
        pending.append((m.group(1), int(m.group(3)), int(m.group(4))))
        continue
    m = PROC.match(line)
    if m:
        name = m.group(1)
        if any(w in name for w in wanted):
            print(f"## {name}")
            for v, off, sz in sorted(pending, key=lambda t: t[1]):
                print(f"  0x{off:x}  {v}[{sz}]")
        pending = []
