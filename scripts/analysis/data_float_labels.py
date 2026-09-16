#!/usr/bin/env python3
"""Find mutable float statics in the target and the functions that load them.

A float the target loads from a `.data` label (dtk names these `lbl_8xxxxxxx`)
rather than from a `__real@<hex>` COMDAT is a **mutable** file-scope or
function-local `static float`.  MSVC puts genuine immutable literals in
`.rdata` as `__real@...` COMDATs; anything that ends up in `.data` under a
`lbl_` name was written by an initialiser into writable storage, which means
the source declared it `static float x = <value>;`.

Both the existence AND the value of such a constant are things our decomp
guessed, and a constant that already compiles is a constant nobody audits.
Three confirmed behavioural defects were found this way:

  HamMaster::CheckLevels     lbl_82F0F1C0  .float 40  (we had 96)
  EaseElasticIn                            scaled by period, not amplitude
  ArcDetector::UpdateOverlay lbl_82F44758  .float 0.1 (we had no fade at all)

This script is the systematic version of that hunt.  It reports DENOMINATORS,
not just hits: a sweep that reports only hits is not believable.

THE TARGET ALSO MATERIALISES BASE REGISTERS  (fixed 2026-09-16)
---------------------------------------------------------------
This parser used to recognise exactly ONE addressing mode, the folded form

    lfs f0, lbl_82F44758@l(r24)

which is one relocation per load site.  But the image frequently materialises
the address into a GPR first and then reads the group through displacements:

    lis  r11, lbl_82F16D28@ha
    addi r25, r11, lbl_82F16D28@l     <-- the reference; NOT a load
    ...
    addi r26, r25, 0x4                <-- a base derived from a base
    lfs  f31, -0x4(r26)               <-- the actual read, NO relocation at all

Every one of those reads was structurally invisible: the label is referenced by
an `addi`, and the sibling slots at +4/+8/+12 carry no relocation to find.  This
is the SAME defect that was fixed on the our-side walker in `1b75dc677`, in the
same week, for the same reason -- see `our_float_statics` in
`mutable_float_audit.py`, which is the reference implementation this walk is
ported from.

Measured whole-binary at the time of the fix: **29** `addi`-materialised
references reach a `.data` float label, covering **17** distinct labels, of
which **9** (5 non-XDK) are reachable by NO folded load and so were invisible
to this file entirely.  `lbl_82F16D28` -- ten floats, referenced from
`?BlurSurface@RndSoftParticleBuffer@@AAAXXZ` -- is the worked example.

WHAT THE WALK DELIBERATELY DOES NOT DO
--------------------------------------
* It never guesses.  A base register is dropped on a call (volatiles) and on
  any instruction that writes it, so a stale base can never be attributed to
  the wrong static.  Over-clearing loses a site; mis-attribution invents a bug.
* It follows LOADS only.  `stfs f26, 0x4(r25)` tells you the slot exists, not
  what constant is behind it, so stores are counted and not turned into sites.

Usage:
    python3 scripts/analysis/data_float_labels.py            # summary + table
    python3 scripts/analysis/data_float_labels.py --json     # machine readable
    python3 scripts/analysis/data_float_labels.py --all-sections
"""
from __future__ import annotations

import argparse
import bisect
import json
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field

ASM_ROOT_DEFAULT = "build/373307D9/asm"

# `# .data:0x5C | 0x82F44758 | size: 0x4`
OBJ_HDR = re.compile(
    r"^#\s+(\.\w+):0x[0-9A-Fa-f]+\s+\|\s+0x([0-9A-Fa-f]+)\s+\|\s+size:\s+0x([0-9A-Fa-f]+)\s*$"
)
OBJ_START = re.compile(r'^\.obj\s+("?)(.+?)\1(?:,\s*\w+)?\s*$')
OBJ_END = re.compile(r"^\.endobj\b")
FN_START = re.compile(r'^\.fn\s+("?)(.+?)\1(?:,\s*\w+)?\s*$')
FN_END = re.compile(r"^\.endfn\b")
# Any @ha/@l/@h reference to a lbl_, for the "referenced at all" denominator.
ANY_LBL_REF = re.compile(r"\b(lbl_[0-9A-Fa-f]+)@(?:ha|l|h)\b")

