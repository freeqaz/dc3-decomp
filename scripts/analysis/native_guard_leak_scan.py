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

# The idiom this bug class actually takes.  docs/native/HACK_AUDIT.md names it
# outright: "null checks that mask uninitialized data, early returns that skip
# initialization, MILO_WARN downgrades that hide missing objects".  866ba1082's
# leak was `if (!t2) return;` -- shape, not wording, is what identifies it.
GUARD_SHAPE_RE = re.compile(
    r"""(
        ^\s*if\s*\(\s*!\s*[\w\->.:\[\]()]+\s*\)\s*(\{\s*)?(return|continue|break)\b
      | ^\s*if\s*\(\s*[\w\->.:\[\]()]+\s*(==|!=)\s*(nullptr|NULL|0)\s*\)\s*(\{\s*)?(return|continue|break)\b
      | ^\s*if\s*\(\s*[\w\->.:]+\.empty\(\)\s*\)\s*(\{\s*)?(return|continue|break)\b
      | ^\s*if\s*\(\s*!\s*[\w\->.:\[\]()]+\s*\)\s*$          # guard whose body is on the next line
      | ^\s*if\s*\(\s*[\w\->.:\[\]()]+\s*(==|!=)\s*(nullptr|NULL)\s*\)\s*$
    )""",
    re.VERBOSE,
)

# A bare `return;` / `return X;` on the line after a guard condition.
BARE_RETURN_RE = re.compile(r"^\s*(return\b[^;]*;|continue;|break;)\s*$")

# "Interpolated" = a native commit owns only a small minority of a function body
# that somebody else decompiled.  Both bounds must hold.  A native commit that
# wrote the whole function is authoring, not leaking.
MAX_INTERPOLATED_LINES = 10
MAX_INTERPOLATED_SHARE = 0.40

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
        if NOT_NATIVE_COMMIT_RE.search(subject + "\n" + body):
            continue
        # SUBJECT ONLY.  Admitting commits whose *body* merely names HX_NATIVE
        # was measured at 83 extra commits and 6467 hits -- bulk decomp commits
        # ("post-merge recovery: +101 functions") mention the macro in passing
        # and then own thousands of ordinary matched lines.  The real exemplar
        # (5d19777db) is caught by its subject alone.
        if NATIVE_COMMIT_RE.search(subject):
            result[sha] = subject
    return result


def ppc_build_files(repo):
    """source_path of every unit in report.json -> the files the PPC build compiles.

    Returns (set_of_paths, {path: (worst_fn_percent, n_functions_below_100)}) or
    (None, {}) if the report is unavailable -- callers then skip the filter.
    """
    report = os.path.join(repo, "build", "373307D9", "report.json")
    if not os.path.exists(report):
        return None, {}
    with open(report) as f:
        data = json.load(f)
    paths, stats = set(), {}
    for unit in data.get("units", []):
        sp = (unit.get("metadata") or {}).get("source_path")
        if not sp:
            continue
        paths.add(sp)
        fns = unit.get("functions") or []
        below = [
            f for f in fns if (f.get("match_percent_normalized") or 0) < 99.995
        ]
        worst = min(
            (f.get("match_percent_normalized") or 0) for f in fns
        ) if fns else 100.0
        # name -> worst match% among symbols whose demangled name ends in that
        # scoped name followed by '('.  Several overloads may share a name; take
        # the worst, so a leak is never hidden behind a matching overload.
        byname = {}
        for f in fns:
            dem = (f.get("metadata") or {}).get("demangled_name") or f.get("name", "")
            m = re.search(r"([A-Za-z_~]\w*(?:::[A-Za-z_~]\w*)*)\s*\(", dem)
            if not m:
                continue
            pct = f.get("match_percent_normalized")
            if pct is None:
                continue
            key = m.group(1)
            byname[key] = min(byname.get(key, 100.0), pct)
            short = key.rsplit("::", 1)[-1]
            byname.setdefault("~short~" + short, 100.0)
            byname["~short~" + short] = min(byname["~short~" + short], pct)
        stats[sp] = (worst, len(below), byname)
    return paths, stats


def fn_match_percent(stats, path, fnname):
    """Match% of the decomp function `fnname` in `path`, or None if unknown."""
    entry = stats.get(path)
    if not entry or not fnname:
        return None
    byname = entry[2]
    if fnname in byname:
        return byname[fnname]
    # FUNC_RE sometimes clips a leading char off the qualified name; fall back
    # to the trailing identifier.
    short = fnname.rsplit("::", 1)[-1]
    return byname.get("~short~" + short)


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


