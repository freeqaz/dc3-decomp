#!/usr/bin/env python3
"""TU-boundary inlining control catalog.

Identifies functions whose bodies in headers cause MSVC PPC to inline them
when the target binary kept them as outlined `bl` calls.  The fix is moving
the body to the corresponding .cpp file.

Background
----------
MSVC for Xbox 360 has an inlining threshold of 150 counted IL nodes.  Small
accessor/helper functions defined in headers fall well below this threshold,
so the compiler inlines them at every call site.  When the original binary
was built with a slightly different compiler version or flags, those same
functions may have been kept as outlined calls.  The resulting mismatch shows
up as missing `bl` instructions in our build vs the target.

Usage
-----
    python -m scripts.analysis.inlining_catalog scan-headers [--src-dir DIR]
    python -m scripts.analysis.inlining_catalog catalog [--json]
    python -m scripts.analysis.inlining_catalog check <header>

COVERAGE / HONESTY NOTES  (see scripts/analysis/coverage.py)
------------------------------------------------------------
What this tool reports is narrower than what it used to claim.  The three
gaps that mattered, and what changed:

1. `scan_inline_mismatches` did ``func.get("fuzzy_match_percent", 100.0)``
   followed by ``if pct >= 100.0: continue``.  **The default was the most
   optimistic value possible**, so every function objdiff scored WITHOUT that
   key — 16,920 of 48,344 rows on this tree, i.e. every function for which we
   emitted no body at all — silently classified itself as "already matching"
   and left the population.  It now falls back to `match_percent_normalized`
   (present on all 48,344 rows) and every discard is counted.

2. The accessor regex is SINGLE-BRACE-DEPTH (``\\{([^}]*)\\}``).  Any inline
   accessor whose body contains a nested brace — an `if`/`for` block, a braced
   initialiser, a lambda — is structurally invisible to it.  The real
   denominator of this catalog is therefore "inline accessors WITHOUT nested
   braces", and it used to be presented as "inline accessors".  The regex is
   deliberately NOT fixed here (that would change what the tool finds); the
   limitation is now stated in every summary and in the coverage block.

3. `_count_includes_from_ninja` is a STUB that returns ``{}``, so every
   `include_count` is `-1`.  It now says so in the output rather than looking
   like a measurement that came back empty.

Additionally `_extract_class_context` attributes every accessor in a header to
the LAST class declared in that file — wrong for multi-class headers.  Left as
a heuristic (see the TODO there); the number of affected headers/accessors is
counted and reported so the error has a size.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402


# --------------------------------------------------------------------------- #
# Stated limitations.  These are printed with every summary; they are the
# difference between this catalog's denominator and the one a reader assumes.
# --------------------------------------------------------------------------- #

LIMITATION_REGEX_BRACE_DEPTH = (
    "accessor detection is SINGLE-BRACE-DEPTH (`{[^}]*}`). A body containing a "
    "nested brace (if/for/switch block, braced initialiser, lambda) is captured "
    "only UP TO THE FIRST `}`, so its statement_count and size_class are "
    "computed from a TRUNCATED fragment: e.g. "
    "`int F() const { if (v<0) { return 0; } return v; }` is recorded as the "
    "trivial one-statement body `if (v<0) { return 0;`. Such a body can also "
    "fall the wrong side of --max-statements and disappear entirely. Rows whose "
    "capture has unbalanced braces are flagged `body_truncated` and counted, but "
    "the denominator is still 'what this regex could see', not 'accessors'."
)
LIMITATION_CLASS_CONTEXT = (
    "class attribution uses the LAST `class`/`struct` declared in the file, so "
    "in a multi-class header every accessor is attributed to the last class. "
    "See _extract_class_context()."
)
LIMITATION_INCLUDE_COUNT = (
    "include_count is UNIMPLEMENTED (_count_includes_from_ninja is a stub that "
    "returns {}); every include_count is -1 and means 'not measured', not 'zero "
    "TUs include this header'."
)
LIMITATION_HEADERS_ONLY = (
    "only *.h files are scanned; inline bodies in .hpp/.inl/.cpp are not seen."
)

ALL_LIMITATIONS = [
    LIMITATION_REGEX_BRACE_DEPTH,
    LIMITATION_CLASS_CONTEXT,
    LIMITATION_INCLUDE_COUNT,
    LIMITATION_HEADERS_ONLY,
]

# Sentinel for an include count that was never measured.
INCLUDE_COUNT_UNIMPLEMENTED = -1


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------

@dataclass
class HeaderAccessor:
    """A small inline function found in a header file."""
    header: str            # relative path from project root
    class_name: str        # enclosing class (empty for free functions)
    method_name: str       # function / method name
    statement_count: int   # approximate statement count
    body: str              # raw body text (trimmed)
    is_const: bool = False
    return_type: str = ""
    size_class: str = ""   # "trivial", "small", "medium"

    def __post_init__(self):
        if self.statement_count <= 1:
            self.size_class = "trivial"
        elif self.statement_count <= 3:
            self.size_class = "small"
        else:
            self.size_class = "medium"

    @property
    def body_truncated(self) -> bool:
        """True when the captured body has unbalanced braces.

        The accessor regex stops at the FIRST `}`, so a nested-brace body is
        captured as a fragment.  `size`/`size_class` are then derived from that
        fragment and are wrong.  This flag is the countable evidence — see
        LIMITATION_REGEX_BRACE_DEPTH.
        """
        return self.body.count("{") != self.body.count("}")

    def to_dict(self) -> dict:
        return {
            "header": self.header,
            "class": self.class_name,
            "method": self.method_name,
            "size": self.statement_count,
            "size_class": self.size_class,
            "body": self.body,
            "is_const": self.is_const,
            "return_type": self.return_type,
            # size/size_class are UNRELIABLE when this is true.
            "body_truncated": self.body_truncated,
        }


@dataclass
class CatalogEntry:
    """Per-method catalog entry combining scan data with known status."""
    accessor: HeaderAccessor
    status: str = "candidate"           # "outlined", "candidate", "keep_inline"
    fixed_functions: list[str] = field(default_factory=list)
    notes: str = ""

    def to_dict(self) -> dict:
        d = self.accessor.to_dict()
        d["status"] = self.status
        if self.fixed_functions:
            d["fixed_functions"] = self.fixed_functions
        if self.notes:
            d["notes"] = self.notes
        return d


# ---------------------------------------------------------------------------
# Known outline fixes (from proven decomp work)
# ---------------------------------------------------------------------------

_KNOWN_OUTLINES: list[dict] = [
    {
        "header": "ui/UIListWidget.h",
        "class": "UIListWidget",
        "method": "DisabledAlphaScale",
        "status": "outlined",
        "fixed_functions": ["UIListSlot::Draw"],
        "notes": "Moved body to UIListWidget.cpp; fixed UIListSlot::Draw 96.6->100%",
    },
    {
        "header": "ui/UIListWidget.h",
        "class": "UIListWidget",
        "method": "ParentList",
        "status": "outlined",
        "fixed_functions": ["UIListSlot::Draw"],
        "notes": "Moved body to UIListWidget.cpp; fixed UIListSlot::Draw 96.6->100%",
    },
]


# ---------------------------------------------------------------------------
# Accessor patterns (regexes applied to header text)
# ---------------------------------------------------------------------------

# Match a simple inline method body inside a class definition.
# Captures: return_type, method_name, params, const, body
# This deliberately does NOT match virtual methods with empty bodies (those
# are intentional no-ops, not accessors).
#
# !! DENOMINATOR WARNING — see LIMITATION_REGEX_BRACE_DEPTH.
# `\{([^}]*)\}` is single-brace-depth.  `int F() { if (a) { return 1; } return 0; }`
# does not match, so this scan cannot see it AT ALL — it is not "examined and
# rejected", it is never a row.  Do not read "N accessors" as "N accessors
# exist".  Fixing the regex is a change to what this tool FINDS and belongs in
# its own change, not in the honesty pass.
_INLINE_METHOD_RE = re.compile(
    r"""
    ^\s*                                # leading whitespace
    ((?:(?:static|inline|virtual)\s+)*) # optional qualifiers (group 1)
    ([\w:<>&*\s]+?)                     # return type (group 2)
    \s+
    (\w+)                               # method name (group 3)
    \s*\(([^)]*)\)                      # parameters (group 4)
    \s*(const)?                         # optional const (group 5)
    \s*\{([^}]*)\}                      # body between braces (group 6) — single-brace depth
    """,
    re.VERBOSE | re.MULTILINE,
)

# Accessor body patterns (one-liners returning a member)
_RETURN_MEMBER_RE = re.compile(
    r"^\s*return\s+m\w+\s*;\s*$"
)
_RETURN_MEMBER_METHOD_RE = re.compile(
    r"^\s*return\s+m\w+\.\w+\(\)\s*;\s*$"
)
_RETURN_MEMBER_DEREF_RE = re.compile(
    r"^\s*return\s+m\w+->\w+\(\)\s*;\s*$"
)
_RETURN_SIMPLE_RE = re.compile(
    r"^\s*return\s+\w+\s*;\s*$"
)
_RETURN_NULLPTR_RE = re.compile(
    r"^\s*return\s+(nullptr|0|NULL)\s*;\s*$"
)


def _count_statements(body: str) -> int:
    """Approximate statement count from a brace-delimited body."""
    body = body.strip()
    if not body:
        return 0
    # Count semicolons as a rough proxy for statements
    stmts = body.count(";")
    return max(stmts, 1) if body else 0


def _is_accessor_body(body: str) -> bool:
    """Check whether a function body looks like a simple accessor/getter."""
    lines = [l.strip() for l in body.strip().splitlines() if l.strip()]
    if not lines:
        return False
    # Single-line return of a member
    if len(lines) == 1:
        line = lines[0]
        if _RETURN_MEMBER_RE.match(line):
            return True
        if _RETURN_MEMBER_METHOD_RE.match(line):
            return True
        if _RETURN_MEMBER_DEREF_RE.match(line):
            return True
        if _RETURN_SIMPLE_RE.match(line):
            return True
        if _RETURN_NULLPTR_RE.match(line):
            return True
    return False


def _is_small_body(body: str, max_statements: int = 5) -> bool:
    """Check if body has few enough statements to be an inlining risk."""
    return _count_statements(body) <= max_statements


# ---------------------------------------------------------------------------
# Header scanning
# ---------------------------------------------------------------------------

class MissingSourceDirError(FileNotFoundError):
    """A --src-dir that does not exist.  Loud on purpose.

    The old behaviour was ``if not src_path.is_dir(): continue``, so a typo'd
    --src-dir printed `Found 0 inline accessors in headers` and exited 0 — the
    exact shape where "the input was wrong" and "there is nothing here" are
    indistinguishable.
    """


def scan_header_accessors(
    src_dirs: list[str | Path],
    *,
    max_statements: int = 5,
    project_root: str | Path | None = None,
    cov: Optional[CoverageReport] = None,
    strict_dirs: bool = False,
) -> list[HeaderAccessor]:
    """Scan header files for small inline function bodies.

    Parameters
    ----------
    src_dirs
        Directories to search recursively for .h files.
    max_statements
        Maximum statement count to include (default 5).
    project_root
        If given, header paths are made relative to this root.
    cov
        Optional CoverageReport.  When given, the universe is declared as the
        set of *.h files enumerated, and every skipped file is counted.
    strict_dirs
        Raise `MissingSourceDirError` for a src_dir that is not a directory
        instead of quietly contributing zero headers.  The CLI passes True;
        the default stays False so library callers keep their old contract.

    Returns
    -------
    List of HeaderAccessor records for every qualifying inline function.
    NOTE the denominator caveat in LIMITATION_REGEX_BRACE_DEPTH: bodies with
    nested braces are not merely rejected, they are never seen.
    """
    if project_root is not None:
        project_root = Path(project_root)
    resolved_root = project_root.resolve() if project_root else None

    # --- enumerate first, so the universe is known before any filtering ---
    missing_dirs: list[str] = []
    headers: list[Path] = []
    for src_dir in src_dirs:
        src_path = Path(src_dir).resolve()
        if not src_path.is_dir():
            missing_dirs.append(str(src_dir))
            continue
        headers.extend(src_path.rglob("*.h"))
    headers = sorted(set(headers))

    if missing_dirs and strict_dirs:
        raise MissingSourceDirError(
            "not a directory: " + ", ".join(sorted(missing_dirs))
            + " — a mistyped --src-dir used to produce 'Found 0 inline "
              "accessors' and exit 0")

    if cov is not None:
        cov.universe(len(headers), "*.h files under --src-dir (recursive)")
        for lim in ALL_LIMITATIONS:
            cov.note(lim)
        if missing_dirs:
            cov.note("src-dir(s) that do not exist and contributed ZERO headers: "
                     + ", ".join(sorted(missing_dirs)))
            cov.extra("missing_src_dirs", sorted(missing_dirs))

    accessors: list[HeaderAccessor] = []
    multiclass_headers = 0
    multiclass_accessors = 0

    for header in headers:
        # Skip CL temp dirs
        if "/_CL_" in str(header):
            if cov is not None:
                cov.drop("cl-temp-dir", note="compiler temp copy, not a source header")
            continue
        try:
            text = header.read_text(errors="replace")
        except OSError as exc:
            # Used to be a bare `continue`: an unreadable header and a header
            # with no accessors produced identical output.
            if cov is not None:
                cov.drop("header-unreadable", note=f"OSError while reading ({exc.__class__.__name__})")
            else:
                print(f"warning: cannot read {header}: {exc}", file=sys.stderr)
            continue

        header_resolved = header.resolve()
        if resolved_root and header_resolved.is_relative_to(resolved_root):
            rel = str(header_resolved.relative_to(resolved_root))
        else:
            rel = str(header)
        found = _scan_single_header(text, rel, max_statements)
        if _count_class_decls(text) > 1:
            multiclass_headers += 1
            multiclass_accessors += len(found)
        accessors.extend(found)
        if cov is not None:
            cov.examine()

    if cov is not None:
        n_trunc = sum(1 for a in accessors if a.body_truncated)
        cov.extra("accessors_found", len(accessors))
        cov.extra("accessors_with_truncated_body", n_trunc)
        cov.note(f"brace-depth damage: {n_trunc} of {len(accessors)} captured bodies "
                 f"have unbalanced braces, i.e. they were cut at the first `}}` and "
                 f"their size/size_class are wrong")
        cov.extra("multiclass_headers", multiclass_headers)
        cov.extra("accessors_possibly_misattributed", multiclass_accessors)
        cov.note(
            f"class attribution: {multiclass_accessors} of {len(accessors)} accessors "
            f"come from {multiclass_headers} headers declaring more than one "
            f"class/struct, so their `class` field may name the wrong class "
            f"(last-declaration heuristic)")
    return accessors


def _count_class_decls(text: str) -> int:
    """How many class/struct definitions this header declares.

    Only used to SIZE the `_extract_class_context` misattribution, never to
    change attribution.  Same regex as `_extract_class_context` on purpose.
    """
    return len(_CLASS_DECL_RE.findall(text))


def _scan_single_header(
    text: str,
    header_path: str,
    max_statements: int = 5,
) -> list[HeaderAccessor]:
    """Extract inline accessors from a single header's text."""
    results: list[HeaderAccessor] = []

    # Determine current class context via simple tracking
    class_name = _extract_class_context(text)

    for m in _INLINE_METHOD_RE.finditer(text):
        qualifiers = m.group(1).strip()
        return_type = m.group(2).strip()
        method_name = m.group(3).strip()
        is_const = bool(m.group(5))
        body = m.group(6).strip()

        # Skip virtual methods with empty/trivial no-op bodies
        is_virtual = "virtual" in qualifiers
        if is_virtual:
            # Virtual with empty body is an intentional no-op, not an accessor
            if not body or body.strip() in ("", "return;"):
                continue
            if _RETURN_NULLPTR_RE.match(body.strip()):
                continue

        # Count statements
        stmt_count = _count_statements(body)
        if stmt_count > max_statements:
            continue

        # Must have some body content
        if not body:
            continue

        # Check if it looks like an accessor or small helper
        is_accessor = _is_accessor_body(body)
        is_small = _is_small_body(body, max_statements)
        if not (is_accessor or is_small):
            continue

        results.append(HeaderAccessor(
            header=header_path,
            class_name=class_name,
            method_name=method_name,
            statement_count=stmt_count,
            body=body,
            is_const=is_const,
            return_type=return_type,
        ))

    return results


