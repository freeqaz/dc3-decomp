#!/usr/bin/env python3
"""symbol_sweep.py -- the bulk / data-symbol shape of objdiff, as a real tool.

WHY THIS EXISTS
===============
`CLAUDE.md` says "use the `mcp__orchestrator__` tools for all decomp analysis;
do not call `objdiff-cli` directly."  Agents kept calling it directly anyway.
A transcript sweep over 474 session files found 464 tool calls that shell out to
`bin/objdiff-cli`; the single largest pattern (84 calls) is

    objdiff-cli diff -p . -u <unit> '??_7Class@@6B@' -f json --include-data

because `run_objdiff` is function-oriented and never passed `--include-data`,
and because every MCP diff call handles exactly ONE symbol -- so any question
of the form "over every vtable in the binary, which slots diverge?" had to be
a hand-rolled loop.  One such loop produced a real result and was written down
only as a paragraph of prose in
`docs/analysis/dispatch-data-rescan-20260818.md`:

    for every `??_7` symbol in every unit, run
    `objdiff-cli diff -u <unit> <sym> --include-data`, keep relocation rows whose
    `kind` is not `equal` AND where target and base symbols resolve to different
    addresses in `ham_xbox_r.map` (equal addresses = proven ICF fold = benign).

    141 divergent vtable slots at 49ad7cfd5 -> 52 after this branch.

That method is implemented here, so the next agent runs a tool instead of
rewriting the loop.

THE DENOMINATOR IS PART OF THE ANSWER
=====================================
The direct ancestor of that bypass is `data_symbol_scan.py` silently capping at
`--max-symbols 4000` and dropping 14,549 of 18,549 symbols as `capped` while
printing only `scanned=`.  Every sweep here therefore routes every discarded row
through a counted `drop()` and prints a COVERAGE block naming the universe, the
examined count and every drop reason.  When `scripts/analysis/coverage.py` (the
scanner-honesty lane's shared contract) is importable we use it verbatim; when
it is not yet merged we fall back to a local implementation emitting the SAME
key shape, so consumers never have to branch.

READ-ONLY BY DEFAULT
====================
`build=False` diffs already-built objects: no ninja, no writes to decomp.db.
Safe to run alongside the build/permuter fleet.

USAGE
=====
    python3 -m scripts.orchestrator.symbol_sweep --project . --kind vtable_slots
    python3 -m scripts.orchestrator.symbol_sweep --project . --kind vtable_slots \
        --unit-glob 'default/lazer/*' --format json
    python3 -m scripts.orchestrator.symbol_sweep --project . --kind data_symbols \
        --symbol-glob '??_R4*'
    python3 -m scripts.orchestrator.symbol_sweep --project . --kind functions \
        --symbols-file /tmp/worklist.txt          # uses objdiff --batch (JSONL)
"""
from __future__ import annotations

import argparse
import collections
import fnmatch
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

# --------------------------------------------------------------------------
# Coverage contract.  Prefer the shared one (scripts/analysis/coverage.py, owned
# by the scanner-honesty lane); fall back to a same-shaped local implementation
# so this module works before/independently of that merge.
# --------------------------------------------------------------------------
_SHARED_COVERAGE = False
try:  # pragma: no cover - exercised by whichever half is present
    _here = Path(__file__).resolve().parent.parent  # scripts/
    if str(_here.parent) not in sys.path:
        sys.path.insert(0, str(_here.parent))
    from scripts.analysis.coverage import CoverageReport as _SharedCoverageReport  # type: ignore

    _SHARED_COVERAGE = True
except Exception:  # noqa: BLE001 - any import failure means "not merged yet"
    _SharedCoverageReport = None  # type: ignore


