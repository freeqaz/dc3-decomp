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

Usage:
    python3 scripts/analysis/data_float_labels.py            # summary + table
    python3 scripts/analysis/data_float_labels.py --json     # machine readable
    python3 scripts/analysis/data_float_labels.py --all-sections
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
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
# `lfs f0, lbl_82F44758@l(r24)`  /  `lfd f1, lbl_...@l(r3)`
FLOAT_LOAD = re.compile(r"\b(lf[sd]u?)\s+(f\d+),\s*(lbl_[0-9A-Fa-f]+)@l\(")
# Any @ha/@l/@h reference to a lbl_, for the "referenced at all" denominator.
ANY_LBL_REF = re.compile(r"\b(lbl_[0-9A-Fa-f]+)@(?:ha|l|h)\b")

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
            if d in (".float", ".double")
        }

    @property
    def is_pure_float(self):
        return bool(self.slots) and all(
            d in (".float", ".double") for d, _ in self.slots.values()
        )


@dataclass
class Site:
    fn: str
    file: str
    line: int
    insn: str
    label: str


def parse_file(path: str, rel: str):
    """Return (blobs, sites, fn_ref_labels)."""
    blobs: list[Blob] = []
    sites: list[Site] = []
    fn_refs: dict[str, set] = defaultdict(set)

    cur_hdr = None
    cur_blob = None
    cur_fn = None
    off = 0

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
                continue
            if FN_END.match(stripped):
                cur_fn = None
                continue

            if cur_fn and "lbl_" in line:
                for m in ANY_LBL_REF.finditer(line):
                    fn_refs[cur_fn].add(m.group(1))
                m = FLOAT_LOAD.search(line)
                if m:
                    sites.append(
                        Site(cur_fn, rel, lineno, m.group(1), m.group(3))
                    )

    return blobs, sites, fn_refs


def collect(asm_root: str):
    blobs_by_addr: dict[int, Blob] = {}
    blobs_by_name: dict[str, Blob] = {}
    all_blobs: list[Blob] = []
    sites: list[Site] = []
    fn_refs: dict[str, set] = defaultdict(set)
    fn_file: dict[str, str] = {}

    for dirpath, _dirs, files in os.walk(asm_root):
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
                fn_file.setdefault(k, rel)
            for site in s:
                fn_file.setdefault(site.fn, rel)

    return all_blobs, blobs_by_name, sites, fn_refs, fn_file


def is_xdk(rel: str) -> bool:
    return rel.startswith("xdk/") or rel.startswith("auto_")


def label_value(blobs_by_name, label: str):
    """Resolve a lbl_ reference to (section, directive, value) if it is a float.

    A load through `lbl_X@l` names the exact symbol; dtk emits one `.obj` per
    label, so the offset is always 0.  If the label is not its own `.obj`, fall
    back to an address lookup inside the enclosing blob.
    """
    blob = blobs_by_name.get(label)
    if blob is None:
        return None
    slot = blob.slots.get(0)
    if slot is None:
        return None
    d, v = slot
    if d not in (".float", ".double"):
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

    # float-load sites through lbl_ labels
    sel_sections = None if args.all_sections else {".data"}
    resolved = []
    unresolved = []
    for s in sites:
        r = label_value(by_name, s.label)
        if r is None:
            unresolved.append(s)
            continue
        blob, d, v = r
        if sel_sections and blob.section not in sel_sections:
            continue
        if not args.include_xdk and is_xdk(s.file):
            continue
        resolved.append((s, blob, d, v))

    # group by function
    per_fn = defaultdict(list)
    for s, blob, d, v in resolved:
        per_fn[s.fn].append((s, blob, d, v))

    referenced_labels = {blob.name for _s, blob, _d, _v in resolved}

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
            "float_load_sites_through_lbl_total": len(sites),
            "float_load_sites_selected": len(resolved),
            "float_load_sites_label_not_a_float": len(unresolved),
            "functions_with_selected_sites": len(per_fn),
            "distinct_labels_referenced": len(referenced_labels),
            "data_nonxdk_labels_never_float_loaded": len(
                [b for b in data_lbl_nonxdk if b.name not in referenced_labels]
            ),
        },
        "functions": [],
    }

    for fn in sorted(per_fn, key=lambda f: -len(per_fn[f])):
        entries = per_fn[fn]
        vals = {}
        for s, blob, d, v in entries:
            vals.setdefault(blob.name, {"addr": f"0x{blob.addr:08X}",
                                        "section": blob.section,
                                        "directive": d,
                                        "value": v,
                                        "loads": 0,
                                        "lines": []})
            vals[blob.name]["loads"] += 1
            vals[blob.name]["lines"].append(s.line)
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
                f"     {name:20} {info['addr']}  {info['directive']:8} {info['value']:<22}"
                f" x{info['loads']}  asm lines {info['lines'][:6]}"
            )
        print()


if __name__ == "__main__":
    main()