# Shared by _extract_class_context and _count_class_decls so the population
# being SIZED is exactly the population being mis-attributed.
_CLASS_DECL_RE = re.compile(
    r"(?:class|struct)\s+(\w+)\s*(?::\s*(?:public|private|protected))?[^;]*\{"
)


def _extract_class_context(text: str) -> str:
    """Extract the most likely enclosing class name from header text.

    Simple heuristic: find the last `class Foo` declaration.

    TODO(heuristic): this returns `matches[-1]`, i.e. the LAST class in the
    file.  In a multi-class header every accessor — including ones belonging to
    the first class — is attributed to the last one.  Deliberately NOT changed
    here: widening/repairing attribution changes what the catalog FINDS, which
    does not belong in an honesty pass.  What IS done is counting it:
    `scan_header_accessors` reports `multiclass_headers` and
    `accessors_possibly_misattributed` so the error has a measured size.  A real
    fix needs a brace-matching pass that tracks the enclosing scope per match
    offset, not a file-wide findall.
    """
    matches = _CLASS_DECL_RE.findall(text)
    return matches[-1] if matches else ""


# ---------------------------------------------------------------------------
# Catalog building
# ---------------------------------------------------------------------------

def build_catalog(
    src_dirs: list[str | Path],
    *,
    known_outlines: list[dict] | None = None,
    project_root: str | Path | None = None,
    max_statements: int = 5,
    ninja_deps_path: str | Path | None = None,
    cov: Optional[CoverageReport] = None,
    strict_dirs: bool = False,
) -> dict:
    """Build a full inlining control catalog.

    Combines header accessor scan with known outline fixes.

    `ninja_deps_path` is accepted but **has no effect**: the include-count
    estimator it feeds is an unimplemented stub (see
    `_count_includes_from_ninja` / LIMITATION_INCLUDE_COUNT).  Every
    `include_count` in the returned catalog is
    `INCLUDE_COUNT_UNIMPLEMENTED` (-1) and the catalog says so in
    `summary.limitations` and `summary.include_count_status`.

    Returns a catalog dict with per-header breakdown and summary.
    """
    accessors = scan_header_accessors(
        src_dirs,
        max_statements=max_statements,
        project_root=project_root,
        cov=cov,
        strict_dirs=strict_dirs,
    )

    if known_outlines is None:
        known_outlines = _KNOWN_OUTLINES

    # Build known-outline index for fast lookup
    known_index: dict[tuple[str, str], dict] = {}
    for ko in known_outlines:
        key = (_normalize_header_key(ko["header"]), ko["method"])
        known_index[key] = ko

    # Include count estimation from ninja deps.
    # _count_includes_from_ninja is a STUB — it always returns {}, so this
    # branch cannot produce a number.  Kept so the wiring is visible, but the
    # catalog now labels the result `unimplemented` instead of publishing -1
    # as if it were a measurement.
    include_counts: dict[str, int] = {}
    if ninja_deps_path:
        include_counts = _count_includes_from_ninja(ninja_deps_path)

    # Build catalog entries grouped by header
    headers_catalog: dict[str, dict] = {}
    total_accessors = 0
    outlined_count = 0
    candidate_count = 0

    for acc in accessors:
        header_key = acc.header
        norm_key = _normalize_header_key(header_key)
        lookup_key = (norm_key, acc.method_name)

        entry = CatalogEntry(accessor=acc)

        if lookup_key in known_index:
            ko = known_index[lookup_key]
            entry.status = ko.get("status", "outlined")
            entry.fixed_functions = ko.get("fixed_functions", [])
            entry.notes = ko.get("notes", "")
            outlined_count += 1
        else:
            candidate_count += 1

        total_accessors += 1

        if header_key not in headers_catalog:
            headers_catalog[header_key] = {
                "accessors": [],
                "include_count": include_counts.get(norm_key, INCLUDE_COUNT_UNIMPLEMENTED),
                "include_count_status": (
                    "measured" if norm_key in include_counts else "unimplemented"),
            }

        headers_catalog[header_key]["accessors"].append(entry.to_dict())

    # Add known outlines that weren't found by the scan (body already moved
    # to .cpp, so the header no longer has an inline definition).
    matched_known: set[tuple[str, str]] = set()
    for acc in accessors:
        norm_key = _normalize_header_key(acc.header)
        matched_known.add((norm_key, acc.method_name))

    # `known_outlines` was already normalised (None -> _KNOWN_OUTLINES) above, so
    # the old `known_outlines if known_outlines else _KNOWN_OUTLINES` could only
    # fire for an EXPLICIT empty list — i.e. a caller saying "no known outlines"
    # silently got the built-in two back, counted as `outlined`, and listed under
    # a header that was never scanned. Inert for the default path.
    for ko in known_outlines:
        ko_key = (_normalize_header_key(ko["header"]), ko["method"])
        if ko_key not in matched_known:
            # This outline was already applied — body no longer in header
            header_display = ko["header"]
            outlined_count += 1
            total_accessors += 1
            entry_dict = {
                "header": header_display,
                "class": ko.get("class", ""),
                "method": ko["method"],
                "size": 1,
                "size_class": "trivial",
                "body": "(moved to .cpp)",
                "is_const": False,
                "return_type": "",
                "status": ko.get("status", "outlined"),
                "fixed_functions": ko.get("fixed_functions", []),
                "notes": ko.get("notes", ""),
            }
            if header_display not in headers_catalog:
                norm_hdr = _normalize_header_key(header_display)
                headers_catalog[header_display] = {
                    "accessors": [],
                    "include_count": include_counts.get(norm_hdr, INCLUDE_COUNT_UNIMPLEMENTED),
                    "include_count_status": (
                        "measured" if norm_hdr in include_counts else "unimplemented"),
                }
            headers_catalog[header_display]["accessors"].append(entry_dict)

    return {
        "headers": dict(sorted(headers_catalog.items())),
        "summary": {
            # "accessors" here means "inline bodies this REGEX could see" —
            # see summary.limitations before quoting it as a population.
            "total_accessors": total_accessors,
            "outlined": outlined_count,
            "candidates": candidate_count,
            "headers_with_accessors": len(headers_catalog),
            "headers_scanned": len(headers_catalog),   # back-compat alias
            "include_count_status": "unimplemented",
            "limitations": list(ALL_LIMITATIONS),
        },
    }


