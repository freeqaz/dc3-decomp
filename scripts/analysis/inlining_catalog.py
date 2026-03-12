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

def scan_header_accessors(
    src_dirs: list[str | Path],
    *,
    max_statements: int = 5,
    project_root: str | Path | None = None,
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

    Returns
    -------
    List of HeaderAccessor records for every qualifying inline function.
    """
    if project_root is not None:
        project_root = Path(project_root)

    accessors: list[HeaderAccessor] = []

    for src_dir in src_dirs:
        src_path = Path(src_dir).resolve()
        if not src_path.is_dir():
            continue
        resolved_root = project_root.resolve() if project_root else None
        for header in sorted(src_path.rglob("*.h")):
            # Skip CL temp dirs
            if "/_CL_" in str(header):
                continue
            try:
                text = header.read_text(errors="replace")
            except OSError:
                continue

            header_resolved = header.resolve()
            if resolved_root and header_resolved.is_relative_to(resolved_root):
                rel = str(header_resolved.relative_to(resolved_root))
            else:
                rel = str(header)
            found = _scan_single_header(text, rel, max_statements)
            accessors.extend(found)

    return accessors


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


def _extract_class_context(text: str) -> str:
    """Extract the most likely enclosing class name from header text.

    Simple heuristic: find the last `class Foo` declaration.
    """
    # Match class/struct declarations (not forward declarations)
    matches = re.findall(
        r"(?:class|struct)\s+(\w+)\s*(?::\s*(?:public|private|protected))?[^;]*\{",
        text,
    )
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
) -> dict:
    """Build a full inlining control catalog.

    Combines header accessor scan with known outline fixes and optionally
    ninja dependency data for include-count estimation.

    Returns a catalog dict with per-header breakdown and summary.
    """
    accessors = scan_header_accessors(
        src_dirs,
        max_statements=max_statements,
        project_root=project_root,
    )

    if known_outlines is None:
        known_outlines = _KNOWN_OUTLINES

    # Build known-outline index for fast lookup
    known_index: dict[tuple[str, str], dict] = {}
    for ko in known_outlines:
        key = (_normalize_header_key(ko["header"]), ko["method"])
        known_index[key] = ko

    # Include count estimation from ninja deps
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
                "include_count": include_counts.get(norm_key, -1),
            }

        headers_catalog[header_key]["accessors"].append(entry.to_dict())

    # Add known outlines that weren't found by the scan (body already moved
    # to .cpp, so the header no longer has an inline definition).
    matched_known: set[tuple[str, str]] = set()
    for acc in accessors:
        norm_key = _normalize_header_key(acc.header)
        matched_known.add((norm_key, acc.method_name))

    for ko in known_outlines if known_outlines else _KNOWN_OUTLINES:
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
                    "include_count": include_counts.get(norm_hdr, -1),
                }
            headers_catalog[header_display]["accessors"].append(entry_dict)

    return {
        "headers": headers_catalog,
        "summary": {
            "total_accessors": total_accessors,
            "outlined": outlined_count,
            "candidates": candidate_count,
            "headers_scanned": len(headers_catalog),
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
    """Count how many TUs include each header using ninja's .ninja_deps.

    This is a best-effort heuristic: we parse the build.ninja file's dep
    info if available, or fall back to an empty dict.
    """
    # This would parse the ninja deps database; for now return empty
    # since the binary .ninja_deps format is complex to parse
    return {}


# ---------------------------------------------------------------------------
# Inline mismatch scanning (lightweight heuristic mode)
# ---------------------------------------------------------------------------

def scan_inline_mismatches(
    report_path: str | Path,
    src_dirs: list[str | Path],
    *,
    project_root: str | Path | None = None,
    max_statements: int = 5,
) -> dict:
    """Cross-reference report.json with header accessors to find likely mismatches.

    This is the lightweight heuristic mode: it identifies non-100% functions
    in TUs that include headers with inline accessors.  A more expensive mode
    would diff each function to confirm `bl` vs inlined load, but this gives
    a useful first approximation.

    Returns a dict with:
        - ``candidates``: list of {function, unit, match_pct, suspect_accessors}
        - ``summary``: counts
    """
    report_path = Path(report_path)
    if not report_path.exists():
        return {"candidates": [], "summary": {"total": 0}}

    # Load report
    with open(report_path) as f:
        report = json.load(f)

    # Scan headers
    accessors = scan_header_accessors(
        src_dirs,
        max_statements=max_statements,
        project_root=project_root,
    )

    # Index accessors by header basename for quick matching
    accessor_by_header: dict[str, list[HeaderAccessor]] = {}
    for acc in accessors:
        basename = Path(acc.header).stem
        accessor_by_header.setdefault(basename, []).append(acc)

    # For each non-100% function, check if its TU likely includes
    # headers with inline accessors
    candidates: list[dict] = []
    units = report.get("units", [])

    for unit in units:
        unit_name = unit.get("name", "")
        functions = unit.get("functions", [])

        for func in functions:
            pct = func.get("fuzzy_match_percent", 100.0)
            if pct >= 100.0:
                continue

            # Check if function name suggests it calls any accessor
            demangled = func.get("metadata", {}).get("demangled_name", "")
            func_name = func.get("name", "")

            # Find suspect accessors from headers likely included by this TU
            unit_base = Path(unit_name).stem
            suspect = []
            for acc in accessors:
                # Heuristic: check if the accessor's class name appears in
                # the function's demangled name (suggests it uses that class)
                if acc.class_name and acc.class_name in demangled:
                    suspect.append(acc.to_dict())

            if suspect:
                candidates.append({
                    "function": demangled or func_name,
                    "unit": unit_name,
                    "match_pct": round(pct, 1),
                    "suspect_accessors": suspect[:5],  # limit for readability
                })

    return {
        "candidates": candidates,
        "summary": {
            "total": len(candidates),
            "unique_accessors": len(accessors),
        },
    }


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

    return {
        "header": str(header_path),
        "accessors": results,
        "summary": {
            "total": len(results),
            "trivial": sum(1 for r in results if r.get("size_class") == "trivial"),
            "small": sum(1 for r in results if r.get("size_class") == "small"),
            "medium": sum(1 for r in results if r.get("size_class") == "medium"),
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
        accessors = scan_header_accessors(
            src_dirs,
            max_statements=args.max_statements,
            project_root=root,
        )

        if args.json:
            print(json.dumps([a.to_dict() for a in accessors], indent=2))
        else:
            _print_scan_results(accessors)

    elif args.command == "catalog":
        src_dirs = _resolve_src_dirs(getattr(args, "src_dir", None))
        root = _infer_project_root(src_dirs)
        catalog = build_catalog(
            src_dirs,
            project_root=root,
        )

        if args.json:
            print(json.dumps(catalog, indent=2))
        else:
            _print_catalog(catalog)

    elif args.command == "check":
        result = check_header(args.header)

        if args.json:
            print(json.dumps(result, indent=2))
        else:
            _print_check_result(result)

    return 0


def _print_scan_results(accessors: list[HeaderAccessor]) -> None:
    """Pretty-print scan results to stdout."""
    by_size: dict[str, list[HeaderAccessor]] = {"trivial": [], "small": [], "medium": []}
    for a in accessors:
        by_size.setdefault(a.size_class, []).append(a)

    print(f"Found {len(accessors)} inline accessors/helpers in headers\n")

    for size_class in ("trivial", "small", "medium"):
        items = by_size.get(size_class, [])
        if not items:
            continue
        print(f"--- {size_class.upper()} ({len(items)}) ---")
        for a in items[:20]:
            const_str = " const" if a.is_const else ""
            print(f"  {a.header}  {a.class_name}::{a.method_name}(){const_str}")
            print(f"    {a.body[:80]}{'...' if len(a.body) > 80 else ''}")
        if len(items) > 20:
            print(f"  ... and {len(items) - 20} more")
        print()


def _print_catalog(catalog: dict) -> None:
    """Pretty-print catalog to stdout."""
    summary = catalog["summary"]
    print(f"Inlining Control Catalog")
    print(f"========================")
    print(f"  Total accessors:   {summary['total_accessors']}")
    print(f"  Already outlined:  {summary['outlined']}")
    print(f"  Candidates:        {summary['candidates']}")
    print(f"  Headers scanned:   {summary['headers_scanned']}")
    print()

    headers = catalog["headers"]
    for header_path, data in sorted(headers.items()):
        accs = data["accessors"]
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


if __name__ == "__main__":
    sys.exit(main())