# --------------------------------------------------------------------------- #
# Instruction forms in dtk's listings.
#
#   /* 82732FA0 007279A0  3B 2B 6D 28 */\taddi r25, r11, lbl_82F16D28@l
#
# dtk QUOTES any symbol containing a character that would not survive as a bare
# token (`"__real@3f000000"`, `"?sFoo@@3MA"`), so the bare alternative never has
# to contain an `@` -- which is what makes a non-greedy match safe here.
# --------------------------------------------------------------------------- #
INSN = re.compile(r"^/\*[^*]*\*/\s*(\S+)\s*(.*?)\s*$")
_SYM = r'(?:"([^"]*)"|([A-Za-z_$.][\w$.]*))'
#: `addi rD, rA, sym@l` / `ori rD, rA, sym@l` -- materialise a symbol address.
LO_MATERIALISE = re.compile(r"^r(\d+),\s*r(\d+),\s*" + _SYM + r"@l$")
#: `addi rD, rA, 0x2c` -- a base DERIVED from a base (the `addi r26, r25, 0x4`
#: in BlurSurface).  Without this the reads through r26 are lost.
IMM_ADD = re.compile(r"^r(\d+),\s*r(\d+),\s*(-?(?:0x[0-9A-Fa-f]+|\d+))$")
#: `lfs f0, sym@l(rX)` -- the folded form, one relocation per load.
LOAD_SYM = re.compile(r"^f\d+,\s*" + _SYM + r"@l\(r(\d+)\)$")
#: `lfs f31, -0x4(r26)` -- a displaced read off a materialised base.
LOAD_DISP = re.compile(r"^f\d+,\s*(-?(?:0x[0-9A-Fa-f]+|\d+))\(r(\d+)\)$")
#: first operand is a GPR -- used to invalidate a base the instruction clobbers.
FIRST_GPR = re.compile(r"^r(\d+)\b")

FLOAT_LOADS = ("lfs", "lfd", "lfsu", "lfdu")
#: Mnemonic prefixes whose FIRST operand is not a destination GPR.  A store's
#: first operand is the SOURCE, a compare's is a CR field, a branch's is a
#: target -- clobbering on those would drop live bases for no reason.
NO_GPR_DEST = ("st", "b", "cmp", "tw", "td", "mt", "dcb", "icbi", "sync",
               "eieio", "trap")
#: The mnemonics dtk emits that actually LINK (write LR) -- i.e. CALLS.
#: This has to be an exact set, not a `startswith("bl")` test: dtk's `b*`
#: vocabulary over the whole asm tree is `bl` 233,601 / `beq` 99,977 / `b`
#: 86,495 / `bne` 75,319 / `blr` 45,132 / `blt` 24,890 / `bctrl` 15,310 /
#: `ble` 14,105 / ... , so a prefix test treats `ble`, `blt`, `blr`, `blelr`
#: and `bltlr` as calls and drops every materialised base at each one.  Those
#: are conditional branches and returns; none of them clobbers a volatile.
CALL_MNEMONICS = frozenset(("bl", "bla", "bctrl", "blrl", "bclrl", "bcctrl"))
#: r0 and r3-r12 are volatile across a call on the Xenon ABI.
VOLATILE_GPRS = {0} | set(range(3, 13))

#: Retained for the legacy `--json` shape and for callers that only want the
#: folded form; the walk below supersedes it.
FLOAT_LOAD = re.compile(r"\b(lf[sd]u?)\s+(f\d+),\s*(lbl_[0-9A-Fa-f]+)@l\(")

# Width in bytes of each data directive, for computing sub-offsets inside a blob.
DIRECTIVE_WIDTH = {
    ".byte": 1,
    ".2byte": 2,
    ".short": 2,
    ".4byte": 4,
    ".long": 4,
    ".8byte": 8,
    ".quad": 8,
    ".float": 4,
    ".double": 8,
}

FLOAT_DIRECTIVES = (".float", ".double")


def _imm(text: str) -> int:
    """Parse a dtk immediate: `0x2c`, `-0x4`, `12`, `-12`."""
    t = text.strip()
    neg = t.startswith("-")
    if neg:
        t = t[1:]
    v = int(t, 16) if t.lower().startswith("0x") else int(t, 10)
    return -v if neg else v


