#!/usr/bin/env python3
"""
Detect OBJ_CLASSNAME mismatches by cross-referencing C++ headers against
the target binary's symbol table.

Logic:
  1. Parse all headers for `OBJ_CLASSNAME(Y)` inside `class X`, where X != Y
  2. From dc_symbols.txt, check if X::StaticClassName and Y::StaticClassName
     exist at DIFFERENT addresses
  3. If different addresses → they return different strings in the target →
     OBJ_CLASSNAME(Y) in class X is WRONG (should be OBJ_CLASSNAME(X))
  4. If same address (ICF-merged) → they return the same string → correct

Also cross-references the DTA objects config: if (X (types ...)) exists in
ham_objects.dta, then X's StaticClassName must return "X".

Usage:
  python3 scripts/analysis/check_obj_classname.py
"""

import os
import re
import sys
from collections import defaultdict
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
SYMBOLS_FILE = PROJECT_ROOT / "docs" / "dc_symbols.txt"
SRC_DIR = PROJECT_ROOT / "src"
INCLUDE_DIR = PROJECT_ROOT / "include"
DTA_DIR = PROJECT_ROOT / "orig-assets" / "extracted"


def parse_obj_classname_from_headers():
    """
    Find all OBJ_CLASSNAME(Y) declarations and determine the enclosing class X.
    Returns list of (file, line, class_name, classname_arg).
    """
    results = []
    header_dirs = [SRC_DIR, INCLUDE_DIR]

    for header_dir in header_dirs:
        if not header_dir.exists():
            continue
        for header in header_dir.rglob("*.h"):
            try:
                content = header.read_text(errors="replace")
            except Exception:
                continue

            lines = content.split("\n")
            # Track the most recent class/struct declaration
            current_class = None

            for i, line in enumerate(lines, 1):
                # Match class declarations: class Foo : public Bar {
                cls_match = re.match(
                    r"\s*(?:class|struct)\s+(\w+)\s*(?::|{)", line
                )
                if cls_match:
                    current_class = cls_match.group(1)

                # Match OBJ_CLASSNAME(X)
                obj_match = re.search(r"OBJ_CLASSNAME\((\w+)\)", line)
                if obj_match and current_class:
                    classname_arg = obj_match.group(1)
                    rel_path = header.relative_to(PROJECT_ROOT)
                    results.append((str(rel_path), i, current_class, classname_arg))

    return results


def parse_static_classname_symbols():
    """
    Parse dc_symbols.txt for StaticClassName entries.
    Returns dict: class_name -> address
    """
    symbols = {}
    if not SYMBOLS_FILE.exists():
        print(f"WARNING: {SYMBOLS_FILE} not found", file=sys.stderr)
        return symbols

    with open(SYMBOLS_FILE) as f:
        for line in f:
            # Match: 0xADDR: public: static class Symbol __cdecl Foo::StaticClassName(void)
            m = re.search(
                r"(0x[0-9a-f]+):\s+.*?(\w+)::StaticClassName\(void\)", line
            )
            if m:
                addr = int(m.group(1), 16)
                class_name = m.group(2)
                symbols[class_name] = addr

    return symbols


def parse_dta_object_configs():
    """
    Find all (ClassName (types ...)) entries in DTA config files.
    Returns set of class names that have their own types config.
    """
    config_classes = set()
    if not DTA_DIR.exists():
        return config_classes

    for dta_file in DTA_DIR.rglob("*.dta"):
        try:
            content = dta_file.read_text(errors="replace")
        except Exception:
            continue

        # Match top-level (ClassName\n   (types entries
        for m in re.finditer(r"^\((\w+)\s*\n\s*\(types\b", content, re.MULTILINE):
            config_classes.add(m.group(1))

    return config_classes


