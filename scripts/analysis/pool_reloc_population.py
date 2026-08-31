#!/usr/bin/env python3
"""Re-derive the population of functions the pre-2026-08-31 per-function ruler scored LOW.

`objdiff-cli diff` used to run with `ppc.calculatePoolRelocations=true` (its own
schema default) while `report generate` ran with it false.  That knob
SYNTHESIZES relocations for pooled data loads out of each object's own symbol
table, and `reloc_eq` charges a target-only synthesized relocation under every
`functionRelocDiffs` mode except `none`.  So the per-function path -- which is
what `run_objdiff` and every lane's tooling reads -- charged rows on textually
identical instructions, and read LOW.

This script reproduces the OLD behaviour by flipping that single knob back on,
and reports every function whose score moves.  That set is the population whose
recorded verdicts were computed over an inflated mismatch row set.

It joins decomp.db so each row carries its recorded verdict, and it reports its
own denominator (universe / examined / every drop reason) rather than
presenting a filtered set as a total.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

OLD_DIFF_DEFAULT = "ppc.calculatePoolRelocations=true"


def find_report(repo: Path) -> Path:
    hits = sorted(repo.glob("build/*/report.json"))
    if not hits:
        raise SystemExit(f"no build/*/report.json under {repo} -- run ninja first")
    return hits[0]


def load_report(repo: Path):
    """name -> (unit, mpn, size, fuzzy).  Names defined in >1 unit are dropped."""
    path = find_report(repo)
    with path.open() as fh:
        data = json.load(fh)
    seen: dict[str, int] = {}
    rows: dict[str, tuple] = {}
    for unit in data["units"]:
        for fn in unit.get("functions", []):
            seen[fn["name"]] = seen.get(fn["name"], 0) + 1
            rows[fn["name"]] = (
                unit["name"],
                fn["match_percent_normalized"],
                int(fn.get("size", 0) or 0),   # report.json serializes u64 as a STRING
                fn.get("fuzzy_match_percent"),
            )
    dropped_multi = sum(1 for v in seen.values() if v > 1)
    return (
        {k: v for k, v in rows.items() if seen[k] == 1},
        {"report": str(path.relative_to(repo)), "dropped_multi_unit": dropped_multi},
    )


def batch_scores(repo: Path, symbols: list[str], extra_config: list[str],
                 cache: Path | None = None) -> dict:
    # A whole-binary batch is ~5 min.  Cache is keyed on the config AND on the
    # report's mtime, so it can never survive a rebuild -- a cache that outlives
    # the objects it describes is exactly the stale-measurement trap this whole
    # lane exists to clean up.
    if cache is not None and cache.is_file():
        blob = json.loads(cache.read_text())
        if (blob.get("config") == extra_config
                and blob.get("report_mtime") == find_report(repo).stat().st_mtime):
            return blob["rows"]
    cmd = [str(repo / "bin" / "objdiff-cli"), "diff", "--batch", "-p", str(repo), "-f", "json"]
    for item in extra_config:
        cmd += ["-c", item]
    proc = subprocess.run(
        cmd, input="\n".join(symbols) + "\n", capture_output=True, text=True, cwd=str(repo)
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-4000:])
        raise SystemExit(f"objdiff-cli diff --batch failed (exit {proc.returncode})")
    out = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if "error" in row:
            continue
        # Keep only the three fields this comparison needs.  The full batch rows
        # are ~83 MB of instruction tables per pass; caching them filled a tmpfs.
        out[row["symbol"]] = dict(
            unit=row.get("unit"),
            base_unit=row.get("base_unit"),
            canonical_match_percent=row.get("canonical_match_percent"),
        )
    if cache is not None:
        cache.write_text(json.dumps(dict(
            config=extra_config, report_mtime=find_report(repo).stat().st_mtime, rows=out)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--db", default="/home/free/code/milohax/dc3-decomp/decomp.db")
    ap.add_argument("--out", help="write JSON population here")
    ap.add_argument("--cache-dir", default=".lane/pool_reloc_cache",
                    help="where to memoize the two whole-binary batch passes "
                         "(NOT /tmp: this box's tmpfs runs at its quota)")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    rows, prov = load_report(repo)
    names = sorted(rows)

    print(f"# report: {prov['report']}")
    print(f"# universe: {len(names)} uniquely-named report functions "
          f"({prov['dropped_multi_unit']} names dropped: defined in >1 unit)")

    cdir = Path(args.cache_dir)
    cdir.mkdir(parents=True, exist_ok=True)
    now = batch_scores(repo, names, [], cdir / "now.json")
    old = batch_scores(repo, names, [OLD_DIFF_DEFAULT], cdir / "old.json")

    stats = dict(examined=0, agree_now=0, disagree_now=0, moved=0,
                 unpaired=0, unresolved=0, cross_unit=0)
    population = []
    now_disagree = []

    for name in names:
        unit, mpn, size, fuzzy = rows[name]
        rn, ro = now.get(name), old.get(name)
        if rn is None or ro is None or rn.get("unit") != unit:
            stats["unresolved"] += 1
            continue
        if rn.get("canonical_match_percent") is None:
            stats["unpaired"] += 1
            continue
        if rn.get("base_unit"):
            stats["cross_unit"] += 1
            continue
        stats["examined"] += 1
        got_now = rn["canonical_match_percent"]
        got_old = ro.get("canonical_match_percent")
        if abs(got_now - mpn) < 1e-4:
            stats["agree_now"] += 1
        else:
            stats["disagree_now"] += 1
            now_disagree.append((name, unit, mpn, got_now))
        if got_old is not None and abs(got_old - got_now) > 1e-4:
            stats["moved"] += 1
            population.append(dict(
                symbol=name, unit=unit, size=size,
                report_mpn=mpn, fuzzy=fuzzy,
                old_diff_pct=got_old, now_diff_pct=got_now,
                delta_pp=got_now - got_old,
                real_gap_bytes=(100.0 - mpn) / 100.0 * size,
            ))

    print("# " + " | ".join(f"{k} {v}" for k, v in stats.items()))
    if now_disagree:
        print(f"!! {len(now_disagree)} rows still disagree with report.json AS CONFIGURED "
              "-- the ruler pin is not holding:")
        for r in now_disagree[:10]:
            print(f"   report {r[2]:9.5f}  diff {r[3]:9.5f}  {r[1]}  {r[0]}")

    # join decomp.db
    con = sqlite3.connect(f"file:{args.db}?mode=ro", uri=True)
    syms = [p["symbol"] for p in population]
    vmap = {}
    for i in range(0, len(syms), 500):
        chunk = syms[i:i + 500]
        q = ",".join("?" * len(chunk))
        for s, v, vr, cp, dbmpn, fc in con.execute(
            f"SELECT symbol, verdict, verdict_reason, current_percent, "
            f"match_percent_normalized, floor_certificate FROM functions WHERE symbol IN ({q})",
            chunk,
        ):
            vmap[s] = dict(verdict=v, verdict_reason=vr, db_current_percent=cp,
                           db_mpn=dbmpn, floor_certificate=fc)
    con.close()
    for p in population:
        p.update(vmap.get(p["symbol"], dict(verdict=None, verdict_reason=None,
                                            db_current_percent=None, db_mpn=None,
                                            floor_certificate=None)))

    population.sort(key=lambda p: -p["real_gap_bytes"])
    total_bytes = sum(p["size"] for p in population)
    at100 = [p for p in population if p["report_mpn"] >= 100.0]
    print(f"\n## POPULATION: {len(population)} functions / {total_bytes} bytes moved by "
          f"{OLD_DIFF_DEFAULT}")
    print(f"   report higher on {sum(1 for p in population if p['delta_pp'] > 0)}, "
          f"diff higher on {sum(1 for p in population if p['delta_pp'] < 0)}")
    print(f"   report_mpn == 100.0 on {len(at100)} ({sum(p['size'] for p in at100)} B)")
    from collections import Counter
    print("   verdicts: " + ", ".join(f"{k}={v}" for k, v in
                                      Counter(p["verdict"] for p in population).most_common()))

    if args.out:
        Path(args.out).write_text(json.dumps(
            dict(stats=stats, provenance=prov, population=population), indent=2))
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