class _LocalCoverage:
    """Minimal stand-in for scripts/analysis/coverage.CoverageReport.

    Emits the same `as_dict()` keys so a consumer cannot tell which one ran
    apart from the `coverage_impl` note.  Deliberately NOT a re-design: if the
    shared module is present it wins.
    """

    def __init__(self, name: str):
        self.name = name
        self._universe: Optional[int] = None
        self._universe_what = ""
        self._examined = 0
        self._drops: Dict[str, int] = collections.Counter()
        self._caps: List[Dict[str, Any]] = []
        self._notes: List[str] = []
        self._extra: Dict[str, Any] = {}

    # -- the three calls a scanner makes -----------------------------------
    def universe(self, n: int, what: str = "") -> None:
        self._universe = n
        self._universe_what = what

    def examine(self, n: int = 1) -> None:
        self._examined += n

    def drop(self, reason: str, n: int = 1) -> None:
        self._drops[reason] += n

    def cap(self, flag: str, limit: Any, before: int, after: int, note: str = "") -> None:
        # Signature AND drop-bookkeeping must mirror the shared CoverageReport:
        # make_coverage() hands callers whichever one is importable, so any
        # divergence is a crash or a double-count at a call site, not a fallback.
        n_cut = max(0, int(before) - int(after))
        self._caps.append({
            "flag": flag, "limit": limit,
            "before": int(before), "after": int(after),
            "dropped": n_cut, "note": note,
        })
        if n_cut:
            self.drop(f"capped-by-{flag.lstrip('-')}", n_cut)

    def note(self, text: str) -> None:
        self._notes.append(text)

    def extra(self, **kw: Any) -> None:
        self._extra.update(kw)

    # -- derived ------------------------------------------------------------
    @property
    def dropped_total(self) -> int:
        return sum(self._drops.values())

    @property
    def truncated(self) -> bool:
        return any(c["dropped"] for c in self._caps)

    @property
    def unaccounted(self) -> Optional[int]:
        if self._universe is None:
            return None
        return self._universe - (self._examined + self.dropped_total)

    @property
    def coverage_fraction(self) -> Optional[float]:
        if not self._universe:
            return None
        return self._examined / self._universe

    def is_clean(self) -> bool:
        return (
            self._universe is not None
            and not self.truncated
            and self.unaccounted == 0
        )

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
            "notes": list(self._notes) + ["coverage_impl=local-fallback"],
        }
        cf = self.coverage_fraction
        d["coverage_pct"] = None if cf is None else round(cf * 100.0, 4)
        d["complete"] = self.is_clean()
        d.update(self._extra)
        return d

    def render(self) -> str:
        bar = "=" * 78
        L = [bar, f"COVERAGE  {self.name}"]
        if self._universe is None:
            L.append("  universe            : UNKNOWN (scanner never declared one)")
        else:
            what = f"  ({self._universe_what})" if self._universe_what else ""
            L.append(f"  universe            : {self._universe}{what}")
        pct = self.coverage_fraction
        pct_s = "" if pct is None else f"  ({self._examined}/{self._universe} = {pct * 100.0:.2f}%)"
        L.append(f"  examined            : {self._examined}{pct_s}")
        if self._drops:
            L.append(f"  dropped             : {self.dropped_total}")
            for reason, n in sorted(self._drops.items(), key=lambda kv: (-kv[1], kv[0])):
                L.append(f"      {n:>7}  {reason}")
        for c in self._caps:
            L.append(f"  CAP {c['flag']}={c['limit']} dropped {c['dropped']}")
        ua = self.unaccounted
        if ua:
            L.append(f"  !! UNACCOUNTED      : {ua} rows are neither examined nor dropped")
        if self.truncated:
            L.append("  !! TRUNCATED        : this is a SAMPLE, not a total")
        for n in self._notes:
            L.append(f"  note                : {n}")
        L.append(bar)
        return "\n".join(L)


def make_coverage(name: str):
    """Return the shared CoverageReport when available, else the local shim."""
    if _SHARED_COVERAGE and _SharedCoverageReport is not None:
        try:
            return _SharedCoverageReport(name)
        except Exception:  # signature drift -> fall back rather than crash a sweep
            pass
    return _LocalCoverage(name)


# --------------------------------------------------------------------------
# Linker map
# --------------------------------------------------------------------------
# icf_pairing_bodytest.read_map() requires the `f i` flag column, which only
# FUNCTION rows carry -- data rows (`??_7...`) have no flags at all and are
# silently skipped by it.  This parser makes the flags optional, which is the
# whole reason the vtable question needs its own reader.
_MAP_ROW = re.compile(
    r"^\s+(\d{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(?:(f\s*i?)\s+)?(\S+)\s*$"
)
_MAP_ROWLIKE = re.compile(r"^\s+\d{4}:[0-9a-fA-F]{8}\s+\S")


def linker_map_path(project: str | os.PathLike) -> Path:
    return Path(project) / "orig" / "373307D9" / "ham_xbox_r.map"


def icf_alias_map_path(project: str | os.PathLike) -> Path:
    return Path(project) / "build" / "373307D9" / "icf_aliases.map"


def _parse_map_file(path: Path) -> Tuple[Dict[str, str], int, int]:
    sym2addr: Dict[str, str] = {}
    parsed = unparsed = 0
    with open(path, errors="replace") as fh:
        for line in fh:
            m = _MAP_ROW.match(line)
            if not m:
                if _MAP_ROWLIKE.match(line):
                    unparsed += 1
                continue
            parsed += 1
            sym2addr.setdefault(m.group(3), m.group(4).lower())
    return sym2addr, parsed, unparsed


def parse_linker_map(project: str | os.PathLike) -> Tuple[Dict[str, str], Dict[str, int]]:
    """symbol -> 8-hex address, plus parse statistics.

    Reads BOTH maps and merges them, because the target side of a data diff is
    named in objdiff's vocabulary, not the linker's:

      `orig/373307D9/ham_xbox_r.map`     real MSVC symbols -> address
      `build/373307D9/icf_aliases.map`   objdiff's SYNTHETIC ICF-group aliases
                                         (`OnlyReturns`, `merged_<Something>`)
                                         -> the address of the folded group

    Reading only the linker map -- the obvious implementation, and the one the
    first draft of this file shipped -- leaves every ICF alias unresolvable, so
    `?RefOwner@Object@Hmx@@` (0x823e3b70) vs `OnlyReturns` (also 0x823e3b70)
    reads as a divergence when it is the single most common benign fold in the
    binary.  That one omission turned 5 real slots in `flow/` into 184.

    Returns (sym2addr, stats); stats reports each file's parsed/unparsed counts
    so a caller can state what the parser itself dropped.
    """
    lm, lp, lu = _parse_map_file(linker_map_path(project))
    stats = {"rows_parsed": lp, "rows_rowlike_unparsed": lu,
             "icf_alias_rows": 0, "icf_alias_map_present": False}
    icf_path = icf_alias_map_path(project)
    if icf_path.exists():
        im, ip, _ = _parse_map_file(icf_path)
        stats["icf_alias_rows"] = ip
        stats["icf_alias_map_present"] = True
        # Aliases are synthetic and never collide with real symbol names; a
        # real name always wins if one somehow exists.
        for k, v in im.items():
            lm.setdefault(k, v)
    return lm, stats


