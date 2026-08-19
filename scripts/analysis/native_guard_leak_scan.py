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
Four signals, reported separately so each can be judged on its own:

  blame  -- the line is still attributed (git blame -w -M) to a commit whose
            SUBJECT marks it as native-port work, AND the line is not inside any
            conditional block controlled by a guard macro.

  content-- the line, or a comment within CONTENT_WINDOW lines above it, names
            the native/web port explicitly ("native only", "stub never opens",
            "avoid crash on native", ...) while sitting outside any guard.
            This catches leaks whose commit message was not native-flavoured
            and leaks that predate the current history.

  interpolated / guard-shape
         -- refinements of `blame`: the commit owns only a small minority of a
            body somebody else decompiled, and the line has the defensive-guard
            shape.

  shape  -- history-independent: EVERY unguarded defensive guard in src/,
            regardless of who wrote it.  `--signal shape` / `--signal all`.
            An upper bound on the population, not a bug list.

ADJUDICATION -- READ THIS BEFORE DELETING ANYTHING
--------------------------------------------------
No signal here is a verdict.  The Xbox build is full of genuine null checks and
early returns; four of the five sites this scanner was built to re-derive were
target-faithful, and of the 41 candidates the current `--leading-stmts` worklist
produces, *all 41* are code the shipped binary genuinely has.  Only the target
assembly settles a candidate.

A claim this file used to make, and which is WRONG: that objdiff's `insert`
count is "the cheap discriminator" for a leaked guard.  Measured over those 41
adjudicated target-faithful sites at branch head:

    insert > 0                                  31 / 41   =  76% FALSE POSITIVE
    insert > 0 at instruction index <= 12        4 / 41   =  10% false positive
    our-side-only compare/branch in the first
      20 instructions (insert OR replace)        0 / 41   =   0%

`insert > 0` is NECESSARY-ish and nowhere near SUFFICIENT.  Acting on it means
deleting target-faithful null checks -- a correctness regression dressed up as a
match improvement.  The "low index" refinement does not save it either: the
lane's own flagship target-faithful example, LiveCameraInput::NuiAudioDataCallback,
carries inserts at indices 5 and 8 (`addi r8, r11, 0x1444` / `lwz r10, 0x0(r8)`,
address recomputation the target folds into a displacement) while the target
plainly contains all three of its chained null tests.

The screen that actually discriminates is the third row: an *our-side-only
compare/branch* (an `insert`, or a `replace` whose SRC is a compare/branch and
whose TGT is not) inside the prologue region.  That is the shape a leaked
early-out takes.  It fires on the one real leak in the tree --
DelayEffect::Process at the merge-base showed `[9] insert cmplwi cr6, r11, 0x0`
and `[10] insert beq cr6, 0x274` -- and on none of the 41.  Even so it is a
screen, not a verdict: confirm against `build/373307D9/asm/` before editing.

USAGE
-----
    python3 scripts/analysis/native_guard_leak_scan.py                 # blame + content
    python3 scripts/analysis/native_guard_leak_scan.py --signal blame
    python3 scripts/analysis/native_guard_leak_scan.py --signal all    # + TIER S / S-lead
    python3 scripts/analysis/native_guard_leak_scan.py --leading-stmts 2
    python3 scripts/analysis/native_guard_leak_scan.py --json out.json
    python3 scripts/analysis/native_guard_leak_scan.py --repo /path/to/worktree
    python3 scripts/analysis/native_guard_leak_scan.py --self-test     # negative control

