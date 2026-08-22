"""Resolve the diff ruler the GRADER is actually using, at runtime.

⚠ PORT NOTE (dc3-decomp, 2026-08-17).  This file was ported VERBATIM from
rb3-xenon `scripts/analysis/ruler.py`, minus its selftest.  **Every measurement
quoted in the discussion below is rb3-xenon's** (title 45410914), not
dc3-decomp's — the two trees share symbol names and address ranges, so an
unattributed number gets "refuted" against the wrong binary.  The MECHANISM is
identical on both trees and dc3 is configured the same way: `objdiff.json`
carries `options = {"functionRelocDiffs": "name_check"}` and
`build/373307D9/report.json` carries a 22-key `provenance.diff_config`, so
source (1) below is authoritative here.

Two dc3-specific facts, measured 2026-08-17:

  * dc3-decomp has NOT migrated its consumers.  Five files still hardcode
    `-c functionRelocDiffs=` — `scripts/orchestrator/mcp_server.py`,
    `scripts/analysis/diff_inspect.py`, `scripts/analysis/reloc_strict_classify.py`,
    `scripts/atexit_fuzzy_verify.py`, `scripts/sync_objdiff.py` — which is
    exactly the defect described below.  That migration is not this lane's work;
    `_selftest` therefore RATCHETS on the set instead of failing outright — it
    fails only when a NEW file acquires one.
  * Consequently an orchestrator `run_objdiff` percentage on dc3 is a `none`
    percentage, i.e. an UPPER BOUND on the graded score.  A row it calls 100%
    can still be withholding its bytes.  `?Handle@HamDirector@@` was such a row.

★ Why this module exists (rb3-xenon lane MCPRULER-1, 2026-08-14)
────────────────────────────────────────────────────────────────
`matched_code` moves by ~675 kB / 6.54 pp on rb3-xenon with ZERO source change,
purely by flipping `functionRelocDiffs` between `none` and `name_check`
(measured whole-binary: 4,397,412 B / 42.61% vs 3,722,476 B / 36.07%, with
`matched_functions` = 44,394 and `masked_equal` = 22,897 BIT-IDENTICAL on both
legs — the flip touches relocation-name comparison only, and `mpn` excludes
arg-only penalties).

So **a percentage without its ruler is not a measurement.** Every consumer here
must (a) score on the same ruler as `report.json`, and (b) say which ruler it
used.

The defect this replaces
────────────────────────
`scripts/orchestrator/mcp_server.py` hardcoded `-c functionRelocDiffs=none` in
four places. That was CORRECT when lane EB-4 wrote it (2026-08-03): back then
`objdiff-cli report generate`'s base config really was `None`, and `objdiff.json`
carried no `options` block to override it. On **2026-08-12 (`d04c83df`)** the
project shipped `options = {"functionRelocDiffs": "name_check"}` and the
hardcoded constant silently became a LIE about the grader.

Consequences, measured on this tree at `1f078361`:
  * **5,555 rows / 674,936 B** read `fuzzy == 100` under `none` but below 100 on
    the graded ruler. Those are rows the orchestrator reported as
    *"100.0% normalized, all equal, Complete — No action needed"* while the
    grader withheld every one of their bytes.
  * 7,157 rows disagree between the two rulers in total.

⇒ **Never hardcode the ruler again.** A second hardcoded constant would rot in
exactly the same way, on exactly the same silent schedule. Read it from the
artifact the grading run itself wrote.

Source of truth, in priority order
──────────────────────────────────
1. **`build/<version>/report.json` → `provenance.diff_config`.** This is a
   COMPLETE dump of every config key the grading run used, written by that run.
   It is authoritative by construction: it is not a description of the config,
   it IS the config. All 22 keys it emits are accepted verbatim as `-c` args
   (verified end-to-end).
2. **`objdiff.json` → `options`, layered on `report generate`'s base.** Used
   when no report has been generated yet. This reproduces the grader's layering
   (`report.rs:512` base → project `options` → unit `options` → `-c`) but cannot
   see per-unit `options` blocks, so it is labelled DERIVED.
3. **`report generate`'s base alone**, labelled a loud FALLBACK.

Why the base four are still needed here
───────────────────────────────────────
`objdiff-cli diff` and `objdiff-cli report generate` have DIFFERENT hardcoded
base configs, and neither is the schema default:

    report generate (report.rs:512)  functionRelocDiffs=None, combineData=true,
                                     combineText=true,  pool=false
    diff            (diff.rs:949)    functionRelocDiffs=DataValue, everything
                                     else = schema default
                                     (combineData=false, combineText=false,
                                      pool=true)

Both then layer the project's `options` block on top. So `objdiff.json`'s
`options` fixes `functionRelocDiffs` for both — but leaves `diff` disagreeing
with the grader on the OTHER three. Lane EB-4 measured `ppc.calculatePoolRelocations`
alone as worth up to 14.75 pp on 118 of 1,639 named sub-100 rows. That is why
source (1) — which carries all of them — is strongly preferred over "just drop
the `-c` flags".

⚠ `map_file` needs no handling here: `diff.rs:964` already loads `objdiff.json`'s
`map_file` on its own, so the ICF alias equivalences (7,174 entries) are shared
with the grader automatically.
"""

