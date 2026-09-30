#!/usr/bin/env python3
"""native_shadow_audit.py — inventory the code the native port runs INSTEAD of the decomp.

THE BLIND SPOT THIS EXISTS FOR
==============================
The native x86_64 port compiles the SAME `src/` tree as the PPC target, with
`HX_NATIVE` defined.  Wherever the source says

    #ifdef HX_NATIVE
        <hand-written native body>       <- what the playable build runs
    #else
        <decompiled body>                <- what objdiff measures
    #endif

every decomp instrument in this repo (objdiff under all three rulers,
report.json, unicorn, every scanner under scripts/analysis/) measures ONLY the
second branch, by construction.  A semantic difference between the two is a
native-only bug that no match percentage can see.  Some guards are legitimate
(LP64 pointer width, endianness, platform APIs, the 64 KB Xbox stack, ref-ring
walk safety); others silently changed behaviour.  Validation case:
`ObjPtrVec<T1,T2>::erase` in src/system/obj/ObjPtr_p.h, whose native body
ignored `mEraseMode` (fixed on branch fix-ptrvec-erase, a71828235).

WHAT IT DOES
============
Enumerates every conditional GROUP (`#if*` ... `#endif`) in `src/` whose
condition mentions HX_NATIVE, resolves nesting and `#elif`/`#else`, works out
per branch which build compiles it (native only / PPC only / both), and
classifies the group by SHAPE:

  REPLACES  (a)  both a native-only and a PPC-only branch carry code
  ADDS      (b)  only a native-only branch carries code
  REMOVES   (c)  only a PPC-only branch carries code — the decomp body is
                 simply not compiled natively ("guard swallows the definition")

and by SCOPE:

  in-function        the guard sits inside a function body (enclosing
                     function reported)
  file-scope-defs    the guard sits at file/class scope and its branches
                     contain function DEFINITIONS            -> shape (d)
  file-scope-decls   file/class scope, declarations/data only
  whole-file         the guard covers >= 90% of the file's code lines -> (d)

and then pairs functions ACROSS regions: a function whose definition sits in a
PPC-only branch in one guard and in a native-only branch in another guard of
the same file is a SPLIT SHADOW — two syntactically unrelated regions (raw
shapes REMOVES + ADDS) that are semantically one REPLACES.  That is exactly the
ObjPtrVec::erase shape (`#ifndef HX_NATIVE` block ... `#endif`, then an
`#ifdef HX_NATIVE` block defining the same members), and a per-region shape
count alone would file it under (b)+(c) and hide it.

WHAT IT CANNOT SEE (stated so a zero here is not read as "no shadows")
======================================================================
  * build-level shadows: a src/ .cpp the native CMake does not compile at all
    (native/CMakeLists.txt source lists / REMOVE_ITEM), replaced wholesale by
    native/src or ../milo-native-engine.  No preprocessor guard exists there.
  * other platform macros (`_XBOX`, `__EMSCRIPTEN__`, `_WIN32`, ...) that also
    differ between the two builds.  Only HX_NATIVE is modelled; every other
    macro is assumed to take the SAME value in both builds.
  * semantics.  This is an inventory, not a verdict: it tells you which bodies
    the native build runs instead of the measured ones.  Deciding whether a
    difference is a legitimate platform adaptation or a divergence is manual
    triage against the TARGET listing (build/373307D9/asm/**).
  * enclosing-function attribution is a column-0 heuristic (clang-formatted
    definitions start at column 0 and close with `}` at column 0); regions in
    indented inline class members report the nearest column-0 context.

DENOMINATOR
===========
The universe is every `#if/#ifdef/#ifndef/#elif` line mentioning HX_NATIVE in
`src/`, counted by an independent regex pass over the raw files BEFORE the
parser runs.  Every one of those directives is either attributed to a
classified group (`examined`) or dropped with a reason (`cov.drop`); if the
parser loses one (a continuation line, a comment it misread), the books do not
balance and the run exits 4.

Usage:
    python3 scripts/analysis/native_shadow_audit.py                 # summary + worklist
    python3 scripts/analysis/native_shadow_audit.py --list all      # every region
    python3 scripts/analysis/native_shadow_audit.py --json out.json
    python3 scripts/analysis/native_shadow_audit.py --root <dir>    # scan another tree
    python3 scripts/analysis/native_shadow_audit.py --find ObjPtrVec::erase
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import sys
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import (CoverageReport, add_coverage_args, EXIT_NO_INPUT,  # noqa: E402
                      EXIT_UNACCOUNTED)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MACRO = "HX_NATIVE"
SRC_EXTS = (".h", ".hpp", ".hh", ".cpp", ".cc", ".c", ".inl", ".inc")
# Vendored trees: counted in the universe, dropped by reason unless
# --include-vendor.  Relative to the scan root.
VENDOR_PREFIXES = ("src/xdk/", "src/system/stlport/")

# Independent universe regex.  Deliberately NOT the parser's regex: it only
# asks "is this a conditional-opening or #elif line that names the macro?",
# with no comment/continuation handling, so a parser that loses a directive
# cannot also lose it from the denominator.
UNIVERSE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b.*\b" + MACRO + r"\b")

DIRECTIVE_RE = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")

WHOLE_FILE_FRACTION = 0.90

SHAPE_LETTER = {"REPLACES": "a", "ADDS": "b", "REMOVES": "c"}


# --------------------------------------------------------------------------- #
# Condition evaluation: three-valued over {HX_NATIVE} x {other atoms}.
# --------------------------------------------------------------------------- #

_TOKEN_RE = re.compile(
    r"\s*(defined\s*\(\s*\w+\s*\)|defined\s+\w+|&&|\|\||==|!=|<=|>=|[()!<>+\-*/]|\w+)")


def _tokenize(expr: str) -> Optional[List[str]]:
    expr = re.sub(r"//.*$", "", expr)
    expr = re.sub(r"/\*.*?\*/", " ", expr)
    toks: List[str] = []
    pos = 0
    expr = expr.rstrip()
    while pos < len(expr):
        m = _TOKEN_RE.match(expr, pos)
        if not m or m.end() == pos:
            return None
        toks.append(m.group(1))
        pos = m.end()
    return toks


def _atom_name(tok: str) -> str:
    m = re.match(r"defined\s*\(?\s*(\w+)", tok)
    return "defined:" + m.group(1) if m else "value:" + tok


def compile_condition(kind: str, rest: str):
    """Return (atoms, fn) where fn(env) -> bool, or None if unparseable.

    `env` maps atom names to bools.  `defined:HX_NATIVE` is the modelled atom;
    every other atom is a free variable assumed to take the same value in both
    builds.  Numeric literals evaluate by C truthiness.
    """
    if kind in ("ifdef", "ifndef"):
        name = rest.strip().split()[0] if rest.strip() else ""
        name = re.sub(r"//.*|/\*.*", "", name).strip()
        if not re.fullmatch(r"\w+", name):
            return None
        atom = "defined:" + name
        if kind == "ifdef":
            return [atom], lambda env, a=atom: env[a]
        return [atom], lambda env, a=atom: not env[a]
    toks = _tokenize(rest)
    if not toks:
        return None
    py: List[str] = []
    atoms: List[str] = []
    for t in toks:
        if t == "&&":
            py.append(" and ")
        elif t == "||":
            py.append(" or ")
        elif t == "!":
            py.append(" not ")
        elif t in ("(", ")"):
            py.append(t)
        elif t.startswith("defined"):
            a = _atom_name(t)
            atoms.append(a)
            py.append(f"env[{a!r}]")
        elif re.fullmatch(r"\d+[uUlL]*", t):
            py.append("True" if int(re.sub(r"[uUlL]", "", t)) else "False")
        elif re.fullmatch(r"\w+", t):
            a = _atom_name(t)
            atoms.append(a)
            py.append(f"env[{a!r}]")
        else:
            # comparisons / arithmetic: out of the modelled subset
            return None
    src = "".join(py)
    # An unparseable condition is NOT swallowed: the caller turns None into the
    # counted parser drop "condition-unparseable", which verdict() refuses to
    # exit 0 on.
    code = None
    try:
        code = compile(src, "<cond>", "eval")
    except SyntaxError as e:
        print(f"native_shadow_audit: unparseable condition {rest!r}: {e}", file=sys.stderr)
    if code is None:
        return None
    return sorted(set(atoms)), lambda env, c=code: bool(eval(c, {}, {"env": env}))


# --------------------------------------------------------------------------- #
# Parsing
# --------------------------------------------------------------------------- #

class Branch:
    __slots__ = ("kind", "rest", "line", "end", "cond", "native", "ppc", "content_lines")

    def __init__(self, kind: str, rest: str, line: int):
        self.kind = kind          # if/ifdef/ifndef/elif/else
        self.rest = rest
        self.line = line          # 1-based directive line
        self.end = line           # 1-based line of the NEXT directive of this group
        self.cond = None
        self.native = False       # compiled in the native build
        self.ppc = False          # compiled in the PPC build
        self.content_lines: List[int] = []


class Group:
    __slots__ = ("branches", "depth", "parent", "parent_branch", "hx_directives",
                 "endif_line", "id")

    def __init__(self, depth: int, parent, parent_branch):
        self.branches: List[Branch] = []
        self.depth = depth
        self.parent = parent
        self.parent_branch = parent_branch
        self.hx_directives: List[int] = []   # lines of universe directives in this group
        self.endif_line: Optional[int] = None
        self.id = -1


def _logical_lines(text: str) -> List[Tuple[int, str, bool]]:
    """Yield (first_physical_line, joined_text, in_block_comment_at_start).

    Joins backslash continuations.  Tracks /* */ state across lines so that a
    `#ifdef` inside a block comment is not treated as a directive.
    """
    phys = text.split("\n")
    out: List[Tuple[int, str, bool]] = []
    in_block = False
    i = 0
    while i < len(phys):
        start = i
        start_in_block = in_block
        line = phys[i]
        while line.endswith("\\") and i + 1 < len(phys):
            i += 1
            line = line[:-1] + " " + phys[i]
        # update block-comment state by scanning this logical line
        j = 0
        s = line
        in_str = None
        while j < len(s):
            if in_block:
                k = s.find("*/", j)
                if k < 0:
                    j = len(s)
                    break
                in_block = False
                j = k + 2
                continue
            c = s[j]
            if in_str:
                if c == "\\":
                    j += 2
                    continue
                if c == in_str:
                    in_str = None
                j += 1
                continue
            if c in ('"', "'"):
                in_str = c
                j += 1
                continue
            if s.startswith("//", j):
                break
            if s.startswith("/*", j):
                in_block = True
                j += 2
                continue
            j += 1
        out.append((start + 1, line, start_in_block))
        for extra in range(start + 1, i + 1):
            out.append((extra + 1, "", start_in_block))  # continuation placeholders
        i += 1
    return out


def _strip_code(line: str) -> str:
    line = re.sub(r"//.*$", "", line)
    line = re.sub(r"/\*.*?\*/", "", line)
    return line.strip()


class FileResult:
    def __init__(self, rel: str):
        self.rel = rel
        self.groups: List[Group] = []
        self.lines: List[str] = []
        self.parse_error: Optional[str] = None
        self.comment_directives: List[int] = []


def parse_file(rel: str, text: str) -> FileResult:
    fr = FileResult(rel)
    fr.lines = text.split("\n")
    stack: List[Group] = []
    for lineno, line, in_block in _logical_lines(text):
        is_universe = bool(UNIVERSE_RE.match(line)) and line != ""
        m = DIRECTIVE_RE.match(line) if not in_block else None
        if in_block:
            if is_universe:
                fr.comment_directives.append(lineno)
            if stack and stack[-1].branches:
                stack[-1].branches[-1].content_lines.append(lineno)
            continue
        if not m:
            if stack:
                stack[-1].branches[-1].content_lines.append(lineno)
            continue
        kind, rest = m.group(1), m.group(2)
        if kind in ("if", "ifdef", "ifndef"):
            if stack:
                # a nested group's lines count as content of the outer branch
                stack[-1].branches[-1].content_lines.append(lineno)
            parent = stack[-1] if stack else None
            g = Group(len(stack), parent, parent.branches[-1] if parent else None)
            g.branches.append(Branch(kind, rest, lineno))
            if is_universe:
                g.hx_directives.append(lineno)
            stack.append(g)
            fr.groups.append(g)
        elif kind in ("elif", "else"):
            if not stack:
                fr.parse_error = f"line {lineno}: #{kind} with no open #if"
                return fr
            g = stack[-1]
            g.branches[-1].end = lineno
            g.branches.append(Branch(kind, rest, lineno))
            if is_universe:
                g.hx_directives.append(lineno)
        elif kind == "endif":
            if not stack:
                fr.parse_error = f"line {lineno}: #endif with no open #if"
                return fr
            g = stack.pop()
            g.branches[-1].end = lineno
            g.endif_line = lineno
            if stack:
                stack[-1].branches[-1].content_lines.append(lineno)
    if stack:
        fr.parse_error = f"{len(stack)} unterminated #if group(s), first at line {stack[0].branches[0].line}"
    return fr


def resolve_group_worlds(g: Group) -> Optional[str]:
    """Set branch.native / branch.ppc.  Return an error slug or None.

    Enumerates every assignment of the non-HX atoms (assumed equal in both
    builds) and, per assignment, which branch each build takes.  A branch is
    native-only if some assignment sends the native build there and not the
    PPC build, and no assignment sends the PPC build there without the native
    build — and symmetrically.  Anything else is "both" (not a shadow).
    """
    comp = []
    atoms: set = set()
    for b in g.branches:
        if b.kind == "else":
            comp.append(None)
            continue
        c = compile_condition(b.kind if b.kind != "elif" else "if", b.rest)
        if c is None:
            return "condition-unparseable"
        comp.append(c)
        atoms.update(c[0])
    hx = "defined:" + MACRO
    others = sorted(a for a in atoms if a not in (hx, "value:" + MACRO))
    if len(others) > 8:
        return "condition-too-many-atoms"
    native_only_seen = [False] * len(g.branches)
    ppc_only_seen = [False] * len(g.branches)
    both_seen = [False] * len(g.branches)
    for vals in itertools.product((False, True), repeat=len(others)):
        base = dict(zip(others, vals))
        taken = {}
        for world, hxval in (("native", True), ("ppc", False)):
            env = dict(base)
            env[hx] = hxval
            env["value:" + MACRO] = hxval
            idx = None
            for i, c in enumerate(comp):
                if c is None or c[1](env):
                    idx = i
                    break
            taken[world] = idx
        if taken["native"] == taken["ppc"]:
            if taken["native"] is not None:
                both_seen[taken["native"]] = True
        else:
            if taken["native"] is not None:
                native_only_seen[taken["native"]] = True
            if taken["ppc"] is not None:
                ppc_only_seen[taken["ppc"]] = True
    for i, b in enumerate(g.branches):
        b.native = native_only_seen[i] and not ppc_only_seen[i] and not both_seen[i]
        b.ppc = ppc_only_seen[i] and not native_only_seen[i] and not both_seen[i]
        if native_only_seen[i] and ppc_only_seen[i]:
            return "condition-branch-in-both-worlds-differently"
    return None


def enclosing_context_live(g: Group) -> Tuple[bool, bool]:
    """(live_in_native, live_in_ppc) from enclosing HX groups' branches."""
    ln, lp = True, True
    p, pb = g.parent, g.parent_branch
    while p is not None:
        if p.hx_directives:
            if pb.native:
                lp = False
            elif pb.ppc:
                ln = False
        pb = p.parent_branch
        p = p.parent
    return ln, lp