def _normalize_header_key(path: str) -> str:
    """Normalize a header path for matching (strip common prefixes)."""
    path = path.replace("\\", "/")
    # Strip src/system/ prefix
    for prefix in ("src/system/", "src/", "include/"):
        idx = path.find(prefix)
        if idx >= 0:
            return path[idx + len(prefix):]
    return path


def _count_includes_from_ninja(deps_path: str | Path) -> dict[str, int]:
    """UNIMPLEMENTED STUB — always returns ``{}``.

    The docstring here used to advertise "a best-effort heuristic: we parse the
    build.ninja file's dep info if available", and `build_catalog` published the
    resulting `-1` as `include_count`.  Nothing is parsed.  The binary
    `.ninja_deps` format was never implemented, so an `include_count` of -1
    means **not measured**, never "no TU includes this header".

    Deliberately left unimplemented (implementing it is a feature, not an
    honesty fix); it now self-declares instead of masquerading as a result.
    Callers get `include_count_status: "unimplemented"` alongside every -1, and
    `warn_once=True` emits a one-line stderr warning.
    """
    if not _count_includes_from_ninja._warned:            # type: ignore[attr-defined]
        _count_includes_from_ninja._warned = True         # type: ignore[attr-defined]
        print(f"warning: _count_includes_from_ninja({deps_path}) is an UNIMPLEMENTED "
              f"stub; every include_count will be {INCLUDE_COUNT_UNIMPLEMENTED} "
              f"(= not measured)", file=sys.stderr)
    return {}