Every run prints a provenance banner first: the commit it scanned, the
report.json it filtered with (and whether ninja still has work to do for it),
and the file denominators.  Every count this script prints is relative to those;
a bare "814 guards" with no commit attached is not reproducible, and was the
defect that made the first revision's headline numbers un-checkable.

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
#
# The criterion is TWO-PART and both parts are enforced: a null/empty CONDITION
# *and* a body that is exactly `return` / `continue` / `break`.  The condition
# arms below are matched against the `if` line; the body is checked separately
# by `guard_shape()`, which is the only entry point callers should use.
#
# An earlier revision of this file matched the bare-condition arms
# (condition alone on a line, body on the next) with NO constraint on the body
# at all, so the implemented criterion was strictly weaker than the prose.  It
# admitted, among others, ScoreUtl.cpp's `if (!ratings) ratings = &default;`
# (a substitution, not a guard) and Bitmap.cpp's `if (!buffer) MILO_NOTIFY(...)
# else ...`.  Those are not this bug class and were reported as "regex
# artifacts" in the tally rather than excluded from it.
GUARD_COND_INLINE_RE = re.compile(
    r"""(
        ^\s*if\s*\(\s*!\s*[\w\->.:\[\]()]+\s*\)\s*(\{\s*)?(return|continue|break)\b
      | ^\s*if\s*\(\s*[\w\->.:\[\]()]+\s*(==|!=)\s*(nullptr|NULL|0)\s*\)\s*(\{\s*)?(return|continue|break)\b
      | ^\s*if\s*\(\s*[\w\->.:]+\.empty\(\)\s*\)\s*(\{\s*)?(return|continue|break)\b
    )""",
    re.VERBOSE,
)

# Condition alone on its line -- the body is on one of the following lines and
# MUST be checked before this counts as a guard.
GUARD_COND_BARE_RE = re.compile(
    r"""(
        ^\s*if\s*\(\s*!\s*[\w\->.:\[\]()]+\s*\)\s*$
      | ^\s*if\s*\(\s*[\w\->.:\[\]()]+\s*(==|!=)\s*(nullptr|NULL)\s*\)\s*$
      | ^\s*if\s*\(\s*[\w\->.:]+\.empty\(\)\s*\)\s*$
    )""",
    re.VERBOSE,
)

# A bare `return;` / `return X;` on the line after a guard condition.
BARE_RETURN_RE = re.compile(r"^\s*(return\b[^;]*;|continue\s*;|break\s*;)\s*$")

BLANK_OR_COMMENT_RE = re.compile(r"^\s*(//|/\*.*\*/\s*$|$)")


def guard_shape(lines, i):
    """True iff line `i` is a defensive guard: null/empty condition, whose body
    is exactly return/continue/break.

    Handles all three spellings the tree uses:

        if (!x) return;                 <- inline
        if (!x)                         <- bare condition, body next line
            return;
        if (!x) {                       <- bare condition, braced body
            return;
        }

    and rejects the shapes that are NOT this bug class -- a condition whose body
    assigns a default, notifies, or falls through into an `else`.
    """
    text = lines[i]
    if GUARD_COND_INLINE_RE.match(text):
        return True
    if not GUARD_COND_BARE_RE.match(text):
        # `if (!x) {` with the body on following lines: the trailing brace keeps
        # it out of the bare arm, so handle it here.
        m = re.match(
            r"^\s*if\s*\(\s*(?:!\s*[\w\->.:\[\]()]+|"
            r"[\w\->.:\[\]()]+\s*(?:==|!=)\s*(?:nullptr|NULL)|"
            r"[\w\->.:]+\.empty\(\))\s*\)\s*\{\s*$",
            text,
        )
        if not m:
            return False
        j = _next_substantive(lines, i + 1)
        if j is None or not BARE_RETURN_RE.match(lines[j]):
            return False
        # The closing brace must come immediately after, and must not be
        # followed by `else` -- an else-arm means this is a branch, not a guard.
        k = _next_substantive(lines, j + 1)
        return k is not None and re.match(r"^\s*\}\s*$", lines[k]) is not None
    j = _next_substantive(lines, i + 1)
    if j is None:
        return False
    if BARE_RETURN_RE.match(lines[j]):
        return True
    if re.match(r"^\s*\{\s*$", lines[j]):
        k = _next_substantive(lines, j + 1)
        return k is not None and BARE_RETURN_RE.match(lines[k]) is not None
    return False


def _next_substantive(lines, start):
    for j in range(start, min(start + 6, len(lines))):
        if BLANK_OR_COMMENT_RE.match(lines[j]):
            continue
        return j
    return None

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


SIG_RE = re.compile(
    r"([A-Za-z_~][\w:~]*(?:\s*<[^<>();]*>)?)\s*\([^;{]*\)\s*"
    r"(?:const\s*)?(?:throw\s*\([^)]*\)\s*)?(?::[^{;]*)?\{\s*$"
)


