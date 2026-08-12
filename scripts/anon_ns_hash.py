#!/usr/bin/env python3
"""MSVC's `?A0x<8 hex>` anonymous-namespace hash, as a function you can run.

The model
---------
Three chained raw CRC-32s (reflected poly 0xEDB88320, initial state
0xFFFFFFFF, **no final inversion**), which is `SigForPbCb` in `mspdb80.dll`
called three times per TU:

    h1 = crc(COMPUTERNAME,        0xFFFFFFFF)
    h2 = crc(canonical_path,      h1)
    hash = crc(one ordinal byte,  h2)

`canonical_path` is the full Windows path with drive letter and backslashes,
**directory case preserved and the final component lowercased** (cl runs
`_splitpath_s` / `GetShortPathNameW` on the directory / `_makepath_s`, then
interns the result, and the intern lowercases only the filename).  The command
line does not enter it: 45 different flag sets give the same hash.

**The third buffer is a one-byte ORDINAL and it is not always zero.**
`docs/plans/ANON_NAMESPACE_HASH_FIX.md` records it as a literal `"\\x00"`
terminator; that is right only for the no-PCH case.  Measured under
`WIBO_SIGFORPBCB_LOG`, and independently brute-forced back out of the CRC
state: it is **0 without a PCH and 1 with `/Yu`**, for any PCH regardless of
how many anonymous namespaces the PCH itself contains, so it behaves like an
index into the compiler's file table with the PCH occupying slot 0 — not a
namespace counter (three anonymous namespaces in one non-PCH TU all share
ordinal 0).  Any search over candidate paths must sweep this byte.

What it explains
----------------
`?A0x49b544a7` is retail's `system/os/HolmesClient.obj`, and

    predict(r'e:\\lazer_build_gmc1\\system\\src\\os', 'HolmesClient.cpp', 0)
        -> 49b544a7      retail
    predict(same, same, 1)
        -> 3eb27431      what OUR build emitted

Same computer name, same path, **different only in the ordinal** — because we
compile that TU with `/Yu` and retail did not.  Verified end to end: compiling
`HolmesClient.cpp` with `/FI"decomp_pch.h"` but WITHOUT `/Yu` makes cl emit
`?A0x49b544a7` directly.  `system/gesture/DrawUtl.cpp` is the same story
(`da23fae1` at ordinal 1, retail's `ad24ca77` at ordinal 0).  `9QVZU3` is not
Harmonix's computer name; it is a meet-in-the-middle CRC preimage that
collides to the same h1 (0x9f6add5d), which is all the chain needs.

What it does NOT explain, and the limits of the model
-----------------------------------------------------
Sweeping all 256 ordinals against every retail object's own `.cpp` path
reproduces **only 8 of 123** objects' hashes.  `system/os/Joypad.obj`'s
`ca10770b` carries 25 unmistakably Joypad.cpp-local symbols and is predicted by
no ordinal, while `HolmesClient.cpp` in the same directory is predicted exactly
— so for most TUs the hashed string is not
`<name> + e:\\lazer_build_gmc1\\...\\<file>.cpp + <1 byte>`.  Either the third
buffer is not always one byte, or there are more calls in the chain.  Unknown;
do not read a failed `predict()` as evidence about a path.

Separately, and independently established: **a `namespace {}` in a HEADER is
never hashed at all under wibo.**  The `SigForPbCb` log shows the call is
simply not made for the header's path, so no path map and no computer name can
produce the header-sourced hashes (`c9fefd64` = `AddToStrings` in 55 objects,
and its four siblings).  Those are the ones that make a retail object carry
several hashes, and they are why `scripts/obj_anon_ns_patcher.py` cannot be
retired even if every TU-local hash were made to come out right.

    anon_ns_hash.py --self-test
    anon_ns_hash.py 'e:\\lazer_build_gmc1\\system\\src\\os' HolmesClient.cpp
"""
import argparse
import sys

POLY = 0xEDB88320
TABLE = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = (_c >> 1) ^ (POLY if _c & 1 else 0)
    TABLE.append(_c)

