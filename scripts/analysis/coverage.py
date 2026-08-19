#!/usr/bin/env python3
"""coverage.py — make a scanner incapable of lying by omission.

WHY THIS EXISTS
===============
A single week in this project turned up SIX defects in its own measurement
tooling.  Every one of them had already been used to declare some area
"exhausted", and every fix reopened territory that contained real bugs:

  fake_impl_scan.py    `if pct is None: continue` on `fuzzy_match_percent` — a
                       key objdiff only emits for functions WE define.  The
                       entire "we wrote no body at all" tier (~1,024 rows) was
                       invisible.  Four waves called that pool exhausted having
                       never looked at it.
  data_symbol_scan.py  truncated at `--max-symbols` default 4000 while stderr
                       printed only `scanned=`.  14,549 of 18,549 symbols were
                       never examined; every candidate count it ever produced
                       was a 22% sample presented as a total.
  data_symbol_scan.py  a lazily-built global index was published EMPTY and then
                       filled from inside the worker pool — a month of counts
                       were nondeterministic noise.
  certify_floor.py     `symbol NOT LIKE '??_%'`; `_` is a single-char wildcard
                       in SQL LIKE, so 6,835 functions vanished from every band
                       query.
  measure_progress.sh  compared the reloc-sensitive `fuzzy_match_percent`
                       ruler — phantom regressions from ICF/atexit churn.
  every % surface      ROUNDS, so 99.97 renders as `100.0`.  Two real bugs hid
                       under the otherwise-correct rule "a divergence on a
                       100%-matched function is by construction an artifact".

They are all ONE bug with six spellings:

    a silent `continue`, cap, or filter on exactly the population you care
    about, with a summary line that reports only what was PROCESSED and never
    what was DROPPED.

THE CONVENTION THIS MODULE ENFORCES
===================================
Every scanner reports its DENOMINATOR.  Not "I found 12 candidates" but
"I found 12 candidates out of 18,549 symbols, having dropped 14,549 to
--max-symbols and 137 to unparseable COFF".

  1. Declare the universe as soon as you know it:      cov.universe(n, "...")
  2. Route EVERY discard through a counted call:       cov.drop("reason")
     — there is no other way to discard a row.  If you write a bare
       `continue`, the arithmetic check in `emit()` will catch you.
  3. Declare any truncation:                           cov.cap("--max-symbols", ...)
  4. Emit before you exit:                             sys.exit(cov.emit())

`emit()` REFUSES to print a clean summary when rows are unaccounted for: if
universe != examined + sum(drops) it prints an UNACCOUNTED banner naming the
gap, and the process exits non-zero.  A truncated run prints a TRUNCATED banner
and exits non-zero too (overridable with --allow-truncation for the rare case
where a sample is genuinely what you wanted — but then the JSON still says so).

USAGE
=====
    from scripts.analysis.coverage import CoverageReport, add_coverage_args

    ap = argparse.ArgumentParser()
    add_coverage_args(ap)                 # --allow-truncation / --coverage-json
    ...
    cov = CoverageReport("data_symbol_scan", args=args)
    cov.universe(len(all_symbols), "data symbols in target .obj files")
    for s in all_symbols:
        if not wanted(s):
            cov.drop("not-selected-by-classes-filter")   # DELIBERATE
            continue
        if s.parse_failed:
            cov.drop("coff-unparseable")                 # ACCIDENTAL — the
            continue                                     # dangerous kind
        cov.examine()
        ...
    results["_coverage"] = cov.as_dict()
    sys.exit(cov.emit())

STANDING RULE for whoever writes the next scanner
=================================================
    If your summary line can be printed without knowing how many rows you never
    looked at, it is not a summary — it is a sample presented as a total.
"""
from __future__ import annotations

import json
import os
import sys
from typing import Any, Dict, List, Optional

__all__ = [
    "CoverageReport",
    "TruncationError",
    "add_coverage_args",
    "EXIT_TRUNCATED",
    "EXIT_UNACCOUNTED",
]

# Distinct exit codes so a caller (or CI) can tell the two failure shapes apart
# without parsing text.  Deliberately NOT 1, which every Python traceback uses.
EXIT_OK = 0
EXIT_TRUNCATED = 3
EXIT_UNACCOUNTED = 4

_BAR = "=" * 78


class TruncationError(RuntimeError):
    """Raised by `assert_complete()` when a run examined less than its universe."""


