"""RB3 file pairing module for DC3 decomp.

Matches DC3 units to RB3 source files based on:
- File path/name similarity
- Function name overlap

This enables agents to use RB3 reference implementations
for shared Milo engine code.
"""

import json
import re
import subprocess
from pathlib import Path
from typing import Any

from .database import (
    get_connection,
    upsert_file_pair,
    query_file_pairs,
    get_file_pairs_stats,
    DEFAULT_DB_PATH,
)


# Default paths
DEFAULT_RB3_PATH = Path.home() / "code/milohax/rb3/src"
DEFAULT_DC3_REPORT = Path("build/373307D9/report.json")


def extract_function_names_from_symbols(symbols: list[str]) -> set[str]:
    """
    Extract function/method names from mangled symbols.

    Handles both MSVC mangled (?Name@Class@@...) and demangled (Class::Name) formats.

    Examples:
        ?Poll@CharMirror@@UAEXXZ -> Poll
        CharMirror::Poll -> Poll
    """
    names = set()
    for symbol in symbols:
        # MSVC mangled format: ?MethodName@ClassName@@...
        if symbol.startswith("?"):
            parts = symbol.split("@")
            if len(parts) >= 2:
                name = parts[0].lstrip("?")
                names.add(name)
        # Demangled format: Class::Method or just Method
        elif "::" in symbol:
            parts = symbol.split("::")
            # Take the last part (method name)
            names.add(parts[-1].split("(")[0].strip())
        else:
            # Plain function name
            names.add(symbol.split("(")[0].strip())
    return names


def get_dc3_units_with_functions(
    report_path: Path = DEFAULT_DC3_REPORT,
) -> dict[str, list[str]]:
    """
    Parse report.json to get DC3 units and their function symbols.

    Returns:
        Dict mapping unit path -> list of function symbols
    """
    if not report_path.exists():
        raise FileNotFoundError(f"Report not found: {report_path}")

    with open(report_path) as f:
        report = json.load(f)

    units = {}
    for unit in report.get("units", []):
        unit_name = unit.get("name", "")
        if not unit_name:
            continue

        functions = []
        for func in unit.get("functions", []):
            symbol = func.get("name", "")
            if symbol:
                functions.append(symbol)

        if functions:
            units[unit_name] = functions

    return units


