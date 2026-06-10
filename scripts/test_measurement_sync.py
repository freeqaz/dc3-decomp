#!/usr/bin/env python3
"""Unit tests for sync_match_percent.py dual-store/promote/demote and reconcile_db.py.

Builds a tiny synthetic decomp.db + report.json fixture in a temp dir and exercises:
  - normalized dual-store (current_percent stays fuzzy, match_percent_normalized filled)
  - --promote keys off normalized==100 (the fuzzy<100/norm==100 case promotes)
  - --demote reverts COMPLETE<100 to NULL, but NOT when normalized>=100
  - stale is_stub clear when current_percent>=100
  - reconcile_db.py read-only exit codes + --fix corrections

Run: python3 scripts/test_measurement_sync.py    (no pytest required)
"""
from __future__ import annotations

import json
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
SYNC = SCRIPTS / "sync_match_percent.py"
RECONCILE = SCRIPTS / "reconcile_db.py"

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {msg}")
    if not cond:
        _failures.append(msg)


def make_db(path: Path) -> None:
    """A minimal `functions` table with the columns sync/reconcile touch."""
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
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    rows = [
        # symbol,            unit,                  size, cur,  best, verdict,     excl, stub
        ("F_secret_complete", "default/system/a", 100, 99.5, 99.5, "AT_LIMIT",   0, 0),  # fuzzy99.5/norm100 -> PROMOTE
        ("F_already_100",     "default/system/a", 100, 100.0,100.0,"AT_LIMIT",   0, 0),  # fuzzy100 verdict!=COMPLETE -> PROMOTE
        ("F_real_partial",    "default/system/a", 100, 80.0, 80.0, None,          0, 0),  # fuzzy80/norm80 -> NOT promoted
        ("F_stale_complete",  "default/system/a", 100, 88.0, 95.0, "COMPLETE",    0, 0),  # COMPLETE but 88 -> DEMOTE
        ("F_complete_normok", "default/system/a", 100, 99.0, 99.0, "COMPLETE",    0, 0),  # COMPLETE, fuzzy99 norm100 -> KEEP
        ("F_stale_stub",      "default/system/a", 100, 100.0,100.0,"COMPLETE",    0, 1),  # is_stub & 100 -> CLEAR
        ("F_real_stub",       "default/system/a", 100, 0.0,  0.0,  "AT_LIMIT",    0, 1),  # is_stub & 0 -> keep stub
        ("F_db_only",         "default/system/a", 100, 100.0,100.0,"COMPLETE",    0, 1),  # not in report; stub & 100
        ("F_sdk",             "default/xdk/lib",  100, 0.0,  0.0,  None,          0, 0),  # SDK, ignored
    ]
    conn.executemany(
        "INSERT INTO functions (symbol,unit,size,current_percent,best_percent,verdict,excluded,is_stub) "
        "VALUES (?,?,?,?,?,?,?,?)", rows)
    conn.commit()
    conn.close()


