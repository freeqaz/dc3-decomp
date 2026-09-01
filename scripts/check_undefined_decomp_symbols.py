#!/usr/bin/env python3
"""Find decomp symbols that are REFERENCED but DEFINED NOWHERE.

Why this exists
---------------
`JoypadSendKeepAlive` was declared in `src/system/os/Joypad.h`, called from
`JoypadPollCommon`, and defined in no translation unit at all -- for as long as
the file had existed. Nothing caught it.

The native link already passes `-Wl,--no-undefined`, and that check is real, but
it is *structurally incapable* of catching this class:

    clang at -O2 proves the only call site unreachable (`gPadsToKeepAlive` has
    internal linkage in Joypad.cpp and nothing ever stores a non-zero value into
    it), and deletes the reference BEFORE the linker sees it. At -O0 the
    reference survives and the link fails.

A check whose result depends on an optimizer decision is not a check. Rebuilding
the whole native port at -O0 would fix that, and is far too expensive to gate on.

This script reads the MSVC objects instead. MSVC at the decomp's own settings
keeps the reference -- measured: `build/373307D9/src/system/os/Joypad.obj`
carries `JoypadSendKeepAlive` as an undefined external. So the COFF symbol
tables answer the question directly, with no optimizer in the loop, no link, and
no build: ~1 second over 989 objects.

What it reports
---------------
The residue R = {external symbols referenced by some object of ours} minus
{external symbols defined by some object of ours}. Names in R are the ones no
part of the decomp defines.

R is NOT empty and is not supposed to be. Most of it is legitimate: XDK imports,
CRT entry points, and middleware the decomp calls but does not contain. The
inventory file records the whole of R, and the check is a ratchet against it:

    a name in R that is NOT in the inventory fails the build.

The inventory additionally *classifies* each name, purely for the reader, by
asking which TARGET object defines it. If the target defines a name in a unit we
also compile, the decomp is missing a body it is on the hook for -- that is the
`JoypadSendKeepAlive` shape, and those entries are a worklist. Classification
never affects the pass/fail decision, so a `symbols.txt` edit that moves a name
between target objects cannot turn the gate red.

Exit codes
----------
  0  current -- residue is a subset of the inventory
  1  NEW UNDEFINED SYMBOL -- a reference nothing defines was introduced
  2  UNREADABLE -- objects missing; run `ninja` first
  3  INVENTORY STALE (improvement) -- a body was implemented; re-run --write.
     Advisory: exits 3 but prints what to do. Implementing a missing body is the
     normal business of this project and must not break every other lane's build.
  5  SELFTEST FAILED -- the check could not be made to fail on purpose

Usage
-----
  python3 scripts/check_undefined_decomp_symbols.py --check
  python3 scripts/check_undefined_decomp_symbols.py --write
  python3 scripts/check_undefined_decomp_symbols.py --selftest
  python3 scripts/check_undefined_decomp_symbols.py --list-gaps
"""

import argparse
import glob
import os
import struct
import sys

IMAGE_SYM_CLASS_EXTERNAL = 2

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "373307D9"
BUILD = os.path.join(REPO, "build", VERSION)
OUR_OBJ_ROOTS = [os.path.join(BUILD, "src"), os.path.join(BUILD, "pch")]
TARGET_OBJ_ROOT = os.path.join(BUILD, "obj")
INVENTORY = os.path.join(REPO, "config", VERSION, "undefined_symbols.txt")


def coff_externals(path):
    """Return (defined, undefined) external symbol names from a COFF object."""
    with open(path, "rb") as fh:
        d = fh.read()
    if len(d) < 20:
        return set(), set()
    symptr, nsym = struct.unpack_from("<II", d, 8)
    if symptr == 0 or nsym == 0 or symptr + nsym * 18 > len(d):
        return set(), set()
    strtab = symptr + nsym * 18
    defined, undef = set(), set()
    i = 0
    while i < nsym:
        off = symptr + i * 18
        raw = d[off:off + 8]
        if raw[:4] == b"\x00\x00\x00\x00":
            so = struct.unpack_from("<I", d, off + 4)[0]
            end = d.index(b"\x00", strtab + so)
            name = d[strtab + so:end].decode("latin1")
        else:
            name = raw.rstrip(b"\x00").decode("latin1")
        value, secnum = struct.unpack_from("<Ih", d, off + 8)
        storage_class = d[off + 16]
        naux = d[off + 17]
        if storage_class == IMAGE_SYM_CLASS_EXTERNAL:
            if secnum == 0 and value == 0:
                undef.add(name)
            else:
                # Either a normal definition in a section, or -- when secnum is
                # 0 and value is non-zero -- a COMMON symbol, which IS a
                # definition (`value` bytes of bss). Filing COMMON as undefined
                # would put real, satisfied globals in the inventory and train
                # everyone to ignore it.
                defined.add(name)
        i += 1 + naux
    return defined, undef


