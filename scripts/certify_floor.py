#!/usr/bin/env python3
"""Floor-certificate tooling for DC3 decomp (roadmap 3.1, audit doc 08).

Makes "done — only cosmetic/floor mismatches remain" *queryable* instead of
vibes. A *floor certificate* records that a function is legitimately below 100%
because the residual diff is a known cosmetic artifact (register/FPR swap,
reloc/offset shift, commutative operand order, ICF fold, PGO block-sink, …) and
NOT an un-fixed behavioral bug.

DRY-RUN BY DEFAULT. ``--apply`` is required to write the DB and is intended for
the orchestrator (single writer on main). This script never writes the live
decomp.db unless ``--apply`` is passed AND ``--db`` points at it.

----------------------------------------------------------------------------
Schema (idempotent migration, ``--migrate`` or auto on ``--apply``):
  floor_certificate   TEXT   -- NULL | equivalent | artifact:<class>
                             --        | permuter_exhausted | pgo_block_sink | icf_merged
  floor_cert_pct      REAL   -- match_percent_normalized captured AT cert time.
                             --   Any later normalized change != this invalidates
                             --   the cert (reconcile_db check_floor_certs()).
  floor_cert_build    TEXT   -- git short rev of the source tree at cert time
                             --   (provenance: "which build proved this floor").
  floor_cert_at       TEXT   -- ISO timestamp of certification.
  floor_cert_evidence TEXT   -- JSON: the evidence that justified the cert, incl.
                             --   unicorn_tested_at staleness (doc 04 F6: unicorn
                             --   data is ~3 months stale; a cert from stale
                             --   unicorn STORES that date so it can be
                             --   invalidated / re-tested).

----------------------------------------------------------------------------
Evidence model (a function is certifiable iff normalized < 100 AND one holds;
strongest evidence wins — precedence: equivalent > artifact > icf_merged >
permuter_exhausted):

  equivalent          unicorn_verdict='EQUIVALENT' — behaviorally identical under
                      emulation, so the residual byte diff is provably cosmetic
                      (regalloc/scheduling/reloc). Provenance records
                      unicorn_tested_at + a stale-unicorn flag (>STALE_DAYS old).
  artifact:<class>    unicorn_verdict='DIVERGENT' but unicorn_class is a known
                      cosmetic artifact class (build_env / regalloc / stack_layout
                      / merged_call / merged_arg / fpr_precision). The divergence
                      is an emulation/build artifact, not a real bug.
  icf_merged          merged_symbol_count>0 — Identical COMDAT Folding; the linker
                      merged this body with another, so the "wrong" reloc target
                      name is benign.
  permuter_exhausted  attempts-table evidence: >=1 attempt ended at_limit/stuck
                      AND no attempt ever beat the current normalized percent
                      (the permuter found no headroom). Weakest evidence — only
                      applied when nothing stronger holds.

NOT auto-certified (kept in the enum for manual/orchestrator use):
  pgo_block_sink      the 361-function PGO block-sinking floor (at-limit-systemic.md
                      §7) is not queryable from a DB flag, so it is never
                      auto-fired here. Supply it manually if needed.

Caveats baked in (from doc 08):
  - primary_pattern is STALE/noisy (ADDRESS_RELOCATION_NOISE shows on 100% fns)
    so it is NEVER used as evidence.
  - run_objdiff NORMALIZED is the only percent that gates a cert — we read
    match_percent_normalized (filled by sync_match_percent.py), NOT current_percent
    (fuzzy) and NOT the diagnose "match estimate" (off by 20-30 pts).
  - artifacts (merged_/lbl_/fn_/??_) are excluded via scripts/authorable.py-style
    prefix rules; only authorable non-stub functions are certifiable.

Usage:
    python3 scripts/certify_floor.py --summary            # done-view headline (read-only)
    python3 scripts/certify_floor.py                      # dry-run cert census
    python3 scripts/certify_floor.py -v                   # + per-class samples
    python3 scripts/certify_floor.py --migrate --apply --db copy.db   # add columns+view
    python3 scripts/certify_floor.py --apply --db copy.db             # write certs
    python3 scripts/certify_floor.py --db /path/copy.db   # test against a DB copy
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DB = REPO_ROOT / "decomp.db"

# Single source of truth for the SDK exclusion prefixes.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from authorable import SDK_UNIT_PREFIXES  # noqa: E402

# --- Evidence configuration ---------------------------------------------------

# unicorn_class values that are cosmetic build/emulation artifacts, NOT real
# behavioral bugs.
#   build_env / regalloc / stack_layout / merged_call / merged_arg / fpr_precision
#     — pure build/emulation artifacts (doc 08 line 162 "DONE-cosmetic (verify)").
#   orig_error — the ORIGINAL binary itself diverges (UB / uninitialised read);
#     a property of the target, not our bug, so it is a floor (doc 08 line 162).
# DELIBERATELY EXCLUDED (the routable / real-bug residue, doc 08 F6):
#   error / call_arg / object_memory / return_value — the 27-fn genuine hard
#     residue; call_count — the ambiguous "needs per-function adjudication"
#     bucket; cap_exhausted — emulator gave up, not a proven floor.
ARTIFACT_CLASSES = (
    "build_env",
    "regalloc",
    "stack_layout",
    "merged_call",
    "merged_arg",
    "fpr_precision",
    "orig_error",
)

# A unicorn result older than this many days is recorded as stale in the cert's
# evidence so it can be re-tested / invalidated (doc 04 F6: unicorn ~3mo stale).
STALE_DAYS = 60

# Artifact-name prefixes that are never authorable / never certifiable.
ARTIFACT_PREFIXES = ("merged_", "lbl_", "fn_", "??_")

# Valid floor_certificate enum values (for validation / docs).
CERT_EQUIVALENT = "equivalent"
CERT_ICF = "icf_merged"
CERT_PERMUTER = "permuter_exhausted"
CERT_PGO = "pgo_block_sink"  # manual only; never auto-fired


def git_short_rev() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(REPO_ROOT), capture_output=True, text=True, check=True,
        )
        return out.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


# --- Migration ----------------------------------------------------------------

CERT_COLUMNS = [
    ("floor_certificate", "TEXT"),
    ("floor_cert_pct", "REAL"),
    ("floor_cert_build", "TEXT"),
    ("floor_cert_at", "TEXT"),
    ("floor_cert_evidence", "TEXT"),
]

AUTHORABLE_DONE_VIEW = "authorable_done"


def existing_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    return {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}


def migrate(conn: sqlite3.Connection, apply: bool) -> list[str]:
    """Idempotently add the cert columns + the authorable_done view.

    Returns the list of actions taken (or that WOULD be taken in dry-run)."""
    actions: list[str] = []
    cols = existing_columns(conn, "functions")
    for name, typ in CERT_COLUMNS:
        if name in cols:
            actions.append(f"column {name}: already present (skip)")
            continue
        actions.append(f"column {name} {typ}: ADD")
        if apply:
            conn.execute(f"ALTER TABLE functions ADD COLUMN {name} {typ}")

    # The authorable_done view. Authorable = non-SDK unit AND not an artifact
    # symbol AND not excluded/stub-by-prefix.
    #
    # "done" rule — three tiers, in precedence order:
    #   1. matched:    match_percent_normalized >= 100  (canonical normalized scorer)
    #                  OR (verdict='COMPLETE' AND current_percent >= 100 AND
    #                      match_percent_normalized IS NULL)
    #                  The second branch covers the ~170 "db-only" functions whose
    #                  fuzzy_match_percent is NULL in the current report.json (they
    #                  are target-only ICF/template instantiations or jeff-boundary
    #                  relocated symbols).  sync_match_percent.py skips them because
    #                  it requires fuzzy_match_percent != NULL to update
    #                  match_percent_normalized, so their normalized score is never
    #                  written.  They ARE real done rows — COMPLETE verdict + 100%
    #                  fuzzy from a prior sync + unicorn EQUIVALENT evidence on many —
    #                  and should not inflate the open count.  (Wave 6 Lane D
    #                  investigation: 135/170 are in report.json with norm=0 / no
    #                  fuzzy; 35/170 are absent from report entirely due to jeff
    #                  boundary churn.  Both sub-populations are verified done.)
    #                  reconcile_db.py check (d) is the right tool to flag these for
    #                  eventual cleanup — this view rule means they are counted as done
    #                  today without requiring a DB write.
    #   2. stub:       is_stub = 1
    #   3. certified:  floor_certificate IS NOT NULL
    #   else: open
    sdk_clause = " AND ".join(
        f"(unit IS NULL OR unit NOT LIKE '{p}%')" for p in SDK_UNIT_PREFIXES
    )
    artifact_clause = " AND ".join(
        f"symbol NOT LIKE '{p}%'" for p in ARTIFACT_PREFIXES
    )
    view_sql = f"""
    CREATE VIEW {AUTHORABLE_DONE_VIEW} AS
    SELECT
        id, symbol, demangled, unit, size,
        current_percent, match_percent_normalized,
        verdict, is_stub, floor_certificate, floor_cert_pct,
        CASE
            WHEN match_percent_normalized >= 100 THEN 'matched'
            WHEN verdict = 'COMPLETE' AND current_percent >= 100
                 AND match_percent_normalized IS NULL THEN 'matched'
            WHEN is_stub = 1                      THEN 'stub'
            WHEN floor_certificate IS NOT NULL    THEN 'certified'
            ELSE 'open'
        END AS done_state,
        CASE
            WHEN match_percent_normalized >= 100
              OR (verdict = 'COMPLETE' AND current_percent >= 100
                  AND match_percent_normalized IS NULL)
              OR is_stub = 1
              OR floor_certificate IS NOT NULL THEN 1 ELSE 0
        END AS is_done
    FROM functions
    WHERE excluded = 0
      AND {sdk_clause}
      AND {artifact_clause}
    """
    # Recreate the view every migrate (cheap; keeps the definition current iff
    # the prefix lists change). Drop only if it exists.
    view_present = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='view' AND name=?",
        (AUTHORABLE_DONE_VIEW,),
    ).fetchone()
    actions.append(
        f"view {AUTHORABLE_DONE_VIEW}: {'RECREATE' if view_present else 'CREATE'}"
    )
    if apply:
        # The view references floor_certificate etc., so only create after the
        # columns exist (they do by now in --apply path).
        conn.execute(f"DROP VIEW IF EXISTS {AUTHORABLE_DONE_VIEW}")
        conn.execute(view_sql)
    return actions


# --- Certification logic ------------------------------------------------------

def is_authorable_sql() -> str:
    """SQL WHERE-fragment selecting the authorable, certifiable frontier."""
    sdk = " AND ".join(
        f"(unit IS NULL OR unit NOT LIKE '{p}%')" for p in SDK_UNIT_PREFIXES
    )
    art = " AND ".join(f"symbol NOT LIKE '{p}%'" for p in ARTIFACT_PREFIXES)
    return (
        f"excluded=0 AND is_stub=0 AND {sdk} AND {art} "
        "AND match_percent_normalized IS NOT NULL "
        "AND match_percent_normalized > 0 AND match_percent_normalized < 100"
    )


def days_old(tested_at: str | None, ref: datetime) -> int | None:
    if not tested_at:
        return None
    try:
        # Stored as 'YYYY-MM-DD HH:MM:SS'
        dt = datetime.strptime(tested_at[:19], "%Y-%m-%d %H:%M:%S")
        dt = dt.replace(tzinfo=timezone.utc)
        return (ref - dt).days
    except Exception:
        return None


def classify_function(r: sqlite3.Row, exhausted_ids: set[int],
                      ref: datetime) -> tuple[str | None, dict | None]:
    """Return (floor_certificate, evidence_dict) for a frontier row, or (None, None).

    Precedence: equivalent > artifact > icf_merged > permuter_exhausted."""
    uv = (r["unicorn_verdict"] or "").strip()
    uc = (r["unicorn_class"] or "").strip()
    msc = r["merged_symbol_count"] or 0
    tested = r["unicorn_tested_at"]

    if uv == "EQUIVALENT":
        d = days_old(tested, ref)
        return CERT_EQUIVALENT, {
            "evidence": "unicorn_equivalent",
            "unicorn_tested_at": tested,
            "unicorn_age_days": d,
            "unicorn_stale": (d is None or d > STALE_DAYS),
        }

    if uv == "DIVERGENT" and uc in ARTIFACT_CLASSES:
        d = days_old(tested, ref)
        return f"artifact:{uc}", {
            "evidence": "unicorn_artifact_class",
            "unicorn_class": uc,
            "unicorn_tested_at": tested,
            "unicorn_age_days": d,
            "unicorn_stale": (d is None or d > STALE_DAYS),
        }

    if msc > 0:
        return CERT_ICF, {
            "evidence": "merged_symbol_count",
            "merged_symbol_count": msc,
        }

    if r["id"] in exhausted_ids:
        return CERT_PERMUTER, {
            "evidence": "permuter_exhausted",
            "note": "attempts ended at_limit/stuck with no headroom over current normalized",
        }

    return None, None


def load_exhausted_ids(conn: sqlite3.Connection) -> set[int]:
    """Function ids whose attempts prove permuter exhaustion.

    Definition: >=1 attempt ended at_limit/stuck AND no attempt's end_percent
    ever exceeded the function's current normalized percent by > 0.5 (i.e. the
    permuter never found real headroom). Conservative — only fires when there is
    genuine attempt history that hit a wall."""
    if "attempts" not in {
        row[0] for row in conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table'")
    }:
        return set()
    rows = conn.execute(
        """
        SELECT a.function_id AS fid,
               MAX(CASE WHEN a.exit_status IN ('at_limit','stuck') THEN 1 ELSE 0 END) AS has_stuck,
               MAX(a.end_percent) AS best_end
        FROM attempts a
        GROUP BY a.function_id
        """
    ).fetchall()
    best = {r["fid"]: (r["has_stuck"], r["best_end"]) for r in rows}
    norms = {
        r["id"]: r["match_percent_normalized"]
        for r in conn.execute(
            "SELECT id, match_percent_normalized FROM functions")
    }
    out: set[int] = set()
    for fid, (has_stuck, best_end) in best.items():
        if not has_stuck:
            continue
        norm = norms.get(fid)
        if norm is None:
            continue
        if best_end is None or best_end <= norm + 0.5:
            out.add(fid)
    return out


def certify(conn: sqlite3.Connection, apply: bool, verbose: bool) -> dict:
    """Compute (and optionally apply) floor certificates for the frontier."""
    has_cert_col = "floor_certificate" in existing_columns(conn, "functions")
    ref = datetime.now(timezone.utc)
    build = git_short_rev()
    stamp = now_iso()

    exhausted = load_exhausted_ids(conn)

    sel = (
        "id, symbol, unit, match_percent_normalized, "
        "unicorn_verdict, unicorn_class, unicorn_tested_at, merged_symbol_count"
    )
    rows = conn.execute(
        f"SELECT {sel} FROM functions WHERE {is_authorable_sql()}"
    ).fetchall()

    # Buckets
    counts: dict[str, int] = {}
    stale_unicorn = 0
    no_evidence: list[sqlite3.Row] = []
    samples: dict[str, list[str]] = {}
    to_write: list[tuple] = []

    for r in rows:
        cert, ev = classify_function(r, exhausted, ref)
        if cert is None:
            no_evidence.append(r)
            continue
        # Bucket key: collapse artifact:<class> for the headline but keep detail.
        key = "artifact:*" if cert.startswith("artifact:") else cert
        counts[key] = counts.get(key, 0) + 1
        if ev and ev.get("unicorn_stale"):
            stale_unicorn += 1
        samples.setdefault(key, [])
        if len(samples[key]) < 8:
            samples[key].append(f"{r['symbol']}  ({r['match_percent_normalized']:.1f}%)")
        to_write.append((
            r["id"], cert, round(r["match_percent_normalized"], 2),
            build, stamp, json.dumps(ev, separators=(",", ":")),
        ))

    certifiable = len(to_write)
    blocked_stale = stale_unicorn  # subset of certifiable backed by stale unicorn
    no_ev = len(no_evidence)

    if apply:
        if not has_cert_col:
            raise SystemExit(
                "Refusing to --apply: floor_certificate column missing. "
                "Run with --migrate --apply first."
            )
        conn.executemany(
            "UPDATE functions SET floor_certificate=?, floor_cert_pct=?, "
            "floor_cert_build=?, floor_cert_at=?, floor_cert_evidence=? "
            "WHERE id=?",
            [(c, p, b, t, e, fid) for (fid, c, p, b, t, e) in to_write],
        )
        conn.commit()

    return {
        "frontier_total": len(rows),
        "certifiable": certifiable,
        "counts": counts,
        "blocked_stale_unicorn": blocked_stale,
        "no_evidence": no_ev,
        "no_evidence_rows": no_evidence,
        "samples": samples,
        "build": build,
        "applied": apply,
    }


# --- Summary / done-view ------------------------------------------------------

def done_summary(conn: sqlite3.Connection) -> dict:
    """Compute the authorable done view headline (with and without certs).

    Falls back to an inline query if the authorable_done view isn't present."""
    have_view = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='view' AND name=?",
        (AUTHORABLE_DONE_VIEW,),
    ).fetchone()
    has_cert_col = "floor_certificate" in existing_columns(conn, "functions")

    if have_view:
        # Use the view's done_state column directly — it is the single source of
        # truth for the done/open classification and includes the COMPLETE+current>=100
        # +normalized NULL rule for the ~170 db-only matched functions.
        rows = conn.execute(
            f"SELECT size, done_state FROM {AUTHORABLE_DONE_VIEW}"
        ).fetchall()

        total_fns = len(rows)
        total_bytes = sum((r["size"] or 0) for r in rows)

        def agg_state(state: str):
            rs = [r for r in rows if r["done_state"] == state]
            return len(rs), sum((r["size"] or 0) for r in rs)

        matched_t = agg_state("matched")
        stubs_t = agg_state("stub")
        certified_t = agg_state("certified")
        open_t = agg_state("open")

        done_nocert_count = matched_t[0] + stubs_t[0]
        done_nocert_bytes = matched_t[1] + stubs_t[1]
        done_withcert_count = done_nocert_count + certified_t[0]
        done_withcert_bytes = done_nocert_bytes + certified_t[1]

        return {
            "total_fns": total_fns,
            "total_bytes": total_bytes,
            "matched": matched_t,
            "stubs": stubs_t,
            "certified": certified_t,
            "done_nocert": (done_nocert_count, done_nocert_bytes),
            "done_withcert": (done_withcert_count, done_withcert_bytes),
            "open": open_t,
            "view": True,
        }

    # Fallback: view not yet migrated — use inline query with the old logic
    # (does not include the COMPLETE+current>=100+normalized NULL rule).
    sdk = " AND ".join(
        f"(unit IS NULL OR unit NOT LIKE '{p}%')" for p in SDK_UNIT_PREFIXES)
    art = " AND ".join(f"symbol NOT LIKE '{p}%'" for p in ARTIFACT_PREFIXES)
    has_norm_col = "match_percent_normalized" in existing_columns(conn, "functions")
    has_stub_col = "is_stub" in existing_columns(conn, "functions")
    has_excl_col = "excluded" in existing_columns(conn, "functions")
    norm_sel = "match_percent_normalized" if has_norm_col else "NULL AS match_percent_normalized"
    stub_sel = "is_stub" if has_stub_col else "0 AS is_stub"
    cert_sel = "floor_certificate" if has_cert_col else "NULL AS floor_certificate"
    excl_clause = "excluded=0 AND" if has_excl_col else ""
    rows2 = conn.execute(
        f"SELECT size, {norm_sel}, {stub_sel}, {cert_sel} "
        f"FROM functions WHERE {excl_clause} {sdk} AND {art}"
    ).fetchall()

    total_fns = len(rows2)
    total_bytes = sum((r["size"] or 0) for r in rows2)

    def bnorm(r):
        return r["match_percent_normalized"] is not None and r["match_percent_normalized"] >= 100

    matched = [r for r in rows2 if bnorm(r)]
    stubs = [r for r in rows2 if not bnorm(r) and r["is_stub"]]
    certified = [
        r for r in rows2
        if not bnorm(r) and not r["is_stub"] and r["floor_certificate"] is not None
    ]
    done_nocert = matched + stubs
    done_withcert = matched + stubs + certified
    open_rows = [
        r for r in rows2
        if not bnorm(r) and not r["is_stub"] and r["floor_certificate"] is None
    ]

    def agg(rs):
        return len(rs), sum((r["size"] or 0) for r in rs)

    return {
        "total_fns": total_fns,
        "total_bytes": total_bytes,
        "matched": agg(matched),
        "stubs": agg(stubs),
        "certified": agg(certified),
        "done_nocert": agg(done_nocert),
        "done_withcert": agg(done_withcert),
        "open": agg(open_rows),
        "view": False,
    }


