#!/usr/bin/env python3
"""Correct file-scope linkage where retail's mangling disagrees with ours.

MSVC leaves an internal-linkage (``static``) file-scope variable UNMANGLED and
decorates an external one, so at ``functionRelocDiffs=name_check`` the two
spellings of the same variable are a direct statement about its storage class:

    retail ?gConsole@@3PAVRndConsole@@A   ours gConsole      -> retail: EXTERNAL
    retail gFile                          ours ?gFile@@3V... -> retail: static

That is the whole inference.  This applies it to the definition line in the
unit's own source, and refuses anything it cannot see unambiguously -- a
declaration inside ``extern "C"``, a name defined more than once in the file, or
a name it cannot find at file scope.

    fix_linkage.py <sites.jsonl> [--apply] [--repo .]
"""
import argparse
import json
import re
import sys
from collections import OrderedDict
from pathlib import Path


def undecorated(name):
    """The identifier a mangled MSVC data/function symbol refers to."""
    if name.startswith("?"):
        return name.lstrip("?").split("@", 1)[0]
    return name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sites")
    ap.add_argument("--repo", default=".")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()
    repo = Path(args.repo)

    src_of = {u["name"]: u.get("metadata", {}).get("source_path")
              for u in json.loads((repo / "objdiff.json").read_text())["units"]}

    jobs = OrderedDict()
    for line in open(args.sites):
        r = json.loads(line)
        if r["lane"] != "mangled_vs_plain_linkage":
            continue
        t, b = r["target"], r["base"]
        ident = undecorated(t)
        if undecorated(b) != ident:
            continue
        # retail mangled + ours plain  -> retail had EXTERNAL linkage
        # retail plain  + ours mangled -> retail had INTERNAL linkage
        jobs.setdefault((r["unit"], ident),
                        "external" if t.startswith("?") else "static")

    done = skipped = 0
    edits = {}
    for (unit, ident), want in jobs.items():
        src = src_of.get(unit)
        if not src or not (repo / src).exists():
            print(f"  SKIP  {unit}: no source path")
            skipped += 1
            continue
        path = repo / src
        text = edits.get(path, path.read_text())
        # A file-scope definition: at column 0, optional `static`, a type, the
        # name, then `=`, `;`, `[` or `(`.  Anything indented is inside a scope.
        pat = re.compile(
            rf"^(?P<static>static\s+)?(?P<type>[A-Za-z_][^;=\n]*?[\s*&])"
            rf"(?P<name>{re.escape(ident)})\s*(?P<tail>[=;\[])",
            re.M)
        hits = list(pat.finditer(text))
        if len(hits) != 1:
            print(f"  SKIP  {unit}: {ident} -- {len(hits)} file-scope "
                  f"definitions found, need exactly 1")
            skipped += 1
            continue
        m = hits[0]
        if 'extern "C"' in text[max(0, m.start() - 400):m.start()]:
            print(f"  SKIP  {unit}: {ident} sits under an extern \"C\" block")
            skipped += 1
            continue
        if want == "external" and not m.group("static"):
            print(f"  ok    {unit}: {ident} is already external")
            continue
        if want == "static" and m.group("static"):
            print(f"  ok    {unit}: {ident} is already static")
            continue
        if want == "external":
            new = text[:m.start()] + text[m.start("type"):]
        else:
            new = text[:m.start()] + "static " + text[m.start():]
        print(f"  {unit}: {ident} -> {want} linkage")
        edits[path] = new
        done += 1

    if args.apply:
        for path, text in edits.items():
            path.write_text(text)
    print(f"\n{done} change(s) in {len(edits)} file(s), {skipped} skipped"
          f"{'' if args.apply else '  [dry run -- pass --apply]'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
