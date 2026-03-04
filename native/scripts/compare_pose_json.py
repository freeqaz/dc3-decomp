#!/usr/bin/env python3
import argparse
import json
import math
import sys
from typing import Dict, Tuple


def load_json(path: str):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def flatten_matrix(m):
    if not isinstance(m, list) or len(m) != 3:
        raise ValueError("matrix must be 3x3 list")
    out = []
    for row in m:
        if not isinstance(row, list) or len(row) != 3:
            raise ValueError("matrix row must be length 3")
        out.extend(row)
    return out


def max_abs_diff(a, b) -> float:
    if len(a) != len(b):
        raise ValueError(f"vector length mismatch: {len(a)} vs {len(b)}")
    return max((abs(float(x) - float(y)) for x, y in zip(a, b)), default=0.0)


def bone_map(doc) -> Dict[str, dict]:
    bones = doc.get("bones", [])
    out = {}
    for b in bones:
        name = b.get("name")
        if not isinstance(name, str):
            raise ValueError("bone missing name")
        out[name] = b
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare two pose dump JSON files with float tolerances")
    ap.add_argument("golden")
    ap.add_argument("capture")
    ap.add_argument("--pos-tol", type=float, default=1e-3)
    ap.add_argument("--mat-tol", type=float, default=1e-3)
    ap.add_argument("--beat-tol", type=float, default=1e-4)
    ap.add_argument("--require-same-clip", action="store_true")
    args = ap.parse_args()

    g = load_json(args.golden)
    c = load_json(args.capture)

    if args.require_same_clip:
        if g.get("clip", "") != c.get("clip", ""):
            print(f"DIFF: clip mismatch golden='{g.get('clip','')}' capture='{c.get('clip','')}'")
            return 1

    if "beat" in g and "beat" in c:
        beat_diff = abs(float(g["beat"]) - float(c["beat"]))
        if beat_diff > args.beat_tol:
            print(f"DIFF: beat mismatch {g['beat']} vs {c['beat']} (diff={beat_diff:.6g})")
            return 1

    gm = bone_map(g)
    cm = bone_map(c)

    gnames = set(gm.keys())
    cnames = set(cm.keys())
    if gnames != cnames:
        only_g = sorted(gnames - cnames)
        only_c = sorted(cnames - gnames)
        print("DIFF: bone name sets differ")
        if only_g:
            print("  only in golden:", ", ".join(only_g[:8]))
        if only_c:
            print("  only in capture:", ", ".join(only_c[:8]))
        return 1

    max_pos = 0.0
    max_mat = 0.0
    worst_pos: Tuple[str, str] = ("", "")
    worst_mat: Tuple[str, str] = ("", "")

    for name in sorted(gnames):
        gb = gm[name]
        cb = cm[name]
        for space in ("local", "world"):
            gp = gb[space]["pos"]
            cp = cb[space]["pos"]
            gd = max_abs_diff(gp, cp)
            if gd > max_pos:
                max_pos = gd
                worst_pos = (name, space)

            gm_flat = flatten_matrix(gb[space]["m"])
            cm_flat = flatten_matrix(cb[space]["m"])
            md = max_abs_diff(gm_flat, cm_flat)
            if md > max_mat:
                max_mat = md
                worst_mat = (name, space)

    ok = (max_pos <= args.pos_tol) and (max_mat <= args.mat_tol)
    if ok:
        print(
            f"PASS: max_pos={max_pos:.6g} ({worst_pos[0]} {worst_pos[1]}), "
            f"max_mat={max_mat:.6g} ({worst_mat[0]} {worst_mat[1]})"
        )
        return 0

    print(
        f"DIFF: max_pos={max_pos:.6g} ({worst_pos[0]} {worst_pos[1]}) > {args.pos_tol:.6g} "
        f"or max_mat={max_mat:.6g} ({worst_mat[0]} {worst_mat[1]}) > {args.mat_tol:.6g}"
    )
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(2)
