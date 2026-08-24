#!/usr/bin/env python3
"""Post-build patcher giving ??__E dynamic-initializer symbols the right storage class.

MSVC emits ??__E symbols (C++ dynamic initializers for global/static objects)
with STATIC storage class, and derives their NAME from the bare variable name
alone.  Two things follow:

1. The CRT init table carved from the original image
   (auto_08_82F05C00_data.obj) references some of these thunks by their
   symbols.txt names, so the decomp obj that owns such a name must export it
   EXTERNAL or the reference cannot resolve to it.

2. Same-named file-scope variables in DIFFERENT TUs (legal C++: DataFile.cpp
   and DataArray.cpp both have file-scope gFile/gConditional, five TUs have an
   sLicense) compile to identically-named ??__E thunks.  If more than one of
   them is EXTERNAL, /FORCE:MULTIPLE resolves every by-name reference --
   including each obj's own .CRT$XCU entry -- to the FIRST definition, so one
   variable is initialized twice and the others stay zero-filled BSS.  That is
   what left DataArray.cpp's gConditional (the DTB #ifdef conditional stack)
   unconstructed in every linked image between 2026-08-19 and 2026-08-24.

So the rule is:

- A ??__E name defined by exactly ONE decomp obj is promoted STATIC->EXTERNAL
  (previous behavior; lets auto_08 resolve it when it wants it).
- A ??__E name defined by SEVERAL decomp objs is promoted only in the obj that
  OWNS the name: symbols.txt records the name at exactly one address, and the
  original linker map says which unit that address belongs to.  Every other
  definition is kept (or demoted back to) STATIC -- a static thunk still binds
  section-locally to its own obj's .CRT$XCU entry, which is the only reference
  it needs to serve.

Patches are LOST on rebuild (same as regswap/anon_ns patchers) — this is a post-build step.

Usage:
    python3 scripts/obj_dynamic_init_patcher.py --batch [--apply] [--verbose]

Without --apply, performs a dry run showing what would be changed.
"""

import argparse
import glob
import re
import struct
import sys
from pathlib import Path
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
from obj_patch_io import write_patched_obj  # mtime-preserving in-place write

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = PROJECT_ROOT / "build" / "373307D9" / "src"
SYMBOLS_TXT = PROJECT_ROOT / "config" / "373307D9" / "symbols.txt"
ORIG_MAP = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3

# Minimum size of a COFF file header. We read the symbol-table pointer and
# count at offsets 8 and 12 (4 bytes each), so anything shorter than the full
# 20-byte header is not a parseable COFF object.
COFF_HEADER_SIZE = 20


def _iter_coff_syms(data):
    """Yield (entry_offset, name, storage_class, section) for each COFF symbol."""
    if len(data) < COFF_HEADER_SIZE:
        return
    sym_offset = struct.unpack_from('<I', data, 8)[0]
    num_syms = struct.unpack_from('<I', data, 12)[0]
    if num_syms == 0 or sym_offset == 0:
        return
    str_table_offset = sym_offset + num_syms * 18
    i = 0
    while i < num_syms:
        entry_off = sym_offset + i * 18
        name_bytes = bytes(data[entry_off:entry_off + 8])
        section = struct.unpack_from('<h', data, entry_off + 12)[0]
        storage_class = data[entry_off + 16]
        num_aux = data[entry_off + 17]
        if name_bytes[:4] == b'\x00\x00\x00\x00':
            str_off = struct.unpack_from('<I', name_bytes, 4)[0]
            abs_off = str_table_offset + str_off
            end = data.index(b'\x00', abs_off)
            name = data[abs_off:end].decode('ascii', errors='replace')
        else:
            name = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')
        yield entry_off, name, storage_class, section
        i += 1 + num_aux


_OWNERS = None       # {??__E name: unit stem that owns it}
_CENSUS = None       # {??__E name: number of decomp objs defining it}