def _strip_code(line, in_block_comment):
    """Return (line minus strings/char-literals/comments, new block-comment state)."""
    out = []
    i, n, inb = 0, len(line), in_block_comment
    while i < n:
        c = line[i]
        if inb:
            if line.startswith("*/", i):
                inb = False
                i += 2
                continue
            i += 1
            continue
        if line.startswith("/*", i):
            inb = True
            i += 2
            continue
        if line.startswith("//", i):
            break
        if c in '"\'':
            q = c
            i += 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == q:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out), inb


def function_spans(lines):
    """[(name, start_idx, end_idx_inclusive)] for every function *definition*.

    Brace-accurate, and it understands the two shapes the previous revision got
    wrong:

      * a signature split over several lines --

            CharClip *HamDirector::GetClipStartAndEndBeats(
                Symbol clipName, float &startBeat, ...
            ) {

        The old FUNC_RE required `(...)` to close on the signature's first line,
        so this opened no span at all, and every hit inside the body was
        attributed to the PRECEDING function (HamDirector::DrawIconMan).  That
        silently moves a hit into a different function's match%, which is the
        number the sub-100% filter is built on.

      * a definition that is not at column 0 -- inside `namespace { ... }` or an
        in-class method in a header.

    Only definitions are recorded: a signature ending in `;` never matches, and
    lines inside a body are skipped wholesale, so lambdas and nested classes do
    not open spurious spans.
    """
    n = len(lines)
    codes, inb = [], False
    for ln in lines:
        code, inb = _strip_code(ln, inb)
        codes.append(code)

    spans = []
    i = 0
    while i < n:
        if "{" not in codes[i]:
            i += 1
            continue
        # Join this line with up to 8 predecessors so a multi-line signature is
        # visible as one string.  Stop at a line that ends a statement/scope.
        name = None
        start = i
        for back in range(0, 9):
            j = i - back
            if j < 0:
                break
            if back and re.search(r"[;{}]\s*$", codes[j]):
                break
            joined = re.sub(r"\s+", " ", " ".join(c.strip() for c in codes[j : i + 1]))
            head = joined[: joined.index("{") + 1]
            m = SIG_RE.search(head)
            if m:
                name, start = m.group(1), j
                break
        if name is None:
            i += 1
            continue
        depth, k = 0, i
        while k < n:
            depth += codes[k].count("{") - codes[k].count("}")
            if depth <= 0:
                break
            k += 1
        # `i` is the line carrying the body's opening brace; statement numbering
        # starts after it, so a multi-line signature does not count its own
        # parameter lines as statements.
        spans.append((name, start, min(k, n - 1), i))
        i = k + 1
    return spans


def enclosing_function(spans, idx):
    """Name of the innermost recorded function span containing line `idx`."""
    best = None
    for name, start, end, _open in spans:
        if start <= idx <= end:
            if best is None or start >= best[1]:
                best = (name, start)
    return best[0] if best else None


# ---------------------------------------------------------------------------
# Scan
# ---------------------------------------------------------------------------