# --------------------------------------------------------------------------
# Symbol enumeration from the TARGET split objects
# --------------------------------------------------------------------------
def _load_coffx():
    """Import the repo's COFF reader without taking ownership of it."""
    analysis = Path(__file__).resolve().parent.parent / "analysis"
    if str(analysis) not in sys.path:
        sys.path.insert(0, str(analysis))
    import coffx  # type: ignore

    return coffx


def enumerate_target_symbols(
    project: str | os.PathLike,
    symbol_glob: str = "??_7*",
    unit_glob: str = "*",
) -> Tuple[List[Tuple[str, str]], Dict[str, Any]]:
    """Every DEFINED (unit, symbol) pair in the TARGET split objects.

    The universe is the target side on purpose: a slot we fail to emit at all is
    exactly the bug class this sweep exists to find, and enumerating our own
    build would make those invisible.

    Returns (pairs, stats).  `stats["matched"]` is the universe INCLUDING the
    undefined externals that `stats["undefined_external"]` counts, so the caller
    can declare a denominator that adds up.
    """
    coffx = _load_coffx()
    project = Path(project)
    cfg = json.loads((project / "objdiff.json").read_text())
    units = cfg.get("units") or []

    pairs: List[Tuple[str, str]] = []
    stats = {
        "units_declared": len(units),
        "units_selected": 0,
        "units_missing_object": 0,
        "units_unreadable_object": 0,
        "matched": 0,
        "undefined_external": 0,
    }
    for u in units:
        name = u.get("name") or ""
        tp = u.get("target_path")
        if not fnmatch.fnmatch(name, unit_glob) or not tp:
            continue
        stats["units_selected"] += 1
        p = project / tp
        if not p.exists():
            stats["units_missing_object"] += 1
            continue
        try:
            syms = coffx.read_coff(p.read_bytes())[1]
        except Exception:  # noqa: BLE001
            stats["units_unreadable_object"] += 1
            continue
        for s in syms:
            if not fnmatch.fnmatchcase(s.name, symbol_glob):
                continue
            stats["matched"] += 1
            # COFF section index 0 == UNDEFINED: an external *reference* to a
            # vtable this TU merely uses.  objdiff answers "Symbol not found in
            # target" for those, and counting them as errors made 43% of the
            # first flow/ sweep look like tool failure.  Deliberate, counted.
            if getattr(s, "sec", 0) == 0:
                stats["undefined_external"] += 1
                continue
            pairs.append((name, s.name))
    pairs.sort()
    return pairs, stats


# --------------------------------------------------------------------------
# One-shot data diff
# --------------------------------------------------------------------------
def objdiff_cli(project: str | os.PathLike) -> Path:
    return Path(project) / "bin" / "objdiff-cli"


_VERSION_CACHE: Dict[str, str] = {}


def objdiff_version(project: str | os.PathLike) -> str:
    """`objdiff-cli --version`, verbatim, cached per project.

    The version string carries the git short hash and a build fingerprint, which
    is a far better provenance record than a binary mtime: `bin/objdiff-cli` is
    a SYMLINK shared with ../rb3 and ../rb3-xenon and NOTHING in any of the three
    ninja graphs rebuilds it, so the detector semantics behind a recorded number
    can change with no edge firing anywhere.  v4.2.6 renamed the population of
    LINKER_MERGED to ~2% of what v4.2.5 called by that name.
    """
    key = str(Path(project).resolve())
    if key not in _VERSION_CACHE:
        try:
            proc = subprocess.run([str(objdiff_cli(project)), "--version"],
                                  capture_output=True, text=True, timeout=60)
            _VERSION_CACHE[key] = (proc.stdout or proc.stderr or "").strip() or "unknown"
        except Exception:  # noqa: BLE001
            _VERSION_CACHE[key] = "unknown"
    return _VERSION_CACHE[key]


def _project_reloc_ruler(project: str | os.PathLike) -> str:
    """The `functionRelocDiffs` value `objdiff.json` will impose if we pass none.

    Omitting `-c` does NOT get objdiff's own default; it gets the PROJECT's, and
    the project config travels with the repo while the binary is shared across
    three of them.  A census that does not resolve this cannot say which ruler
    it ran under.
    """
    cfg = Path(project) / "objdiff.json"
    try:
        doc = json.loads(cfg.read_text())
    except (OSError, json.JSONDecodeError):
        return "unknown"
    val = (doc.get("custom_config") or doc).get("functionRelocDiffs")
    if val is None:
        # objdiff.json nests it; scan for the key anywhere one level down.
        for v in doc.values():
            if isinstance(v, dict) and "functionRelocDiffs" in v:
                return str(v["functionRelocDiffs"])
    return str(val) if val is not None else "unknown"


#: objdiff serialises the MakeString variant two ways in ONE document: serde's
#: `rename_all = "SCREAMING_SNAKE_CASE"` splits the internal capital in
#: `MakeStringTemplateMismatch` for `patterns[].pattern`, while `as_str` --
#: which feeds `patterns_checked` and every human-readable surface -- does not.
#: `sync_objdiff` compared against the second spelling and so could never set
#: `has_makestring_mismatch` on any row.  Canonicalise onto the `as_str` form.
_PATTERN_ALIASES = {
    "MAKE_STRING_TEMPLATE_MISMATCH": "MAKESTRING_TEMPLATE_MISMATCH",
}


