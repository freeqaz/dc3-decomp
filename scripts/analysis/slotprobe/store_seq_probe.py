#!/usr/bin/env python3
"""Fast store-slot sequence probe. usage: s2_seq.py <unit-rel-no-ext> <symbol> <src-rel-path> [variant.py ...]
For each variant (python defining src2 from src), compile the TU with /FAs into the scratch dir, diff the scratch obj
against the target obj with objdiff-cli, and print the (member-offset -> frame-slot) store sequence vs the target's."""
import os, re, subprocess, sys, shutil
WT = os.environ.get("S2_WT") or os.getcwd()
unit, sym, srcrel = sys.argv[1:4]
variants = sys.argv[4:] or [None]
src_path = os.path.join(WT, srcrel)
backup = open(src_path).read()
tgt_obj = os.path.join(WT, f"build/373307D9/obj/{unit.split('/',1)[1]}.obj")
scratch_obj = f"/tmp/slotprobe_tu/{os.path.basename(unit)}.obj"
def seq(col_rows):
    out=[]; last='?'
    for ins in col_rows:
        m=re.match(r'(lfs|lwz|lbz|lhz)\s+\S+,\s*(0x[0-9a-f]+)\((r31|r30|r29|r28)\)', ins)
        if m: last=m.group(2)
        m=re.match(r'(stfs|stw|stb|sth|stfd)\s+\S+,\s*(0x[0-9a-f]+)\((?:r1|r31)\)', ins) if os.environ.get('S2_FP') else re.match(r'(stfs|stw|stb|sth|stfd)\s+\S+,\s*(0x[0-9a-f]+)\(r1\)', ins)
        if m: out.append((last, m.group(2))); last='?'
    return out
def run(variant):
    s = backup
    if variant:
        ns={"src": s}; exec(open(variant).read(), ns); s = ns["src2"]
    open(src_path,"w").write(s)
    try:
        r = subprocess.run(["python3",os.path.join(os.path.dirname(os.path.abspath(__file__)), "slot_table.py"), WT, unit, "__none__"], capture_output=True, text=True)
        if "NO LISTING" in r.stdout: return None, r.stdout[-800:]
        d = subprocess.run([WT+"/bin/objdiff-cli","diff","-1",tgt_obj,"-2",scratch_obj,"--include-instructions","--full-listing","-o","-","-f","markdown",sym], capture_output=True, text=True, cwd=WT)
    finally:
        open(src_path,"w").write(backup)
    t=[]; b=[]
    for line in d.stdout.splitlines():
        m = re.match(r'\|\s*\d+\s*\|\s*`?([^`|]*)`?\s*\|\s*`?([^`|]*)`?\s*\|', line)
        if m: t.append(m.group(1).strip()); b.append(m.group(2).strip())
    mm = re.search(r'\*\*Match\*\*: ([\d.]+)%', d.stdout)
    nd = sum(1 for x,y in zip(t,b) if x != y)
    return (seq(t), seq(b), (mm.group(1) if mm else '?') + f" rows-differing={nd}", d.stdout), None
for v in variants:
    res, err = run(v)
    if res is None: print(f"== {v}: COMPILE FAIL\n{err}"); continue
    t, b, pct, _ = res
    print(f"== {os.path.basename(v) if v else 'baseline'}: match {pct}%  ({len(t)} tgt stores, {len(b)} base stores)")
    line_t = " ".join(f"{m}:{s[2:]}" for m,s in t); line_b = " ".join(f"{m}:{s[2:]}" for m,s in b)
    diffs = [(i, t[i], b[i]) for i in range(min(len(t),len(b))) if t[i][1] != b[i][1]]
    print("   tgt:", line_t)
    print("   base:", line_b)
    print("   differing:", " ".join(f"[{i}]{a[0]}:{a[1][2:]}vs{c[1][2:]}" for i,a,c in diffs))