# --------------------------------------------------------------------------- #
# Function attribution (column-0 heuristic)
# --------------------------------------------------------------------------- #

_BEGIN_MACRO_RE = re.compile(r"^(BEGIN_\w+|DEF_\w+)\s*\(\s*([\w:]+)")
_DEF_START_RE = re.compile(r"^[A-Za-z_~][^;#]*$")


def _norm_name(header: str) -> Optional[str]:
    header = re.sub(r"//.*", "", header)
    header = re.sub(r"/\*.*?\*/", "", header)
    header = " ".join(header.split())
    m = re.search(r"\boperator\s*(\(\)|[^\s(]+)\s*\(", header)
    if m:
        pre = header[:m.start()]
        qual = re.findall(r"([\w<>, ]+::)\s*$", pre)
        q = qual[0] if qual else ""
        name = q + "operator" + m.group(1)
    else:
        k = header.find("(")
        if k < 0:
            return None
        pre = header[:k].rstrip()
        # walk back over a qualified name with template args
        depth = 0
        j = len(pre)
        while j > 0:
            c = pre[j - 1]
            if c == ">":
                depth += 1
            elif c == "<":
                depth -= 1
            elif depth == 0 and not (c.isalnum() or c in "_:~"):
                break
            j -= 1
        name = pre[j:]
    name = re.sub(r"<[^<>]*>", "", name)
    name = re.sub(r"<[^<>]*>", "", name)
    name = name.replace(" ", "")
    if not name or not re.search(r"[A-Za-z_]", name):
        return None
    if name in ("if", "for", "while", "switch", "return", "sizeof", "defined"):
        return None
    return name


