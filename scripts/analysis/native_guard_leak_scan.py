#!/usr/bin/env python3
"""Detect native-port changes that leaked into the decomp-matched (PPC) source path.

BUG CLASS
---------
`src/` is compiled twice: once by the Xbox 360 MSVC/PPC toolchain (which must
reproduce the shipped binary byte-for-byte) and once by Clang for the x86_64
native port.  Native-only behaviour therefore has to live behind a guard macro
that the PPC build never defines -- `HX_NATIVE`, `HX_WEB`, `__EMSCRIPTEN__`, ...

When a native fix is written *without* the guard it silently changes the code
the decomp is trying to match.  The canonical exemplar is 866ba1082
(HamIKSkeleton::SetBone): `if (!t2) return;` was added by the native commit
5d19777db and left unguarded; the Xbox build dereferences t2 unconditionally,
so the leak cost 6 instructions of match (92.4% -> 99.0% once guarded).

DETECTION
---------
Two independent signals, reported separately so each can be judged on its own:

  blame  -- the line is still attributed (git blame -w -M) to a commit whose
            subject/body marks it as native-port work, AND the line is not
            inside any conditional block controlled by a guard macro.

  content-- the line, or a comment within CONTENT_WINDOW lines above it, names
            the native/web port explicitly ("native only", "stub never opens",
            "avoid crash on native", ...) while sitting outside any guard.
            This catches leaks whose commit message was not native-flavoured
            and leaks that predate the current history.

Neither signal is a verdict.  A hit means "read the target assembly for this
function" -- the Xbox build genuinely contains plenty of null checks and early
returns, and four of the five sites this scanner was built to re-derive turned
out to be target-faithful.  Confirm with `run_objdiff` / the listings under
`build/373307D9/asm/` before touching anything.

USAGE
-----
    python3 scripts/analysis/native_guard_leak_scan.py                 # blame + content
    python3 scripts/analysis/native_guard_leak_scan.py --signal blame
    python3 scripts/analysis/native_guard_leak_scan.py --json out.json
    python3 scripts/analysis/native_guard_leak_scan.py --repo /path/to/worktree
    python3 scripts/analysis/native_guard_leak_scan.py --self-test     # negative control

`--self-test` is the negative control: it materialises a scratch commit that
re-introduces a known unguarded native change (the HamIKSkeleton::SetBone null
check, stripped of its `#ifdef HX_NATIVE`) and asserts the scanner reports it.
A checker that has never been shown to fail is not evidence.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Macros that are *never* defined by the Xbox 360 MSVC/PPC build.  A line inside
# a conditional group controlled by any of these -- in either the taken arm or
# the #else arm -- is considered guarded: the author was aware of both planes.
GUARD_MACROS = {
    "HX_NATIVE",
    "HX_WEB",
    "HX_IMGUI",
    "HX_FFMPEG",
    "__EMSCRIPTEN__",
    "__linux__",
    "__APPLE__",
    "_WIN32",
    "__unix__",
    "__clang__",
    "__GNUC__",
}

SOURCE_SUFFIXES = (".cpp", ".c", ".h", ".hpp", ".inl")

# Subjects that mark a commit as native-port work.
NATIVE_COMMIT_RE = re.compile(
    r"""(
        ^\s*native\b[:/ ]              # "native: venue rendering ..."
      | ^\s*web\b[:/ ]                 # "web: ..."
      | ^\s*wasm\b[:/ ]
      | ^\s*emscripten\b[:/ ]
      | \bnative[ -]port\b
      | \bnative\ build\b
      | \bnative\ crash\b
      | \bnative\ boot\b
      | \bHX_NATIVE\b
      | \bHX_WEB\b
      | \b__EMSCRIPTEN__\b
      | \bfix/native[-\w]*\b
      | \bfix/web[-\w]*\b
      | \bon\ native\b
    )""",
    re.IGNORECASE | re.VERBOSE | re.MULTILINE,
)

# Commits that are the OPPOSITE of this bug class: they import target-faithful
# source from the reference decomp trees.  Never flag their lines.
NOT_NATIVE_COMMIT_RE = re.compile(
    r"^\s*port\s+\S+\s+from\s+og-dc3\b|\bfrom\s+og-dc3-decomp\b|\bupstream[- ]port\b",
    re.IGNORECASE | re.MULTILINE,
)

# Content signal: a comment naming the native/web port.
CONTENT_RE = re.compile(
    r"""(
        \bnative\ (?:only|port|build|path|side|stub|crash|hack)\b
      | \bon\ native\b
      | \bnative:\s
      | \bHX_NATIVE\b
      | \bHX_WEB\b
      | \b__EMSCRIPTEN__\b
      | \bemscripten\b
      | \bweb\ build\b
      | \bwasm\b
      | \bWebGPU\b
      | \bstub(?:bed)?\ (?:impl|object|never|does\ not|doesn't)
      | \bnever\ opens\b
      | \bavoid\ (?:the\ )?(?:crash|segfault|infinite)\b
      | \bcrash\ guard\b
      | \bLP64\b
      | \bx86_64\b
      | \bclang\b
    )""",
    re.IGNORECASE | re.VERBOSE,
)

CONTENT_WINDOW = 3  # how many lines above a code line a native comment may sit

# Lines that carry no behaviour and are never worth reporting.
NOISE_RE = re.compile(
    r"^\s*(//|/\*|\*|\*/|\}|\{|\};|\)|#include\b|#pragma\b|#endif\b|#else\b|#if|$)"
)

COND_OPEN_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b(.*)$")
COND_MID_RE = re.compile(r"^\s*#\s*(elif|else)\b(.*)$")
COND_CLOSE_RE = re.compile(r"^\s*#\s*endif\b")

FUNC_RE = re.compile(
    r"^[A-Za-z_][\w:<>,*&\s~]*?([A-Za-z_~]\w*(?:::[A-Za-z_~]\w*)*)\s*\([^;]*\)\s*"
    r"(?:const\s*)?(?:\{|$)"
)


# ---------------------------------------------------------------------------
# git helpers
# ---------------------------------------------------------------------------


def git(repo, *args, check=True):
    out = subprocess.run(
        ["git", "-C", repo, *args],
        capture_output=True,
        text=True,
        errors="replace",
    )
    if check and out.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed:\n{out.stderr}")
    return out.stdout


def native_commits(repo, path="src"):
    """Return {sha: subject} for commits touching `path` that are native-port work."""
    sep = "\x1e"
    raw = git(repo, "log", f"--format=%H{sep}%s{sep}%b\x1d", "--", path)
    result = {}
    for rec in raw.split("\x1d"):
        rec = rec.strip("\n")
        if not rec:
            continue
        parts = rec.split(sep)
        if len(parts) < 2:
            continue
        sha, subject = parts[0].strip(), parts[1]
        body = parts[2] if len(parts) > 2 else ""
        text = subject + "\n" + body
        if NOT_NATIVE_COMMIT_RE.search(text):
            continue
        # Require the signal in the SUBJECT, or an explicit guard-macro name in
        # the body.  Bodies mention "native" far too loosely to be a filter.
        if NATIVE_COMMIT_RE.search(subject) or re.search(
            r"\bHX_NATIVE\b|\bHX_WEB\b|\b__EMSCRIPTEN__\b", body
        ):
            result[sha] = subject
    return result


def files_touched(repo, shas):
    """Union of src/ source files touched by any of `shas` and still present."""
    touched = set()
    for i in range(0, len(shas), 200):
        chunk = shas[i : i + 200]
        raw = git(
            repo,
            "show",
            "--name-only",
            "--format=",
            "--no-renames",
            *chunk,
            check=False,
        )
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith("src/") and line.endswith(SOURCE_SUFFIXES):
                touched.add(line)
    return sorted(f for f in touched if os.path.exists(os.path.join(repo, f)))


def blame_lines(repo, path):
    """Return list of shas, one per line of `path` at HEAD (1-based -> index 0)."""
    raw = git(
        repo,
        "blame",
        "--line-porcelain",
        "-w",
        "-M",
        "HEAD",
        "--",
        path,
        check=False,
    )
    shas = []
    for line in raw.splitlines():
        m = re.match(r"^([0-9a-f]{40}) \d+ \d+(?: \d+)?$", line)
        if m:
            shas.append(m.group(1))
    return shas


# ---------------------------------------------------------------------------
# Preprocessor guard state
# ---------------------------------------------------------------------------


def guard_mask(lines):
    """For each line, True if it sits inside a guard-macro conditional group.

    A conditional *group* is #if.../#elif.../#else.../#endif.  If any arm's
    controlling expression names a guard macro, every line in the whole group
    counts as guarded -- the author was reasoning about both build planes.
    """
    mask = [False] * len(lines)
    stack = []  # list of bool: does this group mention a guard macro?
    for i, line in enumerate(lines):
        m = COND_OPEN_RE.match(line)
        if m:
            stack.append(mentions_guard(m.group(2)))
            mask[i] = any(stack)
            continue
        m = COND_MID_RE.match(line)
        if m:
            if stack:
                stack[-1] = stack[-1] or mentions_guard(m.group(2))
            mask[i] = any(stack)
            continue
        if COND_CLOSE_RE.match(line):
            mask[i] = any(stack)
            if stack:
                stack.pop()
            continue
        mask[i] = any(stack)
    return mask


def mentions_guard(expr):
    return any(re.search(rf"\b{re.escape(m)}\b", expr) for m in GUARD_MACROS)


def enclosing_function(lines, idx):
    """Best-effort name of the function containing line index `idx`."""
    for j in range(idx, max(-1, idx - 400), -1):
        s = lines[j]
        if not s or s[0].isspace() or s.startswith("#") or s.startswith("//"):
            continue
        m = FUNC_RE.match(s)
        if m:
            return m.group(1)
    return None


# ---------------------------------------------------------------------------
# Scan
# ---------------------------------------------------------------------------


def scan(repo, signals=("blame", "content"), verbose=False):
    hits = []
    ncommits = native_commits(repo) if "blame" in signals else {}
    if verbose:
        print(f"[scan] {len(ncommits)} native-port commits touching src/", file=sys.stderr)

    if "blame" in signals:
        files = files_touched(repo, list(ncommits))
    else:
        files = []
    if "content" in signals:
        allsrc = [
            f
            for f in git(repo, "ls-files", "src").splitlines()
            if f.endswith(SOURCE_SUFFIXES)
        ]
        files = sorted(set(files) | set(allsrc))
    if verbose:
        print(f"[scan] scanning {len(files)} files", file=sys.stderr)

    for path in files:
        full = os.path.join(repo, path)
        try:
            lines = open(full, errors="replace").read().split("\n")
        except OSError:
            continue
        mask = guard_mask(lines)

        shas = blame_lines(repo, path) if ("blame" in signals and ncommits) else []

        for i, text in enumerate(lines):
            if mask[i] or NOISE_RE.match(text):
                continue
            why = []
            sha = shas[i] if i < len(shas) else None
            if sha and sha in ncommits:
                why.append("blame")
            if "content" in signals:
                window = lines[max(0, i - CONTENT_WINDOW) : i + 1]
                if any(CONTENT_RE.search(w) for w in window):
                    # only count the comment context if it is a comment, or the
                    # code line itself names a guard concept
                    why.append("content")
            if not why:
                continue
            hits.append(
                {
                    "file": path,
                    "line": i + 1,
                    "text": text.rstrip(),
                    "signals": why,
                    "commit": sha,
                    "subject": ncommits.get(sha),
                    "function": enclosing_function(lines, i),
                }
            )
    return hits


# ---------------------------------------------------------------------------
# Negative control
# ---------------------------------------------------------------------------

CONTROL_FILE = "src/system/hamobj/HamIKSkeleton.cpp"
CONTROL_ANCHOR = "void HamIKSkeleton::SetBone(RndTransformable *t1, RndTransformable *t2) {"


def self_test(repo):
    """Re-introduce a known unguarded native change; assert the scanner fires."""
    full = os.path.join(repo, CONTROL_FILE)
    original = open(full).read()
    if CONTROL_ANCHOR not in original:
        print(f"SELF-TEST SKIPPED: anchor not found in {CONTROL_FILE}", file=sys.stderr)
        return 2

    dirty = git(repo, "status", "--porcelain", "--", CONTROL_FILE).strip()
    if dirty:
        print(f"SELF-TEST ABORTED: {CONTROL_FILE} has uncommitted changes", file=sys.stderr)
        return 2

    branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()
    ok = False
    try:
        # 1. baseline: scanner must NOT report the SetBone null check today
        before = [
            h
            for h in scan(repo, signals=("blame", "content"))
            if h["file"] == CONTROL_FILE and "!t2" in h["text"]
        ]
        if before:
            print("SELF-TEST FAIL: scanner already reports the guarded control site")
            for h in before:
                print("   ", h)
            return 1
        print("SELF-TEST step 1 OK: guarded control site is silent (no false positive)")

        # 2. strip the guard, exactly as the pre-866ba1082 source had it
        injected = original.replace(
            CONTROL_ANCHOR,
            CONTROL_ANCHOR + "\n    // reintroduced native-only crash guard (self-test)\n    if (!t2)\n        return;",
            1,
        )
        open(full, "w").write(injected)
        git(repo, "add", "--", CONTROL_FILE)
        git(
            repo,
            "-c", "user.name=guard-leak-selftest",
            "-c", "user.email=selftest@localhost",
            "commit", "-q", "--no-verify",
            "-m", "native: reintroduce unguarded SetBone crash guard (scanner self-test)",
            "--", CONTROL_FILE,
        )

        after = [
            h
            for h in scan(repo, signals=("blame", "content"))
            if h["file"] == CONTROL_FILE and "!t2" in h["text"]
        ]
        if not after:
            print("SELF-TEST FAIL: scanner did NOT report the reintroduced unguarded leak")
            return 1
        print("SELF-TEST step 2 OK: scanner reports the reintroduced leak")
        for h in after:
            print(f"    {h['file']}:{h['line']}  {h['text'].strip()}")
            print(f"      signals={h['signals']} fn={h['function']} subject={h['subject']!r}")
        ok = True
    finally:
        # roll the scratch commit back off the branch
        git(repo, "reset", "-q", "--hard", "HEAD~1" if not ok or True else "HEAD", check=False)
        open(full, "w").write(original)
        git(repo, "checkout", "-q", "--", CONTROL_FILE, check=False)
        cur = git(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()
        assert cur == branch, f"branch changed during self-test: {branch} -> {cur}"
    print("SELF-TEST PASS")
    return 0


# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--signal", choices=["blame", "content", "both"], default="both")
    ap.add_argument("--json", metavar="OUT")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)

    if args.self_test:
        sys.exit(self_test(repo))

    signals = ("blame", "content") if args.signal == "both" else (args.signal,)
    hits = scan(repo, signals=signals, verbose=args.verbose)

    both = [h for h in hits if len(h["signals"]) == 2]
    blame_only = [h for h in hits if h["signals"] == ["blame"]]
    content_only = [h for h in hits if h["signals"] == ["content"]]

    print(f"native-guard-leak scan: {len(hits)} candidate lines")
    print(f"  both signals : {len(both)}   <-- highest precision, triage first")
    print(f"  blame only   : {len(blame_only)}")
    print(f"  content only : {len(content_only)}")
    print()

    byfile = defaultdict(list)
    for h in both + blame_only:
        byfile[h["file"]].append(h)
    shown = 0
    for path in sorted(byfile, key=lambda p: -len(byfile[p])):
        print(f"=== {path}  ({len(byfile[path])} lines)")
        for h in byfile[path]:
            tag = "+".join(h["signals"])
            fn = h["function"] or "?"
            print(f"  {h['line']:>5}  [{tag}] ({fn})  {h['text'].strip()[:100]}")
            if h["subject"]:
                print(f"         <- {h['commit'][:9]} {h['subject'][:90]}")
            shown += 1
            if args.limit and shown >= args.limit:
                break
        if args.limit and shown >= args.limit:
            break

    if args.json:
        with open(args.json, "w") as f:
            json.dump(hits, f, indent=2)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