def canonical_pattern_name(pattern: str) -> str:
    return _PATTERN_ALIASES.get(pattern, pattern)


def diff_symbol(
    project: str | os.PathLike,
    unit: str,
    symbol: str,
    include_data: bool = True,
    include_instructions: bool = False,
    timeout: int = 180,
) -> Dict[str, Any]:
    """One-shot `objdiff-cli diff` for a single symbol; returns parsed JSON.

    Raises RuntimeError with the CLI's own message on failure -- never returns a
    plausible zero, which is how the ancestor scanners produced wrong totals.
    """
    cmd = [
        str(objdiff_cli(project)),
        "diff",
        "-p", str(project),
        "-u", unit,
        symbol,
        "-f", "json",
    ]
    if include_data:
        cmd.append("--include-data")
    if include_instructions:
        cmd.append("--include-instructions")
    proc = subprocess.run(
        cmd, capture_output=True, text=True, timeout=timeout, cwd=str(project)
    )
    out = proc.stdout
    start = out.find("{")
    if proc.returncode != 0 or start < 0:
        # objdiff writes an informational "Loaded N ICF equivalence entries"
        # banner to stderr on EVERY run.  Reporting it as the error made every
        # failure look identical and hid "Symbol not found in target".
        lines = [
            ln.strip()
            for ln in ((proc.stderr or "") + "\n" + out).splitlines()
            if ln.strip() and "ICF equivalence entries" not in ln
            and not ln.startswith("Symbol index built")
            and not ln.startswith("Batch mode restricted")
        ]
        raise RuntimeError(lines[0] if lines else f"objdiff-cli exited {proc.returncode}")
    return json.loads(out[start:])


# --------------------------------------------------------------------------
# Adjudication: which relocation rows are REAL divergences
# --------------------------------------------------------------------------
ICF_ARTIFACT = re.compile(r"^(merged_[0-9A-Fa-f]+|OnlyReturns|Returns\d+|merged_Returns\d+)$")


def adjudicate_relocations(
    data_diff: Dict[str, Any],
    sym2addr: Dict[str, str],
) -> List[Dict[str, Any]]:
    """The published method, verbatim.

    Keep a relocation row when `kind != "equal"` AND the two sides resolve to
    DIFFERENT addresses in `ham_xbox_r.map`.  Equal addresses are a proven ICF
    fold: the linker put our correct body and the target's name at one address,
    so the name difference is cosmetic.

    Classification of the kept rows.  The first two are the PUBLISHED tier --
    the doc's phrase "where target AND base symbols resolve to different
    addresses" is only satisfiable when both sides name something:

      wrong-target        both sides name a symbol, both resolve, addresses differ
      unresolved-target   both sides name a symbol, at least one is absent from
                          both maps, so a fold cannot be proven

    The next two are a SEPARATE tier (`length` findings).  They are real signal
    -- an over-long or truncated vtable -- but they are not slot-for-slot
    divergences and were not part of the 141/52 count:

      base-only           we emit a slot the target does not (kind=insert)
      target-only         the target has a slot we do not (kind=delete)
    """
    kept: List[Dict[str, Any]] = []
    for r in data_diff.get("relocations") or []:
        kind = r.get("kind")
        if kind == "equal":
            continue
        tgt = r.get("target_symbol") or ""
        base = r.get("base_target_symbol") or ""
        if kind not in ("insert", "delete") and tgt and not base:
            # For `replace`, objdiff emits base_target_symbol ONLY when it
            # DIFFERS, so a target name with no base name means both sides name
            # the same symbol: same name -> same address -> benign.
            #
            # That reading is WRONG for `delete`, where the base side genuinely
            # has no slot at all.  Applying it unconditionally (the first draft
            # here) silently discarded every target-only slot -- including the
            # `??_R4` RTTI locators -- as if they matched.  Gate on `kind`.
            continue
        ta = sym2addr.get(tgt) if tgt else None
        ba = sym2addr.get(base) if base else None
        if tgt and base:
            if ta and ba and ta == ba:
                continue  # proven ICF fold
            cls = "wrong-target" if (ta and ba) else "unresolved-target"
        elif base and not tgt:
            cls = "base-only"
        elif tgt and not base:
            cls = "target-only"
        else:
            continue
        kept.append(
            {
                "offset": r.get("offset"),
                "kind": kind,
                "class": cls,
                "target_symbol": tgt,
                "target_addr": ta,
                "base_target_symbol": base,
                "base_addr": ba,
                "target_is_icf_artifact": bool(tgt and ICF_ARTIFACT.match(tgt)),
            }
        )
    return kept


def vtable_class_name(symbol: str) -> str:
    """`??_7Flow@@6BRndPollable@@@` -> `Flow`.  Best-effort, for grouping only."""
    m = re.match(r"^\?\?_7(.+?)@@6", symbol)
    if not m:
        return symbol
    name = m.group(1)
    # strip a template argument list so `?$Foo@VBar@@` groups as `?$Foo`
    return name.split("@")[0] if not name.startswith("?$") else name


