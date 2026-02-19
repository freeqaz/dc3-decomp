#!/usr/bin/env python3
"""
Standalone proof-of-concept: link split .obj files back into a PE.

Reads build/373307D9/config.json for the ordered unit list,
writes a response file, and invokes X360 link.exe via wibo.

Usage:
    python3 scripts/build/link_test.py [--force-multiple] [--verbose] [--map]
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BUILD_DIR = ROOT / "build" / "373307D9"
CONFIG_JSON = BUILD_DIR / "config.json"
ORIG_PE = ROOT / "orig" / "373307D9" / "ham_xbox_r.exe"
WIBO = ROOT / "build" / "tools" / "wibo"
LINK_EXE = ROOT / "build" / "compilers" / "X360" / "16.00.11886.00" / "link.exe"

OUT_PE = BUILD_DIR / "ham_xbox_r_test.exe"
OUT_MAP = BUILD_DIR / "ham_xbox_r_test.map"
RSP_FILE = BUILD_DIR / "link_test.rsp"


def main():
    parser = argparse.ArgumentParser(description="PoC: link split objects into PE")
    parser.add_argument("--force-multiple", action="store_true",
                        help="Use /FORCE:MULTIPLE to ignore duplicate symbols")
    parser.add_argument("--verbose", action="store_true",
                        help="Pass /VERBOSE to linker")
    parser.add_argument("--map", action="store_true", default=True,
                        help="Generate .MAP file (default: yes)")
    parser.add_argument("--no-map", action="store_false", dest="map",
                        help="Don't generate .MAP file")
    parser.add_argument("--dry-run", action="store_true",
                        help="Write response file but don't invoke linker")
    parser.add_argument("--wrapper", type=str, default=None,
                        help="Wrapper command (default: auto-detect wibo/wine)")
    parser.add_argument("--exclude", type=str, nargs="*", default=[],
                        help="Object files to exclude (substring match)")
    parser.add_argument("--force", action="store_true",
                        help="Use /FORCE to bypass all linker errors")
    args = parser.parse_args()

    # Load build config
    if not CONFIG_JSON.exists():
        sys.exit(f"Build config not found: {CONFIG_JSON}\nRun 'ninja' first to generate it.")

    with open(CONFIG_JSON, "r", encoding="utf-8") as f:
        build_config = json.load(f)

    units = build_config["units"]
    entry = build_config.get("entry", "mainCRTStartup")
    print(f"Loaded {len(units)} units, entry={entry}")

    # Collect object paths and verify they exist
    # Use relative paths from ROOT because MSVC linker treats / as option prefix
    obj_paths = []
    missing = []
    excluded = []
    for unit in units:
        obj_path = unit.get("object")
        if obj_path is None:
            print(f"  Warning: unit '{unit['name']}' has no object path, skipping")
            continue
        # Check exclusions
        if any(ex in obj_path for ex in args.exclude):
            excluded.append(obj_path)
            continue
        full_path = ROOT / obj_path
        if not full_path.exists():
            missing.append(obj_path)
        # Use the path as-is from config (relative to project root)
        obj_paths.append(obj_path)

    if excluded:
        print(f"Excluded {len(excluded)} objects: {', '.join(os.path.basename(p) for p in excluded)}")

    if missing:
        print(f"\nWarning: {len(missing)} objects missing:")
        for p in missing[:10]:
            print(f"  {p}")
        if len(missing) > 10:
            print(f"  ... and {len(missing) - 10} more")
        sys.exit("Cannot link with missing objects. Run 'ninja' to split first.")

    print(f"All {len(obj_paths)} objects found")

    # Write response file
    with open(RSP_FILE, "w", encoding="utf-8") as f:
        for obj_path in obj_paths:
            f.write(f'"{obj_path}"\n')
    print(f"Wrote response file: {RSP_FILE}")

    # Use relative paths for linker output (MSVC treats / as option prefix)
    rel_out = os.path.relpath(OUT_PE, ROOT)
    rel_map = os.path.relpath(OUT_MAP, ROOT)
    rel_rsp = os.path.relpath(RSP_FILE, ROOT)

    # Build linker command
    ldflags = [
        "/NOLOGO",
        "/MACHINE:PPCBE",
        "/SUBSYSTEM:XBOX",
        "/BASE:0x82000000",
        f"/ENTRY:{entry}",
        "/NODEFAULTLIB",
        "/XEX:NO",
        f"/OUT:{rel_out}",
    ]

    if args.map:
        ldflags.append(f"/MAP:{rel_map}")

    if args.force and not args.force_multiple:
        ldflags.append("/FORCE")
    elif args.force_multiple:
        ldflags.append("/FORCE:MULTIPLE")

    if args.verbose:
        ldflags.append("/VERBOSE")

    # Determine wrapper
    # link.exe requires wine (not wibo) because it uses mspdb's RPC/named pipe
    # IPC which wibo cannot emulate, causing a segfault during the output phase.
    if args.wrapper:
        wrapper = args.wrapper
    else:
        import shutil
        if shutil.which("wine"):
            wrapper = "wine"
        else:
            sys.exit("wine is required for linking (wibo crashes due to mspdb RPC).\n"
                     "Install wine: sudo pacman -S wine  (or apt install wine)")

    cmd = [wrapper, str(LINK_EXE)] + ldflags + [f"@{rel_rsp}"]

    print(f"\nLinker command:")
    print(f"  {' '.join(str(c) for c in cmd[:2])} \\")
    for flag in ldflags:
        print(f"    {flag} \\")
    print(f"    @{rel_rsp}")

    if args.dry_run:
        print("\n[dry-run] Skipping actual link")
        return

    # Run linker
    log_file = BUILD_DIR / "link_test.log"
    print(f"\nLinking... (log: {log_file})")
    env = os.environ.copy()
    if "wine" in wrapper:
        env["WINEDEBUG"] = "-all"
        if "WINEPREFIX" not in env:
            env["WINEPREFIX"] = "/tmp/claude/wineprefix"

    with open(log_file, "w") as log:
        result = subprocess.run(
            cmd,
            cwd=str(ROOT),
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=600,
            env=env,
        )

    # Print summary from log
    with open(log_file) as log:
        lines = log.readlines()

    errors = [l.strip() for l in lines if "error LNK" in l or "fatal error" in l]
    warnings = [l.strip() for l in lines if "warning LNK" in l]
    other = [l.strip() for l in lines if "error LNK" not in l and "warning LNK" not in l and l.strip()]

    print(f"\nLinker output: {len(lines)} lines total")
    print(f"  Warnings: {len(warnings)}")
    print(f"  Errors: {len(errors)}")

    if errors:
        # Deduplicate error types
        error_types = {}
        for e in errors:
            # Extract LNK code
            import re
            m = re.search(r'LNK\d+', e)
            code = m.group() if m else 'unknown'
            error_types.setdefault(code, []).append(e)

        print(f"\nError summary:")
        for code, errs in sorted(error_types.items()):
            print(f"  {code}: {len(errs)} occurrences")
            for e in errs[:3]:
                print(f"    {e}")
            if len(errs) > 3:
                print(f"    ... and {len(errs) - 3} more")

    if other:
        print(f"\nOther output:")
        for line in other[:10]:
            print(f"  {line}")

    if result.returncode != 0:
        print(f"\nLink FAILED (exit code {result.returncode})")
        sys.exit(1)

    # Check output
    if OUT_PE.exists():
        size = OUT_PE.stat().st_size
        orig_size = ORIG_PE.stat().st_size if ORIG_PE.exists() else 0
        print(f"\nLink SUCCEEDED!")
        print(f"  Output: {OUT_PE} ({size:,} bytes)")
        if orig_size:
            print(f"  Original: {ORIG_PE} ({orig_size:,} bytes)")
            diff = size - orig_size
            print(f"  Size difference: {diff:+,} bytes")
    else:
        print(f"\nLink reported success but output not found: {OUT_PE}")
        sys.exit(1)


if __name__ == "__main__":
    main()