def scan(repo, signals=("blame", "content"), verbose=False, cov=None):
    """Return the hit list.  If `cov` is a dict it is filled with the
    denominators -- how many files were in the universe, how many were examined,
    and how many were discarded for each reason.  A count with no denominator is
    a sample presented as a total; see scripts/analysis/coverage.py.
    """
    if cov is None:
        cov = {}
    cov.setdefault("drops", {})
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
    cov["universe"] = len(files)
    cov["universe_desc"] = (
        "src/ source files reachable from the selected signals"
        if "shape" not in signals and "content" not in signals
        else "tracked src/ source files"
    )
    ppc_files, ppc_stats = ppc_build_files(repo)
    if ppc_files is None:
        cov["report_json"] = None
    if ppc_files is not None:
        # Headers have no unit of their own; keep them, they are compiled into
        # whichever TU includes them.  Drop only .cpp files the PPC build never
        # compiles (native-only translation units).
        dropped = [
            f for f in files if f.endswith((".cpp", ".c")) and f not in ppc_files
        ]
        files = [f for f in files if f not in set(dropped)]
        cov["drops"]["not-compiled-by-the-ppc-build"] = len(dropped)
        if verbose:
            print(
                f"[scan] dropped {len(dropped)} .cpp not in the PPC build",
                file=sys.stderr,
            )
    if verbose:
        print(f"[scan] scanning {len(files)} files", file=sys.stderr)

    examined = 0
    for path in files:
        full = os.path.join(repo, path)
        try:
            lines = open(full, errors="replace").read().split("\n")
        except OSError:
            cov["drops"]["unreadable"] = cov["drops"].get("unreadable", 0) + 1
            continue
        examined += 1
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
        stmt_index = {}  # line idx -> 1-based statement position in its function
        spans = function_spans(lines)
        for name, start, end, body_open in spans:
            own, tot, seen = [], 0, 0
            for i in range(start, min(end + 1, len(lines))):
                if mask[i] or NOISE_RE.match(lines[i]):
                    continue
                tot += 1
                # A line already attributed to a nested span (a lambda or a
                # local struct's method) keeps the inner name.
                fn_of.setdefault(i, name)
                if i > body_open:
                    seen += 1
                    stmt_index.setdefault(i, seen)
                if shas and i < len(shas) and shas[i] in ncommits:
                    own.append(i)
            if not own or tot == 0:
                continue
            if len(own) <= MAX_INTERPOLATED_LINES and len(own) / tot <= MAX_INTERPOLATED_SHARE:
                interpolated.update(own)

        for i, text in enumerate(lines):
            if mask[i] or NOISE_RE.match(text):
                continue
            why = []
            shaped = guard_shape(lines, i)
            if "shape" in signals and shaped:
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
                    if shaped or (
                        BARE_RETURN_RE.match(text) and i and guard_shape(lines, i - 1)
                    ):
                        why.append("guard-shape")
            if "content" in signals:
                window = lines[max(0, i - CONTENT_WINDOW) : i + 1]
                if any(CONTENT_RE.search(w) for w in window):
                    why.append("content")
            if not why:
                continue
            fname = fn_of.get(i)
            hits.append(
                {
                    "file": path,
                    "line": i + 1,
                    "text": text.rstrip(),
                    "signals": why,
                    "commit": sha,
                    "subject": ncommits.get(sha),
                    "function": fname,
                    "stmt_index": stmt_index.get(i),
                    "unit_worst_fn_percent": ppc_stats.get(path, (None, None, None))[0],
                    "unit_fns_below_100": ppc_stats.get(path, (None, None, None))[1],
                    "fn_match_percent": fn_match_percent(ppc_stats, path, fname),
                }
            )
    cov["examined"] = examined
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


def provenance_banner(repo, cov):
    """Every number this script prints is relative to a commit and a report.json.

    Saying which is not decoration.  The first revision of this scanner printed
    "814 unguarded defensive guards / 107 in sub-100% functions" with no
    baseline attached; those figures are the merge-base eda64e956's.  On the
    branch head they are one lower each, because the guard the branch fixed was
    itself one of the 814 -- so a reader re-running the scanner got a different
    number than the doc and had no way to tell whether the tree or the tool had
    moved.
    """
    import time

    head = git(repo, "rev-parse", "HEAD", check=False).strip()[:12]
    subject = git(repo, "log", "-1", "--format=%s", check=False).strip()[:78]
    dirty = bool(git(repo, "status", "--porcelain", check=False).strip())
    report = os.path.join(repo, "build", "373307D9", "report.json")
    lines = [
        "=" * 78,
        f"native-guard-leak scan of {repo}",
        f"  commit  : {head}{' +DIRTY' if dirty else ''}  {subject}",
    ]
    if os.path.exists(report):
        mt = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(report)))
        lines.append(f"  report  : build/373307D9/report.json  (mtime {mt})")
        state = ninja_state(repo)
        if state == "dirty":
            lines += [
                "            *** STALE: `ninja -n build/373307D9/report.json` still has",
                "            *** work to do, so every match% below describes a DIFFERENT",
                "            *** source tree than the one being scanned. Run `ninja`.",
            ]
        elif state == "unknown":
            lines.append("            (freshness unverified: no ninja / no build graph here)")
    else:
        lines.append("  report  : ABSENT -- no match% filtering, and no PPC-build file filter")
    lines += [
        "            NOTE: a worktree made by scripts/setup_worktree.sh gets",
        "            build/373307D9 as a reflink COPY of the main repo's, so until a",
        "            full `ninja` runs here this file describes MAIN's source, not",
        "            this branch's.",
        f"  files   : {cov.get('universe', '?')} in the universe"
        f" ({cov.get('universe_desc', '')})",
    ]
    for reason, n in sorted(cov.get("drops", {}).items()):
        lines.append(f"            -{n:>5}  dropped: {reason}")
    lines.append(f"            ={cov.get('examined', '?'):>5}  examined")
    acct = cov.get("universe", 0) - sum(cov.get("drops", {}).values()) - cov.get("examined", 0)
    if acct:
        lines.append(f"            *** UNACCOUNTED: {acct} files neither examined nor dropped")
    lines.append("=" * 78)
    return "\n".join(lines)