def function_spans(lines):
    """[(name, start_idx, end_idx_inclusive)] for column-0 function definitions.

    Deliberately crude -- brace counting from a column-0 signature line that
    opens a body.  Misses templates split over lines and in-class methods in
    headers; that is fine, it only needs to bracket the ordinary .cpp bodies
    where this bug class lives.
    """
    spans = []
    i = 0
    n = len(lines)
    while i < n:
        s = lines[i]
        if s and not s[0].isspace() and not s.startswith(("#", "//", "/*", "}")):
            m = FUNC_RE.match(s)
            if m and "{" in s:
                depth = s.count("{") - s.count("}")
                j = i
                while depth > 0 and j + 1 < n:
                    j += 1
                    code = re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'|//.*', "", lines[j])
                    depth += code.count("{") - code.count("}")
                spans.append((m.group(1), i, j))
                i = j + 1
                continue
        i += 1
    return spans


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
    if "content" in signals or "shape" in signals:
        allsrc = [
            f
            for f in git(repo, "ls-files", "src").splitlines()
            if f.endswith(SOURCE_SUFFIXES)
        ]
        files = sorted(set(files) | set(allsrc))
    ppc_files, ppc_stats = ppc_build_files(repo)
    if ppc_files is not None:
        # Headers have no unit of their own; keep them, they are compiled into
        # whichever TU includes them.  Drop only .cpp files the PPC build never
        # compiles (native-only translation units).
        dropped = [
            f for f in files if f.endswith((".cpp", ".c")) and f not in ppc_files
        ]
        files = [f for f in files if f not in set(dropped)]
        if verbose:
            print(
                f"[scan] dropped {len(dropped)} .cpp not in the PPC build",
                file=sys.stderr,
            )
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

        # --- interpolation analysis -------------------------------------
        # A native leak is a native commit editing INTO a function somebody else
        # decompiled.  A native commit that authored the whole function is not
        # leaking, it is authoring.  Compute, per function body, the share of
        # unguarded substantive lines a native commit still owns; only a small
        # minority share is the signature we are after.
        interpolated = set()  # line indices
        fn_of = {}
        if shas:
            for name, start, end in function_spans(lines):
                own, tot = [], 0
                for i in range(start, min(end + 1, len(lines))):
                    if mask[i] or NOISE_RE.match(lines[i]):
                        continue
                    tot += 1
                    fn_of[i] = name
                    if i < len(shas) and shas[i] in ncommits:
                        own.append(i)
                if not own or tot == 0:
                    continue
                if len(own) <= MAX_INTERPOLATED_LINES and len(own) / tot <= MAX_INTERPOLATED_SHARE:
                    interpolated.update(own)

        for i, text in enumerate(lines):
            if mask[i] or NOISE_RE.match(text):
                continue
            why = []
            if "shape" in signals and GUARD_SHAPE_RE.match(text):
                # History-independent sweep: EVERY unguarded defensive guard,
                # regardless of who wrote it.  Bounds the population from above
                # without trusting commit messages at all -- a leak added by a
                # commit titled "progress: ..." is invisible to `blame`.
                why.append("shape-static")
            sha = shas[i] if i < len(shas) else None
            if sha and sha in ncommits:
                why.append("blame")
                if i in interpolated:
                    why.append("interpolated")
                    prev = lines[i - 1] if i else ""
                    if GUARD_SHAPE_RE.match(text) or (
                        BARE_RETURN_RE.match(text) and GUARD_SHAPE_RE.match(prev)
                    ):
                        why.append("guard-shape")
            if "content" in signals:
                window = lines[max(0, i - CONTENT_WINDOW) : i + 1]
                if any(CONTENT_RE.search(w) for w in window):
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
                    "function": fn_of.get(i) or enclosing_function(lines, i),
                    "unit_worst_fn_percent": ppc_stats.get(path, (None, None, None))[0],
                    "unit_fns_below_100": ppc_stats.get(path, (None, None, None))[1],
                    "fn_match_percent": fn_match_percent(
                        ppc_stats, path, fn_of.get(i) or enclosing_function(lines, i)
                    ),
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

    # The rollback below is `git reset --hard`, which destroys ANY uncommitted
    # change in the worktree, not just the injected one.  It ate an unrelated
    # edit to this very file once.  Refuse to run unless the tree is clean.
    dirty = git(repo, "status", "--porcelain").strip()
    if dirty:
        print(
            "SELF-TEST ABORTED: worktree is dirty and the rollback is a hard "
            "reset, which would destroy these changes:\n" + dirty,
            file=sys.stderr,
        )
        return 2

    branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()
    base = git(repo, "rev-parse", "HEAD").strip()  # exact sha to restore
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
            # Verbatim as 5d19777db left it: no comment, no guard, nothing that
            # names the native port.  With an explanatory comment the `content`
            # signal fires for free and the test stops exercising blame +
            # guard-shape, which are the signals that actually matter.
            CONTROL_ANCHOR + "\n    if (!t2) return;",
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
        # Restore the exact pre-test commit.  Reset to the recorded sha, never
        # to HEAD~1 -- if the scratch commit was never created, HEAD~1 would
        # destroy real work.
        git(repo, "reset", "-q", "--hard", base, check=False)
        open(full, "w").write(original)
        git(repo, "checkout", "-q", "--", CONTROL_FILE, check=False)
        cur_branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD").strip()
        cur_head = git(repo, "rev-parse", "HEAD").strip()
        assert cur_branch == branch, f"branch changed during self-test: {branch} -> {cur_branch}"
        assert cur_head == base, f"HEAD not restored: {base} -> {cur_head}"
        assert not git(repo, "status", "--porcelain", "--", CONTROL_FILE).strip(), (
            f"{CONTROL_FILE} left dirty by self-test"
        )
    print("SELF-TEST PASS")
    return 0


# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--signal", choices=["blame", "content", "shape", "both", "all"], default="both")
    ap.add_argument("--json", metavar="OUT")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--all-tier1", action="store_true",
                    help="also print the non-guard-shaped interpolated tier")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)

    if args.self_test:
        sys.exit(self_test(repo))

    if args.signal == "both":
        signals = ("blame", "content")
    elif args.signal == "all":
        signals = ("blame", "content", "shape")
    else:
        signals = (args.signal,)
    hits = scan(repo, signals=signals, verbose=args.verbose)

    static = [h for h in hits if "shape-static" in h["signals"]]
    if static:
        sub = [
            h
            for h in static
            if h["fn_match_percent"] is not None and h["fn_match_percent"] < 99.995
        ]
        withnative = [h for h in static if "blame" in h["signals"] or "content" in h["signals"]]
        print(
            f"TIER S (history-independent): {len(static)} unguarded defensive guards in src/\n"
            f"  in a function below 100%       : {len(sub)}   <-- can be costing match\n"
            f"  also carrying a native signal  : {len(withnative)}\n"
            f"  NOTE: most of these are target-faithful. The Xbox build genuinely\n"
            f"  null-checks. This is an UPPER BOUND on the population, not a bug list.\n"
        )
        for h in sorted(sub, key=lambda x: x["fn_match_percent"])[: (args.limit or 40)]:
            extra = "+".join(s for s in h["signals"] if s != "shape-static")
            extra = f" [{extra}]" if extra else ""
            print(
                f"  {h['fn_match_percent']:6.2f}%  {h['file']}:{h['line']}"
                f" ({h['function']}){extra}\n           {h['text'].strip()[:100]}"
            )
        print()

    shaped = [h for h in hits if "guard-shape" in h["signals"]]
    interp = [h for h in hits if "interpolated" in h["signals"] and "guard-shape" not in h["signals"]]
    blame_only = [h for h in hits if h["signals"] == ["blame"]]
    content_only = [h for h in hits if h["signals"] == ["content"]]

    print(f"native-guard-leak scan: {len(hits)} candidate lines")
    print(f"  TIER 0 guard-shape  : {len(shaped)}   <-- unguarded defensive guard,")
    print("                             interpolated by a native commit. THE bug class.")
    print(f"  TIER 1 interpolated : {len(interp)}   (native edit into someone else's body,")
    print("                             but not guard-shaped -- usually ordinary decomp)")
    print(f"  TIER 2 blame only   : {len(blame_only)}   (native commit owns the whole body)")
    print(f"  TIER 3 content only : {len(content_only)}   (native wording, no native blame)")
    print()

    tier = shaped if not args.all_tier1 else shaped + interp
    bysite = defaultdict(list)
    for h in tier:
        bysite[(h["file"], h["function"])].append(h)
    def pct(key):
        p = bysite[key][0]["fn_match_percent"]
        return 200.0 if p is None else p

    costly = [k for k in bysite if pct(k) < 99.995]
    label = "TIER 0+1" if args.all_tier1 else "TIER 0"
    print(f"--- {label}: {len(bysite)} distinct functions ---")
    print(
        f"    of which {len(costly)} are in a function BELOW 100% -- an unguarded\n"
        f"    native edit there may be costing match; the rest are cosmetic at worst\n"
        f"    (the function already reproduces the target byte-for-byte).\n"
    )
    order = sorted(bysite, key=pct)
    for n, key in enumerate(order):
        if args.limit and n >= args.limit:
            print(f"\n  ... {len(order) - n} more (raise --limit)")
            break
        path, fn = key
        group = bysite[key]
        p = group[0]["fn_match_percent"]
        ptxt = f"fn {p:.2f}%" if p is not None else "fn not resolved in report.json"
        print(f"\n=== {path} :: {fn}   [{ptxt}]")
        for h in group:
            print(f"  {h['line']:>5}  {h['text'].strip()[:110]}")
        subs = {(h["commit"][:9], h["subject"]) for h in group}
        for sha, subj in subs:
            print(f"         <- {sha} {str(subj)[:95]}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(hits, f, indent=2)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