def make_report(path: Path) -> None:
    def fn(name, fuzzy, norm, size=100):
        return {"name": name, "address": "0", "size": str(size),
                "fuzzy_match_percent": fuzzy, "match_percent_normalized": norm,
                "metadata": {"demangled_name": name}}
    report = {
        "units": [
            {"name": "default/system/a", "functions": [
                fn("F_secret_complete", 99.5, 100.0),
                fn("F_already_100", 100.0, 100.0),
                fn("F_real_partial", 80.0, 80.0),
                fn("F_stale_complete", 88.0, 88.0),
                fn("F_complete_normok", 99.0, 100.0),
                fn("F_stale_stub", 100.0, 100.0),
                fn("F_real_stub", 0.0, 0.0),
                # F_db_only intentionally absent from report
            ]},
            {"name": "default/xdk/lib", "functions": [
                fn("F_sdk", 0.0, 0.0),
            ]},
            # report-only symbol (in report, not in db)
            {"name": "default/system/b", "functions": [
                fn("F_report_only", 50.0, 50.0),
            ]},
        ]
    }
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

        print("== sync --dry-run (no mutation) ==")
        p = run([sys.executable, str(SYNC), "--db", str(db), "--report", str(report),
                 "--dry-run", "--promote", "--demote"])
        check(p.returncode == 0, f"sync dry-run exits 0 (rc={p.returncode})\n{p.stderr}")
        check("Promoted:         2" in p.stdout, "dry-run promotes exactly 2 (norm==100, not COMPLETE)")
        check("Demoted:          1" in p.stdout, "dry-run demotes exactly 1 (F_stale_complete)")
        check("is_stub cleared:  1" in p.stdout, "dry-run clears exactly 1 in-report stub (F_stale_stub)")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_secret_complete", "verdict") == "AT_LIMIT",
              "dry-run did NOT mutate verdict")
        conn.close()

        print("== sync --promote --demote (apply) ==")
        p = run([sys.executable, str(SYNC), "--db", str(db), "--report", str(report),
                 "--promote", "--demote"])
        check(p.returncode == 0, f"sync apply exits 0 (rc={p.returncode})\n{p.stderr}")
        conn = sqlite3.connect(str(db))
        # dual-store: current_percent stays fuzzy; normalized column filled
        check(col(conn, "F_secret_complete", "current_percent") == 99.5,
              "current_percent stays FUZZY (99.5) for secret-complete fn")
        check(col(conn, "F_secret_complete", "match_percent_normalized") == 100.0,
              "match_percent_normalized stored = 100.0 for secret-complete fn")
        # promotion keyed off normalized
        check(col(conn, "F_secret_complete", "verdict") == "COMPLETE",
              "fuzzy99.5/norm100 PROMOTED to COMPLETE")
        check(col(conn, "F_already_100", "verdict") == "COMPLETE",
              "fuzzy100 non-COMPLETE PROMOTED to COMPLETE")
        check(col(conn, "F_real_partial", "verdict") is None,
              "fuzzy80/norm80 NOT promoted (verdict stays NULL)")
        # demotion
        check(col(conn, "F_stale_complete", "verdict") is None,
              "COMPLETE/fuzzy88/norm88 DEMOTED to NULL")
        check(col(conn, "F_complete_normok", "verdict") == "COMPLETE",
              "COMPLETE/fuzzy99/norm100 KEPT (normalized gate protects it)")
        # stub clear
        check(col(conn, "F_stale_stub", "is_stub") == 0,
              "is_stub cleared for 100%% in-report fn")
        check(col(conn, "F_real_stub", "is_stub") == 1,
              "is_stub kept for genuine 0%% stub")
        check(col(conn, "F_db_only", "is_stub") == 1,
              "db-only stub NOT cleared by sync (not in report) — reconcile owns it")
        conn.close()

        print("== reconcile read-only (post-sync; expect db-only residue) ==")
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check(p.returncode == 1, f"reconcile flags residual db-only drift, exit 1 (rc={p.returncode})")
        check("report-only symbols (in report, not db):          1" in p.stdout,
              "reconcile detects 1 report-only symbol (F_report_only)")
        check("is_stub=1 AND current_percent>=100:               1" in p.stdout,
              "reconcile detects 1 stale db-only stub (F_db_only)")

        print("== reconcile --fix (clears db-only stub) ==")
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report), "--fix"])
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_db_only", "is_stub") == 0,
              "reconcile --fix cleared db-only stale stub")
        conn.close()

        print("== reconcile read-only after fix (only report-only/db-only-authorable remain) ==")
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check("is_stub=1 AND current_percent>=100:               0" in p.stdout,
              "no stale stub remains after fix")

    print()
    if _failures:
        print(f"FAILED: {len(_failures)} assertion(s)")
        for f in _failures:
            print(f"  - {f}")
        return 1
    print("ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
