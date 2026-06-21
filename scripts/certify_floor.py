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
  native_divergence   our source is provably NATIVE-CORRECT — it matches the
                      milo-native-engine's required signature/layout (override
                      const-ness, overloaded-virtual hoisting order, sub-object
                      vtable shape) — but the PPC target diverges in a way we
                      CANNOT match without breaking the native/web build. A
                      legitimate, PERMANENT floor distinct from regalloc/
                      equivalent/permuter: the residual byte diff is a forced
                      cross-platform trade, not an un-fixed bug. There is no DB
                      flag for it (it requires reading the engine's required
                      signature), so it is MANUAL-ONLY and never auto-fired.

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
from authorable import SDK_UNIT_PREFIXES, is_authorable  # noqa: E402

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


def like_prefix_clause(column: str, prefix: str, negate: bool = True) -> str:
    """LIKE-prefix SQL with wildcards in the prefix ESCAPED.

    SQL LIKE treats '_' as a single-char wildcard, so a naive
    ``symbol NOT LIKE '??_%'`` excludes EVERY '??'-prefixed symbol —
    ctors (??0), dtors (??1), operators (??4...) — not just the literal
    '??_' artifact prefix. That bug hid 6,835 authorable fns (~1.0 MB,
    112 of them open) from the authorable_done view and the cert census
    until 2026-06-11."""
    esc = prefix.replace("~", "~~").replace("_", "~_").replace("%", "~%")
    op = "NOT LIKE" if negate else "LIKE"
    return f"{column} {op} '{esc}%' ESCAPE '~'"

# Valid floor_certificate enum values (for validation / docs).
CERT_EQUIVALENT = "equivalent"
CERT_ICF = "icf_merged"
CERT_PERMUTER = "permuter_exhausted"
CERT_PGO = "pgo_block_sink"  # manual only; never auto-fired
CERT_NATIVE_DIVERGENCE = "native_divergence"  # manual only; never auto-fired

# Cert classes that may ONLY be supplied via --manual-file (no DB flag exists to
# auto-fire them; they require evidence proven outside the DB).
MANUAL_ONLY_CERTS = frozenset({CERT_PGO, CERT_NATIVE_DIVERGENCE})

# Every accepted floor_certificate value. The four auto classes (equivalent,
# icf_merged, permuter_exhausted, and artifact:<class> for each ARTIFACT_CLASSES)
# plus the manual-only classes. manual_certify() validates the supplied `cert`
# string against this set so a typo can't write an un-queryable certificate.
VALID_CERT_CLASSES = frozenset(
    {CERT_EQUIVALENT, CERT_ICF, CERT_PERMUTER}
    | MANUAL_ONLY_CERTS
    | {f"artifact:{c}" for c in ARTIFACT_CLASSES}
)


def is_valid_cert(cert: str) -> bool:
    """True iff `cert` is an accepted floor_certificate enum value."""
    return cert in VALID_CERT_CLASSES


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
        f"(unit IS NULL OR {like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES
    )
    artifact_clause = " AND ".join(
        like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES
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
        f"(unit IS NULL OR {like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES
    )
    art = " AND ".join(like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES)
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


