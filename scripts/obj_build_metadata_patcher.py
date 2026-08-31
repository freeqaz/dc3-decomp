#!/usr/bin/env python3
"""Post-compile pass: zero the two build-metadata fields MSVC stamps into every
`.obj`, so that recompiling unchanged source produces byte-identical objects.

What was actually wrong
----------------------
Issue #150 was filed as "`OptionsPanel.obj` is byte-nondeterministic, suspect
the anon-namespace or atexit-scope patcher".  Both halves of that were wrong,
and the *population* was the bigger of the two errors.

Measured 2026-08-31 in a worktree, two full rebuilds of identical source in the
SAME tree (`touch` every `.cpp`, full `ninja`, sha256 all objects):

    989 objects compared
    980 DIFFER
      9 identical  -- and those 9 are simply the ones ninja did not rebuild
                      (ContentLoadingPanel.manual.obj, seven orphaned
                      src/system/synth objects, ZlibLicense.obj)

So it is not one object, it is **every object that gets recompiled**.  And it
is not a patcher: masking exactly two fields makes all 980 compare equal, with
ZERO residual differing bytes anywhere in the tree.

    COFF header TimeDateStamp   file offset 4..7          all 980
    CodeView S_OBJNAME signature  in `.debug$S`, 4 bytes    582 of them

`S_OBJNAME` is `0x1101`; its first four payload bytes are the object signature,
which MSVC derives from the clock.  The other CodeView record we emit,
`S_COMPILE3` (`0x1116`), is stable.

Both fields are compiler-emitted build metadata.  No source edit and no patcher
produced them; nothing in this repo could have made them stable by being
"deterministic", because the input that varies is the wall clock.

Why zero, specifically
----------------------
Because that is what the TARGET objects carry.  Every object under
`build/<version>/obj` -- the retail side of every diff, written by
`dtk xex split` -- has `TimeDateStamp = 0` and no `.debug$S` section at all.
Normalising to 0 moves our objects toward the baseline rather than to an
arbitrary sentinel, and it is the value a reproducible-build convention would
pick anyway.

Score-neutral by construction
-----------------------------
objdiff scores `.text`/`.data`/`.rdata`/`.pdata` and their relocations.  The
COFF timestamp is in the file header and `.debug$S` is not a diffed section, so
this pass cannot move `match_percent_normalized` for any function.  Verified
end-to-end: headline unchanged across the change (see the merge message).

What this does NOT claim
------------------------
That the build is now reproducible in every sense.  It is reproducible
**in this tree**: two rebuilds at the same absolute path now agree byte for
byte.  Building the same source at a DIFFERENT path still differs, because
MSVC writes the source path into `.debug$S` (`S_OBJNAME`'s name field) -- that
is a genuine path dependence and normalising it away would be destroying real
information rather than clock noise.  This is why the first A/B run for #150
(main repo objects vs worktree rebuild) showed 980/989 differing for a
*second*, unrelated reason and had to be discarded.

Usage:
    python3 scripts/obj_build_metadata_patcher.py --batch [--apply] [--verbose]
    python3 scripts/obj_build_metadata_patcher.py --batch --check   # exit 2 if pending
"""

import argparse
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from obj_patch_io import write_patched_obj  # mtime-preserving in-place write

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = PROJECT_ROOT / "build" / "373307D9" / "src"

#: CodeView subsection holding symbol records.
DEBUG_S_SYMBOLS = 0xF1
#: `S_OBJNAME`.  Payload is `signature:u32` then a NUL-terminated name.
S_OBJNAME = 0x1101
#: CodeView signature this compiler emits at the head of `.debug$S`.
CV_SIGNATURE_C13 = 4


def _debug_s_sections(data: bytes):
    """Yield `(pointer_to_raw_data, size)` for every `.debug$S` section."""
    if len(data) < 20:
        return
    nsec = struct.unpack_from("<H", data, 2)[0]
    if 20 + 40 * nsec > len(data):
        return
    for i in range(nsec):
        off = 20 + 40 * i
        if data[off:off + 8].rstrip(b"\0") != b".debug$S":
            continue
        size, praw = struct.unpack_from("<II", data, off + 16)
        if praw and size and praw + size <= len(data):
            yield praw, size