def find_rb3_file(dc3_unit: str, rb3_path: Path = DEFAULT_RB3_PATH) -> Path | None:
    """
    Find matching RB3 source file for a DC3 unit.

    Matches by filename, handling path differences between projects.

    Args:
        dc3_unit: DC3 unit path (e.g., "default/system/char/CharBones")
        rb3_path: Root of RB3 source tree

    Returns:
        Path to matching RB3 file, or None if not found
    """
    # Extract filename from unit path
    # DC3: "default/system/char/CharBones" -> "CharBones.cpp"
    parts = dc3_unit.replace("\\", "/").split("/")
    filename = parts[-1]

    # Handle common filename patterns
    if not filename.endswith((".cpp", ".c", ".h")):
        filename = filename + ".cpp"

    # Search in RB3 system directory (most common)
    # Map DC3 paths to RB3 equivalents
    search_dirs = [rb3_path / "system"]

    # Also check for direct mapping
    # DC3: default/system/char -> RB3: system/char
    if "system" in parts:
        idx = parts.index("system")
        subpath = "/".join(parts[idx:])
        search_dirs.insert(0, rb3_path / Path(subpath).parent)

    for search_dir in search_dirs:
        if not search_dir.exists():
            continue

        # Use find to locate the file
        try:
            result = subprocess.run(
                ["find", str(search_dir), "-name", filename, "-type", "f"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if result.stdout.strip():
                # Return first match
                matches = result.stdout.strip().split("\n")
                return Path(matches[0])
        except (subprocess.TimeoutExpired, Exception):
            continue

    return None


def get_rb3_function_names(rb3_file: Path) -> set[str]:
    """
    Extract function/method names from RB3 source file.

    Uses simple regex to find function definitions.
    """
    if not rb3_file.exists():
        return set()

    try:
        content = rb3_file.read_text(errors="replace")
    except Exception:
        return set()

    names = set()

    # Match function definitions (simplified patterns)
    # Pattern 1: ReturnType ClassName::MethodName(
    method_pattern = r'\b(\w+)::(\w+)\s*\('
    for match in re.finditer(method_pattern, content):
        names.add(match.group(2))

    # Pattern 2: Standalone functions
    standalone_pattern = r'(?<![:\w])\b(?:void|bool|int|float|char|unsigned|const|static|inline|virtual)\s+(\w+)\s*\('
    for match in re.finditer(standalone_pattern, content):
        name = match.group(1)
        if name not in ('if', 'while', 'for', 'switch', 'return'):
            names.add(name)

    return names


def get_rb3_names_and_classes(rb3_file: Path) -> tuple[set[str], set[str]]:
    """
    Extract both method names and class names from RB3 source file.

    Returns:
        Tuple of (method_names, class_names)
    """
    if not rb3_file.exists():
        return set(), set()

    try:
        content = rb3_file.read_text(errors="replace")
    except Exception:
        return set(), set()

    methods = set()
    classes = set()

    # Pattern 1: ClassName::MethodName( - extracts both
    method_pattern = r'\b(\w+)::(\w+)\s*\('
    for match in re.finditer(method_pattern, content):
        classes.add(match.group(1))
        methods.add(match.group(2))

    # Pattern 2: Standalone functions
    standalone_pattern = r'(?<![:\w])\b(?:void|bool|int|float|char|unsigned|const|static|inline|virtual)\s+(\w+)\s*\('
    for match in re.finditer(standalone_pattern, content):
        name = match.group(1)
        if name not in ('if', 'while', 'for', 'switch', 'return'):
            methods.add(name)

    # Pattern 3: Class definitions
    class_pattern = r'\bclass\s+(\w+)'
    for match in re.finditer(class_pattern, content):
        classes.add(match.group(1))

    # Pattern 4: Struct definitions
    struct_pattern = r'\bstruct\s+(\w+)'
    for match in re.finditer(struct_pattern, content):
        classes.add(match.group(1))

    return methods, classes


def extract_class_and_method_from_symbols(symbols: list[str]) -> tuple[set[str], set[str]]:
    """
    Extract both class names and method names from DC3 symbols.

    Args:
        symbols: List of mangled or demangled symbol names

    Returns:
        Tuple of (method_names, class_names)
    """
    methods = set()
    classes = set()

    for symbol in symbols:
        # MSVC mangled format: ?MethodName@ClassName@@...
        if symbol.startswith("?"):
            parts = symbol.split("@")
            if len(parts) >= 2:
                methods.add(parts[0].lstrip("?"))
                if len(parts) >= 3:
                    # Second part is the class name
                    classes.add(parts[1])
        # Demangled format: Class::Method or Namespace::Class::Method
        elif "::" in symbol:
            parts = symbol.split("::")
            if len(parts) >= 2:
                # Last part is method name (strip params)
                methods.add(parts[-1].split("(")[0].strip())
                # Second-to-last is class name
                classes.add(parts[-2].split("(")[0].strip())
        else:
            # Plain function name
            methods.add(symbol.split("(")[0].strip())

    return methods, classes


def calculate_compatibility(
    dc3_functions: list[str],
    rb3_names: set[str],
) -> tuple[float, int]:
    """
    Calculate compatibility score between DC3 unit and RB3 file.

    Args:
        dc3_functions: List of DC3 function symbols
        rb3_names: Set of RB3 function/method names

    Returns:
        (compatibility_score, overlap_count)
        Score = overlap / max(dc3_count, rb3_count)
    """
    dc3_names = extract_function_names_from_symbols(dc3_functions)

    # Find overlap
    overlap = dc3_names & rb3_names
    overlap_count = len(overlap)

    # Calculate score
    max_count = max(len(dc3_names), len(rb3_names))
    if max_count == 0:
        return 0.0, 0

    score = overlap_count / max_count
    return score, overlap_count


def sync_file_pairs(
    rb3_path: Path = DEFAULT_RB3_PATH,
    report_path: Path = DEFAULT_DC3_REPORT,
    db_path: str | Path = DEFAULT_DB_PATH,
    verbose: bool = False,
) -> dict[str, int]:
    """
    Build/update file pairing database.

    Scans all DC3 units and attempts to match them with RB3 files.

    Args:
        rb3_path: Root of RB3 source tree
        report_path: Path to DC3 report.json
        db_path: Database path
        verbose: Print progress

    Returns:
        Dict with counts: matched, unmatched, updated
    """
    if not rb3_path.exists():
        raise FileNotFoundError(f"RB3 path not found: {rb3_path}")

    # Get DC3 units
    if verbose:
        print(f"Loading DC3 units from {report_path}...")
    dc3_units = get_dc3_units_with_functions(report_path)
    if verbose:
        print(f"Found {len(dc3_units)} DC3 units with functions")

    matched = 0
    unmatched = 0
    updated = 0

    for unit_path, functions in dc3_units.items():
        if verbose:
            print(f"  Processing: {unit_path}...", end=" ")

        # Find matching RB3 file
        rb3_file = find_rb3_file(unit_path, rb3_path)

        if rb3_file:
            # Calculate compatibility
            rb3_names = get_rb3_function_names(rb3_file)
            score, overlap = calculate_compatibility(functions, rb3_names)

            upsert_file_pair(
                dc3_unit=unit_path,
                rb3_file=str(rb3_file),
                compatibility_score=score,
                function_overlap=overlap,
                dc3_function_count=len(functions),
                rb3_function_count=len(rb3_names),
                db_path=db_path,
            )

            if verbose:
                print(f"matched ({score:.1%} compat, {overlap} funcs)")
            matched += 1
        else:
            # Store even unmatched units for tracking
            upsert_file_pair(
                dc3_unit=unit_path,
                rb3_file=None,
                compatibility_score=None,
                function_overlap=0,
                dc3_function_count=len(functions),
                rb3_function_count=0,
                db_path=db_path,
            )

            if verbose:
                print("no RB3 match")
            unmatched += 1

        updated += 1

    return {
        "matched": matched,
        "unmatched": unmatched,
        "updated": updated,
        "total_units": len(dc3_units),
    }


def get_paired_files_for_directory(
    directory: str,
    min_compat: float = 0.5,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Get paired files for a specific directory pattern.

    Args:
        directory: Directory pattern (e.g., "char", "system/char")
        min_compat: Minimum compatibility score
        db_path: Database path

    Returns:
        List of file pair records
    """
    # Convert to glob pattern
    if not directory.startswith("*"):
        pattern = f"*{directory}*"
    else:
        pattern = directory

    return query_file_pairs(
        min_compat=min_compat,
        pattern=pattern,
        db_path=db_path,
    )


def get_rb3_source_for_unit(
    dc3_unit: str,
    rb3_path: Path = DEFAULT_RB3_PATH,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> str | None:
    """
    Get RB3 source code for a DC3 unit.

    First checks database for cached pairing, then searches if not found.

    Args:
        dc3_unit: DC3 unit path
        rb3_path: RB3 source root (fallback search)
        db_path: Database path

    Returns:
        Source code string, or None if not available
    """
    from .database import get_file_pair

    # Check database first
    pair = get_file_pair(dc3_unit, db_path=db_path)

    if pair and pair.get("rb3_file"):
        rb3_file = Path(pair["rb3_file"])
        if rb3_file.exists():
            try:
                return rb3_file.read_text(errors="replace")
            except Exception:
                pass

    # Fallback: search for file
    rb3_file = find_rb3_file(dc3_unit, rb3_path)
    if rb3_file and rb3_file.exists():
        try:
            return rb3_file.read_text(errors="replace")
        except Exception:
            pass

    return None


def sync_rb3_refs(
    db_path: str | Path = DEFAULT_DB_PATH,
    verbose: bool = False,
) -> dict[str, int]:
    """
    Populate has_rb3_ref for functions based on class + method matching.

    For each DC3 unit with an RB3 match, extracts both method names and
    class names from both sources. A DC3 function has an RB3 reference if:
    - The method name exists in RB3, OR
    - The class name exists in RB3 (class structure/members are similar)

    Args:
        db_path: Database path
        verbose: Print progress

    Returns:
        Dict with counts: {updated, with_ref, without_ref, units_processed}
    """
    from .database import (
        get_functions_for_unit,
        batch_update_rb3_refs,
        query_file_pairs,
    )

    # Get all file pairs with RB3 matches
    pairs = query_file_pairs(
        min_compat=0.0,
        pattern="*",
        limit=10000,  # Get all pairs
        db_path=db_path,
    )

    matched_pairs = [p for p in pairs if p.get("rb3_file")]

    if verbose:
        print(f"Processing {len(matched_pairs)} matched units...")

    updates: list[tuple[str, int]] = []
    with_ref = 0
    without_ref = 0
    units_processed = 0

    # Cache of RB3 methods and classes per file
    rb3_cache: dict[str, tuple[set[str], set[str]]] = {}

    for pair in matched_pairs:
        dc3_unit = pair["dc3_unit"]
        rb3_file_path = pair.get("rb3_file")

        if not rb3_file_path:
            continue

        # Get RB3 methods and classes (with caching)
        if rb3_file_path not in rb3_cache:
            rb3_file = Path(rb3_file_path)
            rb3_cache[rb3_file_path] = get_rb3_names_and_classes(rb3_file)

        rb3_methods, rb3_classes = rb3_cache[rb3_file_path]

        # Get DC3 functions for this unit
        dc3_functions = get_functions_for_unit(dc3_unit, db_path=db_path)

        if not dc3_functions:
            continue

        units_processed += 1

        if verbose and units_processed % 50 == 0:
            print(f"  Processed {units_processed}/{len(matched_pairs)} units...")

        for func in dc3_functions:
            symbol = func["symbol"]
            demangled = func.get("demangled", "")

            # Extract method and class names from DC3 function
            dc3_methods, dc3_classes = extract_class_and_method_from_symbols(
                [demangled] if demangled else [symbol]
            )

            # Match if method name OR class name exists in RB3
            method_match = bool(dc3_methods & rb3_methods)
            class_match = bool(dc3_classes & rb3_classes)
            has_ref = 1 if method_match or class_match else 0

            updates.append((symbol, has_ref))

            if has_ref:
                with_ref += 1
            else:
                without_ref += 1

    # Apply all updates in a single transaction
    if verbose:
        print(f"Applying {len(updates)} updates...")

    updated = batch_update_rb3_refs(updates, db_path=db_path)

    # Clear has_rb3_ref for functions in unmatched units
    # (They keep their default value of 0, so no action needed)

    return {
        "updated": updated,
        "with_ref": with_ref,
        "without_ref": without_ref,
        "units_processed": units_processed,
    }
