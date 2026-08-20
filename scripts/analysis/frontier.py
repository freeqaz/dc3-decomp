#!/usr/bin/env python3
"""Re-derive the DC3 work frontier from ``report.json`` + ``decomp.db``.

This is the *query*, not a worklist.  Every hardcoded worklist this project has
written rotted within weeks, so what is committed here is the derivation; run it
to get today's answer.

Why it reads ``report.json`` and not the DB's percentages
--------------------------------------------------------
``build/373307D9/report.json``'s ``match_percent_normalized`` is the sharpest
surface available: unlike ``decomp.db.current_percent`` (a drifting
work-selection index that the ninja sync deliberately does not write) and unlike
``run_objdiff``'s rounded headline, it is an exact score-weighted f32.  The DB
is joined on top of it only to answer *"what has a human said about this row"* —
verdict, floor certificate, exclusion.

What the canonical ruler does and does not forgive
--------------------------------------------------
``match_percent_normalized == 100`` forgives register permutation and, by
construction, the entire relocation target name (relocation penalties fold into
``arg_diff_score``; objdiff-core ``src/diff/code.rs``).  It does *not* forgive
wrong constants, offsets or vtable slot values.  So "100" here means "no
non-register, non-relocation mismatch", never byte identity.

Every count this script prints states its denominator and every drop reason.

Usage
-----
    python3 scripts/analysis/frontier.py                       # all sections
    python3 scripts/analysis/frontier.py --section bands
    python3 scripts/analysis/frontier.py --section near-complete --max-remaining 2
    python3 scripts/analysis/frontier.py --section units --top 40
    python3 scripts/analysis/frontier.py --json

From a worktree, ``decomp.db`` is a deliberate tripwire -- pass the real one::

    python3 scripts/analysis/frontier.py \
        --db /home/free/code/milohax/dc3-decomp/decomp.db \
        --report /home/free/code/milohax/dc3-decomp/build/373307D9/report.json
"""
from __future__ import annotations

import argparse
import collections
import json
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from authorable import is_authorable  # type: ignore[import]
from progress_metrics import dedup_glue_shadows  # type: ignore[import]

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_REPORT = REPO_ROOT / "build" / "373307D9" / "report.json"
DEFAULT_DB = REPO_ROOT / "decomp.db"
GLUE_UNIT = "default/link_glue"

# Subsystems the native port does not run.  The user has said explicitly that
# curl, jpeg and Holmes are not important; zlib and Bink are vendor codecs.
# They are NOT dropped from any denominator here -- they are labelled, so a
# ranking can deprioritise them without a count quietly changing underneath it.
LOW_PRIORITY_MARKERS = ("/curl/", "/jpeg", "/holmes", "/zlib", "binkxenon",
                        "Holmes", "BinkMovie", "BinkIntegration")

BANDS = [
    ("[99.9,100)", lambda v: 99.9 <= v < 100.0),
    ("[99,99.9)", lambda v: 99.0 <= v < 99.9),
    ("[95,99)", lambda v: 95.0 <= v < 99.0),
    ("[90,95)", lambda v: 90.0 <= v < 95.0),
    ("[80,90)", lambda v: 80.0 <= v < 90.0),
    ("[50,80)", lambda v: 50.0 <= v < 80.0),
    ("(0,50)", lambda v: 0.0 < v < 50.0),
    ("0 (no body)", lambda v: v <= 0.0),
]


def main_checkout_db() -> Path:
    """Path to the REAL decomp.db, resolved through the main checkout.

    ``REPO_ROOT`` is the worktree when we are in one, and the worktree's
    ``decomp.db`` is the tripwire -- so an error message built from REPO_ROOT
    would tell the reader to pass the tripwire back in.  ``--git-common-dir``
    points at the main checkout's ``.git`` from anywhere.
    """
    import subprocess
    try:
        common = subprocess.run(
            ["git", "rev-parse", "--git-common-dir"], cwd=REPO_ROOT,
            capture_output=True, text=True, check=True).stdout.strip()
        return (REPO_ROOT / common).resolve().parent / "decomp.db"
    except Exception:
        return REPO_ROOT / "decomp.db"