def build_function_map(lines: List[str]) -> List[Tuple[int, int, str]]:
    """Return [(start_line, end_line, name)] for column-0 function definitions.

    A definition starts at a column-0 line that is not a directive, comment,
    `}`/`{`, or declaration ending in `;`, whose header (joined up to the first
    `{`) contains `(`; it ends at the next column-0 `}`.  `BEGIN_HANDLERS(X)`
    style macros are attributed to `X::<MACRO>` and end at the matching
    column-0 `END_*`.
    """
    out: List[Tuple[int, int, str]] = []
    n = len(lines)
    i = 0
    while i < n:
        s = lines[i]
        if not s or s[0] in " \t#/}{*" or s.startswith("//"):
            i += 1
            continue
        mm = _BEGIN_MACRO_RE.match(s)
        if mm:
            macro, cls = mm.group(1), mm.group(2)
            j = i + 1
            while j < n and not re.match(r"^END_\w+", lines[j]):
                if _BEGIN_MACRO_RE.match(lines[j]):
                    break
                j += 1
            out.append((i + 1, min(j, n - 1) + 1, f"{cls}::<{macro}>"))
            i = j + 1
            continue
        if not _DEF_START_RE.match(s) or s.rstrip().endswith(";"):
            i += 1
            continue
        # gather header up to '{' (max 8 lines), refusing ';' before '{'
        header = ""
        j = i
        opened = False
        while j < n and j < i + 8:
            t = re.sub(r"//.*", "", lines[j])
            if "{" in t:
                header += " " + t[:t.index("{")]
                opened = True
                break
            if t.rstrip().endswith(";"):
                break
            if j > i and lines[j].startswith("#"):
                break
            header += " " + t
            j += 1
        if not opened or "(" not in header:
            i += 1
            continue
        if re.match(r"\s*(class|struct|union|enum|namespace|extern\s+\"C\")\b", header):
            i += 1
            continue
        name = _norm_name(header)
        if not name:
            i += 1
            continue
        # find closing column-0 '}'.  A one-line body ending in '}' closes here.
        body_line = lines[j]
        if body_line.rstrip().endswith("}") and body_line.count("{") <= body_line.count("}"):
            out.append((i + 1, j + 1, name))
            i = j + 1
            continue
        k = j + 1
        while k < n and not lines[k].startswith("}"):
            if k > j + 1 and _BEGIN_MACRO_RE.match(lines[k]):
                break
            k += 1
        out.append((i + 1, min(k, n - 1) + 1, name))
        i = k + 1
    return out