@dataclass
class Blob:
    name: str
    section: str
    addr: int
    size: int
    file: str
    line: int
    # sub-offset -> (directive, textual value)
    slots: dict = field(default_factory=dict)

    @property
    def floats(self):
        return {
            off: val
            for off, (d, val) in self.slots.items()
            if d in FLOAT_DIRECTIVES
        }

    @property
    def is_pure_float(self):
        return bool(self.slots) and all(
            d in FLOAT_DIRECTIVES for d, _ in self.slots.values()
        )


@dataclass
class Site:
    """One float READ, resolved to the byte it reads.

    `label`/`sub_off` are what the instruction stream said: a symbol name and a
    byte offset from it.  `blob_name`/`blob_off`/`value` are filled in by
    `collect()` once every blob in the binary is known, because a materialised
    base can read PAST the end of the symbol it was formed from and land in the
    next blob (dtk ends a blob at the next NAMED symbol, so a run of unnamed
    siblings folds into one blob and a named one starts a new one).
    """
    fn: str
    file: str
    line: int
    insn: str
    label: str
    sub_off: int = 0
    via: str = "folded"          # "folded" | "materialised"
    blob_name: str | None = None
    blob_off: int = 0
    addr: int = 0
    directive: str | None = None
    value: str | None = None


def parse_file(path: str, rel: str):
    """Return (blobs, sites, fn_ref_labels).

    Sites are UNRESOLVED here -- they carry (symbol, byte offset).  Resolution
    needs the whole-binary blob index and happens in `collect()`.
    """
    blobs: list[Blob] = []
    sites: list[Site] = []
    fn_refs: dict[str, set] = defaultdict(set)

    cur_hdr = None
    cur_blob = None
    cur_fn = None
    off = 0
    # GPR -> (symbol name, byte offset from that symbol)
    base: dict[int, tuple[str, int]] = {}

    with open(path, errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()

            m = OBJ_HDR.match(stripped)
            if m:
                cur_hdr = (m.group(1), int(m.group(2), 16), int(m.group(3), 16))
                continue

            if cur_blob is None and stripped.startswith(".obj "):
                m = OBJ_START.match(stripped)
                if m:
                    sec, addr, size = cur_hdr or ("?", 0, 0)
                    cur_blob = Blob(m.group(2), sec, addr, size, rel, lineno)
                    off = 0
                    continue

            if cur_blob is not None:
                if OBJ_END.match(stripped):
                    blobs.append(cur_blob)
                    cur_blob = None
                    cur_hdr = None
                    continue
                parts = stripped.split(None, 1)
                if parts and parts[0] in DIRECTIVE_WIDTH:
                    d = parts[0]
                    val = parts[1] if len(parts) > 1 else ""
                    cur_blob.slots[off] = (d, val)
                    off += DIRECTIVE_WIDTH[d]
                elif stripped.startswith(".skip") or stripped.startswith(".space"):
                    try:
                        off += int(stripped.split()[1], 0)
                    except (IndexError, ValueError):
                        pass
                continue

            if stripped.startswith(".fn "):
                m = FN_START.match(stripped)
                if m:
                    cur_fn = m.group(2)
                base = {}
                continue
            if FN_END.match(stripped):
                cur_fn = None
                base = {}
                continue

            if not cur_fn:
                continue

            for m in ANY_LBL_REF.finditer(line):
                fn_refs[cur_fn].add(m.group(1))

            mi = INSN.match(stripped)
            if not mi:
                continue
            mnem, ops = mi.group(1), mi.group(2)

            # -- a float READ ------------------------------------------------
            if mnem in FLOAT_LOADS:
                m = LOAD_SYM.match(ops)
                if m:
                    sym = m.group(1) if m.group(1) is not None else m.group(2)
                    sites.append(Site(cur_fn, rel, lineno, mnem, sym, 0, "folded"))
                    continue
                m = LOAD_DISP.match(ops)
                if m:
                    disp, ra = _imm(m.group(1)), int(m.group(2))
                    b = base.get(ra)
                    if b is not None:
                        sites.append(Site(cur_fn, rel, lineno, mnem,
                                          b[0], b[1] + disp, "materialised"))
                continue

            # -- materialise a symbol address into a GPR ---------------------
            if mnem in ("addi", "ori"):
                m = LO_MATERIALISE.match(ops)
                if m:
                    rd = int(m.group(1))
                    sym = m.group(3) if m.group(3) is not None else m.group(4)
                    base[rd] = (sym, 0)
                    continue
                m = IMM_ADD.match(ops)
                if m:
                    rd, ra, imm = int(m.group(1)), int(m.group(2)), _imm(m.group(3))
                    b = base.get(ra)
                    # r1 is the stack pointer: a frame offset is not a static.
                    if b is not None and ra != 1 and rd != 1:
                        base[rd] = (b[0], b[1] + imm)
                    else:
                        base.pop(rd, None)
                    continue

            # -- invalidate, conservatively ----------------------------------
            if mnem in CALL_MNEMONICS:
                for g in VOLATILE_GPRS:
                    base.pop(g, None)
                continue
            if mnem.startswith(NO_GPR_DEST):
                continue
            m = FIRST_GPR.match(ops)
            if m:
                base.pop(int(m.group(1)), None)

    return blobs, sites, fn_refs


class BlobIndex:
    """Address -> the blob covering it.  dtk emits blobs in address order."""

    def __init__(self, blobs):
        self._items = sorted(
            ((b.addr, b) for b in blobs if b.addr), key=lambda kv: kv[0]
        )
        self._keys = [a for a, _b in self._items]

    def at(self, addr: int):
        if not self._keys:
            return None
        i = bisect.bisect_right(self._keys, addr) - 1
        if i < 0:
            return None
        a, b = self._items[i]
        if addr < a or addr >= a + max(b.size, 1):
            return None
        return b, addr - a


def collect(asm_root: str):
    blobs_by_addr: dict[int, Blob] = {}
    blobs_by_name: dict[str, Blob] = {}
    all_blobs: list[Blob] = []
    sites: list[Site] = []
    fn_refs: dict[str, set] = defaultdict(set)
    fn_file: dict[str, str] = {}

    # sorted(): os.walk yields DIRECTORIES in arbitrary order, and the dicts
    # below are last-write-wins.  Filenames were already sorted; the directory
    # level was not, which is the scope_index_census defect with one level of
    # indirection.  Measured 2026-09-16: 0 blob names occur twice, so this is
    # LATENT rather than live -- which is the right time to pin it.
    for dirpath, dirs, files in os.walk(asm_root):
        dirs.sort()
        for fn in sorted(files):
            if not fn.endswith(".s"):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, asm_root)
            b, s, r = parse_file(path, rel)
            all_blobs.extend(b)
            for blob in b:
                blobs_by_addr[blob.addr] = blob
                blobs_by_name[blob.name] = blob
            sites.extend(s)
            for k, v in r.items():
                fn_refs[k] |= v
            for site in s:
                fn_file.setdefault(site.fn, rel)
    for k in sorted(fn_refs):
        fn_file.setdefault(k, "?")

    # -- resolve every site now that the whole binary is known ---------------
    index = BlobIndex(all_blobs)
    for s in sites:
        anchor = blobs_by_name.get(s.label)
        if anchor is None:
            continue
        addr = anchor.addr + s.sub_off
        hit = index.at(addr)
        if hit is None:
            continue
        blob, off = hit
        slot = blob.slots.get(off)
        if slot is None or slot[0] not in FLOAT_DIRECTIVES:
            continue
        s.blob_name, s.blob_off, s.addr = blob.name, off, addr
        s.directive, s.value = slot

    return all_blobs, blobs_by_name, sites, fn_refs, fn_file


