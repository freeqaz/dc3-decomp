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
        ("F_artifact",      "default/system/a", 100, 88.0, 88.0, None, 0, 0, 0, "DIVERGENT", "stack_layout", fresh, 88.0),  # artifact:stack_layout
        ("F_artifact_stale","default/system/a", 100, 87.0, 87.0, None, 0, 0, 0, "DIVERGENT", "stack_layout", old, 87.0),  # WITHHELD: right class, stale evidence
        ("F_orig_error",    "default/system/a", 100, 90.0, 90.0, None, 0, 0, 0, "DIVERGENT", "orig_error", fresh, 90.0),  # artifact:orig_error
        ("F_icf",           "default/system/a", 100, 82.0, 82.0, None, 0, 0, 3, None, None, None, 82.0),                  # icf_merged
        ("F_permuter",      "default/system/a", 100, 91.0, 91.0, None, 0, 0, 0, None, None, None, 91.0),                  # permuter_exhausted (attempt below)
        ("F_callcount",     "default/system/a", 100, 95.0, 95.0, None, 0, 0, 0, "DIVERGENT", "call_count", old, 95.0),    # NOT certifiable (routable)
        ("F_realbug",       "default/system/a", 100, 70.0, 70.0, None, 0, 0, 0, "DIVERGENT", "call_arg", old, 70.0),      # NOT certifiable (real bug)
        ("F_untested",      "default/system/a", 100, 85.0, 85.0, None, 0, 0, 0, None, None, None, 85.0),                  # NOT certifiable (no evidence)
        # native_divergence: target-only symbol the engine forced a different
        # signature for. is_stub=1 (base_size==0 heuristic) + norm=0, yet our
        # source IS correct. Only native_divergence may cert this row.
        ("F_native_div",    "default/system/a", 100, 0.0,  0.0,  None, 0, 1, 0, None, None, None, 0.0),
        # native_divergence at the *NULL-norm* symbol-mismatch floor: the target
        # symbol has NO base counterpart at all, so report.json leaves fuzzy NULL
        # and sync never writes a normalized score (norm IS NULL). is_stub=1.
        # This is the SampleInst360::IsPlaying (UAA vs our UBA) shape — the cert
        # must accept NULL norm for native_divergence (and ONLY that class).
        ("F_native_div_null","default/system/a", 100, 0.0,  0.0,  None, 0, 1, 0, None, None, None, None),
        ("F_matched",       "default/system/a", 100, 100.0,100.0,"COMPLETE", 0, 0, 0, "EQUIVALENT", None, old, 100.0),    # 100 -> not on frontier
        # Wave-6 Lane D: COMPLETE + current=100 + normalized NULL -> counts as 'matched' in view
        ("F_db_only",       "default/system/a", 100, 100.0,100.0,"COMPLETE", 0, 0, 0, None, None, None, None),             # db-only: no norm score
        ("F_stub",          "default/system/a", 100, 0.0,  0.0,  None, 0, 1, 0, None, None, None, 0.0),                   # stub -> not certifiable
        ("merged_deadbeef", "default/system/a", 100, 80.0, 80.0, None, 0, 0, 0, "EQUIVALENT", None, old, 80.0),          # artifact symbol -> excluded
        ("F_sdk",           "default/xdk/lib",  100, 90.0, 90.0, None, 0, 0, 0, "EQUIVALENT", None, old, 90.0),          # SDK -> excluded
        # A '??'-prefixed ctor: AUTHORABLE (must be COUNTED). The wave-9 bug
        # ('??_%' unescaped) would wrongly EXCLUDE it; the escaped path + the
        # Python startswith path both keep it. This is the row that makes the
        # two-path denominator self-check actually exercise the wildcard bug.
        ("??0Foo@@QAA@XZ",  "default/system/a", 100, 100.0,100.0,"COMPLETE", 0, 0, 0, "EQUIVALENT", None, old, 100.0),  # ctor -> matched/authorable
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
        fn("F_artifact_stale", 87.0), fn("F_orig_error", 90.0), fn("F_icf", 82.0), fn("F_permuter", 91.0),
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
        check("CERTIFIABLE TODAY (have evidence):        5" in p.stdout,
              "dry-run reports 5 certifiable (1 equiv_fresh + 2 artifact + 1 icf + 1 permuter; 2 stale rows withheld)")
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
        # ⚠ CONTRACT CHANGED 2026-08-22. This used to assert
        #     F_equiv_stale -> "equivalent"
        # i.e. the test ENCODED THE DEFECT: the headline said "blocked on STALE
        # unicorn" and the row was written anyway. `F_equiv_stale` is 98 days
        # old against STALE_DAYS=60. Stale-unicorn rows are now WITHHELD unless
        # --allow-stale-unicorn, and the pair of assertions below is the
        # negative control -- without the second one, "not certified" would
        # also be satisfied by a classifier that can no longer certify at all.
        check(col(conn, "F_equiv_stale", "floor_certificate") is None,
              "F_equiv_stale NOT certified (unicorn evidence 98d > STALE_DAYS)")
        check(col(conn, "F_equiv_fresh", "floor_certificate") == "equivalent",
              "F_equiv_fresh -> equivalent (3d evidence; the control that "
              "proves the equivalent path still fires)")
        check(col(conn, "F_artifact_stale", "floor_certificate") is None,
              "F_artifact_stale NOT certified (right class, 98d evidence)")
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
        check(col(conn, "F_equiv_fresh", "floor_cert_pct") == 96.0,
              "floor_cert_pct captured = 96.0")
        ev = json.loads(col(conn, "F_equiv_fresh", "floor_cert_evidence"))
        check(ev.get("unicorn_stale") is False,
              "fresh-unicorn provenance recorded (unicorn_stale=false for 3d-old test)")
        ev2 = json.loads(col(conn, "F_equiv_fresh", "floor_cert_evidence"))
        check(ev2.get("unicorn_stale") is False,
              "re-apply keeps fresh-unicorn provenance")
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
        check(n0 == n1 == 5, f"cert count stable across re-apply (n0={n0}, n1={n1})")

        print("== authorable_done view + --summary ==")
        conn = sqlite3.connect(str(db))
        view_rows = conn.execute("SELECT done_state, count(*) FROM authorable_done GROUP BY done_state").fetchall()
        states = dict(view_rows)
        # 16 authorable rows (merged_ + sdk excluded; ??0Foo ctor INCLUDED):
        #   matched=3 (F_matched norm==100 + F_db_only COMPLETE+cur=100+norm NULL
        #              + ??0Foo ctor COMPLETE+cur=100+norm==100)
        #   stub=3 (F_stub + F_native_div(0%) + F_native_div_null(NULL norm),
        #           none certified at this point),
        #   certified=6, open=3
        check(states.get("matched") == 3, f"view: 3 matched ({states})")
        check(states.get("stub") == 3, f"view: 3 stub (F_stub + uncertified F_native_div + F_native_div_null) ({states})")
        check(states.get("certified") == 5, f"view: 5 certified ({states})")
        check(states.get("open") == 5, f"view: 5 open (callcount+realbug+untested+2 withheld-stale) ({states})")
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

        print("== two-path denominator self-check ==")
        # Healthy DB: the SQL view WHERE and the Python startswith filter must agree
        # (16 authorable rows: 18 total minus merged_deadbeef + F_sdk).
        p = run([sys.executable, str(CERTIFY), "--check-denominator", "--db", str(db)])
        check(p.returncode == 0, f"check-denominator exits 0 when consistent (rc={p.returncode})\n{p.stderr}")
        check("AGREE" in p.stdout, "check-denominator reports AGREE on a healthy DB")
        check("16 fns" in p.stdout, f"both paths count 16 authorable fns\n{p.stdout}")

        # Inject the wave-9 bug: recreate the view with an UNESCAPED '??_%'-style
        # artifact clause and confirm the self-check fails LOUDLY (nonzero).
        sys.path.insert(0, str(SCRIPTS))
        import certify_floor as cf  # noqa: E402
        conn = sqlite3.connect(str(db))
        sdk = " AND ".join(
            f"(unit IS NULL OR unit NOT LIKE '{pfx}%')" for pfx in cf.SDK_UNIT_PREFIXES)
        art_buggy = " AND ".join(
            f"symbol NOT LIKE '{pfx}%'" for pfx in cf.ARTIFACT_PREFIXES)  # UNESCAPED bug
        conn.execute("DROP VIEW IF EXISTS authorable_done")
        conn.execute(
            "CREATE VIEW authorable_done AS SELECT id, symbol, demangled, unit, size, "
            "current_percent, match_percent_normalized, verdict, is_stub, "
            "floor_certificate, floor_cert_pct, 'open' AS done_state, 0 AS is_done "
            f"FROM functions WHERE excluded=0 AND {sdk} AND {art_buggy}")
        conn.commit(); conn.close()
        p = run([sys.executable, str(CERTIFY), "--check-denominator", "--db", str(db)])
        check(p.returncode == 1, f"check-denominator exits 1 on wave-9-style undercount (rc={p.returncode})")
        check("DISAGREE" in p.stderr, "check-denominator prints DISAGREE on undercount")
        # restore the correct view so downstream tests are unaffected
        p = run([sys.executable, str(CERTIFY), "--migrate", "--apply", "--db", str(db)])
        check(p.returncode == 0, "view restored after denominator-bug test")

        print("== reconcile (e): cert invalidated when normalized moves ==")
        # baseline: no stale certs
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check("(e) stale floor certificates (percent moved/matched): 0" in p.stdout,
              "reconcile (e)=0 on fresh certs")
        # move one cert's normalized away from floor_cert_pct
        conn = sqlite3.connect(str(db))
        conn.execute("UPDATE functions SET match_percent_normalized=80.0 WHERE symbol='F_equiv_fresh'")
        conn.commit(); conn.close()
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report)])
        check("(e) stale floor certificates (percent moved/matched): 1" in p.stdout,
              "reconcile (e)=1 after percent move")
        # --fix clears it
        p = run([sys.executable, str(RECONCILE), "--db", str(db), "--report", str(report), "--fix"])
        check("(e) cleared stale floor certs:          1" in p.stdout,
              "reconcile --fix clears the stale cert")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_equiv_fresh", "floor_certificate") is None,
              "stale cert columns NULLed by --fix")
        conn.close()

        print("== manual --manual-file: native_divergence (manual-only class) ==")
        # F_untested is an open frontier row (norm 85, no auto-evidence). The
        # native_divergence class is MANUAL-ONLY (no DB flag auto-fires it), so it
        # can only land via --manual-file. Use it to certify F_untested.
        nd_backlog = tdp / "nd_backlog.json"
        nd_backlog.write_text(json.dumps([{
            "symbol_like": "F_untested",
            "cert": "native_divergence",
            "expect_pct": 85.0,
            "evidence": {"source_doc": "test", "diagnosis": "engine override forces const base"},
        }]))
        # dry-run: resolves but writes nothing
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(nd_backlog), "--db", str(db)])
        check(p.returncode == 0, f"manual native_divergence dry-run exits 0 (rc={p.returncode})\n{p.stderr}")
        check("1 cert(s) resolvable" in p.stdout,
              f"manual native_divergence dry-run reports 1 resolvable\n{p.stdout}")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_untested", "floor_certificate") is None,
              "manual dry-run did NOT write native_divergence cert")
        conn.close()
        # apply: lands the native_divergence cert
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(nd_backlog),
                 "--apply", "--db", str(db)])
        check(p.returncode == 0, f"manual native_divergence apply exits 0 (rc={p.returncode})\n{p.stderr}")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_untested", "floor_certificate") == "native_divergence",
              "F_untested -> native_divergence (manual-only class landed)")
        check(col(conn, "F_untested", "floor_cert_pct") == 85.0,
              "native_divergence cert captured CURRENT norm (85.0), not expect_pct")
        ev_nd = json.loads(col(conn, "F_untested", "floor_cert_evidence"))
        check(ev_nd.get("diagnosis", "").startswith("engine override"),
              "native_divergence evidence diagnosis recorded")
        # view counts it as 'certified' (done) — closes the open count
        st = conn.execute("SELECT done_state FROM authorable_done WHERE symbol='F_untested'").fetchone()
        check(st and st[0] == "certified",
              f"native_divergence cert makes F_untested 'certified' (done) (got {st})")
        conn.close()
        print("== manual native_divergence at norm==0 + is_stub=1 (symbol-mismatch floor) ==")
        # F_native_div is the canonical native_divergence: target-only symbol
        # (norm=0, is_stub=1) the engine forced a different signature for. ONLY
        # native_divergence may cert it (0% + stub are the symptoms of the
        # divergence, not an un-implemented function).
        nd0 = tdp / "nd0_backlog.json"
        nd0.write_text(json.dumps([{
            "symbol_like": "F_native_div", "cert": "native_divergence", "expect_pct": 0.0,
            "evidence": {"diagnosis": "target UAA vs our UBA; engine override needs const base"},
        }]))
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(nd0), "--apply", "--db", str(db)])
        check(p.returncode == 0, f"native_divergence@0%+stub applies (rc={p.returncode})\n{p.stdout}\n{p.stderr}")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_native_div", "floor_certificate") == "native_divergence",
              "F_native_div (norm=0, is_stub=1) -> native_divergence cert landed")
        check(col(conn, "F_native_div", "floor_cert_pct") == 0.0,
              "native_divergence@0% captured norm=0.0")
        conn.close()
        # other classes must STILL refuse a norm==0 / is_stub=1 row
        bad0 = tdp / "bad0_backlog.json"
        bad0.write_text(json.dumps([{
            "symbol_like": "F_native_div", "cert": "permuter_exhausted", "expect_pct": 0.0, "force": True,
        }]))
        # reset the cert so the class-gate is what's tested, not the KEEP path
        conn = sqlite3.connect(str(db))
        conn.execute("UPDATE functions SET floor_certificate=NULL WHERE symbol='F_native_div'")
        conn.commit(); conn.close()
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(bad0), "--apply", "--db", str(db)])
        check(p.returncode == 1, "permuter_exhausted refuses norm==0/is_stub=1 row (rc=1)")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_native_div", "floor_certificate") is None,
              "non-native_divergence class did NOT cert the 0%/stub row")
        conn.close()
        # re-apply the native_divergence cert so the row is 'done' for downstream
        run([sys.executable, str(CERTIFY), "--manual-file", str(nd0), "--apply", "--db", str(db)])

        print("== manual native_divergence at norm IS NULL + is_stub=1 (no base counterpart) ==")
        # F_native_div_null is the SampleInst360::IsPlaying shape: the target
        # symbol has NO base counterpart at all, so norm IS NULL (not 0). The
        # native_divergence class must accept NULL norm (pinning cert_pct=0.0);
        # every other class must still refuse it.
        ndN = tdp / "ndnull_backlog.json"
        ndN.write_text(json.dumps([{
            "symbol_like": "F_native_div_null", "cert": "native_divergence", "expect_pct": 0.0,
            "evidence": {"diagnosis": "target UAA symbol has no base counterpart; engine override needs const base"},
        }]))
        # dry-run resolves the NULL-norm row (the regression this guards against)
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(ndN), "--db", str(db)])
        check(p.returncode == 0, f"native_divergence@NULL dry-run exits 0 (rc={p.returncode})\n{p.stdout}\n{p.stderr}")
        check("1 cert(s) resolvable" in p.stdout,
              f"native_divergence@NULL norm resolves in dry-run\n{p.stdout}")
        # apply lands it with cert_pct pinned to 0.0
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(ndN), "--apply", "--db", str(db)])
        check(p.returncode == 0, f"native_divergence@NULL applies (rc={p.returncode})\n{p.stdout}\n{p.stderr}")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_native_div_null", "floor_certificate") == "native_divergence",
              "F_native_div_null (norm IS NULL, is_stub=1) -> native_divergence cert landed")
        check(col(conn, "F_native_div_null", "floor_cert_pct") == 0.0,
              "native_divergence@NULL captured cert_pct pinned to 0.0")
        # The view's done_state precedence is matched > stub > certified, so an
        # is_stub=1 native_divergence row reports 'stub' — BOTH stub and certified
        # are "done", so the contract is is_done=1 (the cert + the stub flag both
        # count it out of the open bucket).
        st = conn.execute(
            "SELECT done_state, is_done FROM authorable_done WHERE symbol='F_native_div_null'"
        ).fetchone()
        check(st and st[1] == 1,
              f"native_divergence cert keeps F_native_div_null DONE (is_done=1, state={st[0] if st else None})")
        conn.close()
        # other classes must STILL refuse a NULL-norm row
        badN = tdp / "badnull_backlog.json"
        badN.write_text(json.dumps([{
            "symbol_like": "F_native_div_null", "cert": "permuter_exhausted", "expect_pct": 0.0, "force": True,
        }]))
        conn = sqlite3.connect(str(db))
        conn.execute("UPDATE functions SET floor_certificate=NULL WHERE symbol='F_native_div_null'")
        conn.commit(); conn.close()
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(badN), "--apply", "--db", str(db)])
        check(p.returncode == 1, "permuter_exhausted refuses NULL-norm row (rc=1)")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_native_div_null", "floor_certificate") is None,
              "non-native_divergence class did NOT cert the NULL-norm row")
        conn.close()

        # invalid cert class is rejected (not written)
        bad_backlog = tdp / "bad_backlog.json"
        bad_backlog.write_text(json.dumps([{
            "symbol_like": "F_callcount", "cert": "totally_made_up", "expect_pct": 95.0,
        }]))
        p = run([sys.executable, str(CERTIFY), "--manual-file", str(bad_backlog),
                 "--apply", "--db", str(db)])
        check(p.returncode == 1, f"invalid cert class -> rc=1 (rc={p.returncode})")
        check("invalid cert class" in p.stdout, "invalid cert class reported as ERROR")
        conn = sqlite3.connect(str(db))
        check(col(conn, "F_callcount", "floor_certificate") is None,
              "invalid cert class NOT written")
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