from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

# ── `objdiff-cli report generate`'s base config (report.rs:512) ───────────────
# These four differ from the schema defaults and are the de-facto scoring
# semantics of every project that runs `report generate`. Used only as the
# FALLBACK layer when no report.json provenance is available.
REPORT_GENERATE_BASE: dict[str, str] = {
    "functionRelocDiffs": "none",
    "combineDataSections": "true",
    "combineTextSections": "true",
    "ppc.calculatePoolRelocations": "false",
}

RELOC_KEY = "functionRelocDiffs"

# Ruler selectors accepted by the MCP tools.
RULER_GRADED = "graded"
RULER_NONE = "none"
RULER_DATA_VALUE = "data_value"
VALID_RULERS = (RULER_GRADED, RULER_NONE, RULER_DATA_VALUE)

_RULER_OVERRIDE = {
    RULER_NONE: "none",
    RULER_DATA_VALUE: "data_value",
}

# Memoize per (path, mtime) — report.json is ~15 MB and these tools are called
# in tight loops. Parsing it is ~0.11 s, which is cheap but not free.
_CACHE: dict[tuple[str, float], list[str]] = {}


@dataclass
class Ruler:
    """The effective diff configuration, plus where it came from."""

    reloc_mode: str                 # e.g. "name_check" / "none" / "data_value"
    config: dict[str, str]          # full key -> value map
    source: str                     # human-readable provenance
    selector: str = RULER_GRADED    # which ruler the caller asked for
    warning: str | None = None      # loud text when derived/fallback
    graded_reloc_mode: str | None = None  # the grader's mode, when overridden
    authoritative: bool = True      # False => read from no grading run

    @property
    def args(self) -> list[str]:
        """Flat `-c key=value` argv for objdiff-cli."""
        out: list[str] = []
        for k, v in self.config.items():
            out += ["-c", f"{k}={v}"]
        return out

    def label(self) -> str:
        """One-line ruler disclosure. ALWAYS render this next to a percentage."""
        if self.selector == RULER_GRADED:
            if self.authoritative:
                head = (
                    f"ruler: `functionRelocDiffs={self.reloc_mode}` "
                    "(GRADED — same as report.json)"
                )
            else:
                head = (
                    f"ruler: `functionRelocDiffs={self.reloc_mode}` "
                    "(**NOT read from a grading run** — see warning)"
                )
        else:
            head = (
                f"ruler: `functionRelocDiffs={self.reloc_mode}` "
                f"(**NOT the graded ruler** — grader uses `{self.graded_reloc_mode}`)"
            )
        return f"{head} · source: {self.source}"

    def banner(self) -> str:
        """Multi-line disclosure, including any warning."""
        lines = [self.label()]
        if self.selector == RULER_NONE:
            lines.append(
                "⚠ `none` ignores relocation NAMES: a wrong callee and a folded "
                "callee both read as equal. Percentages here are an UPPER BOUND "
                "on the graded score, never a completion proof."
            )
        elif self.selector == RULER_DATA_VALUE:
            lines.append(
                "⚠ `data_value` charges relocation ADDRESSES too, so it reads "
                "LOWER than the graded score. It is a defect-hunting ruler (a "
                "wrong `bl` callee is visible), never the graded score."
            )
        if self.warning:
            lines.append(f"⚠ {self.warning}")
        return "\n".join(lines)