_count_includes_from_ninja._warned = False                # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# Inline mismatch scanning (lightweight heuristic mode)
# ---------------------------------------------------------------------------

_SUSPECT_SAMPLE_LIMIT = 5


def _row_pct(func: dict) -> tuple[float, str]:
    """Return ``(pct, ruler)`` for one report.json function row.

    ***DENOMINATOR FIX — do not "simplify" this back.***
    The old code was ``func.get("fuzzy_match_percent", 100.0)`` immediately
    followed by ``if pct >= 100.0: continue``.  objdiff emits
    `fuzzy_match_percent` ONLY for functions we actually define; it is absent
    from 16,920 of the 48,344 rows on this tree.  Defaulting a MISSING key to
    the most optimistic possible value made every one of those rows claim to be
    already matching and leave the population without a trace — the "we wrote
    no body at all" tier, invisible.  `match_percent_normalized` is present on
    all 48,344 rows, so falling back to it is what makes them countable.
    """
    p = func.get("fuzzy_match_percent")
    if p is not None:
        return float(p), "fuzzy"
    n = func.get("match_percent_normalized")
    if n is not None:
        return float(n), "normalized"
    return 0.0, "absent"


def scan_inline_mismatches(
    report_path: str | Path,
    src_dirs: list[str | Path],
    *,
    project_root: str | Path | None = None,
    max_statements: int = 5,
    cov: Optional[CoverageReport] = None,
    header_cov: Optional[CoverageReport] = None,
    strict_dirs: bool = False,
) -> dict:
    """Cross-reference report.json with header accessors to find likely mismatches.

    This is the lightweight heuristic mode: it identifies non-100% functions
    in TUs that include headers with inline accessors.  A more expensive mode
    would diff each function to confirm `bl` vs inlined load, but this gives
    a useful first approximation.

    Two universes are involved and they are counted separately: `cov` counts
    FUNCTION ROWS from report.json, `header_cov` counts HEADER FILES.

    Returns a dict with:
        - ``candidates``: list of {function, unit, match_pct, suspect_accessors}
        - ``summary``: counts + stated limitations
    """
    report_path = Path(report_path)
    if not report_path.exists():
        # Loud: a missing report and "no candidates" must not look identical.
        raise FileNotFoundError(
            f"report.json not found: {report_path} — run ninja first. "
            f"(This used to return an empty result, which reads as 'no work exists'.)")

    # Load report
    with open(report_path) as f:
        report = json.load(f)

    # Scan headers
    accessors = scan_header_accessors(
        src_dirs,
        max_statements=max_statements,
        project_root=project_root,
        cov=header_cov,
        strict_dirs=strict_dirs,
    )

    rows = [(unit.get("name", ""), func)
            for unit in report.get("units", [])
            for func in unit.get("functions", [])]

    if cov is not None:
        cov.universe(len(rows), "function rows in report.json (ALL units)")
        for lim in ALL_LIMITATIONS:
            cov.note(lim)
        cov.note("a row is only a candidate if an accessor's class name appears "
                 "verbatim in the demangled function name — a coarse heuristic; "
                 "rows dropped as `no-suspect-accessor` are NOT proven clean")

    candidates: list[dict] = []
    ruler_counts: dict[str, int] = {}

    for unit_name, func in rows:
        pct, ruler = _row_pct(func)
        ruler_counts[ruler] = ruler_counts.get(ruler, 0) + 1

        if pct >= 100.0:
            if cov is not None:
                cov.drop("already-100-pct", note="pct >= 100.0 on its own ruler")
            continue

        # Check if function name suggests it calls any accessor
        demangled = func.get("metadata", {}).get("demangled_name", "")
        func_name = func.get("name", "")

        # Find suspect accessors from headers likely included by this TU
        suspect = [acc.to_dict() for acc in accessors
                   if acc.class_name and acc.class_name in demangled]

        if not suspect:
            if cov is not None:
                cov.drop("no-suspect-accessor", note=(
                    "no scanned accessor's class name appears in the demangled "
                    "name; see the brace-depth limitation — absence of a suspect "
                    "is not absence of an accessor"))
            continue

        suspect.sort(key=lambda d: (d["header"], d["class"], d["method"]))
        candidates.append({
            "function": demangled or func_name,
            "unit": unit_name,
            "match_pct": round(pct, 1),
            "match_pct_ruler": ruler,
            "suspect_accessors": suspect[:_SUSPECT_SAMPLE_LIMIT],
            # The emitted list is a SAMPLE; the total is what there really are.
            "suspect_total": len(suspect),
        })
        if cov is not None:
            cov.examine()

    candidates.sort(key=lambda c: (c["unit"], c["function"], -c["suspect_total"]))

    if cov is not None:
        for ruler in sorted(ruler_counts):
            cov.extra(f"rows_on_{ruler}_ruler", ruler_counts[ruler])
        cov.note("ruler census over the whole universe: "
                 + ", ".join(f"{r}={ruler_counts[r]}" for r in sorted(ruler_counts))
                 + "  (`normalized` rows have no fuzzy_match_percent key — the tier "
                   "the old 100.0 default made invisible)")

    result = {
        "candidates": candidates,
        "summary": {
            "total": len(candidates),
            "rows_in_report": len(rows),
            "unique_accessors": len(accessors),
            "suspect_sample_limit": _SUSPECT_SAMPLE_LIMIT,
            "rows_by_ruler": dict(sorted(ruler_counts.items())),
            "limitations": list(ALL_LIMITATIONS),
        },
    }
    if cov is not None:
        result["_coverage"] = cov.as_dict()
    if header_cov is not None:
        result["_coverage_headers"] = header_cov.as_dict()
    return result