def manual_certify(conn: sqlite3.Connection, path: Path, apply: bool) -> int:
    """Write orchestrator-supplied certificates from a backlog JSON file.

    This is the manual path the evidence model reserves for floors proven
    outside the DB (worktree permuter runs, per-wave diagnosis docs) whose
    evidence never landed in the attempts table. Entry format:

        [{"symbol_like": "?CheckBSPTree@@%",      -- must resolve to EXACTLY 1 row
          "unit_like": "%math/Geo%",              -- optional disambiguator
          "cert": "permuter_exhausted",           -- or artifact:<class> /
                                                  --   pgo_block_sink /
                                                  --   native_divergence etc.
          "expect_pct": 99.0,                     -- doc-recorded normalized pct
          "tolerance": 2.0,                       -- max |norm - expect_pct| (default 2.0)
          "force": false,                         -- write despite pct drift
          "evidence": {"source_doc": "...", "diagnosis": "..."}}, ...]

    Rules: validates `cert` against VALID_CERT_CLASSES (a typo can't write an
    un-queryable certificate); never overwrites an existing cert; refuses
    ambiguous/missing symbols; refuses norm>=100 / NULL rows; refuses norm<=0
    EXCEPT for native_divergence (which legitimately sits at 0% — its target
    symbol has no base counterpart because the engine forced a different
    signature — and is also exempt from the is_stub=0 filter for the same
    reason); records the CURRENT normalized pct (reconcile_db check (e)
    contract), not expect_pct.
    Returns the number of entries that failed to resolve cleanly."""
    entries = json.loads(Path(path).read_text())
    build = git_short_rev()
    stamp = now_iso()
    sdk = " AND ".join(
        f"(unit IS NULL OR {like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES
    )
    art = " AND ".join(like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES)
    to_write: list[tuple] = []
    problems = 0
    print(f"=== Manual certification ({path}) ===")
    print(f"  build: {build}   mode: {'APPLY' if apply else 'DRY-RUN'}")
    for e in entries:
        pat = e["symbol_like"]
        cert = e["cert"]
        if not is_valid_cert(cert):
            print(f"  ERROR  {pat}: invalid cert class {cert!r} "
                  f"(valid: {', '.join(sorted(VALID_CERT_CLASSES))})")
            problems += 1
            continue
        # native_divergence target-only symbols are flagged is_stub=1 by the
        # byte-size heuristic (base_size==0 because our build emits the OTHER
        # mangled name) even though the function IS implemented. The stub flag is
        # itself the symptom of the divergence, so this class alone may cert an
        # is_stub=1 row; every other class still requires is_stub=0.
        stub_clause = "" if cert == CERT_NATIVE_DIVERGENCE else " AND is_stub=0"
        q = (
            "SELECT id, symbol, unit, match_percent_normalized, floor_certificate "
            f"FROM functions WHERE symbol LIKE ? AND excluded=0{stub_clause} "
            f"AND {sdk} AND {art}"
        )
        params: list = [pat]
        if e.get("unit_like"):
            q += " AND unit LIKE ?"
            params.append(e["unit_like"])
        rows = conn.execute(q, params).fetchall()
        if len(rows) != 1:
            names = ", ".join(r["symbol"][:60] for r in rows[:4])
            print(f"  ERROR  {pat}: {len(rows)} matches ({names})")
            problems += 1
            continue
        r = rows[0]
        norm = r["match_percent_normalized"]
        is_nd = cert == CERT_NATIVE_DIVERGENCE
        # native_divergence is the one class that legitimately sits at the
        # symbol-mismatch floor: when the engine forces a different signature
        # (e.g. const base), our build emits a DIFFERENTLY-MANGLED symbol, so the
        # PPC target's symbol has NO base counterpart. objdiff therefore scores
        # it 0% — and report.json leaves fuzzy NULL so sync never writes a
        # normalized score either, leaving norm IS NULL. BOTH norm==0 and
        # norm IS NULL are the same valid symbol-mismatch floor for this class
        # (and only this class). Its captured floor_cert_pct is pinned to 0.0
        # (reconcile_db check (e) compares against this, and a NULL norm has no
        # number to compare). Every OTHER class still requires a genuine partial
        # match (0 < norm < 100) — NULL/0 is "not certifiable" for them.
        # A 100%-matched function is NEVER a floor, so norm>=100 is refused for
        # ALL classes.
        if is_nd:
            bad_low = (norm is not None) and (norm < 0)
            bad_high = (norm is not None) and (norm >= 100)
        else:
            bad_low = (norm is None) or (norm <= 0)
            bad_high = (norm is not None) and (norm >= 100)
        if bad_low or bad_high:
            print(f"  SKIP   {r['symbol']}: not certifiable (norm={norm}, cert={cert})")
            problems += 1
            continue
        if r["floor_certificate"]:
            print(f"  KEEP   {r['symbol']}: already certified "
                  f"({r['floor_certificate']})")
            continue
        exp = e.get("expect_pct")
        tol = e.get("tolerance", 2.0)
        # native_divergence at the symbol-mismatch floor has no comparable norm
        # (NULL/0), so skip the drift check for it; pin its cert_pct to 0.0.
        cert_pct = round(norm, 2) if norm is not None else 0.0
        if (not is_nd) and exp is not None and abs(norm - exp) > tol \
                and not e.get("force"):
            print(f"  DRIFT  {r['symbol']}: norm {norm:.1f} vs doc {exp:.1f} "
                  f"(>±{tol}) — re-diagnose or set force:true")
            problems += 1
            continue
        ev = dict(e.get("evidence", {}))
        ev.setdefault("evidence", "manual_backlog")
        if exp is not None:
            ev["doc_pct"] = exp
        to_write.append((
            cert, cert_pct, build, stamp,
            json.dumps(ev, separators=(",", ":")), r["id"],
        ))
        print(f"  {'WRITE' if apply else 'DRY'}  {r['symbol']}  "
              f"{cert} @ {cert_pct:.1f}%  [{r['unit'] or '-'}]"
              + ("  (symbol-mismatch floor: norm IS NULL)"
                 if is_nd and norm is None else ""))
    if apply and to_write:
        if "floor_certificate" not in existing_columns(conn, "functions"):
            raise SystemExit("Refusing to --apply: floor_certificate column "
                             "missing. Run with --migrate --apply first.")
        conn.executemany(
            "UPDATE functions SET floor_certificate=?, floor_cert_pct=?, "
            "floor_cert_build=?, floor_cert_at=?, floor_cert_evidence=? "
            "WHERE id=?",
            to_write,
        )
        conn.commit()
    print(f"  {len(to_write)} cert(s) {'written' if apply else 'resolvable'}, "
          f"{problems} problem(s)")
    return problems


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
        f"(unit IS NULL OR {like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES)
    art = " AND ".join(like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES)
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


