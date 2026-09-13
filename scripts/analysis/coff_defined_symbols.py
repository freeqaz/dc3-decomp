"""List the symbols an Xbox 360 PPC COFF object DEFINES.

`llvm-nm` and `nm` both refuse IMAGE_FILE_MACHINE_POWERPCBE, so this is the only
thing in the tree that can answer "does our build emit a symbol for this name".

    coff_defined_symbols.py <obj>              every defined symbol (class 2/3)
    coff_defined_symbols.py <obj> --functions  only defined FUNCTIONS

⚠ Use --functions for anything that attributes an address or a relocation to a
symbol. A file-static function and its section symbol BOTH carry storage class 3,
and a section symbol's value is 0, so a "last symbol at or before this offset"
walk over the unfiltered list credits every static function's relocations to
`.text` instead — measured 2026-09-13 in a sibling repo, where a function read as
calling nothing while the target called two. The discriminator is the COFF TYPE
field: DT_FUNCTION is 0x20. On one dc3 object the unfiltered list is 1,238 names
of which 578 are sections and other non-functions.

The unfiltered list is still correct for set-membership questions whose keys are
mangled names (a `.text` entry cannot collide with `?Foo@Bar@@QAAXXZ`), which is
what missing_instantiations.py asks.
"""

import struct
import sys

DT_FUNCTION = 0x20


def syms(path):
    d = open(path, 'rb').read()
    mach, nsec, ts, psym, nsym, osz, ch = struct.unpack_from('<HHIIIHH', d, 0)
    out = []
    st = psym + nsym * 18
    i = 0
    while i < nsym:
        rec = d[psym + i * 18:psym + i * 18 + 18]
        z = struct.unpack_from('<I', rec, 0)[0]
        if z == 0:
            off = struct.unpack_from('<I', rec, 4)[0]
            e = d.index(b'\0', st + off)
            name = d[st + off:e].decode('latin1')
        else:
            name = rec[:8].rstrip(b'\0').decode('latin1')
        val, secnum, typ, sclass, naux = struct.unpack_from('<IhHBB', rec, 8)
        out.append((name, secnum, sclass, typ, val))
        i += 1 + naux
    return out


def defined(path, functions_only=False):
    for name, sec, sc, typ, val in syms(path):
        if sc not in (2, 3) or sec <= 0:
            continue
        if functions_only and typ != DT_FUNCTION:
            continue
        yield name, sec, sc, typ, val


if __name__ == '__main__':
    fns = '--functions' in sys.argv[1:]
    path = [a for a in sys.argv[1:] if not a.startswith('--')][0]
    for name, sec, sc, typ, val in defined(path, fns):
        print(name)