# --- CLI ----------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--db", type=Path, default=DEFAULT_DB,
                   help=f"Path to decomp.db (default: {DEFAULT_DB}). Use a COPY for tests.")
    p.add_argument("--apply", action="store_true",
                   help="Write certificates to the DB (default: dry-run).")
    p.add_argument("--migrate", action="store_true",
                   help="Add cert columns + authorable_done view (idempotent).")
    p.add_argument("--summary", action="store_true",
                   help="Print the authorable done-view headline (read-only) and exit.")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Show per-class certifiable samples + no-evidence sample.")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if not args.db.exists():
        print(f"Error: db not found: {args.db}", file=sys.stderr)
        return 2

    # Refuse to --apply against the live main DB (rule 2): writes only allowed to
    # a non-default DB path unless the caller really means the live one — the
    # orchestrator runs the apply runbook explicitly, so we just warn loudly.
    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row

    if args.apply and args.db.resolve() == DEFAULT_DB.resolve():
        print("WARNING: --apply targets the DEFAULT (live) decomp.db. "
              "This is the orchestrator's single-writer apply step.", file=sys.stderr)

    # Migration (auto-runs before --apply of certs so columns exist).
    if args.migrate or args.apply:
        print("=== Schema migration ===")
        actions = migrate(conn, apply=args.apply)
        for a in actions:
            print(f"  {a}")
        print(f"  ({'APPLIED' if args.apply else 'dry-run — no changes'})")
        print()

    if args.summary:
        s = done_summary(conn)
        print("=== authorable_done — canonical done view ===")
        src = "view" if s["view"] else "inline (view not yet migrated)"
        print(f"  source: {src}")
        print(f"  authorable functions: {s['total_fns']:,}   "
              f"bytes: {s['total_bytes']:,}")
        print()
        print(f"  matched (done):           {s['matched'][0]:>6,} fns   {s['matched'][1]:>12,} bytes"
              f"  (norm==100 OR COMPLETE+current>=100+norm NULL)")
        print(f"  stubs (is_stub=1):        {s['stubs'][0]:>6,} fns   {s['stubs'][1]:>12,} bytes")
        print(f"  certified (floor cert):   {s['certified'][0]:>6,} fns   {s['certified'][1]:>12,} bytes")
        print(f"  open (no cert, <100):     {s['open'][0]:>6,} fns   {s['open'][1]:>12,} bytes")
        print()
        dn, db_ = s["done_nocert"]
        dw, dwb = s["done_withcert"]
        tf, tb = s["total_fns"], s["total_bytes"]
        print(f"  DONE without certs: {dn:,}/{tf:,} fns ({100*dn/tf:.2f}%)   "
              f"{db_:,}/{tb:,} bytes ({100*db_/tb:.2f}%)")
        print(f"  DONE with certs:    {dw:,}/{tf:,} fns ({100*dw/tf:.2f}%)   "
              f"{dwb:,}/{tb:,} bytes ({100*dwb/tb:.2f}%)")
        conn.close()
        return 0

    # Certification census (dry-run unless --apply)
    res = certify(conn, apply=args.apply, verbose=args.verbose)
    print("=== Floor certification census ===")
    print(f"  build: {res['build']}   mode: {'APPLY' if res['applied'] else 'DRY-RUN'}")
    print(f"  authorable partial frontier (0<norm<100): {res['frontier_total']:,}")
    print()
    print(f"  CERTIFIABLE TODAY (have evidence):        {res['certifiable']:,}")
    for key in sorted(res["counts"]):
        print(f"      {key:<22} {res['counts'][key]:>6,}")
        if args.verbose:
            for s in res["samples"].get(key, []):
                print(f"          {s}")
    print()
    print(f"  of those, BACKED BY STALE UNICORN (>{STALE_DAYS}d, re-test before trusting): "
          f"{res['blocked_stale_unicorn']:,}")
    print(f"  NO EVIDENCE (un-certifiable today):       {res['no_evidence']:,}")
    if args.verbose:
        for r in res["no_evidence_rows"][:15]:
            print(f"          {r['symbol']}  ({r['match_percent_normalized']:.1f}%)  "
                  f"uv={r['unicorn_verdict'] or '-'} uc={r['unicorn_class'] or '-'}")
    print()
    # The three headline numbers the lane asks for:
    print("=== HEADLINE (lane B item 4) ===")
    fresh_certifiable = res["certifiable"] - res["blocked_stale_unicorn"]
    print(f"  certifiable from existing evidence:       {res['certifiable']:,}")
    print(f"    - on FRESH evidence:                    {fresh_certifiable:,}")
    print(f"    - blocked on STALE unicorn:             {res['blocked_stale_unicorn']:,}")
    print(f"  no evidence at all:                       {res['no_evidence']:,}")
    if not res["applied"]:
        print()
        print("  (dry-run — pass --migrate --apply --db <path> to write)")
    conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