def ninja_state(repo):
    """'clean' | 'dirty' | 'unknown' for build/373307D9/report.json."""
    try:
        out = subprocess.run(
            ["ninja", "-n", "build/373307D9/report.json"],
            cwd=repo,
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    if out.returncode != 0:
        return "unknown"
    return "clean" if "no work to do" in out.stdout else "dirty"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=os.getcwd())
    ap.add_argument("--signal", choices=["blame", "content", "shape", "both", "all"], default="both")
    ap.add_argument("--json", metavar="OUT")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--all-tier1", action="store_true",
                    help="also print the non-guard-shaped interpolated tier")
    ap.add_argument(
        "--leading-stmts",
        type=int,
        default=2,
        metavar="N",
        help="TIER S-lead: restrict the sub-100%% TIER S list to guards that are "
             "among the first N statements of their function -- the shape both "
             "confirmed leaks took. Default 2. 0 disables the subset.",
    )
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
    cov = {}
    hits = scan(repo, signals=signals, verbose=args.verbose, cov=cov)

    print(provenance_banner(repo, cov))

    static = [h for h in hits if "shape-static" in h["signals"]]
    if static:
        sub = [
            h
            for h in static
            if h["fn_match_percent"] is not None and h["fn_match_percent"] < 99.995
        ]
        unresolved = [h for h in static if h["fn_match_percent"] is None]
        withnative = [h for h in static if "blame" in h["signals"] or "content" in h["signals"]]
        print(
            f"TIER S (history-independent): {len(static)} unguarded defensive guards in src/\n"
            f"  in a function below 100%       : {len(sub)}   <-- can be costing match\n"
            f"  in a function at 100%          : {len(static) - len(sub) - len(unresolved)}\n"
            f"  fn not resolved in report.json : {len(unresolved)}   (NOT counted as either)\n"
            f"  also carrying a native signal  : {len(withnative)}\n"
            f"  NOTE: most of these are target-faithful. The Xbox build genuinely\n"
            f"  null-checks. This is an UPPER BOUND on the population, not a bug list.\n"
        )

        lead = []
        if args.leading_stmts > 0:
            lead = [
                h
                for h in sub
                if h["stmt_index"] is not None and h["stmt_index"] <= args.leading_stmts
            ]
            print(
                f"TIER S-lead (--leading-stmts {args.leading_stmts}): {len(lead)} of those "
                f"{len(sub)} are among the\n"
                f"  first {args.leading_stmts} statements of their function -- the shape both "
                f"confirmed leaks took.\n"
                f"  This is the adjudication worklist: short enough to read every one\n"
                f"  against build/373307D9/asm/, and it is a REPRODUCIBLE flag, not a\n"
                f"  number somebody counted by hand.\n"
            )
            for h in sorted(lead, key=lambda x: x["fn_match_percent"]):
                extra = "+".join(s for s in h["signals"] if s != "shape-static")
                extra = f" [{extra}]" if extra else ""
                print(
                    f"  {h['fn_match_percent']:6.2f}%  {h['file']}:{h['line']}"
                    f" ({h['function']}, stmt {h['stmt_index']}){extra}\n"
                    f"           {h['text'].strip()[:100]}"
                )
            print()

        rest = [h for h in sub if h not in lead]
        for h in sorted(rest, key=lambda x: x["fn_match_percent"])[: (args.limit or 40)]:
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