#: Inverse of the table's top byte, for stepping the CRC backwards.
_TOPBYTE = {TABLE[i] >> 24: i for i in range(256)}
assert len(_TOPBYTE) == 256


def crc_update(state: int, data: bytes) -> int:
    for b in data:
        state = (state >> 8) ^ TABLE[(state ^ b) & 0xFF]
    return state


def crc_raw(data: bytes) -> int:
    """CRC-32 with no final inversion -- the raw register state."""
    return crc_update(0xFFFFFFFF, data)


def _step_back(u: int) -> int:
    """Inverse of one byte of `crc_update`, for a known-zero input byte."""
    i = _TOPBYTE[u >> 24]
    return (((u ^ TABLE[i]) << 8) | i) & 0xFFFFFFFF


def state_after_name(target: int, suffix: bytes) -> int:
    """Recover h1 from a known hash and the bytes hashed after the name.

    Lets you fingerprint an unknown build machine from a single known hash:
    `state_after_name(0x49b544a7, path + bytes([0]))` is the CRC state any
    computer name would have to reach.
    """
    u = target ^ crc_update(0, suffix)
    for _ in range(len(suffix)):
        u = _step_back(u)
    return u & 0xFFFFFFFF


def predict(windir: str, filename: str, ordinal: int = 0,
            computer: str = "9QVZU3") -> int:
    """The `?A0x` value cl emits for an anonymous namespace in this file.

    `windir` has no trailing separator; its case is preserved, `filename` is
    lowercased, exactly as cl interns them.
    """
    payload = (computer + windir + "\\" + filename.lower()).encode("latin1")
    return crc_raw(payload + bytes([ordinal]))


#: (windir, filename, ordinal, expected) -- retail objects this model
#: reproduces, plus the ordinal-1 siblings our own PCH build emits for them.
KNOWN = [
    (r'e:\lazer_build_gmc1\system\src\os', 'HolmesClient.cpp', 0, 0x49b544a7),
    (r'e:\lazer_build_gmc1\system\src\os', 'HolmesClient.cpp', 1, 0x3eb27431),
    (r'e:\lazer_build_gmc1\system\src\gesture', 'DrawUtl.cpp', 0, 0xad24ca77),
    (r'e:\lazer_build_gmc1\system\src\gesture', 'DrawUtl.cpp', 1, 0xda23fae1),
    (r'e:\lazer_build_gmc1\system\src\os', 'DateTime.cpp', 1, 0x233d738b),
    (r'e:\lazer_build_gmc1\lazer\src\meta_ham', 'MetagameRank.cpp', 0,
     0x44bf5786),
]


def self_test() -> int:
    bad = 0
    for windir, name, ordinal, want in KNOWN:
        got = predict(windir, name, ordinal)
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {name:20s} ord={ordinal} "
              f"-> {got:08x} (want {want:08x})")
    # The inverse must land back on h1 for the computer name we use.
    h1 = crc_raw(b"9QVZU3")
    back = state_after_name(
        0x49b544a7,
        br'e:\lazer_build_gmc1\system\src\os\holmesclient.cpp' + bytes([0]))
    ok = h1 == back == 0x9f6add5d
    bad += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} inverse -> {back:08x} "
          f"(h1 {h1:08x}, want 9f6add5d)")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("windir", nargs="?",
                    help=r"Windows directory, e.g. e:\lazer_build_gmc1\...\os")
    ap.add_argument("filename", nargs="?", help="e.g. HolmesClient.cpp")
    ap.add_argument("--ordinal", type=int, default=None,
                    help="one-byte file-table ordinal (default: print 0-3)")
    ap.add_argument("--computer", default="9QVZU3")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        bad = self_test()
        print("FAILURES:" if bad else "all ok", bad or "")
        return 1 if bad else 0
    if not args.windir or not args.filename:
        ap.error("give windir and filename, or --self-test")
    ords = [args.ordinal] if args.ordinal is not None else range(4)
    for o in ords:
        print(f"  ordinal {o}: "
              f"{predict(args.windir, args.filename, o, args.computer):08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
