#!/usr/bin/env python3
"""Find and list assets in an extracted DC3 ark directory.

Usage:
  find_assets.py [extracted_dir]                    List all assets by type
  find_assets.py [extracted_dir] --type milo_xbox   Filter by extension
  find_assets.py [extracted_dir] --grep pattern     Filter by name pattern
  find_assets.py [extracted_dir] --tree             Show directory tree
  find_assets.py [extracted_dir] --summary          Show type counts only
"""

import argparse
import fnmatch
import os
import re
import sys
from collections import defaultdict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_EXTRACTED = os.path.join(REPO_ROOT, "orig-assets", "extracted")
ALT_ASSETS = os.path.expanduser(
    "~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3"
)


def find_all_files(root):
    """Walk directory and yield (relative_path, size) tuples."""
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            try:
                size = os.path.getsize(full)
            except OSError:
                size = 0
            yield rel, size


def get_extension(path):
    """Get extension, handling compound extensions like .milo_xbox."""
    name = os.path.basename(path)
    # Handle compound extensions
    for ext in (".milo_xbox", ".milo_ps3", ".milo_wii", ".milo_ps2",
                ".rnd_xbox", ".rnd_gc", ".rnd_gz", ".rnd_ps2",
                ".png_xbox", ".png_ps3", ".bmp_xbox"):
        if name.endswith(ext):
            return ext
    _, ext = os.path.splitext(name)
    return ext or "(no ext)"


def format_size(size):
    """Human-readable file size."""
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024:
            return f"{size:.1f} {unit}" if unit != "B" else f"{size} {unit}"
        size /= 1024
    return f"{size:.1f} TB"


def print_summary(files):
    """Print counts by extension."""
    by_ext = defaultdict(lambda: {"count": 0, "size": 0})
    for path, size in files:
        ext = get_extension(path)
        by_ext[ext]["count"] += 1
        by_ext[ext]["size"] += size

    total_count = sum(v["count"] for v in by_ext.values())
    total_size = sum(v["size"] for v in by_ext.values())

    print(f"{'Extension':<20} {'Count':>8}  {'Size':>12}")
    print("-" * 44)
    for ext in sorted(by_ext, key=lambda e: by_ext[e]["size"], reverse=True):
        info = by_ext[ext]
        print(f"{ext:<20} {info['count']:>8}  {format_size(info['size']):>12}")
    print("-" * 44)
    print(f"{'TOTAL':<20} {total_count:>8}  {format_size(total_size):>12}")


def print_tree(files, max_depth=3):
    """Print directory tree."""
    dirs = defaultdict(list)
    for path, size in files:
        parts = path.split(os.sep)
        key = os.sep.join(parts[:min(len(parts) - 1, max_depth)])
        dirs[key].append((parts[-1], size))

    for dir_path in sorted(dirs):
        entries = dirs[dir_path]
        print(f"\n{dir_path}/ ({len(entries)} files)")
        for name, size in sorted(entries)[:20]:
            print(f"  {name:<50} {format_size(size):>10}")
        if len(entries) > 20:
            print(f"  ... and {len(entries) - 20} more")


def main():
    parser = argparse.ArgumentParser(description="Find and list DC3 extracted assets")
    parser.add_argument("dir", nargs="?", help="Extracted assets directory")
    parser.add_argument("--type", "-t", metavar="EXT", help="Filter by extension (e.g. milo_xbox, dta, dtb)")
    parser.add_argument("--grep", "-g", metavar="PATTERN", help="Filter by name pattern (fnmatch or regex)")
    parser.add_argument("--tree", action="store_true", help="Show directory tree")
    parser.add_argument("--summary", "-s", action="store_true", help="Show type counts only")
    parser.add_argument("--depth", type=int, default=3, help="Tree max depth (default: 3)")
    parser.add_argument("--path", "-p", metavar="SUBDIR", help="Only search within this subdirectory")

    args = parser.parse_args()

    # Find the assets directory
    root = args.dir
    if not root:
        if os.path.isdir(DEFAULT_EXTRACTED):
            root = DEFAULT_EXTRACTED
        elif os.path.isdir(ALT_ASSETS):
            root = ALT_ASSETS
            print(f"(Using pre-extracted assets at {root})")
        else:
            print("ERROR: No extracted assets found.")
            print(f"  Expected: {DEFAULT_EXTRACTED}")
            print(f"  Or run:   scripts/milo/extract_ark.sh")
            sys.exit(1)

    if not os.path.isdir(root):
        print(f"ERROR: Not a directory: {root}")
        sys.exit(1)

    if args.path:
        root = os.path.join(root, args.path)
        if not os.path.isdir(root):
            print(f"ERROR: Subdirectory not found: {root}")
            sys.exit(1)

    # Collect files
    all_files = list(find_all_files(root))

    # Filter by type
    if args.type:
        ext_filter = args.type if args.type.startswith(".") else "." + args.type
        all_files = [(p, s) for p, s in all_files if get_extension(p) == ext_filter]

    # Filter by pattern
    if args.grep:
        try:
            regex = re.compile(args.grep, re.IGNORECASE)
            all_files = [(p, s) for p, s in all_files if regex.search(p)]
        except re.error:
            all_files = [(p, s) for p, s in all_files
                         if fnmatch.fnmatch(os.path.basename(p).lower(), args.grep.lower())]

    if not all_files:
        print("No matching files found.")
        sys.exit(0)

    print(f"Assets in: {root}")
    print(f"Matched: {len(all_files)} files\n")

    if args.summary:
        print_summary(all_files)
    elif args.tree:
        print_tree(all_files, args.depth)
    else:
        # Default: summary + list
        print_summary(all_files)
        if len(all_files) <= 100:
            print(f"\n{'Path':<70} {'Size':>10}")
            print("-" * 82)
            for path, size in sorted(all_files):
                print(f"{path:<70} {format_size(size):>10}")


if __name__ == "__main__":
    main()