# --- Two-path denominator self-check ------------------------------------------

# Artifact symbol prefixes whose '_' must be treated LITERALLY (these match the
# SQL ARTIFACT_PREFIXES). The Python path below uses str.startswith — a genuinely
# independent implementation from the SQL LIKE path, so a future SQL filter bug
# (like wave-9's unescaped '??_%' wildcard) makes the two totals DISAGREE instead
# of silently undercounting in lockstep.
_PY_ARTIFACT_PREFIXES = ARTIFACT_PREFIXES  # ("merged_", "lbl_", "fn_", "??_")


def _py_is_authorable_row(unit: str | None, symbol: str | None,
                          excluded: int | None) -> bool:
    """Pure-Python predicate mirroring the authorable_done view's WHERE.

    Deliberately uses str.startswith (NOT SQL LIKE) so it can never share a
    wildcard-escaping bug with the SQL path."""
    if excluded:
        return False
    if unit is not None and not is_authorable(unit):
        return False
    sym = symbol or ""
    if any(sym.startswith(p) for p in _PY_ARTIFACT_PREFIXES):
        return False
    return True


def denominator_self_check(conn: sqlite3.Connection) -> dict:
    """Compute the authorable function/byte denominator TWO independent ways and
    report whether they agree.

    Path A (SQL):    the authorable_done view's WHERE (the SQL LIKE/ESCAPE path).
                     Falls back to the inline is_authorable_sql() fragment when
                     the view is not migrated.
    Path B (Python): SELECT every row, filter with str.startswith in Python.

    Returns a dict with both totals and an ``agree`` bool. Read-only; no writes.

    This is the guard against the wave-9 class of bug: a filter that silently
    drops authorable rows (the '??_%' wildcard hid 6,835 fns / ~1.0 MB) would now
    make A and B diverge and fail LOUDLY."""
    # --- Path A: SQL view WHERE (or inline fallback) ---
    have_view = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='view' AND name=?",
        (AUTHORABLE_DONE_VIEW,),
    ).fetchone()
    if have_view:
        row = conn.execute(
            f"SELECT COUNT(*), COALESCE(SUM(size),0) FROM {AUTHORABLE_DONE_VIEW}"
        ).fetchone()
        a_fns, a_bytes = int(row[0]), int(row[1])
        a_src = "authorable_done view"
    else:
        row = conn.execute(
            f"SELECT COUNT(*), COALESCE(SUM(size),0) "
            f"FROM functions WHERE {is_authorable_sql_denominator(conn)}"
        ).fetchone()
        a_fns, a_bytes = int(row[0]), int(row[1])
        a_src = "is_authorable_sql() inline fragment"

    # --- Path B: Python startswith filter over every row ---
    b_fns = 0
    b_bytes = 0
    has_excl = "excluded" in existing_columns(conn, "functions")
    excl_sel = "excluded" if has_excl else "0 AS excluded"
    for r in conn.execute(f"SELECT unit, symbol, size, {excl_sel} FROM functions"):
        if _py_is_authorable_row(r["unit"], r["symbol"], r["excluded"]):
            b_fns += 1
            b_bytes += (r["size"] or 0)

    agree = (a_fns == b_fns) and (a_bytes == b_bytes)
    return {
        "a_fns": a_fns, "a_bytes": a_bytes, "a_src": a_src,
        "b_fns": b_fns, "b_bytes": b_bytes,
        "agree": agree,
        "delta_fns": a_fns - b_fns,
        "delta_bytes": a_bytes - b_bytes,
    }