def classify_scope(fr: FileResult, g: Group, fmap, code_lines_total: int):
    start = g.branches[0].line
    end = g.endif_line or start
    enclosing = None
    for (s, e, name) in fmap:
        if s < start and e > end:
            enclosing = name
            break
    defs = {"native": [], "ppc": []}
    for b in g.branches:
        side = "native" if b.native else ("ppc" if b.ppc else None)
        if side is None:
            continue
        for (s, e, name) in fmap:
            if b.line < s and e < b.end:
                defs[side].append(name)
    span_code = sum(1 for ln in range(start, end + 1)
                    if ln - 1 < len(fr.lines) and _strip_code(fr.lines[ln - 1]))
    if code_lines_total and span_code >= WHOLE_FILE_FRACTION * code_lines_total and g.depth <= 1:
        scope = "whole-file"
    elif enclosing:
        scope = "in-function"
    elif defs["native"] or defs["ppc"]:
        scope = "file-scope-defs"
    else:
        scope = "file-scope-decls"
    return scope, enclosing, defs


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def iter_source_files(root: str) -> List[str]:
    out = []
    src = os.path.join(root, "src")
    for dp, dns, fns in os.walk(src):
        dns.sort()
        for fn in sorted(fns):
            if fn.endswith(SRC_EXTS):
                out.append(os.path.relpath(os.path.join(dp, fn), root))
    return sorted(out)


