#!/usr/bin/env python3
"""Write the ccache "external checksum" for a freshly built clang PCH.

Called by scripts/native_ccache_launcher.sh right after a successful
`-Xclang -emit-pch` compile; see that script and docs/native/BUILD_CACHE.md.

ccache (pch_external_checksum=true) hashes <pch>.sum in place of the .pch
itself when compiling a TU that uses the PCH. The .pch bytes embed absolute
header paths, so they differ in every worktree; this digest is made of what
the PCH was BUILT FROM, with the tree root replaced by a placeholder:

  * every compiler argument (the tree root replaced), and
  * for every file in the PCH's depfile, in order: its path (tree root
    replaced) and the sha256 of its content.

Same headers + same flags => same digest in any worktree. Any header byte or
flag that differs => a different digest, so a stale or foreign cache entry can
never be selected. Paths outside the tree (the shared engine checkout, deps,
system headers) are kept absolute: they are the same files for every tree.
"""

import argparse
import hashlib
import os
import sys


def parse_depfile(text):
    """Makefile-syntax depfile -> list of prerequisite paths, in order."""
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    _, sep, rest = text.partition(": ")
    if not sep:
        _, _, rest = text.partition(":")
    deps, cur, i = [], [], 0
    while i < len(rest):
        c = rest[i]
        if c == "\\" and i + 1 < len(rest) and rest[i + 1] in " #":
            cur.append(rest[i + 1])
            i += 2
            continue
        if c.isspace():
            if cur:
                deps.append("".join(cur))
                cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        deps.append("".join(cur))
    return deps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True)
    ap.add_argument("--depfile", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("args", nargs=argparse.REMAINDER)
    ns = ap.parse_args()
    args = ns.args[1:] if ns.args[:1] == ["--"] else ns.args

    tree = os.path.realpath(ns.tree)
    cwd = os.getcwd()

    def norm(s):
        return s.replace(tree, "@TREE@")

    h = hashlib.sha256()
    h.update(b"native_pch_sum v1\n")
    h.update(("cwd " + norm(os.path.realpath(cwd)) + "\n").encode())
    for a in args:
        h.update(("arg " + norm(a) + "\n").encode())

    with open(ns.depfile, encoding="utf-8", errors="surrogateescape") as fh:
        deps = parse_depfile(fh.read())
    if not deps:
        print("native_pch_sum.py: empty depfile %s" % ns.depfile, file=sys.stderr)
        return 1
    for d in deps:
        p = d if os.path.isabs(d) else os.path.join(cwd, d)
        p = os.path.realpath(p)
        fh_h = hashlib.sha256()
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                fh_h.update(chunk)
        h.update(("dep " + norm(p) + " " + fh_h.hexdigest() + "\n").encode())

    tmp = ns.out + ".tmp.%d" % os.getpid()
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(h.hexdigest() + "\n")
    os.replace(tmp, ns.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