def _find_report_json(project_dir: Path) -> Path | None:
    """Newest build/<version>/report.json under project_dir, if any."""
    build = project_dir / "build"
    if not build.is_dir():
        return None
    best: tuple[float, Path] | None = None
    try:
        for version_dir in build.iterdir():
            candidate = version_dir / "report.json"
            if candidate.is_file():
                mtime = candidate.stat().st_mtime
                if best is None or mtime > best[0]:
                    best = (mtime, candidate)
    except OSError:
        return None
    return best[1] if best else None


def _provenance_config(report_path: Path) -> list[str] | None:
    """`provenance.diff_config` from a report.json, memoized on mtime."""
    try:
        key = (str(report_path), report_path.stat().st_mtime)
    except OSError:
        return None
    if key in _CACHE:
        return _CACHE[key]
    try:
        with open(report_path) as fh:
            data = json.load(fh)
    except (OSError, ValueError):
        return None
    cfg = (data.get("provenance") or {}).get("diff_config")
    if not isinstance(cfg, list) or not cfg:
        return None
    cfg = [str(x) for x in cfg]
    _CACHE[key] = cfg
    return cfg


def _parse_kv(pairs: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for item in pairs:
        if "=" in item:
            k, v = item.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def graded_ruler(project_dir: str | os.PathLike) -> Ruler:
    """The ruler `report.json` is scored on, resolved from artifacts on disk."""
    project_dir = Path(project_dir)

    # ── (1) report.json provenance — authoritative ────────────────────────────
    report_path = _find_report_json(project_dir)
    if report_path is not None:
        cfg_list = _provenance_config(report_path)
        if cfg_list:
            cfg = _parse_kv(cfg_list)
            mode = cfg.get(RELOC_KEY, "?")
            try:
                rel = report_path.relative_to(project_dir)
            except ValueError:
                rel = report_path
            return Ruler(
                reloc_mode=mode,
                config=cfg,
                source=f"{rel} `provenance.diff_config` ({len(cfg)} keys)",
                graded_reloc_mode=mode,
            )

    # ── (2) objdiff.json options layered on report generate's base ────────────
    objdiff_json = project_dir / "objdiff.json"
    if objdiff_json.is_file():
        try:
            with open(objdiff_json) as fh:
                proj = json.load(fh)
        except (OSError, ValueError):
            proj = {}
        options = proj.get("options")
        if isinstance(options, dict) and options:
            cfg = dict(REPORT_GENERATE_BASE)
            cfg.update({str(k): _as_cfg_value(v) for k, v in options.items()})
            mode = cfg.get(RELOC_KEY, "?")
            return Ruler(
                reloc_mode=mode,
                config=cfg,
                source="objdiff.json `options` + report-generate base (DERIVED)",
                graded_reloc_mode=mode,
                authoritative=False,
                warning=(
                    "No report.json found, so the ruler was DERIVED from "
                    "objdiff.json rather than read from a grading run. Per-unit "
                    "`options` blocks are invisible this way. Run `ninja "
                    "build/<version>/report.json` for an authoritative read."
                ),
            )

    # ── (3) loud fallback ─────────────────────────────────────────────────────
    cfg = dict(REPORT_GENERATE_BASE)
    mode = cfg[RELOC_KEY]
    return Ruler(
        reloc_mode=mode,
        config=cfg,
        source="report-generate base only (FALLBACK)",
        graded_reloc_mode=mode,
        authoritative=False,
        warning=(
            "Could not find report.json OR an objdiff.json `options` block under "
            f"{project_dir}. Fell back to `objdiff-cli report generate`'s base "
            "config. THE RULER IS UNVERIFIED — do not quote this percentage as "
            "the graded score."
        ),
    )


def _as_cfg_value(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    return str(v)


def resolve_ruler(project_dir: str | os.PathLike, selector: str = RULER_GRADED) -> Ruler:
    """Graded ruler, optionally overridden to an explicit opt-in ruler.

    `none` and `data_value` keep EVERY other graded key identical and change
    only `functionRelocDiffs`, so a graded-vs-`none` delta isolates the
    relocation-name class cleanly — which is what `ab_measure`'s
    `control_none_shape()` depends on, and what separates a relocation-name
    issue from an instruction issue.
    """
    selector = (selector or RULER_GRADED).strip().lower()
    if selector not in VALID_RULERS:
        raise ValueError(
            f"Unknown ruler {selector!r}. Valid: {', '.join(VALID_RULERS)}"
        )
    base = graded_ruler(project_dir)
    if selector == RULER_GRADED:
        return base
    cfg = dict(base.config)
    cfg[RELOC_KEY] = _RULER_OVERRIDE[selector]
    return Ruler(
        reloc_mode=cfg[RELOC_KEY],
        config=cfg,
        source=f"{base.source} + explicit `{RELOC_KEY}={cfg[RELOC_KEY]}` override",
        selector=selector,
        warning=base.warning,
        graded_reloc_mode=base.graded_reloc_mode,
        authoritative=base.authoritative,
    )


__all__ = [
    "Ruler",
    "graded_ruler",
    "resolve_ruler",
    "REPORT_GENERATE_BASE",
    "RULER_GRADED",
    "RULER_NONE",
    "RULER_DATA_VALUE",
    "VALID_RULERS",
]




# ── selftest ─────────────────────────────────────────────────────────────────
# `python3 scripts/analysis/ruler.py --selftest [project_dir]`
#
# rb3-xenon's version of this guard FAILS on any consumer that hardcodes
# `-c functionRelocDiffs=`, because there the migration is done and a new
# hardcoded constant would be a regression.  dc3-decomp has NOT migrated: eight
# files hardcode it today (2026-08-17).  Failing outright here would mean a red
# selftest from the first run, which is the fastest way to get a guard ignored.
#
# So this port RATCHETS.  The baseline below is the measured state of the tree
# at the time of the port; the guard fails when a file is ADDED to the set.
# That catches the thing that actually matters -- a constant that stops being
# true while every test keeps passing -- without pretending the debt is paid.
#
# ⚠ Do NOT "fix" a failure by editing the baseline upward.  Read the ruler with
# `graded_ruler(project_dir)` in the new code instead.  Editing the baseline
# down as files migrate is correct and encouraged.

_TOOL_REPO = Path(__file__).resolve().parent.parent.parent

#: Files known to hardcode the ruler.  A RATCHET, not an allow-list: the guard
#: fails if a file outside this set acquires one.
#
#: RE-DERIVED 2026-08-22 under a scanner that can see f-strings.  The five-entry
#: 2026-08-17 baseline was recorded with a pattern blind to the
#: `"-c", f"functionRelocDiffs={x}"` spelling, so it undercounted by two files
#: (`reloc_pattern_census.py`, `symbol_sweep.py`) and mis-scored a third
#: (`sync_objdiff.py`) as migrated.  These two additions are NOT new debt --
#: both predate the baseline; they were invisible to it.  Do not read this
#: widening as permission to widen it again: `_selftest_scanner` now fails RED
#: if the pattern loses a spelling.
_HARDCODED_RULER_BASELINE = frozenset({
    "scripts/atexit_fuzzy_verify.py",
    "scripts/sync_objdiff.py",
    "scripts/analysis/diff_inspect.py",
    "scripts/analysis/reloc_pattern_census.py",
    "scripts/analysis/reloc_strict_classify.py",
    "scripts/orchestrator/mcp_server.py",
    "scripts/orchestrator/symbol_sweep.py",
})


#: The `-c functionRelocDiffs=` call form, in every spelling this repo uses.
#
# ⚠ The `(?:[rRbBfFuU]{0,2})?` string prefix is not decoration.  The original
# pattern required a QUOTE immediately after `"-c",`, so it was structurally
# blind to the idiomatic form
#
#     "-c", f"functionRelocDiffs={reloc}"
#
# which is what `scripts/sync_objdiff.py:366` has always used.  Measured
# 2026-08-22: the scan saw 4 files where the baseline named 5, so
# `sync_objdiff.py` landed in `gone` and the selftest printed
# "✔ migrated since the baseline — shrink _HARDCODED_RULER_BASELINE" while
# `sync_objdiff.py` had not migrated by one character.  A ratchet that reports
# PASS and asks you to delete its own last-but-one entry is the failure mode
# this file's own preamble names: "a constant that stops being true while every
# test keeps passing."  `_selftest_scanner` below is the negative control that
# makes a repeat of that regression RED.
_HARDCODED_RULER_RE = re.compile(
    r"""["']-c["']\s*,\s*(?:[rRbBfFuU]{1,2}\s*)?["']functionRelocDiffs=""")


def _scan_hardcoded_ruler() -> tuple[set, list[str]]:
    """Repo-relative paths of .py files that hardcode `-c functionRelocDiffs=`.

    Returns `(found, unreadable)`.  `unreadable` is returned rather than
    swallowed: a file the scanner could not open contributes nothing to `found`,
    which is arithmetically identical to "this file is clean".  The caller
    treats a non-empty `unreadable` as a FAILURE, because a shrinking `found`
    must never be explicable by a permissions problem.
    """
    found = set()
    unreadable: list[str] = []
    self_rel = str(Path(__file__).resolve().relative_to(_TOOL_REPO))
    for path in sorted((_TOOL_REPO / "scripts").rglob("*.py")):
        if "__pycache__" in path.parts or "venv" in str(path):
            continue
        # This file carries both spellings as literal control data
        # (`_SCANNER_CONTROL`), so a scanner that did not exempt itself would
        # match itself forever -- the same self-matching trap as a `pgrep -f`
        # watcher whose pattern is in its own argv.
        if str(path.relative_to(_TOOL_REPO)) == self_rel:
            continue
        try:
            text = path.read_text()
        except OSError as exc:
            unreadable.append(f"{path.relative_to(_TOOL_REPO)}: {exc}")
            continue
        if _HARDCODED_RULER_RE.search(text):
            found.add(str(path.relative_to(_TOOL_REPO)))
    return found, unreadable


#: (spelling, must_match) pairs run against `_HARDCODED_RULER_RE` on every
#: selftest.  The FALSE rows are the negative control: without them a regex that
#: matches everything would pass just as happily as a correct one.
_SCANNER_CONTROL = (
    ('cmd = [cli, "diff", "-c", "functionRelocDiffs=none", "-p", p]', True),
    ('cmd = [cli, "diff", "-c", f"functionRelocDiffs={reloc}", "-p", p]', True),
    ("cmd = [cli, 'diff', '-c', f'functionRelocDiffs={reloc}']", True),
    ('cmd = [cli, "diff", "-c",  f"functionRelocDiffs=all"]', True),
    ('# the ruler is `functionRelocDiffs=name_check` in objdiff.json', False),
    ('cfg = {"functionRelocDiffs": "none"}', False),
    ('cmd = [cli, "diff", "-c", resolved_ruler_flag()]', False),
)


def _selftest_scanner() -> list[tuple[str, bool, str]]:
    """Negative control for `_scan_hardcoded_ruler`'s pattern.

    The ratchet's whole value is that `found` shrinking means a migration.  If
    the pattern cannot see a spelling, `found` shrinks for a reason that has
    nothing to do with migration and the guard reports the opposite of the
    truth.  So the pattern is exercised against both spellings AND against
    strings it must NOT match, in memory, on every run.
    """
    rows = []
    for src, want in _SCANNER_CONTROL:
        got = bool(_HARDCODED_RULER_RE.search(src))
        rows.append((f"scanner {'sees' if want else 'ignores'}: {src[:58]}",
                     got == want, f"matched={got} expected={want}"))
    return rows


def _selftest(project_dir: Path) -> tuple[bool, list[str]]:
    out: list[str] = []
    ok = True

    def check(label: str, cond: bool, detail: str = "") -> None:
        nonlocal ok
        out.append(f"  [{'PASS' if cond else 'FAIL'}] {label}"
                   f"{(' — ' + detail) if detail else ''}")
        if not cond:
            ok = False

    graded = graded_ruler(project_dir)
    out.append(f"resolved: {graded.label()}")

    report = _find_report_json(project_dir)
    if report is not None:
        with open(report) as fh:
            prov = (json.load(fh).get("provenance") or {})
        declared = _parse_kv([str(x) for x in prov.get("diff_config", [])])
        check("graded config == report.json provenance.diff_config",
              graded.config == declared,
              f"{len(graded.config)} keys vs {len(declared)}")
        check("graded ruler is authoritative", graded.authoritative)
    else:
        check("report.json found under project_dir", False,
              "cannot check provenance parity — is this a built tree?")

    # Selector overrides must change EXACTLY one key, or a graded-vs-none delta
    # no longer isolates the relocation-name class.
    for sel in (RULER_NONE, RULER_DATA_VALUE):
        r = resolve_ruler(project_dir, sel)
        differing = {k for k in set(r.config) | set(graded.config)
                     if r.config.get(k) != graded.config.get(k)}
        check(f"ruler={sel} changes exactly one key",
              differing == {RELOC_KEY}, f"changed: {sorted(differing)}")
        check(f"ruler={sel} is labelled NOT graded",
              "NOT the graded ruler" in r.label())

    try:
        resolve_ruler(project_dir, "bogus")
        check("unknown ruler is refused", False, "no exception raised")
    except ValueError:
        check("unknown ruler is refused", True)

    # ★ Ratchet, not a pass/fail gate — see the note above the baseline.
    #
    # The scanner is graded BEFORE it is trusted.  A ratchet reports migration
    # by `found` shrinking, and a blind pattern shrinks `found` for a reason
    # that is not migration -- so the control runs first and its failure is a
    # hard FAIL, not a note.
    for label, cond, detail in _selftest_scanner():
        check(label, cond, detail)

    found, unreadable = _scan_hardcoded_ruler()
    new = found - _HARDCODED_RULER_BASELINE
    gone = _HARDCODED_RULER_BASELINE - found
    out.append(f"hardcoded-ruler scan root: {_TOOL_REPO}/scripts "
               f"(this tool's repo, NOT project_dir)")
    out.append(f"  known debt: {len(_HARDCODED_RULER_BASELINE)} files "
               f"hardcode `-c functionRelocDiffs=`; found {len(found)}")
    check("every scanned file was readable", not unreadable,
          f"unreadable: {unreadable}")
    check("no NEW file hardcodes the ruler", not new, f"new: {sorted(new)}")
    # A baseline entry that has vanished from the scan is EITHER a migration OR
    # a file that was deleted/renamed/moved out of scan scope.  Those are not
    # the same event and the old code printed the first as fact.  Only claim a
    # migration when the file is still there and no longer matches.
    if gone:
        migrated = sorted(p for p in gone if (_TOOL_REPO / p).is_file())
        vanished = sorted(p for p in gone if not (_TOOL_REPO / p).is_file())
        if migrated:
            out.append(f"  ✔ migrated since the baseline (file still present, "
                       f"no longer hardcodes) — shrink "
                       f"_HARDCODED_RULER_BASELINE: {migrated}")
        if vanished:
            out.append(f"  ⚠ baseline entries that NO LONGER EXIST — this is a "
                       f"deleted/renamed file, NOT a migration: {vanished}")

    return ok, out


if __name__ == "__main__":
    import sys

    argv = [a for a in sys.argv[1:] if a != "--selftest"]
    target = Path(argv[0]) if argv else Path.cwd()
    print(f"# ruler.py selftest — project_dir={target}")
    passed, lines = _selftest(target)
    for line in lines:
        print(line)
    print("PASS" if passed else "FAIL")
    sys.exit(0 if passed else 1)
