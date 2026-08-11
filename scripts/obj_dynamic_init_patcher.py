#!/usr/bin/env python3
"""Post-build patcher to promote ??__E dynamic initializer symbols from STATIC to EXTERNAL.

MSVC emits ??__E symbols (C++ dynamic initializers for global/static objects) with
STATIC storage class, making them invisible to the linker. The CRT init table in
auto_08_82F05C00_data.obj has EXTERNAL references to these symbols, requiring
ALTERNATENAME stubs in link_glue.cpp to resolve them.

This patcher promotes ??__E from STATIC to EXTERNAL in decomp .obj files,
allowing the linker to resolve auto_08's references directly and eliminating
the need for those ALTERNATENAME stubs.

Patches are LOST on rebuild (same as regswap/anon_ns patchers) — this is a post-build step.

Usage:
    python3 scripts/obj_dynamic_init_patcher.py --batch [--apply] [--verbose]

Without --apply, performs a dry run showing what would be changed.
"""

import argparse
import glob
import struct
import sys
from pathlib import Path
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
from obj_patch_io import write_patched_obj  # mtime-preserving in-place write

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = PROJECT_ROOT / "build" / "373307D9" / "src"

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3

# Minimum size of a COFF file header. We read the symbol-table pointer and
# count at offsets 8 and 12 (4 bytes each), so anything shorter than the full
# 20-byte header is not a parseable COFF object.
COFF_HEADER_SIZE = 20


def patch_obj(path, apply=False, verbose=False):
    """Find ??__E STATIC symbols in a COFF .obj and promote to EXTERNAL.

    Returns list of patched symbol names.
    """
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    # Skip empty/truncated orphan .obj files. Reflinked worktrees can pick up
    # zero-byte orphan objects (e.g. a build/.../system/utl/StreamRecorder.obj
    # with no ninja rule), and struct.unpack_from would crash on them. Anything
    # smaller than a full COFF header has no symbols to patch — skip it cleanly.
    if len(data) < COFF_HEADER_SIZE:
        if verbose:
            print(f"  SKIP (empty/short, {len(data)} bytes): {path}", file=sys.stderr)
        return []

    # COFF header
    sym_offset = struct.unpack_from('<I', data, 8)[0]
    num_syms = struct.unpack_from('<I', data, 12)[0]
    if num_syms == 0 or sym_offset == 0:
        return []

    str_table_offset = sym_offset + num_syms * 18

    patched_names = []
    i = 0
    while i < num_syms:
        entry_off = sym_offset + i * 18
        name_bytes = bytes(data[entry_off:entry_off + 8])
        storage_class = data[entry_off + 16]
        num_aux = data[entry_off + 17]

        # Resolve symbol name
        if name_bytes[:4] == b'\x00\x00\x00\x00':
            str_off = struct.unpack_from('<I', name_bytes, 4)[0]
            abs_off = str_table_offset + str_off
            end = data.index(b'\x00', abs_off)
            name = data[abs_off:end].decode('ascii', errors='replace')
        else:
            name = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')

        if name.startswith('??__E') and storage_class == IMAGE_SYM_CLASS_STATIC:
            data[entry_off + 16] = IMAGE_SYM_CLASS_EXTERNAL
            patched_names.append(name)

        i += 1 + num_aux

    if patched_names and apply:
        write_patched_obj(path, data)

    return patched_names


def process_batch(args):
    """Process all decomp .obj files in batch mode."""
    src_dir = Path(args.src_dir) if args.src_dir else SRC_DIR

    if not src_dir.exists():
        print(f"ERROR: Decomp .obj directory not found: {src_dir}", file=sys.stderr)
        sys.exit(1)

    total_patched = 0
    files_patched = 0
    all_symbols = []

    for obj_path in sorted(glob.glob(str(src_dir / '**' / '*.obj'), recursive=True)):
        names = patch_obj(obj_path, apply=args.apply, verbose=args.verbose)
        if names:
            files_patched += 1
            total_patched += len(names)
            all_symbols.extend(names)
            if args.verbose:
                relpath = Path(obj_path).relative_to(src_dir)
                for n in names:
                    print(f"  {relpath}: {n}")

    action = "Patched" if args.apply else "Would patch"
    print(f"\n{action} {total_patched} ??__E symbols across {files_patched} files")
    print(f"  (STATIC -> EXTERNAL promotion for linker visibility)")

    if not args.apply and total_patched > 0:
        print(f"\nRun with --apply to actually patch the files.")

    if getattr(args, 'check', False) and total_patched > 0:
        print('FAIL[dynamic_init]: {n} pending patch(es) -- this build tree carries '
              'objects that were compiled but never post-processed. See '
              'docs/tools/BUILD_SYSTEM.md "post-compile patchers".'.format(n=total_patched),
              file=sys.stderr)
        sys.exit(2)


def main():
    parser = argparse.ArgumentParser(
        description='Promote ??__E dynamic initializer symbols from STATIC to EXTERNAL in decomp .obj files')
    parser.add_argument('--apply', action='store_true',
                        help='Actually apply patches (default: dry run)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show per-file details')
    parser.add_argument('--batch', action='store_true',
                        help='Process all decomp .obj files')
    parser.add_argument('--src-dir',
                        help='Decomp .obj directory (default: build/373307D9/src)')
    parser.add_argument('--check', action='store_true',
                        help='Dry-run and EXIT 2 if any object in the build tree '
                             'still needs this pass (used by '
                             'scripts/verify_objs_patched.py)')
    args = parser.parse_args()

    if not args.batch:
        print("ERROR: Currently only --batch mode is supported.", file=sys.stderr)
        print("Usage: python3 scripts/obj_dynamic_init_patcher.py --batch [--apply] [--verbose]",
              file=sys.stderr)
        sys.exit(1)

    process_batch(args)


if __name__ == '__main__':
    main()