class CoverageReport:
    """Accumulates the denominator of a scan and refuses to hide it.

    Thread-safety: `examine()` and `drop()` take no lock.  They are safe to call
    from the MAIN thread only.  Call them while BUILDING the task list (before
    the pool) and while CONSUMING futures (after the pool), never from inside a
    worker — that is the same shape as the data_symbol_scan race, and the whole
    point of this class is that its numbers are reproducible.  If you truly need
    per-worker counting, aggregate into the worker's return value and call
    `drop()` on the consuming side.
    """

    def __init__(
        self,
        name: str,
        args: Any = None,
        stream=None,
        allow_truncation: Optional[bool] = None,
    ) -> None:
        self.name = name
        self._stream = stream if stream is not None else sys.stderr
        self._universe: Optional[int] = None
        self._universe_what: str = ""
        self._examined = 0
        self._drops: Dict[str, int] = {}
        self._drop_notes: Dict[str, str] = {}
        self._caps: List[Dict[str, Any]] = []
        self._notes: List[str] = []
        self._extra: Dict[str, Any] = {}

        if allow_truncation is None:
            allow_truncation = bool(getattr(args, "allow_truncation", False))
        self.allow_truncation = allow_truncation
        self.coverage_json = getattr(args, "coverage_json", None)

    # -- declaration ------------------------------------------------------- #

    def universe(self, n: int, what: str = "") -> None:
        """The denominator: how many rows EXISTED before any filtering.

        Call this as early as you can compute it.  A scanner that never calls
        it gets an `unknown-universe` banner — an honest "I do not know my own
        denominator" beats a confident wrong total.
        """
        self._universe = int(n)
        self._universe_what = what

    def note(self, text: str) -> None:
        """Free-form caveat printed inside the coverage block (e.g. a ruler choice)."""
        self._notes.append(text)

    def extra(self, key: str, value: Any) -> None:
        """Extra scalar carried into the JSON block (e.g. the report.json mtime)."""
        self._extra[key] = value

    # -- accounting -------------------------------------------------------- #

    def examine(self, n: int = 1) -> None:
        self._examined += int(n)

    def drop(self, reason: str, n: int = 1, note: str = "") -> None:
        """Discard `n` rows for `reason`.  This is the ONLY sanctioned discard.

        `reason` is a short stable slug; it becomes a JSON key, so keep it
        machine-greppable (`missing-fuzzy-percent`, not "no percent :(").
        """
        if n <= 0:
            return
        self._drops[reason] = self._drops.get(reason, 0) + int(n)
        if note and reason not in self._drop_notes:
            self._drop_notes[reason] = note

    def cap(self, flag: str, limit: Any, before: int, after: int, note: str = "") -> None:
        """Record that a cap truncated the ANALYSIS (not merely the display).

        Truncating a *display* of an already-complete count is fine and does not
        belong here — say `--limit only shortens the printout` in help text
        instead.  This method means: rows that existed were never looked at.
        """
        n_cut = max(0, int(before) - int(after))
        self._caps.append({
            "flag": flag,
            "limit": limit,
            "before": int(before),
            "after": int(after),
            "dropped": n_cut,
            "note": note,
        })
        if n_cut:
            self.drop(f"capped-by-{flag.lstrip('-')}", n_cut, note)

    # -- derived ----------------------------------------------------------- #

    @property
    def dropped_total(self) -> int:
        return sum(self._drops.values())

    @property
    def truncated(self) -> bool:
        return any(c["dropped"] > 0 for c in self._caps)

    @property
    def unaccounted(self) -> Optional[int]:
        """universe - (examined + dropped).  None when the universe is unknown.

        A non-zero value is the signature of a bare `continue` that skipped
        `drop()` — i.e. the exact bug this module exists to prevent.  It is
        reported, loudly, rather than silently absorbed.
        """
        if self._universe is None:
            return None
        return self._universe - (self._examined + self.dropped_total)

    @property
    def coverage_fraction(self) -> Optional[float]:
        if not self._universe:
            return None
        return self._examined / float(self._universe)

    def is_clean(self) -> bool:
        """True iff the whole universe is accounted for and nothing was truncated."""
        if self.truncated:
            return False
        u = self.unaccounted
        return u == 0 or (self._universe is not None and u == 0)

    # -- output ------------------------------------------------------------ #

    def as_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {
            "scanner": self.name,
            "universe": self._universe,
            "universe_is": self._universe_what,
            "examined": self._examined,
            "dropped_total": self.dropped_total,
            "dropped": dict(sorted(self._drops.items())),
            "caps": self._caps,
            "truncated": self.truncated,
            "unaccounted": self.unaccounted,
            "notes": list(self._notes),
        }
        cf = self.coverage_fraction
        d["coverage_pct"] = None if cf is None else round(cf * 100.0, 4)
        # A rendered percentage ROUNDS: 99.97 prints as 100.0, and this project
        # has already lost two real bugs to exactly that.  Carry the raw
        # integers alongside so a consumer never has to trust the rendering.
        d["complete"] = (self._universe is not None
                         and self._examined == self._universe
                         and not self.truncated)
        d.update(self._extra)
        return d

    def render(self) -> str:
        """The human block.  Always states the denominator, or admits it cannot."""
        L: List[str] = []
        L.append(_BAR)
        L.append(f"COVERAGE  {self.name}")
        if self._universe is None:
            L.append("  universe            : UNKNOWN  "
                     "(scanner never called cov.universe() — its totals are unverifiable)")
        else:
            what = f"  ({self._universe_what})" if self._universe_what else ""
            L.append(f"  universe            : {self._universe}{what}")
        pct = self.coverage_fraction
        pct_s = "" if pct is None else f"  ({self._examined}/{self._universe} = {pct * 100.0:.2f}%)"
        L.append(f"  examined            : {self._examined}{pct_s}")
        if self._drops:
            L.append(f"  dropped             : {self.dropped_total}")
            width = max(len(r) for r in self._drops)
            for reason, n in sorted(self._drops.items(), key=lambda kv: (-kv[1], kv[0])):
                note = self._drop_notes.get(reason, "")
                note = f"   # {note}" if note else ""
                L.append(f"      {reason.ljust(width)} : {n}{note}")
        else:
            L.append("  dropped             : 0")
        for n in self._notes:
            L.append(f"  note                : {n}")
        for c in self._caps:
            if c["dropped"]:
                L.append(f"  !! TRUNCATED by {c['flag']}={c['limit']} : "
                         f"{c['dropped']} of {c['before']} rows were NEVER EXAMINED"
                         + (f"  ({c['note']})" if c["note"] else ""))
        u = self.unaccounted
        if u:
            L.append(f"  !! UNACCOUNTED      : {u} rows are neither examined nor dropped — "
                     f"some `continue` in this scanner skipped cov.drop()")
        if self.truncated:
            L.append(_BAR)
            L.append("TRUNCATED — this run is a SAMPLE, not a census. Do NOT quote its "
                     "counts as totals,")
            L.append("and do NOT conclude a pool is exhausted from it. Re-run with the cap "
                     "raised/removed.")
        elif u:
            L.append(_BAR)
            L.append("UNACCOUNTED ROWS — the denominator does not balance, so these counts "
                     "are not a census.")
        elif self._universe is None:
            L.append(_BAR)
            L.append("NO DENOMINATOR — this scanner cannot say what it did not look at.")
        L.append(_BAR)
        return "\n".join(L)

    def emit(self, stream=None) -> int:
        """Print the coverage block and return the process exit code.

        0  full census
        3  EXIT_TRUNCATED    a cap cut rows out of the ANALYSIS
        4  EXIT_UNACCOUNTED  the arithmetic does not balance (a bare `continue`)

        `--allow-truncation` downgrades 3 to 0; nothing downgrades 4, because an
        unbalanced denominator is always a scanner bug and never a user choice.
        """
        st = stream if stream is not None else self._stream
        print(self.render(), file=st)
        if self.coverage_json:
            with open(self.coverage_json, "w") as f:
                json.dump(self.as_dict(), f, indent=2)
        if self.unaccounted:
            return EXIT_UNACCOUNTED
        if self.truncated and not self.allow_truncation:
            return EXIT_TRUNCATED
        return EXIT_OK

    def assert_complete(self) -> None:
        """Raise unless this was a full census.  For use inside tests."""
        if self.truncated:
            raise TruncationError(
                f"{self.name}: truncated — {self.dropped_total} rows never examined")
        if self.unaccounted:
            raise TruncationError(
                f"{self.name}: {self.unaccounted} rows unaccounted for")
        if self._universe is None:
            raise TruncationError(f"{self.name}: no universe declared")