def our_objects():
    objs = []
    for root in OUR_OBJ_ROOTS:
        objs += glob.glob(os.path.join(root, "**", "*.obj"), recursive=True)
    return sorted(objs)


def compute_residue():
    """Names referenced by our objects that no object of ours defines."""
    objs = our_objects()
    if not objs:
        return None, [], {}
    defined, referenced = set(), {}
    for path in objs:
        d, u = coff_externals(path)
        defined |= d
        rel = os.path.relpath(path, BUILD)
        for name in u:
            referenced.setdefault(name, []).append(rel)
    residue = {n: sorted(v) for n, v in referenced.items() if n not in defined}
    return residue, objs, referenced


def classify(residue):
    """Split the residue by whether the TARGET defines the name in a unit we compile.

    Reporting only -- never consulted by --check.
    """
    our_units = set()
    src_root = os.path.join(BUILD, "src")
    for path in glob.glob(os.path.join(src_root, "**", "*.obj"), recursive=True):
        our_units.add(os.path.relpath(path, src_root))

    target_def = {}
    for path in glob.glob(os.path.join(TARGET_OBJ_ROOT, "**", "*.obj"), recursive=True):
        rel = os.path.relpath(path, TARGET_OBJ_ROOT)
        if rel not in our_units:
            continue
        d, _ = coff_externals(path)
        for name in d:
            target_def.setdefault(name, []).append(rel)

    gaps, external = {}, {}
    for name, refs in residue.items():
        owners = sorted(target_def.get(name, []))
        (gaps if owners else external)[name] = (owners, refs)
    return gaps, external


HEADER = """\
# Undefined-symbol inventory -- GENERATED, do not hand-edit.
#
# Every external symbol some object under build/{v}/src (or the PCH) REFERENCES
# and no object of ours DEFINES. Regenerate after a full `ninja` with:
#
#     python3 scripts/check_undefined_decomp_symbols.py --write
#
# This file is EVIDENCE, not a threshold. Adding a name to it by hand to make
# `--check` pass is exactly the defect the check exists to find; the diff will
# show you doing it.
#
# GAP lines are symbols the ORIGINAL binary defines in a unit we also compile --
# a body the decomp still owes, and a worklist. EXT lines are XDK imports, CRT
# entry points and middleware the decomp calls but does not contain. The
# classification is for the reader; --check ignores it and compares names only.
"""


def write_inventory(residue):
    gaps, external = classify(residue)
    lines = [HEADER.format(v=VERSION)]
    lines.append("#\n# counts: %d total = %d GAP + %d EXT\n\n"
                 % (len(residue), len(gaps), len(external)))
    for name in sorted(gaps):
        owners = gaps[name][0]
        lines.append("GAP %s  # target defines it in %s\n" % (name, ", ".join(owners)))
    lines.append("\n")
    for name in sorted(external):
        lines.append("EXT %s\n" % name)
    with open(INVENTORY, "w") as fh:
        fh.write("".join(lines))
    return gaps, external


def read_inventory(path=INVENTORY):
    if not os.path.exists(path):
        return None
    names = set()
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(None, 2)
            if len(parts) >= 2 and parts[0] in ("GAP", "EXT"):
                names.add(parts[1])
    return names


def do_check(inventory_path=INVENTORY, quiet=False):
    residue, objs, _ = compute_residue()
    if residue is None:
        print("UNREADABLE: no objects under %s -- run `ninja` first." % BUILD,
              file=sys.stderr)
        return 2
    known = read_inventory(inventory_path)
    if known is None:
        print("UNREADABLE: no inventory at %s.\n"
              "  This gate cannot be disarmed by deleting its own inventory.\n"
              "  Regenerate with --write after a full `ninja`." % inventory_path,
              file=sys.stderr)
        return 2

    new = sorted(set(residue) - known)
    gone = sorted(known - set(residue))

    if new:
        print("NEW UNDEFINED SYMBOL -- referenced by the decomp, defined nowhere in it.",
              file=sys.stderr)
        print("", file=sys.stderr)
        for name in new:
            print("  %s" % name, file=sys.stderr)
            for ref in residue[name][:4]:
                print("      referenced from %s" % ref, file=sys.stderr)
        print("", file=sys.stderr)
        print("Each of these compiles and, on the PPC side, scores. It will fail the",
              file=sys.stderr)
        print("native link the moment the optimizer stops folding the call site away.",
              file=sys.stderr)
        print("Define the body (or, if the target's call site is genuinely dead,",
              file=sys.stderr)
        print("remove the call). Adding the name to the inventory is not a fix.",
              file=sys.stderr)
        return 1

    if gone:
        if not quiet:
            print("INVENTORY STALE (improvement): %d symbol(s) now defined." % len(gone))
            for name in gone[:20]:
                print("  + %s" % name)
            if len(gone) > 20:
                print("  ... and %d more" % (len(gone) - 20))
            print("Lock it in:  python3 scripts/check_undefined_decomp_symbols.py --write")
        return 3

    if not quiet:
        print("undefined-symbol inventory current: %d objects, %d undefined names, "
              "0 new." % (len(objs), len(residue)))
    return 0


