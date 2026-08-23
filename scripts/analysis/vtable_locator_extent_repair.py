#!/usr/bin/env python3
"""Derive (and optionally apply) the ??_R4 locator-tail extent repair for
config/<version>/symbols.txt.

THE DEFECT
----------
MSVC stores a class's RTTI complete-object-locator POINTER at `vtable[-1]`, i.e.
in the 4 bytes immediately BELOW the address of `??_7X@@6B@`.  dtk's recorded
extent for whatever data symbol sits just below a vtable used to run all the way
up to that vtable's address, so it swallowed the locator word that belongs to
the vtable, not to it.

Worked example (verified against build/<v>/asm/system/synth/WavReader.s and
orig/<v>/ham_xbox_r.map):

    ??_7StreamReader@@6B@ = .rdata:0x82263F0C; size:0x1C   <- 0x18 is correct
    ??_7WavReader@@6B@    = .rdata:0x82263F28

    0x82263F24 holds a relocation to ??_R4WavReader@@6B@ -- that word is
    ??_7WavReader@@6B@[-1], not StreamReader's 7th slot.

THE RULE (the only rule this script applies)
--------------------------------------------
Shrink a data symbol's extent by 4 iff BOTH hold:
  * the LAST word of its extent is a relocation to `??_R4Y`, and
  * `??_7Y` begins at exactly `addr + size`.
Anything else is left alone -- in particular this is NOT a blanket "subtract 4",
which would corrupt the under-sized cases and the genuine length divergences.

EVIDENCE SOURCES
----------------
  addresses / sizes : config/<v>/symbols.txt, asserted equal to
                      orig/<v>/ham_xbox_r.map for every ??_7 symbol first.
  word contents     : the dtk-generated .s listings under build/<v>/asm.
                      NB the `# .rdata:0x... | 0xADDR | size:` header ADDRESS
                      column in those listings is NOT reliable (it is 0x3C low
                      for system/gesture/SkeletonViz), so blocks are matched to
                      symbols BY NAME and the address always comes from
                      symbols.txt.
  Our own decompiled build is deliberately NOT an input to the rule; using it
  would launder source bugs into the target baseline.

Every write is guarded: a line must still carry the exact size the plan was
derived against or the run aborts having written nothing.  `--selftest` watches
that guard both succeed and FAIL on deliberately-broken input.

usage:
  vtable_locator_extent_repair.py --report
  vtable_locator_extent_repair.py --apply [--kind vtables|others|all]
  vtable_locator_extent_repair.py --selftest
"""
import argparse
import os
import re
import sys
import tempfile
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
VERSION = "373307D9"
SYMS = os.path.join(ROOT, "config", VERSION, "symbols.txt")
ASM = os.path.join(ROOT, "build", VERSION, "asm")
MAP = os.path.join(ROOT, "orig", VERSION, "ham_xbox_r.map")

SYM_RE = re.compile(
    r'^\s*(?P<name>"[^"]+"|[A-Za-z_$@?][^\s=]*)\s*=\s*'
    r'(?P<sect>[.\w]+):(?P<addr>0x[0-9A-Fa-f]+);(?P<rest>.*)$')
SIZE_RE = re.compile(r'size:(0x[0-9A-Fa-f]+)')
OBJ_START = re.compile(r'^\.obj\s+(?P<name>"[^"]+"|[^,]+),')
WORD_SYM = re.compile(r'^\s*\.4byte\s+(?P<v>"[^"]+"|[A-Za-z_$@?][\w$@?]*)\s*$')


def load_symbols(path=SYMS):
    ents = []
    for i, ln in enumerate(open(path, encoding="utf-8", errors="replace")):
        m = SYM_RE.match(ln.rstrip("\n"))
        if not m:
            continue
        sm = SIZE_RE.search(m.group("rest"))
        ents.append((int(m.group("addr"), 16), m.group("name").strip('"'),
                     int(sm.group(1), 16) if sm else None, i))
    return ents


def load_map(path=MAP):
    out = {}
    for ln in open(path, encoding="utf-8", errors="replace"):
        p = ln.split()
        if len(p) >= 3 and ":" in p[0]:
            try:
                out.setdefault(p[1], int(p[2], 16))
            except ValueError:
                pass
    return out


def scan_asm_lastwords(asm=ASM):
    """symbol name -> the last symbolic `.4byte` of its .obj block (or None)."""
    out = {}
    for dp, _, fs in os.walk(asm):
        for f in fs:
            if not f.endswith(".s"):
                continue
            cur, body = None, []
            for ln in open(os.path.join(dp, f), encoding="utf-8", errors="replace"):
                o = OBJ_START.match(ln)
                if o:
                    cur, body = o.group("name").strip().strip('"'), []
                elif ln.startswith(".endobj"):
                    if cur is not None:
                        out[cur] = body[-1] if body else None
                    cur = None
                elif cur is not None:
                    w = WORD_SYM.match(ln)
                    body.append(w.group("v").strip('"') if w else None)
    return out