def add_coverage_args(ap) -> None:
    """Add the two standard coverage flags to an argparse parser.

    Purely additive — no existing flag changes meaning, so this is safe to drop
    into a scanner that concurrent agents are already calling.
    """
    ap.add_argument("--allow-truncation", action="store_true",
                    help="permit a capped/sampled run to exit 0. The TRUNCATED banner "
                         "and the JSON _coverage block still say it was a sample.")
    ap.add_argument("--coverage-json", default=None,
                    help="write the machine-readable coverage block to this path")


# --------------------------------------------------------------------------- #
# SQL LIKE escaping — the certify_floor.py defect, in one reusable place.
# --------------------------------------------------------------------------- #

def like_escape(s: str) -> str:
    r"""Escape the SQL LIKE metacharacters in a LITERAL string.

    SQL LIKE treats `_` as "any single character" and `%` as "any run".  A naive
    ``symbol NOT LIKE '??_%'`` therefore excludes EVERY '??'-prefixed symbol,
    not just the `??_`-prefixed ones — that one line hid 6,835 functions from
    every band query in this repo.  Always pair with ``ESCAPE '\'``.
    """
    return s.replace("\\", "\\\\").replace("_", "\\_").replace("%", "\\%")


def like_prefix_clause(column: str, prefix: str, negate: bool = False) -> str:
    r"""``column [NOT] LIKE '<escaped prefix>%' ESCAPE '\'`` — wildcards escaped."""
    op = "NOT LIKE" if negate else "LIKE"
    return f"{column} {op} '{like_escape(prefix)}%' ESCAPE '\\'"