def do_selftest():
    """Negative control: the check must FAIL on an inventory missing a real name.

    Runs against a temporary inventory rather than the real one, so it never
    writes to the tree. Exits 5 if the check cannot be made to fail -- including
    the vacuous case where there is nothing to remove.
    """
    import tempfile

    residue, _, _ = compute_residue()
    if residue is None:
        print("SELFTEST VACUOUS: no objects to read -- run `ninja` first.",
              file=sys.stderr)
        return 5
    if not residue:
        print("SELFTEST VACUOUS: residue is empty, so nothing could be removed "
              "from the inventory and a green result would prove nothing.",
              file=sys.stderr)
        return 5

    victim = sorted(residue)[0]
    tmp = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    try:
        for name in sorted(residue):
            if name != victim:
                tmp.write("EXT %s\n" % name)
        tmp.close()

        print("SELFTEST: dropping %r from a scratch inventory; the check must "
              "report it." % victim)
        rc = do_check(tmp.name, quiet=True)
        if rc != 1:
            print("SELFTEST FAILED: expected exit 1, got %d. This gate cannot "
                  "fail and is therefore worthless." % rc, file=sys.stderr)
            return 5

        # Positive control: with the full set present it must pass, or the
        # failure above proves nothing about *this* symbol.
        tmp2 = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
        for name in sorted(residue):
            tmp2.write("EXT %s\n" % name)
        tmp2.close()
        rc2 = do_check(tmp2.name, quiet=True)
        os.unlink(tmp2.name)
        if rc2 != 0:
            print("SELFTEST FAILED: the complete inventory did not pass "
                  "(exit %d) -- the failure above is not attributable to the "
                  "removed symbol." % rc2, file=sys.stderr)
            return 5

        # Missing-inventory control: deleting the file must not read as green.
        rc3 = do_check(os.path.join(tempfile.gettempdir(), "no-such-inventory.txt"),
                       quiet=True)
        if rc3 == 0:
            print("SELFTEST FAILED: a missing inventory passed. The gate would "
                  "be disarmable by `rm`.", file=sys.stderr)
            return 5

        print("SELFTEST PASSED: absent symbol -> 1, complete inventory -> 0, "
              "missing inventory -> %d." % rc3)
        return 0
    finally:
        if os.path.exists(tmp.name):
            os.unlink(tmp.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true", help="compare against the inventory")
    g.add_argument("--write", action="store_true", help="regenerate the inventory")
    g.add_argument("--selftest", action="store_true", help="prove the check can fail")
    g.add_argument("--list-gaps", action="store_true",
                   help="print only the bodies the decomp still owes")
    args = ap.parse_args()

    if args.selftest:
        return do_selftest()

    if args.write:
        residue, objs, _ = compute_residue()
        if residue is None:
            print("UNREADABLE: no objects under %s -- run `ninja` first." % BUILD,
                  file=sys.stderr)
            return 2
        gaps, external = write_inventory(residue)
        print("wrote %s: %d names (%d GAP, %d EXT) from %d objects."
              % (os.path.relpath(INVENTORY, REPO), len(residue), len(gaps),
                 len(external), len(objs)))
        return 0

    if args.list_gaps:
        residue, _, _ = compute_residue()
        if residue is None:
            print("UNREADABLE: run `ninja` first.", file=sys.stderr)
            return 2
        gaps, _ = classify(residue)
        for name in sorted(gaps):
            owners, refs = gaps[name]
            print("%s\n    target unit : %s\n    referenced  : %s"
                  % (name, ", ".join(owners), ", ".join(refs[:3])))
        print("\n%d bodies the decomp still owes." % len(gaps))
        return 0

    return do_check()


if __name__ == "__main__":
    sys.exit(main())
