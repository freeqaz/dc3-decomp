#!/usr/bin/env python3
"""
Generate link order from original map file.

Parses the original linker map file to extract the object file ordering
from the .text segment, then maps those to dtk unit names. The output
can be used by configure.py's link_order_callback to reorder objects
so our linked PE matches the original's function layout.

Usage:
    python3 scripts/build/generate_link_order.py
    python3 scripts/build/generate_link_order.py --verify
"""

import argparse
import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
VERSION = "373307D9"

MAP_FILE = PROJECT_ROOT / "orig" / VERSION / "ham_xbox_r.map"
DTK_CONFIG = PROJECT_ROOT / "build" / VERSION / "obj" / "config.json"
OUTPUT_FILE = PROJECT_ROOT / "config" / VERSION / "link_order.txt"


def build_unit_lookup(
    dtk_config_path: Path,
) -> tuple[dict[str, str], dict[str, list[str]]]:
    """
    Build mappings from map-file object key to dtk unit name.

    Returns:
        (exact_lookup, basename_lookup)

    exact_lookup: maps "lib:basename.obj" -> unit_name
        Uses the immediate parent directory as the library prefix.

    basename_lookup: maps "basename.obj" -> [unit_name, ...]
        Fallback for objects where the library prefix doesn't match
        the parent directory (e.g., deep paths like curl objects).
        Each entry also stores all ancestor directories for prefix matching.

    Map-file keys are like:
        "keygen_xbox.obj"           (top-level, no library prefix)
        "char:Character.obj"        (library-prefixed)

    Unit names are like:
        "keygen_xbox.cpp"
        "system/char/Character.cpp"
        "xdk/xapilibi/xam.cpp"
    """
    with open(dtk_config_path, "r") as f:
        cfg = json.load(f)

    exact: dict[str, str] = {}
    # basename -> list of (unit_name, set of ancestor dirs)
    basename_entries: dict[str, list[tuple[str, set[str]]]] = {}

    for unit in cfg["units"]:
        name = unit["name"]  # e.g., "system/char/Character.cpp"
        stem = re.sub(r"\.(cpp|c)$", "", name)
        parts = stem.split("/")
        basename = parts[-1] + ".obj"

        if len(parts) == 1:
            # Top-level: "keygen_xbox.cpp" -> "keygen_xbox.obj"
            exact[basename] = name
            ancestors: set[str] = set()
        else:
            # Library-prefixed: use last two path components
            map_key = parts[-2] + ":" + basename
            exact[map_key] = name
            # All ancestor directories (for fuzzy prefix matching)
            ancestors = set(parts[:-1])

        basename_entries.setdefault(basename, []).append((name, ancestors))

    # Flatten basename_entries for the simple case
    basename_lookup: dict[str, list[str]] = {}
    for bn, entries in basename_entries.items():
        basename_lookup[bn] = [e[0] for e in entries]

    # Store the ancestor info for disambiguation
    build_unit_lookup._ancestors = basename_entries  # type: ignore[attr-defined]

    return exact, basename_lookup


def resolve_unit_name(
    obj_key: str,
    exact: dict[str, str],
    basename_lookup: dict[str, list[str]],
) -> str | None:
    """Resolve a map-file object key to a dtk unit name."""
    # 1. Exact match (lib:basename.obj or basename.obj)
    if obj_key in exact:
        return exact[obj_key]

    # 1b. Case-insensitive exact match
    obj_lower = obj_key.lower()
    for k, v in exact.items():
        if k.lower() == obj_lower:
            return v

    # 2. Basename fallback
    if ":" in obj_key:
        prefix, basename = obj_key.split(":", 1)
    else:
        prefix, basename = None, obj_key

    candidates = basename_lookup.get(basename, [])
    if len(candidates) == 1:
        return candidates[0]

    if len(candidates) > 1 and prefix:
        # Disambiguate using the map file prefix against ancestor dirs
        ancestor_info = build_unit_lookup._ancestors.get(basename, [])  # type: ignore[attr-defined]
        for unit_name, ancestors in ancestor_info:
            if prefix in ancestors:
                return unit_name

        # Try case-insensitive ancestor match
        prefix_lower = prefix.lower()
        for unit_name, ancestors in ancestor_info:
            if prefix_lower in {a.lower() for a in ancestors}:
                return unit_name

    return None