def branch_text(fr: FileResult, b: Branch) -> List[str]:
    return [fr.lines[ln - 1] for ln in b.content_lines if 0 < ln <= len(fr.lines)]


def branch_has_code(fr: FileResult, b: Branch) -> bool:
    in_block = False
    for ln in b.content_lines:
        s = fr.lines[ln - 1] if 0 < ln <= len(fr.lines) else ""
        # a block comment spanning lines: crude but sufficient for emptiness
        if in_block:
            if "*/" in s:
                s = s.split("*/", 1)[1]
                in_block = False
            else:
                continue
        if "/*" in s and "*/" not in s.split("/*", 1)[1]:
            s = s.split("/*", 1)[0]
            in_block = True
        if _strip_code(s):
            return True
    return False


def run(root: str, include_vendor: bool, cov: CoverageReport):
    files = iter_source_files(root)
    universe_hits: Dict[str, List[int]] = {}
    for rel in files:
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f.read().split("\n"), 1):
                if UNIVERSE_RE.match(line):
                    universe_hits.setdefault(rel, []).append(i)
    n_universe = sum(len(v) for v in universe_hits.values())
    cov.universe(n_universe, f"#if/#ifdef/#ifndef/#elif lines naming {MACRO} in "
                             f"{len(files)} files under src/ (independent regex pass)")
    cov.extra("files_scanned", len(files))
    cov.extra("files_with_guards", len(universe_hits))

    regions = []
    for rel in sorted(universe_hits):
        hits = universe_hits[rel]
        if not include_vendor and rel.startswith(VENDOR_PREFIXES):
            cov.drop("out-of-scope-vendored-tree", len(hits),
                     "src/xdk, src/system/stlport; --include-vendor to classify")
            continue
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
            text = f.read()
        fr = parse_file(rel, text)
        if fr.parse_error:
            cov.drop("file-unbalanced-conditionals", len(hits), fr.parse_error)
            continue
        attributed = set()
        if fr.comment_directives:
            cov.drop("inside-block-comment", len(fr.comment_directives))
            attributed.update(fr.comment_directives)
        fmap = build_function_map(fr.lines)
        code_total = sum(1 for s in fr.lines if _strip_code(s)
                         and not re.match(r"\s*#\s*(ifndef|define|endif|pragma\s+once)\b", s))
        for g in fr.groups:
            if not g.hx_directives:
                continue
            nd = len(g.hx_directives)
            attributed.update(g.hx_directives)
            err = resolve_group_worlds(g)
            if err:
                cov.drop(err, nd)
                continue
            live_n, live_p = enclosing_context_live(g)
            nat = [b for b in g.branches if b.native and live_n]
            ppc = [b for b in g.branches if b.ppc and live_p]
            nat_code = any(branch_has_code(fr, b) for b in nat)
            ppc_code = any(branch_has_code(fr, b) for b in ppc)
            raw_code = any(branch_has_code(fr, b) for b in g.branches if b.native or b.ppc)
            if not nat_code and not ppc_code and raw_code:
                # its code sits only in a branch whose build an ENCLOSING
                # HX_NATIVE guard already excluded: compiled by neither build
                cov.drop("dead-nested-under-opposite-guard", nd)
                continue
            if nat_code and ppc_code:
                shape = "REPLACES"
            elif nat_code:
                shape = "ADDS"
            elif ppc_code:
                shape = "REMOVES"
            else:
                cov.drop("no-code-in-either-build", nd,
                         "both branches empty or comment-only")
                continue
            cov.examine(nd)
            scope, enclosing, defs = classify_scope(fr, g, fmap, code_total)
            nat_lines = sum(len([1 for ln in b.content_lines
                                 if _strip_code(fr.lines[ln - 1])]) for b in nat)
            ppc_lines = sum(len([1 for ln in b.content_lines
                                 if _strip_code(fr.lines[ln - 1])]) for b in ppc)
            outer = None
            p = g.parent
            while p is not None:
                if p.hx_directives:
                    outer = p.branches[0].line
                    break
                p = p.parent
            regions.append({
                "file": rel,
                "line": g.branches[0].line,
                "endif": g.endif_line,
                "directive": ("#" + g.branches[0].kind + " " + g.branches[0].rest.strip()).strip(),
                "shape": shape,
                "bucket": SHAPE_LETTER[shape],
                "scope": scope,
                "enclosing": enclosing,
                "native_defs": sorted(set(defs["native"])),
                "ppc_defs": sorted(set(defs["ppc"])),
                "native_code_lines": nat_lines,
                "ppc_code_lines": ppc_lines,
                "nested_in_guard_at": outer,
                "area": area_of(rel),
            })
        missing = sorted(set(hits) - attributed)
        if missing:
            # The parser lost these: the independent pass saw them, no group owns
            # them.  Not dropped on purpose -> they stay UNACCOUNTED (exit 4).
            cov.note(f"{rel}: parser did not attribute directive line(s) {missing[:5]}")
    # ---- split shadows: same function defined PPC-only in one region and
    #      native-only in another region of the same file ----
    by_file: Dict[str, List[dict]] = {}
    for r in regions:
        by_file.setdefault(r["file"], []).append(r)
    splits = []
    for rel in sorted(by_file):
        ppc_side: Dict[str, dict] = {}
        nat_side: Dict[str, dict] = {}
        for r in by_file[rel]:
            for n in r["ppc_defs"]:
                ppc_side.setdefault(n, r)
            for n in r["native_defs"]:
                nat_side.setdefault(n, r)
        for n in sorted(set(ppc_side) & set(nat_side)):
            rp, rn = ppc_side[n], nat_side[n]
            same = rp is rn
            splits.append({"file": rel, "function": n,
                           "ppc_region": rp["line"], "native_region": rn["line"],
                           "kind": "same-guard" if same else "split-pair"})
            for r in (rp, rn):
                r.setdefault("shadowed_functions", [])
                if n not in r["shadowed_functions"]:
                    r["shadowed_functions"].append(n)
    # ---- cross-file: native-only definition in one file, PPC-only definition
    #      of the same qualified name in ANOTHER file (a header's native block
    #      shadowing a .cpp's decomp block, or vice versa).  Unqualified names
    #      (no `::`) are skipped: `Init`/`Poll` collide across unrelated TUs.
    ppc_all: Dict[str, List[dict]] = {}
    nat_all: Dict[str, List[dict]] = {}
    for r in regions:
        for n in r["ppc_defs"]:
            ppc_all.setdefault(n, []).append(r)
        for n in r["native_defs"]:
            nat_all.setdefault(n, []).append(r)
    for n in sorted(set(ppc_all) & set(nat_all)):
        if "::" not in n:
            continue
        for rp in ppc_all[n]:
            for rn in nat_all[n]:
                if rp["file"] == rn["file"]:
                    continue
                splits.append({"file": rp["file"] + " | " + rn["file"], "function": n,
                               "ppc_region": f"{rp['file']}:{rp['line']}",
                               "native_region": f"{rn['file']}:{rn['line']}",
                               "kind": "cross-file"})
                for r in (rp, rn):
                    r.setdefault("shadowed_functions", [])
                    if n not in r["shadowed_functions"]:
                        r["shadowed_functions"].append(n)
    for r in regions:
        r["bucket_d"] = r["scope"] in ("file-scope-defs", "whole-file")
        r.setdefault("shadowed_functions", [])
        r["shadowed_functions"].sort()
    return regions, splits