# ---------------------------------------------------------------------------
# Check a specific header
# ---------------------------------------------------------------------------

def check_header(
    header_path: str | Path,
    *,
    known_outlines: list[dict] | None = None,
    max_statements: int = 5,
) -> dict:
    """Analyze a single header for inlining risks.

    Returns a dict with accessors found and their status.
    """
    header_path = Path(header_path)
    if not header_path.exists():
        return {"error": f"Header not found: {header_path}", "accessors": []}

    text = header_path.read_text(errors="replace")
    accessors = _scan_single_header(text, str(header_path), max_statements)

    if known_outlines is None:
        known_outlines = _KNOWN_OUTLINES

    known_index: dict[tuple[str, str], dict] = {}
    for ko in known_outlines:
        key = (_normalize_header_key(ko["header"]), ko["method"])
        known_index[key] = ko

    results = []
    for acc in accessors:
        norm_key = _normalize_header_key(str(header_path))
        lookup_key = (norm_key, acc.method_name)

        entry = CatalogEntry(accessor=acc)
        if lookup_key in known_index:
            ko = known_index[lookup_key]
            entry.status = ko.get("status", "outlined")
            entry.fixed_functions = ko.get("fixed_functions", [])
            entry.notes = ko.get("notes", "")

        results.append(entry.to_dict())

    results.sort(key=lambda d: (d["class"], d["method"], d["size"]))

    return {
        "header": str(header_path),
        "accessors": results,
        "summary": {
            "total": len(results),
            "trivial": sum(1 for r in results if r.get("size_class") == "trivial"),
            "small": sum(1 for r in results if r.get("size_class") == "small"),
            "medium": sum(1 for r in results if r.get("size_class") == "medium"),
            "class_decls_in_file": _count_class_decls(text),
            "limitations": list(ALL_LIMITATIONS),
        },
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="inlining_catalog",
        description="TU-boundary inlining control catalog for MSVC PPC decomp.",
    )
    sub = parser.add_subparsers(dest="command")

    # scan-headers
    scan = sub.add_parser(
        "scan-headers",
        help="Scan header files for small inline function bodies.",
    )
    scan.add_argument(
        "--src-dir",
        action="append",
        default=None,
        help="Source directory to scan (repeatable). Default: src/system",
    )
    scan.add_argument(
        "--max-statements",
        type=int,
        default=5,
        help="Maximum statement count to include (default: 5).",
    )
    scan.add_argument(
        "--json",
        action="store_true",
        help="Output as JSON.",
    )

    # catalog
    cat = sub.add_parser(
        "catalog",
        help="Build full inlining control catalog.",
    )
    cat.add_argument(
        "--src-dir",
        action="append",
        default=None,
        help="Source directory to scan (repeatable). Default: src/system",
    )
    cat.add_argument(
        "--json",
        action="store_true",
        help="Output as JSON.",
    )

    # check
    chk = sub.add_parser(
        "check",
        help="Check a specific header for inlining risks.",
    )
    chk.add_argument(
        "header",
        help="Path to the header file to check.",
    )
    chk.add_argument(
        "--json",
        action="store_true",
        help="Output as JSON.",
    )

    # scan-mismatches — cross-reference report.json with the accessor scan.
    # This code path existed as a library function with no way to run it, so
    # its "every no-body function is already matching" defect never printed a
    # coverage block anyone could read.  Purely additive subcommand.
    mm = sub.add_parser(
        "scan-mismatches",
        help="Cross-reference report.json with header accessors (heuristic).",
    )
    mm.add_argument("--report", default="build/373307D9/report.json",
                    help="Path to report.json (default: build/373307D9/report.json)")
    mm.add_argument("--src-dir", action="append", default=None,
                    help="Source directory to scan (repeatable). Default: src/system")
    mm.add_argument("--max-statements", type=int, default=5,
                    help="Maximum statement count to include (default: 5).")
    mm.add_argument("--json", action="store_true", help="Output as JSON.")

    for p in (scan, cat, chk, mm):
        add_coverage_args(p)

    return parser