# --------------------------------------------------------------------------
# The sweep
# --------------------------------------------------------------------------
def sweep_data_symbols(
    project: str | os.PathLike,
    symbol_glob: str = "??_7*",
    unit_glob: str = "*",
    max_symbols: Optional[int] = None,
    workers: int = 12,
    scanner_name: str = "symbol_sweep.vtable_slots",
) -> Dict[str, Any]:
    """Diff every matching data symbol; return findings + a coverage block."""
    cov = make_coverage(scanner_name)
    sym2addr, mapstats = parse_linker_map(project)
    cov.note(
        f"linker map: {mapstats['rows_parsed']} rows parsed, "
        f"{mapstats['rows_rowlike_unparsed']} row-like lines unparsed (section headers); "
        f"icf_aliases.map: {mapstats['icf_alias_rows']} alias rows"
        + ("" if mapstats["icf_alias_map_present"]
           else "  !! icf_aliases.map ABSENT -- every ICF fold will read as a divergence")
    )

    pairs, estats = enumerate_target_symbols(project, symbol_glob, unit_glob)
    cov.note(
        f"units: {estats['units_selected']}/{estats['units_declared']} selected, "
        f"{estats['units_missing_object']} missing object, "
        f"{estats['units_unreadable_object']} unreadable object"
    )
    cov.universe(
        estats["matched"],
        f"(unit, symbol) pairs matching {symbol_glob!r} in target split objects "
        f"under units {unit_glob!r}",
    )
    if estats["undefined_external"]:
        cov.drop("undefined-external-reference", estats["undefined_external"])
    if max_symbols is not None and len(pairs) > max_symbols:
        before = len(pairs)
        pairs = pairs[:max_symbols]
        cov.cap("--max-symbols", max_symbols, before=before, after=max_symbols)

    findings: List[Dict[str, Any]] = []
    errors: List[Dict[str, str]] = []

    def work(pair: Tuple[str, str]):
        unit, sym = pair
        try:
            data = diff_symbol(project, unit, sym, include_data=True)
        except Exception as e:  # noqa: BLE001
            return pair, None, str(e)
        return pair, data, None

    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(work, p) for p in pairs]
        for f in as_completed(futs):
            (unit, sym), data, err = f.result()
            if err is not None:
                # Counted, not silent: an unreadable symbol is a hole in the
                # answer and the coverage block must say so.
                cov.drop("objdiff-error")
                errors.append({"unit": unit, "symbol": sym, "error": err})
                continue
            cov.examine()
            dd = data.get("data_diff")
            if not dd:
                continue
            for row in adjudicate_relocations(dd, sym2addr):
                row["unit"] = unit
                row["symbol"] = sym
                row["class_name"] = vtable_class_name(sym)
                findings.append(row)

    # Dedup: the same vtable is a COMDAT in every TU that includes the header,
    # so one divergent slot legitimately shows up once per unit.  The published
    # count is per (symbol, offset).
    def dedup_rows(rows: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        d: Dict[Tuple[str, Any], Dict[str, Any]] = {}
        for row in rows:
            key = (row["symbol"], row["offset"])
            if key not in d:
                d[key] = dict(row, units=[row["unit"]])
            else:
                d[key]["units"].append(row["unit"])
        out = sorted(d.values(), key=lambda r: (r["class_name"], r["symbol"], r["offset"] or 0))
        for s in out:
            s["units"] = sorted(set(s["units"]))
            s.pop("unit", None)
        return out

    SLOT_CLASSES = {"wrong-target", "unresolved-target"}
    slots = dedup_rows([r for r in findings if r["class"] in SLOT_CLASSES])
    length = dedup_rows([r for r in findings if r["class"] not in SLOT_CLASSES])

    by_class = collections.Counter(s["class_name"] for s in slots)
    by_kind = collections.Counter(s["class"] for s in slots)
    cov.extra(
        divergent_slots=len(slots),
        length_findings=len(length),
        divergent_rows_before_dedup=len(findings),
    )
    return {
        "kind": "vtable_slots" if symbol_glob == "??_7*" else "data_symbols",
        "symbol_glob": symbol_glob,
        "unit_glob": unit_glob,
        "divergent_slots": len(slots),
        "length_findings": len(length),
        "divergent_rows_before_dedup": len(findings),
        "by_class": dict(by_class.most_common()),
        "by_finding_class": dict(by_kind.most_common()),
        "slots": slots,
        "length": length,
        "errors": errors[:50],
        "error_count": len(errors),
        "_coverage": cov.as_dict(),
        "_coverage_render": cov.render(),
    }


#: `functionRelocDiffs` values, and what each one is FOR.  A sweep that does
#: not state which of these it ran under has not stated its result: the same
#: objects, the same symbols and the same detectors give different pattern
#: populations under each.  See CLAUDE.md, "The ruler matters".
RELOC_RULERS = {
    # objdiff's relocation-BLIND mode.  `detect_callee_divergences`,
    # `detect_prologue_mismatch`, `detect_makestring_template_mismatch` and
    # `detect_scope_counter_mismatch` all read `match_type == "diff_arg"` on a
    # `bl`, which this mode reports as `equal`.  Under `none` those four
    # detectors are STRUCTURALLY STARVED and report an honest-looking zero.
    # Six `has_*` columns in decomp.db died of exactly this.
    "none": "relocation-blind; the four callee detectors cannot fire",
    # The project's graded ruler and `report.json`'s (`objdiff.json` sets it).
    # Consults build/<v>/icf_aliases.map and applies the placeholder / counter /
    # anchor exemptions, so a pattern that survives here is CHARGED.
    "name_check": "graded: charges relocation NAMES, forgives proven folds",
    # Charges every name difference including the ~2,992 adjudicated /OPT:ICF
    # folds, plus addends.  Measured ~99.8% noise over a 14-function sample.
    "all": "charges every name AND addend difference; mostly noise",
}

#: The sweep REFUSES this one for a pattern census rather than returning a
#: plausible zero.  `include_patterns=True` + `reloc_config="none"` is not a
#: quieter measurement, it is a measurement of a starved detector.
_PATTERN_BLIND_RULERS = {"none"}


class RelocBlindPatternError(RuntimeError):
    """A pattern sweep was asked to run under a relocation-blind ruler.

    Deliberately a hard error.  The failure this prevents is not a wrong
    number, it is a CONFIDENT ZERO: `functionRelocDiffs=none` makes
    LINKER_MERGED / WRONG_CALLEE / TEMPLATE_INSTANTIATION_MISMATCH /
    REGISTER_SAVE_HELPER_MISMATCH / UNVERIFIABLE_CALLEE_NAME / PROLOGUE_MISMATCH
    / MAKESTRING_TEMPLATE_MISMATCH / SCOPE_COUNTER_MISMATCH structurally
    unable to fire, and an empty bucket reads as "this class is exhausted".
    """


def _batch_shard(
    project: str,
    symbols: List[str],
    unit: Optional[str],
    include_instructions: bool,
    reloc_config: Optional[str],
    timeout: int,
) -> Tuple[List[Dict[str, Any]], List[str], int, str]:
    """One objdiff `--batch` process over one shard.  Returns (rows, raw_lines_bad, rc, stderr)."""
    cmd = [
        str(objdiff_cli(project)), "diff", "-p", str(project),
        "--batch", "-f", "json",
    ]
    if reloc_config:
        cmd.extend(["-c", f"functionRelocDiffs={reloc_config}"])
    if unit:
        cmd.extend(["-u", unit])
    if include_instructions:
        cmd.append("--include-instructions")
    proc = subprocess.run(
        cmd,
        input="\n".join(symbols) + "\n",
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=str(project),
    )
    rows: List[Dict[str, Any]] = []
    bad: List[str] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            bad.append(line[:120])
    return rows, bad, proc.returncode, (proc.stderr or "").strip()


def sweep_functions(
    project: str | os.PathLike,
    symbols: Sequence[str],
    unit: Optional[str] = None,
    include_instructions: bool = False,
    max_symbols: Optional[int] = None,
    timeout: int = 1800,
    scanner_name: str = "symbol_sweep.functions",
    reloc_config: Optional[str] = None,
    include_patterns: bool = False,
    jobs: int = 1,
    allow_blind_patterns: bool = False,
) -> Dict[str, Any]:
    """Batch-diff many FUNCTION symbols in one objdiff process (`--batch`, JSONL).

    `--batch` is ~1 process instead of N, which is the whole reason agents wrote
    their own loops around it.  It refuses `--include-data` (objdiff-side: batch
    does not compute data-section diffs), so the data question must go through
    `sweep_data_symbols`.

    `reloc_config` names the `functionRelocDiffs` ruler.  `None` means "whatever
    `objdiff.json` says", which in this repo is `name_check` -- fine for a
    percentage, NOT fine to leave unstated in a pattern census, so the ruler
    actually used is echoed back in the result as `reloc_ruler`.

    `include_patterns` keeps `analysis.patterns` (the detector payload) on each
    row and builds a histogram.  It raises `RelocBlindPatternError` under
    `functionRelocDiffs=none`, where those detectors cannot fire at all.

    `jobs` shards the symbol list across N objdiff processes.  Still one process
    per SHARD, never one per symbol.
    """
    cov = make_coverage(scanner_name)
    project = str(Path(project).resolve())

    ruler = reloc_config or _project_reloc_ruler(project)
    # The escape hatch exists for ONE purpose: measuring the starvation itself.
    # A guard whose premise has never been checked is an assertion; running the
    # blind ruler deliberately, and labelling the result a negative control, is
    # what turns it into a fact. It must never be used to source a population.
    if include_patterns and ruler in _PATTERN_BLIND_RULERS and not allow_blind_patterns:
        raise RelocBlindPatternError(
            f"REFUSING a pattern sweep under functionRelocDiffs={ruler}: the "
            f"callee/prologue/MakeString/scope detectors all key off "
            f"`match_type == \"diff_arg\"` on a `bl`, and this ruler reports "
            f"those instructions as equal. Every pattern bucket would come back "
            f"0 -- not because the build is clean, but because the detector was "
            f"starved. Use functionRelocDiffs=name_check (the graded ruler, and "
            f"report.json's) or =all (charges everything, ~99.8% noise)."
        )

    symbols = [s.strip() for s in symbols if s.strip()]
    cov.universe(len(symbols), "function symbols supplied to the sweep")
    cov.note(f"functionRelocDiffs={ruler} -- {RELOC_RULERS.get(ruler, 'unrecognised ruler')}")
    if reloc_config is None:
        cov.note("ruler taken from the project's objdiff.json, not from the caller")
    if max_symbols is not None and len(symbols) > max_symbols:
        before = len(symbols)
        symbols = symbols[:max_symbols]
        cov.cap("--max-symbols", max_symbols, before=before, after=max_symbols)

    jobs = max(1, int(jobs))
    if jobs > 1 and symbols:
        size = (len(symbols) + jobs - 1) // jobs
        shards = [symbols[i:i + size] for i in range(0, len(symbols), size)]
    else:
        shards = [symbols] if symbols else []

    raw: List[Dict[str, Any]] = []
    stderr_tails: List[str] = []
    if len(shards) == 1:
        r, bad, _rc, err = _batch_shard(
            project, shards[0], unit, include_instructions, reloc_config, timeout)
        raw.extend(r)
        cov.drop("jsonl-unparseable", len(bad))
        stderr_tails.extend(err.splitlines()[-3:])
    elif shards:
        with ThreadPoolExecutor(max_workers=len(shards)) as pool:
            futs = [pool.submit(_batch_shard, project, s, unit,
                                include_instructions, reloc_config, timeout)
                    for s in shards]
            for f in as_completed(futs):
                r, bad, _rc, err = f.result()
                raw.extend(r)
                cov.drop("jsonl-unparseable", len(bad))
                stderr_tails.extend(err.splitlines()[-1:])

    rows: List[Dict[str, Any]] = []
    errored: List[Dict[str, str]] = []
    seen = set()
    pattern_hist: Dict[str, int] = collections.Counter()
    for d in raw:
        sym = d.get("symbol")
        seen.add(sym)
        if d.get("error"):
            # objdiff emits {"error":"not_found","symbol":...} as a normal JSONL
            # row.  Counting it as `examined` would put a symbol we could not
            # measure into the denominator's numerator -- the exact shape of the
            # six instrument defects this project found in one week.
            cov.drop(f"objdiff-{d['error']}")
            errored.append({"symbol": sym, "error": d["error"]})
            continue
        cov.examine()
        if include_patterns:
            pats = ((d.get("analysis") or {}).get("patterns") or [])
            for p in pats:
                pattern_hist[canonical_pattern_name(p.get("pattern", ""))] += 1
        else:
            d.pop("analysis", None)
        rows.append(d)
    missing = [s for s in symbols if s not in seen]
    if missing:
        cov.drop("no-jsonl-row-emitted", len(missing))
    out = {
        "kind": "functions",
        "reloc_ruler": ruler,
        "reloc_ruler_meaning": RELOC_RULERS.get(ruler, "unrecognised ruler"),
        "objdiff_version": objdiff_version(project),
        "shards": len(shards),
        "rows": rows,
        "errored_symbols": errored[:50],
        "errored_count": len(errored),
        "missing_symbols": missing[:50],
        "missing_count": len(missing),
        "stderr_tail": stderr_tails[-3:],
        "_coverage": cov.as_dict(),
        "_coverage_render": cov.render(),
    }
    if include_patterns:
        out["pattern_histogram"] = dict(pattern_hist.most_common())
        out["patterns_checked"] = sorted({
            p for d in raw
            for p in ((d.get("analysis") or {}).get("patterns_checked") or [])
        })
    return out


# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------
def render_markdown(result: Dict[str, Any], top: int = 60) -> str:
    L: List[str] = []
    L.append(result["_coverage_render"])
    L.append("")
    kind = result.get("kind")
    if kind == "functions":
        rows = result.get("rows") or []
        L.append(f"# Batch function sweep -- {len(rows)} symbols diffed")
        L.append("")
        L.append(f"**Ruler: `functionRelocDiffs={result.get('reloc_ruler')}`** "
                 f"({result.get('reloc_ruler_meaning')}), "
                 f"`{result.get('objdiff_version')}`.")
        if result.get("pattern_histogram") is not None:
            hist = result["pattern_histogram"]
            L.append("")
            L.append(f"## Pattern histogram ({len(hist)} of "
                     f"{len(result.get('patterns_checked') or [])} checked patterns fired)")
            L.append("")
            L.append("| pattern | functions |")
            L.append("|---|---:|")
            for k, v in hist.items():
                L.append(f"| `{k}` | {v} |")
        # Prefer `canonical_match_percent` (objdiff >= 4.2.4). The older
        # `normalized_match_percent` is a DOCUMENTED MISNOMER that carries the
        # FUZZY score -- objdiff-cli computed all three of its percent fields
        # from match_percent and never read match_percent_normalized at all.
        # Labelling that column "norm" invited exactly one comparison: against
        # report.json's match_percent_normalized, which is a different ruler.
        # That manufactured four phantom regressions for a lane on 2026-08-20.
        # Column headers below now say which ruler each one is.
        def _canon(r):
            v = r.get("canonical_match_percent")
            return v if v is not None else r.get("normalized_match_percent")

        rows_sorted = sorted(rows, key=lambda r: (_canon(r) or r.get("fuzzy_match_percent") or 0.0))
        L.append("")
        have_canonical = any(r.get("canonical_match_percent") is not None for r in rows)
        if not have_canonical:
            L.append(
                "> ⚠ This objdiff-cli predates `canonical_match_percent` (4.2.4). The "
                "`match% (fuzzy)` column below is all that is available; do NOT compare "
                "it against `report.json.match_percent_normalized` -- different rulers."
            )
            L.append("")
        L.append("| match% (canonical) | match% (fuzzy) | match% (raw) | symbol | unit |")
        L.append("|---|---|---|---|---|")
        for r in rows_sorted[:top]:
            c = r.get("canonical_match_percent")
            fz = r.get("fuzzy_match_percent")
            raw = r.get("raw_match_percent")
            L.append(
                f"| {c if c is not None else '-'} | {fz if fz is not None else '-'} "
                f"| {raw if raw is not None else '-'} "
                f"| `{r.get('symbol')}` | {r.get('unit', '')} |"
            )
        if len(rows_sorted) > top:
            L.append(f"| ... | | +{len(rows_sorted) - top} more (use format=json) | |")
        if result.get("errored_count"):
            L.append("")
            L.append(f"**{result['errored_count']} symbols objdiff could not diff** "
                     "(dropped, NOT scored as 0): "
                     + ", ".join(f"`{e['symbol']}` ({e['error']})"
                                 for e in result["errored_symbols"][:5]))
        if result.get("missing_count"):
            L.append("")
            L.append(f"**{result['missing_count']} supplied symbols produced no row at all** "
                     f"(first few: {', '.join(result['missing_symbols'][:5])})")
        return "\n".join(L)

    n = result["divergent_slots"]
    L.append(f"# Divergent slots: {n}   (+{result.get('length_findings', 0)} length findings)")
    L.append("")
    L.append("Method: every relocation row with `kind != equal` where BOTH sides name a symbol "
             "and the two names resolve to DIFFERENT addresses across `ham_xbox_r.map` + "
             "`icf_aliases.map`. Equal addresses = proven ICF fold = benign. Rows where only one "
             "side names a symbol (`insert`/`delete`) are a vtable-LENGTH finding, counted "
             "separately -- they are real signal but are not slot-for-slot divergences.")
    L.append("")
    if result.get("error_count"):
        L.append(f"**{result['error_count']} symbols errored** and are counted as drops, not zeros.")
        L.append("")
    if result["by_finding_class"]:
        L.append("| finding class | n |")
        L.append("|---|---|")
        for k, v in result["by_finding_class"].items():
            L.append(f"| {k} | {v} |")
        L.append("")
    if result["by_class"]:
        L.append("| class | divergent slots |")
        L.append("|---|---|")
        for k, v in list(result["by_class"].items())[:top]:
            L.append(f"| {k} | {v} |")
        L.append("")
    L.append("| class | +off | finding | target | base |")
    L.append("|---|---|---|---|---|")
    for s in result["slots"][:top]:
        off = s["offset"]
        L.append(
            f"| {s['class_name']} | {('0x%x' % off) if isinstance(off, int) else off} "
            f"| {s['class']} | `{s['target_symbol'] or '-'}` | `{s['base_target_symbol'] or '-'}` |"
        )
    if len(result["slots"]) > top:
        L.append(f"| ... | | | +{len(result['slots']) - top} more (use format=json) | |")
    return "\n".join(L)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--project", default=".", help="project/worktree directory")
    ap.add_argument("--kind", default="vtable_slots",
                    choices=["vtable_slots", "data_symbols", "functions"])
    ap.add_argument("--symbol-glob", default=None,
                    help="fnmatch glob over symbol names (default ??_7* for vtable_slots)")
    ap.add_argument("--unit-glob", default="*", help="fnmatch glob over unit names")
    ap.add_argument("--symbols-file", default=None, help="functions kind: symbols, one per line")
    ap.add_argument("--unit", default=None, help="functions kind: restrict batch to this unit")
    ap.add_argument("--max-symbols", type=int, default=None,
                    help="TRUNCATE the sweep (a truncated run is reported as TRUNCATED)")
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--reloc-config", default=None, choices=sorted(RELOC_RULERS),
                    help="functions kind: functionRelocDiffs ruler. Default = "
                         "whatever objdiff.json imposes (name_check here). The "
                         "ruler used is always echoed in the output.")
    ap.add_argument("--include-patterns", action="store_true",
                    help="functions kind: keep analysis.patterns and histogram "
                         "them. Refuses functionRelocDiffs=none, where the "
                         "callee detectors cannot fire.")
    ap.add_argument("--format", default="markdown", choices=["markdown", "json"])
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    if args.kind == "functions":
        syms: List[str] = []
        if args.symbols_file:
            syms = Path(args.symbols_file).read_text().splitlines()
        elif not sys.stdin.isatty():
            syms = sys.stdin.read().splitlines()
        result = sweep_functions(args.project, syms, unit=args.unit,
                                 max_symbols=args.max_symbols,
                                 reloc_config=args.reloc_config,
                                 include_patterns=args.include_patterns,
                                 jobs=args.workers)
    else:
        glob = args.symbol_glob or ("??_7*" if args.kind == "vtable_slots" else "*")
        result = sweep_data_symbols(
            args.project,
            symbol_glob=glob,
            unit_glob=args.unit_glob,
            max_symbols=args.max_symbols,
            workers=args.workers,
            scanner_name=f"symbol_sweep.{args.kind}",
        )

    text = json.dumps(result, indent=2) if args.format == "json" else render_markdown(result)
    if args.out:
        Path(args.out).write_text(text)
        print(f"wrote {args.out}", file=sys.stderr)
    else:
        print(text)
    cov = result["_coverage"]
    if cov.get("truncated"):
        return 3
    if cov.get("unaccounted"):
        return 4
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