def load_report(path: Path) -> tuple[list[dict], dict]:
    """Return (authorable function rows, coverage dict).

    Drops, each counted: SDK/vendor units (``default/xdk/*``,
    ``default/lib/binkxenon/*``) and the eleven ``default/link_glue`` rows that
    shadow a function already matched in its real unit.
    """
    with open(path) as fh:
        data = json.load(fh)
    shadows = dedup_glue_shadows(data)
    rows: list[dict] = []
    cov = {"universe": 0, "dropped_sdk_vendor": 0, "dropped_glue_shadow": 0}
    for unit in data.get("units", []):
        name = unit.get("name", "")
        fns = unit.get("functions") or []
        cov["universe"] += len(fns)
        if not is_authorable(name):
            cov["dropped_sdk_vendor"] += len(fns)
            continue
        for fn in fns:
            if name == GLUE_UNIT and fn.get("name", "") in shadows:
                cov["dropped_glue_shadow"] += 1
                continue
            rows.append({
                "unit": name,
                "symbol": fn.get("name", ""),
                "size": int(fn.get("size", 0)),
                "norm": float(fn.get("match_percent_normalized") or 0.0),
                # None (not 0.0) when objdiff emitted no score at all -- i.e. no
                # body was written.  Do not coerce; the distinction is the whole
                # point of the stub tier.
                "fuzzy": (None if fn.get("fuzzy_match_percent") is None
                          else float(fn["fuzzy_match_percent"])),
            })
    cov["authorable"] = len(rows)
    cov["report_path"] = str(path)
    cov["report_provenance"] = data.get("provenance", {})
    return rows, cov


def join_db(rows: list[dict], db_path: Path) -> dict:
    """Annotate rows in place with DB verdict/cert/excluded. Returns coverage."""
    if not db_path.exists():
        for r in rows:
            r["verdict"] = r["cert"] = r["excluded"] = None
        return {"db": None, "note": "no decomp.db -- DB columns are None"}
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        conn.execute("SELECT 1 FROM functions LIMIT 1")
    except sqlite3.DatabaseError as exc:
        # The worktree tripwire lands here. Fail LOUDLY: a silent None would
        # make every DB-derived count read as "nothing adjudicated", which is
        # indistinguishable from "this class is exhausted".
        raise SystemExit(
            f"{db_path} is not a usable decomp.db ({exc}).\n"
            f"In a worktree this is the deliberate tripwire. The real database "
            f"lives in the MAIN checkout, not here -- pass\n"
            f"  --db {main_checkout_db()}") from exc
    db = {(u, s): (v, fc, e, vr) for s, u, v, fc, e, vr in conn.execute(
        "SELECT symbol, unit, verdict, floor_certificate, excluded, verdict_reason "
        "FROM functions")}
    conn.close()
    missing = 0
    for r in rows:
        d = db.get((r["unit"], r["symbol"]))
        if d is None:
            missing += 1
            r["verdict"] = r["cert"] = r["excluded"] = r["reason"] = None
            r["db_row"] = False
        else:
            r["verdict"], r["cert"], r["excluded"], r["reason"] = d
            r["db_row"] = True
    return {"db": str(db_path), "db_rows": len(db), "report_rows_without_db_row": missing}


def low_priority(unit: str, symbol: str) -> bool:
    hay = unit + "|" + symbol
    return any(m in hay for m in LOW_PRIORITY_MARKERS)


# ---------------------------------------------------------------------------
# Sections
# ---------------------------------------------------------------------------

def section_bands(rows, out):
    rem = [r for r in rows if r["norm"] < 100.0]
    out(f"\n== Bands (denominator: {len(rows)} authorable functions; "
        f"{len(rows) - len(rem)} at norm==100, {len(rem)} remaining) ==")
    verdicts = ["AT_LIMIT", "None", "excluded", "no-DB-row"]

    def vkey(r):
        if not r.get("db_row"):
            return "no-DB-row"
        if r.get("excluded"):
            return "excluded"
        return str(r["verdict"])

    out(f"{'band':14}{'fns':>7}{'bytes':>10}" + "".join(f"{v:>11}" for v in verdicts))
    for label, pred in BANDS:
        sel = [r for r in rem if pred(r["norm"])]
        c = collections.Counter(vkey(r) for r in sel)
        out(f"{label:14}{len(sel):7d}{sum(r['size'] for r in sel):10d}"
            + "".join(f"{c[v]:11d}" for v in verdicts))
    c = collections.Counter(vkey(r) for r in rem)
    out(f"{'TOTAL':14}{len(rem):7d}{sum(r['size'] for r in rem):10d}"
        + "".join(f"{c[v]:11d}" for v in verdicts))