def _resolve_src_dirs(src_dir_args: list[str] | None) -> list[Path]:
    """Resolve --src-dir arguments to paths, with defaults."""
    if src_dir_args:
        return [Path(d) for d in src_dir_args]
    # Default: look for src/system relative to CWD or script location
    candidates = [
        Path("src/system"),
        Path(__file__).resolve().parent.parent.parent / "src" / "system",
    ]
    for c in candidates:
        if c.is_dir():
            return [c]
    return [Path("src/system")]


def _project_root() -> Path:
    """Find the project root (directory containing src/)."""
    candidates = [
        Path.cwd(),
        Path(__file__).resolve().parent.parent.parent,
    ]
    for c in candidates:
        if (c / "src").is_dir():
            return c
    return Path.cwd()


def _infer_project_root(src_dirs: list[Path]) -> Path:
    """Infer project root from source directories or fall back to default."""
    # Try to find a parent directory of src_dirs that contains "src/"
    for sd in src_dirs:
        sd = sd.resolve()
        for parent in [sd] + list(sd.parents):
            if (parent / "src").is_dir() and parent != sd:
                return parent
    return _project_root()


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if not args.command:
        parser.print_help()
        return 1

    if args.command == "scan-headers":
        src_dirs = _resolve_src_dirs(getattr(args, "src_dir", None))
        root = _infer_project_root(src_dirs)
        cov = CoverageReport("inlining_catalog.scan-headers", args=args)
        try:
            accessors = scan_header_accessors(
                src_dirs,
                max_statements=args.max_statements,
                project_root=root,
                cov=cov,
                strict_dirs=True,     # a typo'd --src-dir must be LOUD
            )
        except MissingSourceDirError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2

        if args.json:
            payload = {
                "accessors": sorted((a.to_dict() for a in accessors),
                                    key=lambda d: (d["header"], d["class"], d["method"])),
                "limitations": list(ALL_LIMITATIONS),
                "_coverage": cov.as_dict(),
            }
            # Back-compat: the old JSON was a bare list of accessors. Emit the
            # list, and the honest block alongside it on stderr.
            print(json.dumps(payload["accessors"], indent=2))
            print(json.dumps({"limitations": payload["limitations"],
                              "_coverage": payload["_coverage"]}, indent=2),
                  file=sys.stderr)
        else:
            _print_scan_results(accessors, cov=cov)
        return cov.emit()

    elif args.command == "catalog":
        src_dirs = _resolve_src_dirs(getattr(args, "src_dir", None))
        root = _infer_project_root(src_dirs)
        cov = CoverageReport("inlining_catalog.catalog", args=args)
        try:
            catalog = build_catalog(
                src_dirs,
                project_root=root,
                cov=cov,
                strict_dirs=True,
            )
        except MissingSourceDirError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2
        catalog["_coverage"] = cov.as_dict()

        if args.json:
            print(json.dumps(catalog, indent=2))
        else:
            _print_catalog(catalog)
        return cov.emit()

    elif args.command == "check":
        result = check_header(args.header)
        cov = CoverageReport("inlining_catalog.check", args=args)
        if "error" in result:
            cov.universe(0, "header files requested")
            cov.note(result["error"])
            print(f"error: {result['error']}", file=sys.stderr)
            if args.json:
                print(json.dumps(result, indent=2))
            else:
                _print_check_result(result)
            cov.emit()
            return 2
        cov.universe(1, "header files requested")
        cov.examine(1)
        for lim in ALL_LIMITATIONS:
            cov.note(lim)
        cov.extra("accessors_found", len(result["accessors"]))
        cov.extra("class_decls_in_file", result["summary"]["class_decls_in_file"])
        result["_coverage"] = cov.as_dict()

        if args.json:
            print(json.dumps(result, indent=2))
        else:
            _print_check_result(result)
        return cov.emit()

    elif args.command == "scan-mismatches":
        src_dirs = _resolve_src_dirs(getattr(args, "src_dir", None))
        root = _infer_project_root(src_dirs)
        cov = CoverageReport("inlining_catalog.scan-mismatches", args=args)
        header_cov = CoverageReport("inlining_catalog.scan-mismatches[headers]",
                                    args=args)
        try:
            result = scan_inline_mismatches(
                args.report,
                src_dirs,
                project_root=root,
                max_statements=args.max_statements,
                cov=cov,
                header_cov=header_cov,
                strict_dirs=True,
            )
        except (MissingSourceDirError, FileNotFoundError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2

        if args.json:
            print(json.dumps(result, indent=2))
        else:
            s = result["summary"]
            print(f"Inline mismatch candidates: {s['total']} "
                  f"out of {s['rows_in_report']} function rows in {args.report}")
            print(f"  accessors cross-referenced: {s['unique_accessors']}")
            print(f"  rows by ruler: {s['rows_by_ruler']}")
            for c in result["candidates"][:20]:
                print(f"  {c['match_pct']:5}%  {c['function'][:70]}")
                print(f"      {c['suspect_total']} suspect accessor(s), "
                      f"showing {len(c['suspect_accessors'])}")
            if len(result["candidates"]) > 20:
                print(f"  ... and {len(result['candidates']) - 20} more candidates "
                      f"not shown")
            print("\nLIMITATIONS:")
            for lim in s["limitations"]:
                print(f"  - {lim}")
        # Both universes get their own block; the worst code wins.
        return max(header_cov.emit(), cov.emit())

    return 0


def _print_scan_results(accessors: list[HeaderAccessor],
                        cov: Optional[CoverageReport] = None) -> None:
    """Pretty-print scan results to stdout.

    The old headline was `Found N inline accessors/helpers in headers` — a
    numerator with no denominator and no mention of what the regex cannot see.
    """
    by_size: dict[str, list[HeaderAccessor]] = {"trivial": [], "small": [], "medium": []}
    for a in accessors:
        by_size.setdefault(a.size_class, []).append(a)

    if cov is not None:
        d = cov.as_dict()
        print(f"Found {len(accessors)} inline accessors/helpers in headers "
              f"({d['examined']} of {d['universe']} *.h files read, "
              f"{d['dropped_total']} skipped — see the COVERAGE block on stderr)\n")
    else:
        print(f"Found {len(accessors)} inline accessors/helpers in headers\n")

    for size_class in ("trivial", "small", "medium"):
        items = sorted(by_size.get(size_class, []),
                       key=lambda a: (a.header, a.class_name, a.method_name))
        if not items:
            continue
        print(f"--- {size_class.upper()} ({len(items)}) ---")
        for a in items[:20]:
            const_str = " const" if a.is_const else ""
            print(f"  {a.header}  {a.class_name}::{a.method_name}(){const_str}")
            print(f"    {a.body[:80]}{'...' if len(a.body) > 80 else ''}")
        if len(items) > 20:
            print(f"  ... and {len(items) - 20} more (showing 20 of {len(items)})")
        print()

    print("LIMITATIONS — this is what the number above does NOT count:")
    for lim in ALL_LIMITATIONS:
        print(f"  - {lim}")
    print()


def _print_catalog(catalog: dict) -> None:
    """Pretty-print catalog to stdout."""
    summary = catalog["summary"]
    cov = catalog.get("_coverage") or {}
    print(f"Inlining Control Catalog")
    print(f"========================")
    print(f"  Total accessors:   {summary['total_accessors']}")
    print(f"  Already outlined:  {summary['outlined']}")
    print(f"  Candidates:        {summary['candidates']}")
    # The old label was "Headers scanned", but the value is the number of
    # headers that yielded at least one accessor — always smaller.
    print(f"  Headers with accessors: {summary.get('headers_with_accessors', summary['headers_scanned'])}")
    if cov.get("universe") is not None:
        print(f"  Header files read: {cov['examined']} of {cov['universe']} "
              f"(*.h under --src-dir; {cov['dropped_total']} skipped)")
    print(f"  include_count:     {summary.get('include_count_status', 'unimplemented')} "
          f"(every value is {INCLUDE_COUNT_UNIMPLEMENTED} = not measured)")
    print()

    headers = catalog["headers"]
    for header_path, data in sorted(headers.items()):
        accs = sorted(data["accessors"],
                      key=lambda a: (a.get("class", ""), a.get("method", "")))
        outlined = sum(1 for a in accs if a.get("status") == "outlined")
        candidates = sum(1 for a in accs if a.get("status") == "candidate")
        if not accs:
            continue
        print(f"  {header_path} ({len(accs)} accessors, {outlined} outlined, {candidates} candidates)")
        for a in accs:
            status_tag = f" [{a['status']}]" if a["status"] != "candidate" else ""
            print(f"    {a['class']}::{a['method']} ({a['size_class']}){status_tag}")
            if a.get("fixed_functions"):
                print(f"      Fixed: {', '.join(a['fixed_functions'])}")
        print()

    print("LIMITATIONS — this catalog's real denominator:")
    for lim in summary.get("limitations", ALL_LIMITATIONS):
        print(f"  - {lim}")
    print()


def _print_check_result(result: dict) -> None:
    """Pretty-print check result to stdout."""
    if "error" in result:
        print(f"Error: {result['error']}")
        return

    print(f"Header: {result['header']}")
    summary = result["summary"]
    print(f"  Total: {summary['total']} accessors")
    print(f"    Trivial: {summary['trivial']}")
    print(f"    Small:   {summary['small']}")
    print(f"    Medium:  {summary['medium']}")
    n_classes = summary.get("class_decls_in_file", 0)
    if n_classes > 1:
        print(f"  !! {n_classes} class/struct declarations in this file — every "
              f"accessor below is attributed to the LAST one (heuristic)")
    print()

    for a in result["accessors"]:
        status_tag = f" [{a['status']}]" if a["status"] != "candidate" else ""
        print(f"  {a['class']}::{a['method']} ({a['size_class']}){status_tag}")
        print(f"    Body: {a['body'][:80]}{'...' if len(a['body']) > 80 else ''}")
        if a.get("fixed_functions"):
            print(f"    Fixed: {', '.join(a['fixed_functions'])}")
        if a.get("notes"):
            print(f"    Notes: {a['notes']}")
        print()

    print("LIMITATIONS:")
    for lim in summary.get("limitations", ALL_LIMITATIONS):
        print(f"  - {lim}")
    print()


if __name__ == "__main__":
    sys.exit(main())