def is_xdk(rel: str) -> bool:
    return rel.startswith("xdk/") or rel.startswith("auto_")


def label_value(blobs_by_name, label: str):
    """Resolve a lbl_ reference to (blob, directive, value) if it is a float.

    Kept for callers that only have a NAME.  Prefer a resolved `Site`, which
    carries the sub-offset a materialised base may have added.
    """
    blob = blobs_by_name.get(label)
    if blob is None:
        return None
    slot = blob.slots.get(0)
    if slot is None:
        return None
    d, v = slot
    if d not in FLOAT_DIRECTIVES:
        return None
    return blob, d, v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--asm-root", default=ASM_ROOT_DEFAULT)
    ap.add_argument("--json", action="store_true")
    ap.add_argument(
        "--all-sections",
        action="store_true",
        help="include .rdata/.bss labels (default: .data only, the mutable ones)",
    )
    ap.add_argument("--include-xdk", action="store_true")
    args = ap.parse_args()

    all_blobs, by_name, sites, fn_refs, fn_file = collect(args.asm_root)

    # ---- denominators -------------------------------------------------
    float_blobs = [b for b in all_blobs if b.floats]
    by_section = defaultdict(list)
    for b in float_blobs:
        by_section[b.section].append(b)

    lbl_float_blobs = [b for b in float_blobs if b.name.startswith("lbl_")]
    data_lbl = [b for b in lbl_float_blobs if b.section == ".data"]
    data_lbl_nonxdk = [b for b in data_lbl if not is_xdk(b.file)]

    sel_sections = None if args.all_sections else {".data"}
    resolved = []
    unresolved = []
    for s in sites:
        if s.blob_name is None:
            unresolved.append(s)
            continue
        blob = by_name[s.blob_name]
        if sel_sections and blob.section not in sel_sections:
            continue
        if not args.include_xdk and is_xdk(s.file):
            continue
        resolved.append((s, blob, s.directive, s.value))

    per_fn = defaultdict(list)
    for s, blob, d, v in resolved:
        per_fn[s.fn].append((s, blob, d, v))

    referenced_labels = {blob.name for _s, blob, _d, _v in resolved}
    n_materialised = sum(1 for s, _b, _d, _v in resolved if s.via == "materialised")

    out = {
        "denominators": {
            "asm_files": sum(
                1
                for dp, _d, fs in os.walk(args.asm_root)
                for f in fs
                if f.endswith(".s")
            ),
            "obj_blobs_total": len(all_blobs),
            "blobs_containing_floats": len(float_blobs),
            "float_blobs_by_section": {k: len(v) for k, v in sorted(by_section.items())},
            "lbl_float_blobs": len(lbl_float_blobs),
            "lbl_float_blobs_data_section": len(data_lbl),
            "lbl_float_blobs_data_section_nonxdk": len(data_lbl_nonxdk),
            "float_load_sites_found_total": len(sites),
            "float_load_sites_selected": len(resolved),
            "float_load_sites_via_FOLDED_reloc": len(resolved) - n_materialised,
            "float_load_sites_via_MATERIALISED_base": n_materialised,
            "float_load_sites_unresolvable": len(unresolved),
            "functions_with_selected_sites": len(per_fn),
            "distinct_labels_referenced": len(referenced_labels),
            "data_nonxdk_labels_never_float_loaded": len(
                [b for b in data_lbl_nonxdk if b.name not in referenced_labels]
            ),
        },
        "functions": [],
    }

    for fn in sorted(per_fn, key=lambda f: (-len(per_fn[f]), f)):
        entries = per_fn[fn]
        vals = {}
        for s, blob, d, v in entries:
            key = f"{blob.name}+0x{s.blob_off:x}" if s.blob_off else blob.name
            vals.setdefault(key, {"addr": f"0x{s.addr:08X}",
                                  "section": blob.section,
                                  "directive": d,
                                  "value": v,
                                  "via": s.via,
                                  "loads": 0,
                                  "lines": []})
            vals[key]["loads"] += 1
            vals[key]["lines"].append(s.line)
        out["functions"].append(
            {
                "fn": fn,
                "file": fn_file.get(fn, "?"),
                "n_loads": len(entries),
                "labels": vals,
            }
        )

    if args.json:
        json.dump(out, sys.stdout, indent=2)
        print()
        return

    d = out["denominators"]
    print("== DENOMINATORS ==")
    for k, v in d.items():
        print(f"  {k:52} {v}")
    print()
    print("== FUNCTIONS LOADING A MUTABLE (.data) FLOAT STATIC ==")
    print(f"   ({len(out['functions'])} functions, non-XDK)")
    print()
    for f in out["functions"]:
        print(f"-- {f['fn']}")
        print(f"   {f['file']}   ({f['n_loads']} float loads)")
        for name, info in sorted(f["labels"].items(), key=lambda kv: kv[1]["addr"]):
            print(
                f"     {name:24} {info['addr']}  {info['directive']:8} {info['value']:<18}"
                f" x{info['loads']} {info['via']:12} asm lines {info['lines'][:6]}"
            )
        print()


if __name__ == "__main__":
    main()
