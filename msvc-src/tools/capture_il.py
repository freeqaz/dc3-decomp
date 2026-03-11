#!/usr/bin/env python3
"""
Capture the intermediate language (IL) files that c1xx.dll passes to c2.dll.

MSVC's compilation pipeline:
  c1xx.dll (front-end) -> IL temp file -> c2.dll (back-end)

CL normally deletes the IL file after compilation. This script:
1. Runs a compilation with /FAs to get assembly listing (reveals IL structure indirectly)
2. Attempts to capture the actual IL file by monitoring temp files during compilation
3. Extracts timing/diagnostic info via /d2cgsummary and /d1reportTime

Usage:
    python3 msvc-src/tools/capture_il.py --source <file.cpp> [--keep-il] [--diagnostics]
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time
import shutil
from pathlib import Path


# Default compiler path relative to project root
DEFAULT_CL = "build/compilers/X360/16.00.11886.00/cl.exe"
DEFAULT_WIBO = "../wibo/build/release/wibo"


def get_compile_command(source: str, extra_flags: list[str] = None) -> list[str]:
    """Build the compilation command from ninja build config."""
    # Extract the standard cflags from configure.py output
    # For now, use a reasonable default set
    base_flags = [
        "/nologo", "/c", "/Zi", "/O1", "/TP",
        "/Isrc/system", "/Iinclude",
    ]

    if extra_flags:
        base_flags.extend(extra_flags)

    return base_flags


def find_wibo() -> str:
    """Find the wibo binary."""
    candidates = [
        DEFAULT_WIBO,
        "../wibo/build/wibo",
        "wibo",
    ]
    for c in candidates:
        if Path(c).exists():
            return str(Path(c).resolve())
    raise FileNotFoundError("wibo not found. Build it first.")


def capture_compilation_diagnostics(source: str, cl_path: str, wibo_path: str,
                                     output_dir: str, extra_flags: list[str] = None):
    """Run compilation with diagnostic flags and capture output."""
    flags = get_compile_command(source, extra_flags)

    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    obj_path = output_dir / "output.obj"
    asm_path = output_dir / "output.asm"

    # Add diagnostic flags
    diag_flags = [
        "/FAs",                    # Assembly listing with source
        f"/Fa{asm_path}",         # Assembly output path
        f"/Fo{obj_path}",         # Object output path
    ]

    # Add /d2 diagnostic flags if requested
    if extra_flags and "/d2cgsummary" in extra_flags:
        pass  # Already included

    cmd = [wibo_path, cl_path] + flags + diag_flags + [source]

    print(f"Running: {' '.join(cmd)}")

    env = os.environ.copy()
    env["WIBO_FS_CACHE"] = "1"

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )

    print(f"Return code: {result.returncode}")
    if result.stdout:
        print(f"STDOUT:\n{result.stdout}")
    if result.stderr:
        print(f"STDERR:\n{result.stderr}")

    # Check for generated files
    if asm_path.exists():
        print(f"\nAssembly listing: {asm_path} ({asm_path.stat().st_size} bytes)")
    if obj_path.exists():
        print(f"Object file: {obj_path} ({obj_path.stat().st_size} bytes)")

    return result


def try_capture_il(source: str, cl_path: str, wibo_path: str, output_dir: str):
    """
    Attempt to capture the IL file by monitoring temp directory.

    The front-end writes an IL file to %TMP%, then c2.dll reads and deletes it.
    We can try to:
    1. Set TMP to a monitored directory
    2. Use inotifywait or polling to catch the file
    3. Or use /Bx to intercept the pipeline
    """
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Create a temp dir that we control
    tmp_dir = output_dir / "tmp_capture"
    tmp_dir.mkdir(exist_ok=True)

    flags = get_compile_command(source)
    obj_path = output_dir / "output.obj"

    cmd = [wibo_path, cl_path] + flags + [f"/Fo{obj_path}", source]

    env = os.environ.copy()
    env["TMP"] = str(tmp_dir)
    env["TEMP"] = str(tmp_dir)

    print(f"Setting TMP={tmp_dir}")
    print(f"Running compilation...")

    # Start monitoring the temp dir in background
    # Note: under wibo, Windows temp paths may not map directly

    # Take snapshot before
    before = set(tmp_dir.iterdir()) if tmp_dir.exists() else set()

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )

    # Take snapshot after
    after = set(tmp_dir.iterdir()) if tmp_dir.exists() else set()

    new_files = after - before
    if new_files:
        print(f"New files in temp dir: {[f.name for f in new_files]}")
        for f in new_files:
            dest = output_dir / f"captured_il_{f.name}"
            shutil.copy2(f, dest)
            print(f"  Captured: {dest} ({dest.stat().st_size} bytes)")
    else:
        print("No new files captured in temp dir")
        print("(IL file may have been created and deleted, or wibo mapped paths differently)")

    # Check for any leftover temp files
    remaining = list(tmp_dir.iterdir())
    if remaining:
        print(f"Remaining temp files: {[f.name for f in remaining]}")

    return result


def main():
    parser = argparse.ArgumentParser(description='Capture MSVC IL files and diagnostics')
    parser.add_argument('--source', required=True, help='C++ source file to compile')
    parser.add_argument('--cl', default=DEFAULT_CL, help='Path to cl.exe')
    parser.add_argument('--wibo', default=DEFAULT_WIBO, help='Path to wibo')
    parser.add_argument('--output', default='msvc-src/analysis/captures', help='Output directory')
    parser.add_argument('--keep-il', action='store_true', help='Try to capture IL file')
    parser.add_argument('--diagnostics', action='store_true', help='Enable /d2cgsummary')
    parser.add_argument('--cgsummary', action='store_true', help='Enable /d2cgsummary')
    parser.add_argument('--extra-flags', nargs='*', default=[], help='Extra compiler flags')
    args = parser.parse_args()

    if not Path(args.source).exists():
        print(f"Error: {args.source} not found", file=sys.stderr)
        sys.exit(1)

    wibo_path = find_wibo()
    cl_path = args.cl

    extra = list(args.extra_flags)
    if args.cgsummary or args.diagnostics:
        extra.append("/d2cgsummary")

    print(f"=== MSVC IL Capture Tool ===")
    print(f"Source: {args.source}")
    print(f"Compiler: {cl_path}")
    print(f"Wibo: {wibo_path}")
    print()

    if args.keep_il:
        try_capture_il(args.source, cl_path, wibo_path, args.output)

    capture_compilation_diagnostics(args.source, cl_path, wibo_path, args.output, extra)


if __name__ == '__main__':
    main()