def objname_signature_offsets(data: bytes):
    """Absolute file offsets of every `S_OBJNAME` signature word.

    Parsed properly rather than pattern-matched: a bare search for the record
    type would also hit that byte pair inside a name string or a type index.
    """
    out = []
    for praw, size in _debug_s_sections(data):
        sec = data[praw:praw + size]
        if len(sec) < 4 or struct.unpack_from("<I", sec, 0)[0] != CV_SIGNATURE_C13:
            continue
        p = 4
        while p + 8 <= len(sec):
            sstype, sslen = struct.unpack_from("<II", sec, p)
            body_at = p + 8
            if sslen == 0 or body_at + sslen > len(sec):
                break
            if sstype == DEBUG_S_SYMBOLS:
                q = body_at
                end = body_at + sslen
                while q + 4 <= end:
                    reclen, rectype = struct.unpack_from("<HH", sec, q)
                    if reclen < 2 or q + 2 + reclen > end:
                        break
                    if rectype == S_OBJNAME and reclen >= 6:
                        out.append(praw + q + 4)
                    q += 2 + reclen
            p = body_at + ((sslen + 3) & ~3)
    return out


def plan(data: bytes):
    """`[absolute_offset, ...]` of every 4-byte field that is not already 0."""
    pending = []
    if len(data) >= 8 and data[4:8] != b"\0\0\0\0":
        pending.append(4)
    for off in objname_signature_offsets(data):
        if data[off:off + 4] != b"\0\0\0\0":
            pending.append(off)
    return pending


def normalize(data: bytes, offsets) -> bytes:
    out = bytearray(data)
    for off in offsets:
        out[off:off + 4] = b"\0\0\0\0"
    return bytes(out)


def process_batch(args) -> int:
    src = Path(args.src_dir) if args.src_dir else SRC_DIR
    if not src.exists():
        print(f"ERROR: decomp .obj directory not found: {src}", file=sys.stderr)
        return 1

    objs = sorted(p for p in src.rglob("*.obj") if p.is_file())
    if not objs:
        # Same refusal the rest of the chain makes: "0 pending" over an empty
        # universe is the number a perfectly normalised tree reports.
        print(f"ERROR: {src} contains no .obj files -- refusing to report a "
              f"fixed point over an empty universe.", file=sys.stderr)
        return 3

    pending_files = 0
    pending_fields = 0
    ts_fields = 0
    sig_fields = 0
    for path in objs:
        data = path.read_bytes()
        offsets = plan(data)
        if not offsets:
            continue
        pending_files += 1
        pending_fields += len(offsets)
        ts_fields += sum(1 for o in offsets if o == 4)
        sig_fields += sum(1 for o in offsets if o != 4)
        if args.apply:
            write_patched_obj(str(path), normalize(data, offsets))
        if args.verbose:
            action = "NORMALIZE" if args.apply else "WOULD NORMALIZE"
            print(f"  {action} {path.relative_to(src)}: "
                  f"{len(offsets)} field(s) at {offsets}")

    word = "Normalized" if args.apply else "Would normalize"
    print(f"{word} {pending_files} of {len(objs)} objects "
          f"({pending_fields} fields: {ts_fields} COFF TimeDateStamp, "
          f"{sig_fields} CodeView S_OBJNAME signature)")

    if args.check and pending_files > 0:
        print(f"FAIL[build_metadata]: {pending_files} object(s) still carry a "
              f"clock-derived COFF TimeDateStamp or CodeView S_OBJNAME "
              f"signature. Those fields change on every recompile, so this "
              f"tree is not byte-reproducible and any byte-identity control "
              f"run over it is measuring the clock. See "
              f"scripts/obj_build_metadata_patcher.py.", file=sys.stderr)
        return 2
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Zero MSVC's clock-derived build metadata in decomp .obj files")
    ap.add_argument("--batch", action="store_true", help="Process all decomp .obj files")
    ap.add_argument("--apply", action="store_true", help="Actually write (default: dry run)")
    ap.add_argument("--check", action="store_true",
                    help="Dry-run and EXIT 2 if any object still carries the metadata")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--src-dir", help="Decomp .obj directory (default: build/373307D9/src)")
    args = ap.parse_args()
    if not args.batch:
        print("ERROR: only --batch mode is supported.", file=sys.stderr)
        return 1
    return process_batch(args)


if __name__ == "__main__":
    sys.exit(main())