def derive():
    """-> (vtable_edits, other_edits, skipped); an edit is {name, old, new}."""
    ents = load_symbols()
    mapaddr = load_map()
    lastw = scan_asm_lastwords()

    bad = [(n, a, mapaddr[n]) for (a, n, s, i) in ents
           if n.startswith("??_7") and n in mapaddr and mapaddr[n] != a]
    if bad:
        raise AssertionError(
            f"symbols.txt disagrees with ham_xbox_r.map on {len(bad)} vtable "
            f"addresses; refusing to derive extents from it: {bad[:3]}")

    vt_at = {a: n for (a, n, s, i) in ents if n.startswith("??_7")}
    vt, others, skipped = [], [], []
    for (addr, name, size, i) in ents:
        if size is None:
            continue
        lw = lastw.get(name)
        if not lw or not lw.startswith("??_R4"):
            continue
        nxt = vt_at.get(addr + size)
        if not nxt or lw != "??_R4" + nxt[4:]:
            continue
        if size < 8:
            skipped.append((name, size, "shrinking would leave a 0-byte symbol"))
            continue
        e = {"name": name, "old": size, "new": size - 4}
        if name.startswith("??_7"):
            vt.append(e)
        elif name.startswith("lbl_"):
            # placeholder labels have no counterpart symbol on the decomp side,
            # so trimming them is pure target-object churn with no diff surface.
            skipped.append((name, size, "lbl_ placeholder, no diff surface"))
        else:
            others.append(e)
    return vt, others, skipped


def apply_edits(path, edits, dry=False):
    lines = open(path, encoding="utf-8").read().split("\n")
    index = {}
    for i, ln in enumerate(lines):
        eq = ln.find(" = ")
        if eq > 0:
            index.setdefault(ln[:eq], i)
    problems, staged = [], []
    for e in edits:
        i = index.get(e["name"])
        if i is None:
            problems.append(f"symbol not found: {e['name']}")
            continue
        want = "size:0x%X" % e["old"]
        if want not in lines[i]:
            problems.append(f"{e['name']}: expected {want}, line is: {lines[i].strip()}")
            continue
        staged.append((i, lines[i].replace(want, "size:0x%X" % e["new"], 1)))
    if problems:
        raise AssertionError(f"{len(problems)} guard failure(s), NOTHING written. "
                             "First 5:\n  " + "\n  ".join(problems[:5]))
    if not dry:
        for i, new in staged:
            lines[i] = new
        open(path, "w", encoding="utf-8").write("\n".join(lines))
    return len(staged)


def selftest():
    src = ("??_7Foo@@6B@ = .rdata:0x1000; // type:object size:0x10 scope:global\n"
           "??_7Bar@@6B@ = .rdata:0x1010; // type:object size:0x20 scope:global\n")
    fd, p = tempfile.mkstemp()
    os.write(fd, src.encode())
    os.close(fd)
    assert apply_edits(p, [{"name": "??_7Foo@@6B@", "old": 0x10, "new": 0xC}]) == 1
    assert "size:0xC scope" in open(p).read()
    print("selftest PASS  : a correct edit is applied")
    before = open(p).read()
    rc = 0
    for label, bad in (("stale expected size",
                        {"name": "??_7Bar@@6B@", "old": 0x99, "new": 0x95}),
                       ("unknown symbol",
                        {"name": "??_7Nope@@6B@", "old": 0x4, "new": 0x0})):
        try:
            apply_edits(p, [bad])
        except AssertionError:
            assert open(p).read() == before, "FILE MUTATED DESPITE THE ABORT"
            print(f"selftest PASS  : {label} aborts, file untouched")
        else:
            print(f"selftest FAIL  : {label} did not trip the guard")
            rc = 1
    os.unlink(p)
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--kind", choices=["vtables", "others", "all"], default="vtables")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    vt, others, skipped = derive()
    print(f"vtable (??_7) extents to shrink by 4 : {len(vt)}")
    print(f"other data owners of a locator word  : {len(others)}")
    kinds = defaultdict(int)
    for s in skipped:
        kinds[s[2]] += 1
    for k, v in sorted(kinds.items(), key=lambda x: -x[1]):
        print(f"skipped {v:5d}  {k}")
    if a.apply:
        sel = {"vtables": vt, "others": others, "all": vt + others}[a.kind]
        print(f"applied {apply_edits(SYMS, sel)} extent edits ({a.kind})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