def is_authorable_sql_denominator(conn: sqlite3.Connection) -> str:
    """The DENOMINATOR WHERE (authorable, regardless of done state) for the inline
    Path-A fallback — mirrors the view's WHERE, NOT the partial-frontier filter in
    is_authorable_sql() (which also bounds 0<norm<100)."""
    sdk = " AND ".join(
        f"(unit IS NULL OR {like_prefix_clause('unit', p)})" for p in SDK_UNIT_PREFIXES
    )
    art = " AND ".join(like_prefix_clause("symbol", p) for p in ARTIFACT_PREFIXES)
    has_excl = "excluded" in existing_columns(conn, "functions")
    excl = "excluded=0 AND " if has_excl else ""
    return f"{excl}{sdk} AND {art}"


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
    p.add_argument("--check-denominator", action="store_true",
                   help="Compute the authorable fn/byte denominator two independent "
                        "ways (SQL view WHERE vs Python startswith) and exit nonzero "
                        "if they disagree. Read-only; no DB writes.")
    p.add_argument("--manual-file", type=Path, metavar="JSON",
                   help="Write orchestrator-supplied certs from a backlog JSON "
                        "(see manual_certify docstring). Skips the auto census.")
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

    if args.check_denominator:
        chk = denominator_self_check(conn)
        print("=== denominator two-path self-check (read-only) ===")
        print(f"  Path A (SQL: {chk['a_src']}): {chk['a_fns']:,} fns   {chk['a_bytes']:,} bytes")
        print(f"  Path B (Python startswith):        {chk['b_fns']:,} fns   {chk['b_bytes']:,} bytes")
        if chk["agree"]:
            print("  AGREE — authorable denominator is consistent across both paths.")
            conn.close()
            return 0
        print(f"  !! DISAGREE by {chk['delta_fns']:+,} fns / {chk['delta_bytes']:+,} bytes !!",
              file=sys.stderr)
        print("  A SQL filter is silently mis-counting the authorable denominator "
              "(wave-9 '??_%' wildcard class). Investigate before trusting any band "
              "query or cert census.", file=sys.stderr)
        conn.close()
        return 1

    if args.summary:
        # Self-validate the denominator first: if the two paths disagree, the
        # headline below is built on a mis-counted total — fail loudly.
        chk = denominator_self_check(conn)
        if not chk["agree"]:
            print(f"WARNING: denominator self-check DISAGREES "
                  f"(SQL {chk['a_fns']:,} fns vs Python {chk['b_fns']:,} fns; "
                  f"delta {chk['delta_fns']:+,}). The headline below may be "
                  f"undercounting — run --check-denominator to investigate.",
                  file=sys.stderr)
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
        return 0 if chk["agree"] else 1

    if args.manual_file:
        problems = manual_certify(conn, args.manual_file, apply=args.apply)
        conn.close()
        return 1 if problems else 0

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
