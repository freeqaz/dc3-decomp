#!/usr/bin/env python3
"""Rename function-local statics to the names retail's symbol table gives them.

MSVC parks a function-local static as ``?<var>@?<scope>??<enclosing fn>...``, so
once objdiff runs at ``functionRelocDiffs=name_check`` a disagreement on that
symbol tells us, in retail's own words, what the variable was called.  That is
ground truth of exactly the kind the ``??_C@`` string oracle gives for literals
-- there is nothing to guess, only to apply.

The rename is confined to the body of the enclosing function and to identifiers
declared ``static`` there, so it cannot touch a member, a parameter, or another
function's local of the same name.

    fix_local_static_names.py <sites.jsonl> [--apply] [--repo .]

``sites.jsonl`` is the per-charge dump written by the name_check triage
(``lane == "local_static_rename"``).  Without ``--apply`` this only reports.
A rename does NOT always complete the function: when the scope ordinal differs
as well, the enclosing function has a structural difference that a name cannot
fix, and the tool says so rather than implying the charge is cleared.
"""
import argparse
import json
import re
import sys
from collections import OrderedDict
from pathlib import Path

_ORD = r"(?:[0-9]|[A-P]+@)"
LOCAL_HELPER = re.compile(
    rf"^\?\?__[EF](?P<var>[^?@][^@]*)@\?(?P<ord>{_ORD})\?\?(?P<fn>.+)$")
LOCAL_STATIC = re.compile(
    rf"^\?(?P<var>[^?@][^@]*)@\?(?P<ord>{_ORD})\?\?(?P<fn>.+)$")


def parse_local(name):
    for pat in (LOCAL_HELPER, LOCAL_STATIC):
        m = pat.match(name or "")
        if m:
            return m.group("var"), m.group("ord"), m.group("fn")
    return None


def qualified(fnmangled):
    """`Init@UIManager@@UAAXXZ` -> ('UIManager', 'Init').  Free fn -> (None, n)."""
    parts = fnmangled.split("@@", 1)[0].split("@")
    if len(parts) >= 2 and parts[1]:
        return parts[1], parts[0]
    return None, parts[0]


# A local static is very often a `Symbol`/`Message` whose CONSTRUCTOR ARGUMENT is
# a string spelling the same word -- `static Message special_finished("special_
# finished", 0, 0)`.  A word-boundary rename walks straight into that literal and
# renames the MESSAGE, which is a behavioural change the `none` ruler cannot see
# (a string COMDAT is data, and `matched_code` at `none` ignores its name).  So
# literals and comments are masked out before the rename and restored after.
_MASKABLE = re.compile(r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'"
                       r"|//[^\n]*" r"|/\*.*?\*/", re.S)


def mask_literals(body):
    saved = []

    def stash(m):
        saved.append(m.group(0))
        return f"\x00{len(saved) - 1}\x00"

    return _MASKABLE.sub(stash, body), saved


def unmask(body, saved):
    return re.sub(r"\x00(\d+)\x00", lambda m: saved[int(m.group(1))], body)


def body_span(text, cls, fn):
    """Byte span of the definition body of `cls::fn` (or free `fn`)."""
    pat = (rf"\b{re.escape(cls)}\s*::\s*{re.escape(fn)}\s*\("
           if cls else rf"(?<![\w:]){re.escape(fn)}\s*\(")
    for m in re.finditer(pat, text):
        open_brace = text.find("{", m.end())
        semi = text.find(";", m.end())
        if open_brace < 0 or (0 <= semi < open_brace):
            continue                      # a declaration, or a call
        depth, i = 0, open_brace
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    return open_brace, i + 1
            i += 1
    return None


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
        if r["lane"] != "local_static_rename":
            continue
        pt, pb = parse_local(r["target"]), parse_local(r["base"])
        if not (pt and pb):
            continue
        jobs.setdefault((r["unit"], pt[2], pb[0], pt[0]), pt[1] == pb[1])

    done = skipped = 0
    edits = {}
    for (unit, fnmangled, ours, retail), pure in jobs.items():
        src = src_of.get(unit)
        note = "clears the charge" if pure else "scope ordinal ALSO differs"
        if not src or not (repo / src).exists():
            print(f"  SKIP  {unit}: no source path")
            skipped += 1
            continue
        path = repo / src
        text = edits.get(path, path.read_text())
        cls, fn = qualified(fnmangled)
        span = body_span(text, cls, fn)
        if span is None:
            print(f"  SKIP  {unit}: cannot locate body of "
                  f"{cls + '::' if cls else ''}{fn}")
            skipped += 1
            continue
        lo, hi = span
        body, saved = mask_literals(text[lo:hi])
        decl = re.search(rf"\bstatic\b[^;{{}}]*?\b{re.escape(ours)}\b", body)
        if not decl:
            print(f"  SKIP  {unit}: `{ours}` is not declared static in "
                  f"{cls + '::' if cls else ''}{fn}")
            skipped += 1
            continue
        # A rename that lands on a name already live in the same body would
        # silently merge two variables.  Refuse rather than "fix" the symbol.
        if re.search(rf"\b{re.escape(retail)}\b", body):
            print(f"  SKIP  {unit}: `{retail}` is already used inside "
                  f"{cls + '::' if cls else ''}{fn} -- rename would collide")
            skipped += 1
            continue
        new_body, n = re.subn(rf"\b{re.escape(ours)}\b", retail, body)
        print(f"  {unit}: {ours} -> {retail}  ({n} refs in "
              f"{cls + '::' if cls else ''}{fn}; {note})")
        edits[path] = text[:lo] + unmask(new_body, saved) + text[hi:]
        done += 1

    if args.apply:
        for path, text in edits.items():
            path.write_text(text)
    print(f"\n{done} rename(s) in {len(edits)} file(s), {skipped} skipped"
          f"{'' if args.apply else '  [dry run -- pass --apply]'}")
    return 0 if not skipped else 1


if __name__ == "__main__":
    sys.exit(main())