def section_units(rows, out, top):
    byunit = collections.defaultdict(list)
    for r in rows:
        byunit[r["unit"]].append(r)
    data = []
    for u, fs in byunit.items():
        rem = [f for f in fs if f["norm"] < 100.0]
        if rem:
            data.append((u, len(fs), len(rem), sum(f["size"] for f in rem)))
    data.sort(key=lambda t: -t[3])
    out(f"\n== Units with remaining work: {len(data)} of {len(byunit)} authorable units "
        f"(top {top} by remaining bytes) ==")
    out(f"{'rem_B':>9}{'rem_fn':>7}{'tot_fn':>7}  unit")
    for u, tf, rf, rb in data[:top]:
        tag = "  [low-priority]" if low_priority(u, "") else ""
        out(f"{rb:9d}{rf:7d}{tf:7d}  {u}{tag}")


def section_near_complete(rows, out, max_remaining):
    byunit = collections.defaultdict(list)
    for r in rows:
        byunit[r["unit"]].append(r)
    near = []
    for u, fs in byunit.items():
        rem = [f for f in fs if f["norm"] < 100.0]
        if 0 < len(rem) <= max_remaining:
            near.append((u, len(fs), rem))
    near.sort(key=lambda t: (len(t[2]), -t[1]))
    total_units = len(byunit)
    complete_now = sum(1 for fs in byunit.values()
                       if all(f["norm"] >= 100.0 for f in fs))
    out(f"\n== Units within {max_remaining} function(s) of COMPLETE: {len(near)} units, "
        f"{sum(len(t[2]) for t in near)} functions, "
        f"{sum(f['size'] for t in near for f in t[2])} bytes ==")
    out(f"   complete units now: {complete_now}/{total_units}; closing these would "
        f"reach {complete_now + len(near)}/{total_units} "
        f"({100.0 * (complete_now + len(near)) / total_units:.1f}%)")
    for u, tf, rem in near:
        out(f"  {len(rem)} fn {sum(f['size'] for f in rem):7d}B  ({tf} fns in unit)  {u}")
        for f in rem:
            out(f"        {f['norm']:8.4f}  {f['size']:6d}  {f['symbol'][:88]}")


def section_stubs(rows, out):
    """Functions with NO body written -- objdiff emitted no fuzzy score at all."""
    stubs = [r for r in rows if r["fuzzy"] is None]
    zero = [r for r in rows if r["norm"] <= 0.0]
    out(f"\n== Unwritten bodies ==")
    out(f"   fuzzy_match_percent is None (objdiff scored nothing): {len(stubs)} fns, "
        f"{sum(r['size'] for r in stubs)} bytes")
    out(f"   match_percent_normalized == 0:                        {len(zero)} fns, "
        f"{sum(r['size'] for r in zero)} bytes")
    c = collections.Counter()
    b = collections.Counter()
    for r in zero:
        if not r.get("db_row"):
            k = "no-DB-row"
        elif r.get("excluded"):
            k = f"excluded (DB verdict {r.get('verdict')})"
        elif r.get("verdict") == "AT_LIMIT":
            k = "AT_LIMIT-labelled"
        else:
            k = f"verdict {r.get('verdict')}"
        c[k] += 1
        b[k] += r["size"]
    out("   by DB verdict (an AT_LIMIT here is a bookkeeping reset, not a floor "
        "-- none of them carry a floor certificate):")
    for k, v in c.most_common():
        out(f"      {v:5d} {b[k]:8d}B  {k}")
    sub = collections.Counter()
    for r in zero:
        sub["/".join(r["unit"].split("/")[:3])] += r["size"]
    out("   by subtree (bytes):")
    for k, v in sub.most_common(12):
        out(f"      {v:8d}  {k}")