# Drops that mean "the PARSER could not handle this", as opposed to a
# deliberate scope choice.  Counted (so the denominator balances) but never
# allowed to exit 0: a directive the parser choked on is a region nobody read.
PARSER_DROPS = ("file-unbalanced-conditionals", "condition-unparseable",
                "condition-too-many-atoms", "condition-branch-in-both-worlds-differently")


def verdict(cov: CoverageReport) -> int:
    """cov.emit()'s exit code, escalated to EXIT_UNACCOUNTED on any parser drop."""
    code = cov.emit()
    bad = {k: v for k, v in cov.as_dict()["dropped"].items() if k in PARSER_DROPS}
    if code == 0 and bad:
        print(f"PARSER DROPS {bad}: these guard directives were counted but never "
              f"classified -- not a clean census", file=sys.stderr)
        return EXIT_UNACCOUNTED
    return code


AREA_PRIORITY = ("hamobj", "game", "char", "flow", "meta", "meta_ham", "synth",
                 "rndobj", "obj", "math", "utl", "world", "gesture", "ui")


def area_of(rel: str) -> str:
    parts = rel.split("/")
    if len(parts) >= 3 and parts[1] in ("system", "lazer"):
        return parts[2]
    return parts[1] if len(parts) > 1 else rel


def fmt_region(r: dict) -> str:
    ctx = r["enclosing"] or ""
    if r["scope"] != "in-function":
        bits = []
        if r["native_defs"]:
            bits.append("native{" + ",".join(r["native_defs"][:6])
                        + (",..." if len(r["native_defs"]) > 6 else "") + "}")
        if r["ppc_defs"]:
            bits.append("ppc{" + ",".join(r["ppc_defs"][:6])
                        + (",..." if len(r["ppc_defs"]) > 6 else "") + "}")
        ctx = " ".join(bits)
    sh = (" SHADOWS[" + ",".join(r["shadowed_functions"]) + "]") if r["shadowed_functions"] else ""
    return (f"  ({r['bucket']}) {r['file']}:{r['line']}-{r['endif']}  "
            f"{r['scope']:<16} n={r['native_code_lines']:<4} p={r['ppc_code_lines']:<4} "
            f"{ctx}{sh}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=REPO, help="tree to scan (default: this repo)")
    ap.add_argument("--include-vendor", action="store_true",
                    help="classify src/xdk and src/system/stlport too")
    ap.add_argument("--list", default="triage",
                    choices=("none", "triage", "all", "a", "b", "c", "d"),
                    help="DISPLAY filter only (every region is classified and counted "
                         "regardless): triage = (a)+(c)+split shadows [default]")
    ap.add_argument("--find", default=None,
                    help="print every region whose enclosing/defined functions contain "
                         "this substring (control lookup)")
    ap.add_argument("--json", default=None, help="write regions + splits + coverage here")
    add_coverage_args(ap)
    args = ap.parse_args()

    cov = CoverageReport("native_shadow_audit", args=args)
    cov.require_examined("an HX_NATIVE shadow inventory that classified no region")
    cov.note(f"only {MACRO} is modelled; every other macro is assumed equal in both builds")
    cov.note("build-level shadows (src files the native CMake does not compile) are "
             "invisible to a preprocessor scan")
    root = os.path.abspath(args.root)
    if not os.path.isdir(os.path.join(root, "src")):
        print(f"no src/ under {root}", file=sys.stderr)
        cov.universe_unknown("no src/ directory to scan")
        cov.emit()
        return EXIT_NO_INPUT
    regions, splits = run(root, args.include_vendor, cov)

    by_shape: Dict[str, int] = {}
    by_scope: Dict[str, int] = {}
    for r in regions:
        by_shape[r["shape"]] = by_shape.get(r["shape"], 0) + 1
        by_scope[r["scope"]] = by_scope.get(r["scope"], 0) + 1
    n_d = sum(1 for r in regions if r["bucket_d"])
    print(f"native_shadow_audit: {len(regions)} HX_NATIVE regions classified "
          f"(from {cov.as_dict()['universe']} guard directives; see coverage block)")
    for s in ("REPLACES", "ADDS", "REMOVES"):
        print(f"  ({SHAPE_LETTER[s]}) {s:<9}: {by_shape.get(s, 0)}")
    print(f"  (d) file-scope-defs + whole-file (overlay on a/b/c): {n_d}")
    print("  by scope: " + ", ".join(f"{k}={by_scope[k]}" for k in sorted(by_scope)))
    n_split = sum(1 for s in splits if s["kind"] == "split-pair")
    n_same = sum(1 for s in splits if s["kind"] == "same-guard")
    n_cross = sum(1 for s in splits if s["kind"] == "cross-file")
    print(f"  function shadows (one name, a native-only AND a PPC-only definition): "
          f"{len(splits)}  (one guard: {n_same}; two guards, one file: {n_split}; "
          f"cross-file: {n_cross})")
    by_area: Dict[str, Dict[str, int]] = {}
    for r in regions:
        a = by_area.setdefault(r["area"], {"a": 0, "b": 0, "c": 0})
        a[r["bucket"]] += 1
    print("  by area (a/b/c): " + "  ".join(
        f"{k}={v['a']}/{v['b']}/{v['c']}" for k, v in sorted(by_area.items())))

    if args.find:
        print(f"\n== --find {args.find!r} ==")
        hit = 0
        for r in regions:
            names = [r["enclosing"] or ""] + r["native_defs"] + r["ppc_defs"]
            if any(args.find in n for n in names):
                print(fmt_region(r))
                hit += 1
        for s in splits:
            if args.find in s["function"]:
                print(f"  SPLIT SHADOW {s['file']}::{s['function']}  ppc@{s['ppc_region']} "
                      f"native@{s['native_region']} ({s['kind']})")
                hit += 1
        print(f"  {hit} match(es)")

    if args.list != "none":
        sel = []
        for r in regions:
            if args.list == "all" or \
               (args.list == "triage" and (r["bucket"] in ("a", "c") or r["shadowed_functions"])) or \
               (args.list == "d" and r["bucket_d"]) or \
               args.list == r["bucket"]:
                sel.append(r)
        print(f"\n== regions ({args.list}): {len(sel)} of {len(regions)} "
              f"(display filter; all {len(regions)} are counted above) ==")
        for r in sel:
            print(fmt_region(r))
        if args.list in ("triage", "all", "d"):
            print(f"\n== function shadows: {len(splits)} ==")
            for s in splits:
                print(f"  {s['kind']:<10} {s['file']}::{s['function']}  "
                      f"ppc-region@{s['ppc_region']} native-region@{s['native_region']}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"regions": regions, "function_shadows": splits,
                       "_coverage": cov.as_dict()}, f, indent=1, sort_keys=True)
    sys.stdout.flush()
    return verdict(cov)


if __name__ == "__main__":
    sys.exit(main())
