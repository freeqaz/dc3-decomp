#!/usr/bin/env python3
r"""Oracle for the MSVC anonymous-namespace hash: compile a probe TU at an
arbitrary Windows path and read the `?A0x<hash>` cl emits.

Companion to `scripts/anon_ns_hash.py`, which models the same value in Python.
Use this to check the model, or to answer a question the model cannot -- the
model is only validated for the shape
`<computer name> + <path> + <one ordinal byte>`, which reproduces 8 of retail's
123 hash-bearing objects and no more (see that file's docstring).

`WIBO_PATH_MAP` gives exact control of the path string: the scratch directory
holding the probe file is mapped onto whatever Windows prefix you name, and cl
resolves it through wibo, so the string it hashes is yours to choose.  ~0.1 s
per compile.

Established with this harness, all of it measured rather than assumed:

  * the third `SigForPbCb` buffer is a one-byte ordinal, 0 without a PCH and
    1 under `/Yu`, not the `"\x00"` terminator the plan doc records;
  * the command line does not enter the hash (45 flag sets, one value);
  * a `namespace {}` in a HEADER is never hashed -- the call is not made, so
    no path map can reach a header-sourced hash;
  * `?A@@`, retail's hashless spelling, is not reproducible: 45 flags and 22
    source shapes all produced a hash, and file-scope `static` (with or
    without an enclosing anonymous namespace) produces a plain UNDECORATED
    name, never `?A@@`.

Do not point this at `build/<version>/`: it writes objects, and that tree is
what everything measures through.  It uses its own scratch directory.

    probe_anon_hash.py 'e:\lazer_build_gmc1\system\src\os'
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIBO = os.path.expanduser("~/code/milohax/wibo/build/release/wibo")
CL = f"{ROOT}/build/compilers/X360/16.00.11886.00/cl.exe"
SCRATCH = os.environ.get("ANON_PROBE_SCRATCH",
                         os.path.join(tempfile.gettempdir(), "anon_hash_probe"))

SRC = "namespace { int gProbe; }\nint Use() { return gProbe; }\n"


def symbols(path):
    data = open(path, "rb").read()
    psym, nsym = struct.unpack_from("<II", data, 8)
    strt = psym + nsym * 18
    out, i = [], 0
    while i < nsym:
        rec = data[psym + i * 18: psym + i * 18 + 18]
        if rec[:4] == b"\0\0\0\0":
            off, = struct.unpack_from("<I", rec, 4)
            end = data.index(b"\0", strt + off)
            name = data[strt + off:end].decode("latin1")
        else:
            name = rec[:8].rstrip(b"\0").decode("latin1")
        out.append(name)
        i += 1 + rec[17]
    return out


def hash_for(win_dir, filename="p.cpp", computer="9QVZU3", src=SRC,
             extra_flags=(), tc=False, keepdir=None):
    """win_dir: Windows-style directory, e.g. 'e:\\lazer_build_gmc1\\system\\src\\os'
    (no trailing sep). filename appended with a backslash."""
    os.makedirs(SCRATCH, exist_ok=True)
    d = tempfile.mkdtemp(dir=SCRATCH)
    try:
        p = os.path.join(d, filename)
        with open(p, "w") as f:
            f.write(src)
        wd = win_dir.replace("\\", "/")
        env_map = f"{wd}/={d}"
        cmd = [WIBO,
               f"WIBO_COMPUTER_NAME={computer}",
               "WIBO_FS_CACHE=1",
               f"WIBO_PATH_MAP={env_map}",
               CL, "/nologo", "/c", "/GR", "/O1", "/Oi", "/EHsc",
               "/TC" if tc else "/TP",
               *extra_flags,
               "/Foout.obj", filename]
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
        obj = os.path.join(d, "out.obj")
        if not os.path.exists(obj):
            return ("ERR", r.stdout.strip() + r.stderr.strip())
        syms = symbols(obj)
        anon = [s for s in syms if "?A0x" in s or "?A@@" in s]
        if not anon:
            return ("NONE", syms)
        s = anon[0]
        if "?A@@" in s:
            return ("BARE", s)
        i = s.index("?A0x")
        return (s[i + 4:i + 12], s)
    finally:
        if keepdir:
            shutil.move(d, keepdir)
        else:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    print(hash_for(sys.argv[1] if len(sys.argv) > 1 else "x:\\aaa"))
