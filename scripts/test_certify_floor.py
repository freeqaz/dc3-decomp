#!/usr/bin/env python3
"""Unit tests for certify_floor.py + reconcile_db.py floor-cert invalidation.

Builds a tiny synthetic decomp.db with one function per evidence class and
exercises:
  - dry-run default writes NOTHING
  - --migrate adds the 5 cert columns + authorable_done view (idempotent)
  - each evidence class certifies with the right enum value + precedence
  - the routable/real-bug classes (call_count/error/call_arg) are NOT certified
  - stale-unicorn provenance is recorded in floor_cert_evidence
  - --summary done-view numbers (done with/without certs)
  - reconcile_db.py check (e): a cert whose normalized moved is invalidated; --fix clears it

Run: python3 scripts/test_certify_floor.py    (no pytest required)
"""
from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
import tempfile
from datetime import datetime, timezone, timedelta
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
CERTIFY = SCRIPTS / "certify_floor.py"
RECONCILE = SCRIPTS / "reconcile_db.py"

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {msg}")
    if not cond:
        _failures.append(msg)


def make_db(path: Path) -> None:
    """A functions table with the columns certify/reconcile touch + attempts."""
    conn = sqlite3.connect(str(path))
    conn.execute("""
        CREATE TABLE functions (
            id INTEGER PRIMARY KEY,
            symbol TEXT NOT NULL UNIQUE,
            demangled TEXT,
            unit TEXT,
            size INTEGER,
            current_percent REAL,
            best_percent REAL,
            verdict TEXT,
            excluded INTEGER DEFAULT 0,
            is_stub INTEGER DEFAULT 0,
            merged_symbol_count INTEGER DEFAULT 0,
            unicorn_verdict TEXT,
            unicorn_class TEXT,
            unicorn_tested_at TIMESTAMP,
            match_percent_normalized REAL,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.execute("""
        CREATE TABLE attempts (
            id INTEGER PRIMARY KEY,
            function_id INTEGER,
            exit_status TEXT,
            start_percent REAL,
            end_percent REAL
        )
    """)
    old = (datetime.now(timezone.utc) - timedelta(days=98)).strftime("%Y-%m-%d %H:%M:%S")
    fresh = (datetime.now(timezone.utc) - timedelta(days=3)).strftime("%Y-%m-%d %H:%M:%S")
    rows = [
        # symbol, unit, size, cur, best, verdict, excl, stub, msc, uv, uc, tested, norm
        ("F_equiv_stale",   "default/system/a", 100, 94.0, 94.0, None, 0, 0, 0, "EQUIVALENT", None, old,   94.0),  # equivalent (stale)
        ("F_equiv_fresh",   "default/system/a", 100, 96.0, 96.0, None, 0, 0, 0, "EQUIVALENT", None, fresh, 96.0),  # equivalent (fresh)
        ("F_artifact",      "default/system/a", 100, 88.0, 88.0, None, 0, 0, 0, "DIVERGENT", "stack_layout", old, 88.0),  # artifact:stack_layout
        ("F_orig_error",    "default/system/a", 100, 90.0, 90.0, None, 0, 0, 0, "DIVERGENT", "orig_error", old, 90.0),    # artifact:orig_error
        ("F_icf",           "default/system/a", 100, 82.0, 82.0, None, 0, 0, 3, None, None, None, 82.0),                  # icf_merged
        ("F_permuter",      "default/system/a", 100, 91.0, 91.0, None, 0, 0, 0, None, None, None, 91.0),                  # permuter_exhausted (attempt below)
        ("F_callcount",     "default/system/a", 100, 95.0, 95.0, None, 0, 0, 0, "DIVERGENT", "call_count", old, 95.0),    # NOT certifiable (routable)
        ("F_realbug",       "default/system/a", 100, 70.0, 70.0, None, 0, 0, 0, "DIVERGENT", "call_arg", old, 70.0),      # NOT certifiable (real bug)
        ("F_untested",      "default/system/a", 100, 85.0, 85.0, None, 0, 0, 0, None, None, None, 85.0),                  # NOT certifiable (no evidence)
        ("F_matched",       "default/system/a", 100, 100.0,100.0,"COMPLETE", 0, 0, 0, "EQUIVALENT", None, old, 100.0),    # 100 -> not on frontier
        # Wave-6 Lane D: COMPLETE + current=100 + normalized NULL -> counts as 'matched' in view
        ("F_db_only",       "default/system/a", 100, 100.0,100.0,"COMPLETE", 0, 0, 0, None, None, None, None),             # db-only: no norm score
        ("F_stub",          "default/system/a", 100, 0.0,  0.0,  None, 0, 1, 0, None, None, None, 0.0),                   # stub -> not certifiable
        ("merged_deadbeef", "default/system/a", 100, 80.0, 80.0, None, 0, 0, 0, "EQUIVALENT", None, old, 80.0),          # artifact symbol -> excluded
        ("F_sdk",           "default/xdk/lib",  100, 90.0, 90.0, None, 0, 0, 0, "EQUIVALENT", None, old, 90.0),          # SDK -> excluded
    ]
    conn.executemany(
        "INSERT INTO functions (symbol,unit,size,current_percent,best_percent,verdict,"
        "excluded,is_stub,merged_symbol_count,unicorn_verdict,unicorn_class,"
        "unicorn_tested_at,match_percent_normalized) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
        rows)
    # permuter-exhausted evidence: an attempt that ended at_limit and never beat 91
    fid = conn.execute("SELECT id FROM functions WHERE symbol='F_permuter'").fetchone()[0]
    conn.execute("INSERT INTO attempts (function_id,exit_status,start_percent,end_percent) "
                 "VALUES (?,?,?,?)", (fid, "at_limit", 91.0, 91.0))
    conn.commit()
    conn.close()


def make_report(path: Path) -> None:
    # report.json only needed by reconcile_db.py; fuzzy==norm for simplicity.
    def fn(name, pct):
        return {"name": name, "address": "0", "size": "100",
                "fuzzy_match_percent": pct, "match_percent_normalized": pct,
                "metadata": {"demangled_name": name}}
    report = {"units": [{"name": "default/system/a", "functions": [
        fn("F_equiv_stale", 94.0), fn("F_equiv_fresh", 96.0), fn("F_artifact", 88.0),
        fn("F_orig_error", 90.0), fn("F_icf", 82.0), fn("F_permuter", 91.0),
        fn("F_callcount", 95.0), fn("F_realbug", 70.0), fn("F_untested", 85.0),
        fn("F_matched", 100.0), fn("F_stub", 0.0),
    ]}]}
    path.write_text(json.dumps(report))


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def col(conn, sym, c):
    r = conn.execute(f"SELECT {c} FROM functions WHERE symbol=?", (sym,)).fetchone()
    return r[0] if r else None


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        db = tdp / "test.db"
        report = tdp / "report.json"
        make_db(db)
        make_report(report)

        print("== certify --db (dry-run default, NO writes) ==")
        p = run([sys.executable, str(CERTIFY), "--db", str(db)])
        check(p.returncode == 0, f"dry-run exits 0 (rc={p.returncode})\n{p.stderr}")
        check("CERTIFIABLE TODAY (have evidence):        6" in p.stdout,
              "dry-run reports 6 certifiable (2 equiv + 2 artifact + 1 icf + 1 permuter)")
        conn = sqlite3.connect(str(db))
        # dry-run must NOT add columns
        cols = {r[1] for r in conn.execute("PRAGMA table_info(functions)")}
        check("floor_certificate" not in cols, "dry-run did NOT add floor_certificate column")
        conn.close()

        print("== certify --migrate --apply ==")
        p = run([sys.executable, str(CERTIFY), "--migrate", "--apply", "--db", str(db)])
        check(p.returncode == 0, f"apply exits 0 (rc={p.returncode})\n{p.stderr}")
        conn = sqlite3.connect(str(db))
        cols = {r[1] for r in conn.execute("PRAGMA table_info(functions)")}
        for c in ("floor_certificate", "floor_cert_pct", "floor_cert_build",
                  "floor_cert_at", "floor_cert_evidence"):
            check(c in cols, f"migration added column {c}")
        # per-class certs
        check(col(conn, "F_equiv_stale", "floor_certificate") == "equivalent",
              "F_equiv_stale -> equivalent")
        check(col(conn, "F_artifact", "floor_certificate") == "artifact:stack_layout",
              "F_artifact -> artifact:stack_layout")
        check(col(conn, "F_orig_error", "floor_certificate") == "artifact:orig_error",
              "F_orig_error -> artifact:orig_error (orig_error is a floor)")
        check(col(conn, "F_icf", "floor_certificate") == "icf_merged",
              "F_icf -> icf_merged")
        check(col(conn, "F_permuter", "floor_certificate") == "permuter_exhausted",
              "F_permuter -> permuter_exhausted (attempts evidence)")
        # NOT certified
        check(col(conn, "F_callcount", "floor_certificate") is None,
              "F_callcount NOT certified (routable call_count)")
        check(col(conn, "F_realbug", "floor_certificate") is None,
              "F_realbug NOT certified (real-bug call_arg)")
        check(col(conn, "F_untested", "floor_certificate") is None,
              "F_untested NOT certified (no evidence)")
        check(col(conn, "F_matched", "floor_certificate") is None,
              "F_matched (100%%) NOT certified (not on frontier)")
        check(col(conn, "F_stub", "floor_certificate") is None,
              "F_stub NOT certified")
        check(col(conn, "merged_deadbeef", "floor_certificate") is None,
              "merged_ artifact symbol NOT certified")
        check(col(conn, "F_sdk", "floor_certificate") is None,
              "SDK unit NOT certified")
        # provenance: cert_pct + build + evidence recorded
        check(col(conn, "F_equiv_stale", "floor_cert_pct") == 94.0,
              "floor_cert_pct captured = 94.0")
        ev = json.loads(col(conn, "F_equiv_stale", "floor_cert_evidence"))
        check(ev.get("unicorn_stale") is True,
              "stale-unicorn provenance recorded (unicorn_stale=true for 98d-old test)")
        ev2 = json.loads(col(conn, "F_equiv_fresh", "floor_cert_evidence"))
        check(ev2.get("unicorn_stale") is False,
              "fresh-unicorn provenance recorded (unicorn_stale=false for 3d-old test)")
        conn.close()

        print("== idempotency: re-apply changes nothing ==")
        c0 = sqlite3.connect(str(db))
        n0 = c0.execute("SELECT count(*) FROM functions WHERE floor_certificate IS NOT NULL").fetchone()[0]
        c0.close()
        p = run([sys.executable, str(CERTIFY), "--migrate", "--apply", "--db", str(db)])
        check(p.returncode == 0, "re-apply exits 0")
        c1 = sqlite3.connect(str(db))
        n1 = c1.execute("SELECT count(*) FROM functions WHERE floor_certificate IS NOT NULL").fetchone()[0]
        c1.close()
        check(n0 == n1 == 6, f"cert count stable across re-apply (n0={n0}, n1={n1})")

        print("== authorable_done view + --summary ==")
        conn = sqlite3.connect(str(db))
        view_rows = conn.execute("SELECT done_state, count(*) FROM authorable_done GROUP BY done_state").fetchall()
        states = dict(view_rows)
        # 12 authorable rows (merged_ + sdk excluded):
        #   matched=2 (F_matched norm==100 + F_db_only COMPLETE+cur=100+norm NULL)
        #   stub=1, certified=6, open=3
        check(states.get("matched") == 2, f"view: 2 matched ({states})")
        check(states.get("stub") == 1, f"view: 1 stub ({states})")
        check(states.get("certified") == 6, f"view: 6 certified ({states})")
        check(states.get("open") == 3, f"view: 3 open (callcount+realbug+untested) ({states})")
        # Specifically verify the db-only pattern
        db_only_state = conn.execute(
            "SELECT done_state FROM authorable_done WHERE symbol='F_db_only'"
        ).fetchone()
        check(db_only_state and db_only_state[0] == "matched",
              f"F_db_only (COMPLETE+cur=100+norm NULL) -> matched (got {db_only_state})")
        conn.close()
        p = run([sys.executable, str(CERTIFY), "--summary", "--db", str(db)])
        check(p.returncode == 0, "summary exits 0")
        check("DONE with certs" in p.stdout, "summary prints DONE-with-certs line")

        print("== reconcile (e): cert invalidated when normalized moves ==")
        # baseline: no stale certs
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check("(e) stale floor certificates (percent moved/matched): 0" in p.stdout,
              "reconcile (e)=0 on fresh certs")
        # move one cert's normalized away from floor_cert_pct
        conn = sqlite3.connect(str(db))
        conn.execute("UPDATE functions SET match_percent_normalized=80.0 WHERE symbol='F_equiv_stale'")
        conn.commit(); conn.close()
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check("(e) stale floor certificates (percent moved/matched): 1" in p.stdout,
              "reconcile (e)=1 after percent move")
        # --fix clears it
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report), "--fix"])
        check("(e) cleared stale floor certs:          1" in p.stdout,
              "reconcile --fix clears the stale cert")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_equiv_stale", "floor_certificate") is None,
              "stale cert columns NULLed by --fix")
        conn.close()

    print()
    if _failures:
        print(f"FAILED: {len(_failures)} checks")
        for f in _failures:
            print(f"  - {f}")
        return 1
    print("All certify_floor tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