def section_certs(rows, out):
    rem = [r for r in rows if r["norm"] < 100.0]
    al = [r for r in rem if r.get("verdict") == "AT_LIMIT" and not r.get("excluded")]
    out(f"\n== AT_LIMIT population still below 100 on the canonical ruler: "
        f"{len(al)} fns, {sum(r['size'] for r in al)} bytes ==")
    c = collections.Counter(str(r["cert"]) for r in al)
    b = collections.Counter()
    for r in al:
        b[str(r["cert"])] += r["size"]
    out("   by floor_certificate:")
    for k, v in c.most_common():
        out(f"      {v:5d} {b[k]:8d}B  {k}")
    out("   by band (a certificate at 99.9%+ is one or two instructions from "
        "perfect and is the cheapest thing to re-audit):")
    for label, pred in BANDS:
        sel = [r for r in al if pred(r["norm"])]
        if sel:
            out(f"      {len(sel):5d} {sum(r['size'] for r in sel):8d}B  {label}")


def section_priority(rows, out):
    rem = [r for r in rows if r["norm"] < 100.0]
    buckets = collections.Counter()
    bytes_ = collections.Counter()
    for r in rem:
        if low_priority(r["unit"], r["symbol"]):
            k = "LOW (curl/jpeg/holmes/zlib/bink -- port does not run these)"
        elif r["unit"].startswith("default/lazer/"):
            k = "GAME  (default/lazer/**)"
        elif r["unit"].startswith("default/system/"):
            k = "ENGINE (default/system/**)"
        else:
            k = "OTHER"
        buckets[k] += 1
        bytes_[k] += r["size"]
    out(f"\n== Remaining work by port relevance (denominator: {len(rem)} remaining) ==")
    for k, v in buckets.most_common():
        out(f"   {v:5d} {bytes_[k]:9d}B  {k}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    ap.add_argument("--db", type=Path, default=DEFAULT_DB)
    ap.add_argument("--section", default="all",
                    choices=["all", "bands", "units", "near-complete", "stubs",
                             "certs", "priority"])
    ap.add_argument("--top", type=int, default=30, help="rows in the units table")
    ap.add_argument("--max-remaining", type=int, default=2,
                    help="near-complete threshold (functions still open in a unit)")
    ap.add_argument("--json", action="store_true",
                    help="dump the joined per-function rows as JSON instead")
    args = ap.parse_args()

    rows, cov = load_report(args.report)
    cov.update(join_db(rows, args.db))

    if args.json:
        json.dump({"coverage": cov, "functions": rows}, sys.stdout, indent=1)
        return 0

    buf: list[str] = []
    out = buf.append
    prov = cov.get("report_provenance") or {}
    out("=" * 78)
    out("DC3 work frontier")
    out("=" * 78)
    out(f"  report      : {cov['report_path']}")
    out(f"  objdiff     : {prov.get('tool_version')} ({prov.get('tool_commit')})")
    dc = prov.get("diff_config")
    if dc:
        out(f"  diff_config : {[c for c in dc if 'RelocDiffs' in c] or dc[:1]}")
    out(f"  db          : {cov.get('db')}")
    out(f"  universe    : {cov['universe']} function rows in report.json")
    out(f"  dropped     : {cov['dropped_sdk_vendor']} SDK/vendor "
        f"(default/xdk/*, default/lib/binkxenon/*)")
    out(f"                {cov['dropped_glue_shadow']} link-glue shadow rows "
        f"(same symbol already 100% in its real unit)")
    out(f"  authorable  : {cov['authorable']}  <-- the denominator for everything below")
    if cov.get("report_rows_without_db_row"):
        out(f"  note        : {cov['report_rows_without_db_row']} authorable report rows "
            f"have no decomp.db row at all")

    sec = args.section
    if sec in ("all", "bands"):
        section_bands(rows, out)
    if sec in ("all", "priority"):
        section_priority(rows, out)
    if sec in ("all", "stubs"):
        section_stubs(rows, out)
    if sec in ("all", "certs"):
        section_certs(rows, out)
    if sec in ("all", "units"):
        section_units(rows, out, args.top)
    if sec in ("all", "near-complete"):
        section_near_complete(rows, out, args.max_remaining)
    print("\n".join(buf))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