def main():
    print("=" * 72)
    print("OBJ_CLASSNAME Mismatch Detector")
    print("=" * 72)
    print()

    # Step 1: Parse headers
    declarations = parse_obj_classname_from_headers()
    mismatched = [
        (f, ln, cls, arg) for f, ln, cls, arg in declarations if cls != arg
    ]

    print(f"Total OBJ_CLASSNAME declarations: {len(declarations)}")
    print(f"Declarations where class != classname arg: {len(mismatched)}")
    print()

    if not mismatched:
        print("No mismatches found.")
        return

    # Step 2: Parse target symbols
    static_classnames = parse_static_classname_symbols()
    print(f"StaticClassName symbols in target: {len(static_classnames)}")

    # Step 3: Parse DTA configs
    dta_configs = parse_dta_object_configs()
    print(f"DTA object configs with (types): {len(dta_configs)}")
    print()

    # Step 4: Cross-reference
    bugs = []
    intentional = []
    unknown = []

    for filepath, line, class_name, classname_arg in mismatched:
        cls_addr = static_classnames.get(class_name)
        arg_addr = static_classnames.get(classname_arg)

        has_own_symbol = cls_addr is not None
        has_arg_symbol = arg_addr is not None
        same_address = (
            cls_addr is not None
            and arg_addr is not None
            and cls_addr == arg_addr
        )
        different_address = (
            cls_addr is not None
            and arg_addr is not None
            and cls_addr != arg_addr
        )

        # Check DTA config
        cls_in_dta = class_name in dta_configs
        arg_in_dta = classname_arg in dta_configs

        entry = {
            "file": filepath,
            "line": line,
            "class": class_name,
            "classname_arg": classname_arg,
            "cls_addr": cls_addr,
            "arg_addr": arg_addr,
            "cls_in_dta": cls_in_dta,
            "arg_in_dta": arg_in_dta,
        }

        if different_address:
            # Target has separate symbols → they return different strings
            # → OBJ_CLASSNAME should match the actual class
            if cls_in_dta:
                # DTA has config under class_name → definitely a bug
                entry["reason"] = (
                    f"Target has separate {class_name}::StaticClassName "
                    f"(0x{cls_addr:08x}) and {classname_arg}::StaticClassName "
                    f"(0x{arg_addr:08x}). "
                    f"DTA config exists under '{class_name}'. "
                    f"OBJ_CLASSNAME should be {class_name}."
                )
                bugs.append(entry)
            else:
                entry["reason"] = (
                    f"Target has separate symbols at different addresses "
                    f"(0x{cls_addr:08x} vs 0x{arg_addr:08x}). "
                    f"No DTA config under '{class_name}'. "
                    f"Likely bug — verify what string target returns."
                )
                bugs.append(entry)
        elif same_address:
            # ICF-merged → same string → intentional
            entry["reason"] = (
                f"ICF-merged: both at 0x{cls_addr:08x}. "
                f"Returns same string. OBJ_CLASSNAME({classname_arg}) is correct."
            )
            intentional.append(entry)
        elif has_own_symbol and not has_arg_symbol:
            # Only the class has a symbol, not the arg — unusual
            entry["reason"] = (
                f"{class_name}::StaticClassName exists at 0x{cls_addr:08x} "
                f"but {classname_arg}::StaticClassName not found. "
                f"Check if {classname_arg} is a base class without OBJ_CLASSNAME."
            )
            unknown.append(entry)
        elif not has_own_symbol and has_arg_symbol:
            # Only the arg has a symbol — class might be inlined or merged
            entry["reason"] = (
                f"{class_name}::StaticClassName not found in symbols. "
                f"{classname_arg}::StaticClassName at 0x{arg_addr:08x}. "
                f"Likely ICF-merged (class reports as parent). Probably correct."
            )
            intentional.append(entry)
        else:
            entry["reason"] = "Neither symbol found in target. Cannot verify."
            unknown.append(entry)

    # Print results
    if bugs:
        print("=" * 72)
        print(f"  BUGS DETECTED: {len(bugs)}")
        print("=" * 72)
        for e in bugs:
            print()
            print(f"  {e['file']}:{e['line']}")
            print(f"    class {e['class']} {{ OBJ_CLASSNAME({e['classname_arg']}); }}")
            print(f"    FIX → OBJ_CLASSNAME({e['class']})")
            print(f"    {e['reason']}")

    if intentional:
        print()
        print("-" * 72)
        print(f"  INTENTIONAL (ICF-merged or parent class): {len(intentional)}")
        print("-" * 72)
        for e in intentional:
            print(f"  {e['file']}:{e['line']}  class {e['class']} → OBJ_CLASSNAME({e['classname_arg']})  OK")

    if unknown:
        print()
        print("-" * 72)
        print(f"  UNKNOWN (symbols not found): {len(unknown)}")
        print("-" * 72)
        for e in unknown:
            print(f"  {e['file']}:{e['line']}  class {e['class']} → OBJ_CLASSNAME({e['classname_arg']})")
            print(f"    {e['reason']}")

    print()
    print("=" * 72)
    print(f"Summary: {len(bugs)} bugs, {len(intentional)} intentional, {len(unknown)} unknown")
    print("=" * 72)

    return 1 if bugs else 0


if __name__ == "__main__":
    sys.exit(main() or 0)