def _load_owners():
    """Map each symbols.txt-named ??__E thunk to the original unit that owns it.

    symbols.txt gives the name exactly one address; the original linker map
    says which unit that address belongs to.  Both files ship with the repo,
    so the answer is stable across rebuilds.
    """
    global _OWNERS
    if _OWNERS is not None:
        return _OWNERS
    name_to_addr = {}
    if SYMBOLS_TXT.exists():
        pat = re.compile(r'^(\?\?__E\S+) = \.text:0x([0-9A-Fa-f]{8});')
        with open(SYMBOLS_TXT, 'r', errors='replace') as f:
            for line in f:
                if not line.startswith('??__E'):
                    continue
                m = pat.match(line)
                if m:
                    name_to_addr[m.group(1)] = int(m.group(2), 16)
    addr_to_unit = {}
    if ORIG_MAP.exists():
        with open(ORIG_MAP, 'r', errors='replace') as f:
            for line in f:
                parts = line.split()
                # " 0005:00bab268  ??__EsLicense@@YAXXZ  82edb268 f  math:SHA1.obj"
                if len(parts) >= 4 and parts[1].startswith('??__E'):
                    try:
                        addr = int(parts[2], 16)
                    except ValueError:
                        continue
                    addr_to_unit[addr] = Path(parts[-1].split(':')[-1]).stem
    _OWNERS = {}
    for name, addr in name_to_addr.items():
        unit = addr_to_unit.get(addr)
        if unit:
            _OWNERS[name] = unit
    return _OWNERS


def _load_census(src_dir=None):
    """Count, across all decomp objs, how many define each ??__E name."""
    global _CENSUS
    if _CENSUS is not None:
        return _CENSUS
    census = {}
    base = Path(src_dir) if src_dir else SRC_DIR
    for obj_path in sorted(glob.glob(str(base / '**' / '*.obj'), recursive=True)):
        try:
            with open(obj_path, 'rb') as f:
                data = f.read()
        except OSError:
            continue
        for _off, name, cls, section in _iter_coff_syms(data):
            if (name.startswith('??__E') and section > 0
                    and cls in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC)):
                census[name] = census.get(name, 0) + 1
    _CENSUS = census
    return _CENSUS


def patch_obj(path, apply=False, verbose=False, unit_stem=None):
    """Give each ??__E symbol in a COFF .obj its correct storage class.

    Unique names are promoted STATIC->EXTERNAL; duplicated names are EXTERNAL
    only in the obj that owns the name per symbols.txt + the original map, and
    STATIC everywhere else (demoting if a previous run promoted them).

    `unit_stem` names the logical unit when `path` is a scratch copy living
    outside build/<id>/src (obj_patch_chain's permuter candidates); it defaults
    to the file's own stem.

    Returns list of "promote:<name>" / "demote:<name>" changes.
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

    stem = unit_stem or Path(path).stem
    owners = _load_owners()
    census = _load_census()

    patched_names = []
    for entry_off, name, storage_class, section in _iter_coff_syms(data):
        if not name.startswith('??__E') or section <= 0:
            continue
        if storage_class not in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC):
            continue
        duplicated = census.get(name, 1) > 1
        want_external = (not duplicated) or (owners.get(name) == stem)
        if duplicated and name not in owners and verbose:
            print(f"  NOTE: duplicated ??__E with no symbols.txt owner, "
                  f"keeping STATIC everywhere: {name}", file=sys.stderr)
        if want_external and storage_class == IMAGE_SYM_CLASS_STATIC:
            data[entry_off + 16] = IMAGE_SYM_CLASS_EXTERNAL
            patched_names.append(f"promote:{name}")
        elif not want_external and storage_class == IMAGE_SYM_CLASS_EXTERNAL:
            data[entry_off + 16] = IMAGE_SYM_CLASS_STATIC
            patched_names.append(f"demote:{name}")

    if patched_names and apply:
        write_patched_obj(path, data)

    return patched_names


def process_batch(args):
    """Process all decomp .obj files in batch mode."""
    src_dir = Path(args.src_dir) if args.src_dir else SRC_DIR

    if not src_dir.exists():
        print(f"ERROR: Decomp .obj directory not found: {src_dir}", file=sys.stderr)
        sys.exit(1)

    # A non-default --src-dir must not reuse (or poison) the default census.
    global _CENSUS
    _CENSUS = None
    _load_census(src_dir)

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
    print(f"  (unique names -> EXTERNAL; duplicated names EXTERNAL only in the "
          f"symbols.txt/original-map owner, STATIC elsewhere)")

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
        description='Set the correct storage class on ??__E dynamic initializer symbols in decomp .obj files')
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