def parse_map_text_order(map_path: Path) -> list[str]:
    """
    Parse the map file and extract the first-seen order of objects
    contributing to segment 0005 (.text).

    Returns a list of map-file object keys like:
        ["keygen_xbox.obj", "App.obj", "obj:DirLoader.obj", ...]
    """
    # Pattern for "Publics by Value" lines in segment 0005
    # Format: " 0005:XXXXXXXX       symbol_name    XXXXXXXX f   Lib:Object"
    #     or: " 0005:XXXXXXXX       symbol_name    XXXXXXXX f i Lib:Object"
    # The 'i' marks inline functions. The Lib:Object is the last field.
    text_pattern = re.compile(
        r"^\s+0005:[0-9a-fA-F]+\s+\S+\s+[0-9a-fA-F]+\s+f\s+(?:i\s+)?(.+)$"
    )

    seen: set[str] = set()
    ordered: list[str] = []

    with open(map_path, "r") as f:
        for line in f:
            m = text_pattern.match(line)
            if not m:
                continue

            obj_ref = m.group(1).strip()

            # Skip section class markers
            if obj_ref in ("CODE", "DATA"):
                continue

            # Normalize import stub names:
            # "xapilibi:xam.xex@21173.0+1861.0" -> "xapilibi:xam.obj"
            # "XBOXKRNL:xboxkrnl.exe@21173.0+1861.0" -> "XBOXKRNL:xboxkrnl.obj"
            if "@" in obj_ref:
                # Strip version suffix and replace extension
                base = obj_ref.split("@")[0]
                # Replace .xex or .exe with .obj
                base = re.sub(r"\.(xex|exe)$", ".obj", base)
                obj_ref = base

            if obj_ref not in seen:
                seen.add(obj_ref)
                ordered.append(obj_ref)

    return ordered


def map_to_unit_names(
    text_order: list[str],
    exact: dict[str, str],
    basename_lookup: dict[str, list[str]],
) -> tuple[list[str], list[str]]:
    """
    Map ordered object keys to dtk unit names.
    Returns (mapped_units, unmapped_objects).
    """
    mapped: list[str] = []
    unmapped: list[str] = []

    for obj_key in text_order:
        unit = resolve_unit_name(obj_key, exact, basename_lookup)
        if unit:
            mapped.append(unit)
        else:
            unmapped.append(obj_key)

    return mapped, unmapped


def generate(verify: bool = False) -> int:
    if not MAP_FILE.exists():
        print(f"Error: Map file not found: {MAP_FILE}", file=sys.stderr)
        return 1

    if not DTK_CONFIG.exists():
        print(f"Error: dtk config not found: {DTK_CONFIG}", file=sys.stderr)
        print("Run 'ninja' first to generate the build config.", file=sys.stderr)
        return 1

    log = lambda msg: print(msg, file=sys.stderr)

    log(f"Reading map file: {MAP_FILE}")
    text_order = parse_map_text_order(MAP_FILE)
    log(f"  Found {len(text_order)} unique objects in .text segment")

    log(f"Reading dtk config: {DTK_CONFIG}")
    exact, basename_lookup = build_unit_lookup(DTK_CONFIG)
    log(f"  Found {len(exact)} exact mappings, {len(basename_lookup)} basename entries")

    mapped, unmapped = map_to_unit_names(text_order, exact, basename_lookup)
    log(f"  Mapped: {len(mapped)} units")
    if unmapped:
        log(f"  Unmapped: {len(unmapped)} objects (not in dtk config)")
        for obj in unmapped:
            log(f"    - {obj}")

    if verify:
        # In verify mode, compare against existing link_order.txt
        if not OUTPUT_FILE.exists():
            log(f"\nNo existing {OUTPUT_FILE} to verify against.")
            return 1

        existing = OUTPUT_FILE.read_text().strip().splitlines()
        if existing == mapped:
            log(f"\nVerification PASSED: link order matches ({len(mapped)} units)")
            return 0
        else:
            log(f"\nVerification FAILED: link order differs")
            log(f"  Expected {len(existing)} units, got {len(mapped)}")
            # Show first difference
            for i, (e, m) in enumerate(zip(existing, mapped)):
                if e != m:
                    log(f"  First difference at position {i}:")
                    log(f"    Expected: {e}")
                    log(f"    Got:      {m}")
                    break
            return 1

    # Write output
    output_dir = OUTPUT_FILE.parent
    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        with open(OUTPUT_FILE, "w") as f:
            for unit_name in mapped:
                f.write(unit_name + "\n")
        log(f"\nWrote {len(mapped)} unit names to {OUTPUT_FILE}")
    except OSError as e:
        # Fall back to stdout if config dir isn't writable
        log(f"\nCannot write to {OUTPUT_FILE}: {e}")
        log("Writing to stdout instead:")
        for unit_name in mapped:
            print(unit_name)

    # Also report the autogenerated objects from dtk config that aren't in
    # the map file (data-only objects, etc.)
    map_set = set(mapped)
    with open(DTK_CONFIG, "r") as f:
        cfg = json.load(f)
    extra = []
    for unit in cfg["units"]:
        if unit["name"] not in map_set and unit.get("autogenerated", False):
            extra.append(unit["name"])
    if extra:
        log(f"\n  {len(extra)} autogenerated data objects not in .text (will keep original order)")

    return 0


def main():
    parser = argparse.ArgumentParser(
        description="Generate link order from original map file"
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="verify existing link_order.txt matches map file",
    )
    args = parser.parse_args()

    sys.exit(generate(verify=args.verify))


if __name__ == "__main__":
    main()
