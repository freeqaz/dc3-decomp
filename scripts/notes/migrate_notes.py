#!/usr/bin/env python3
"""Move lane-note comment walls out of src/ into per-unit sidecars, verbatim.

WHAT A LANE NOTE IS
===================
A *block* is a maximal run of whole-line `//` comments (not inside a `/* */`
comment, not a preprocessor continuation line).  It is a lane note when its
text matches `find_refutations.NOTE_RE` -- the same definition the lookup tool
greps for, imported, not copied -- AND carries a lane tag (`w21-z`,
`w-this-adjust`): untagged prose that merely says "inert" or "MEASURED" is
documentation and stays.  Leading paragraphs of a block (separated by an
empty `//` line) that do NOT match stay in the source: they are usually the
real documentation a lane appended its history to.  Blocks shorter than
`--min-lines` (default 5) after that split stay too -- replacing a 3-line note
with a 2-line pointer buys nothing.

WHERE IT GOES
=============
`src/<path>.<ext>` -> `docs/decomp/notes/<path>.md`, the pilot's format (merge
e2cb7e6c5): one ``## `<mangled symbol>` `` section per function, one ```text
fence per original block, each prefixed with where it sat and its original line
range.  The symbol is resolved from the function definition the block sits
inside or directly above (brace-matched over comment/string-blanked code, plus
the BEGIN_*/END_* object macros), matched against report.json's demangled names
for that unit (qualified name, then parameter count, then parameter types).
Anything that cannot be pinned to one symbol says so in the section text; a
block above no definition goes to ``## `(file-level)` `` with the nearest
following definition recorded.  No block is ever dropped: the run self-checks
that every moved line is present in the sidecar fences and that the new source
is the old source with exactly those ranges replaced.

WHAT STAYS IN THE SOURCE
========================
At most four lines, all derived from the block's own text:

    // Lane history (37 lines moved): docs/decomp/notes/system/rndobj/Utl.md
    // > <one sentence quoted VERBATIM from the block> (w7-bs)
    // > Verdicts: NOT A BUG (w-this-adjust); do not retry (w21-z)

The quoted sentence is chosen by keyword (load-bearing / do not / residual /
mechanism / "the image ... we") and must fit in two lines; speculative
sentences (hypothesis, likely, open question, '?') are never quoted.  If no
sentence qualifies, only the pointer line remains -- nothing is paraphrased.
Verdict phrases ("NOT A BUG", "(not a) behaviour bug", "do not retry/re-open")
are copied with their negation and the lane tag that precedes them.

The `// Lane history ...: docs/decomp/notes/` line is the stub marker: a block
holding one is already migrated, and the stub (marker + following `// >`
lines) is never re-moved.  That is what makes a re-run a no-op.

USAGE
=====
    scripts/notes/migrate_notes.py --dry-run src/system/rndobj/Utl.cpp
    scripts/notes/migrate_notes.py src/system/rndobj/*.cpp
    scripts/notes/migrate_notes.py --check src/system/rndobj/Utl.cpp   # exit 1 if a wall remains

Exit codes: 0 ok (or nothing to do)   1 --check found a wall / a refused block
            2 could not run (bad path, self-check failed: nothing written)
"""

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import textwrap
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from find_refutations import NOTE_RE, parse_sidecar  # noqa: E402

VERSION = "373307D9"
MIN_LINES = 5
WIDTH = 90
MARKER_RE = re.compile(r"^\s*//.*\bLane history\b.*docs/decomp/notes/")
STUB_CONT_RE = re.compile(r"^\s*// > ")
LINE_COMMENT_RE = re.compile(r"^\s*//")
DIRECTIVE_RE = re.compile(r"clang-format\s+(?:on|off)|NOLINT|IWYU pragma|@formatter")
LANE_TAG_RE = re.compile(r"\bw(?:\d{1,3})?-[a-z0-9][a-z0-9-]*[a-z0-9]\b|\bw\d{1,3}-[a-z0-9]\b")
# A block must carry a lane tag to be moved at all.  NOTE_RE alone also fires on
# ordinary documentation ("the backslash here is load-bearing and MEASURED",
# "byte-identical to ..."), and moving that out of the source would be exactly
# the over-reach this tool must not do.  Measured over src/ on 2026-11-13:
# 1,252 of 21,157 candidate lines sit in untagged blocks; they stay.
LANE_HISTORY_RE = re.compile(r"\bw\d{1,3}-[a-z0-9]+|\bw-[a-z][a-z0-9-]+")
PILOT_SYMBOLS = {
    "?GetCam@CamShot@@QAAPAVRndCam@@XZ",
    "?SetDefaultShot@HollaBackMinigame@@QAAXXZ",
    "?Copy@RndMatAnim@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z",
    "?Poll@JoypadClient@@AAAXXZ",
    "?Poll@SpeechMgr@@QAAXXZ",
}
FILE_LEVEL = "(file-level)"

SIDECAR_INTRO = """# Lane notes: `{src}`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \\`<mangled symbol>\\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.
"""


# --------------------------------------------------------------------------- lexing

def lex(lines):
    """Per line: (code_only_text, starts_in_block_comment, is_preprocessor).

    Comments and string/char literal contents become spaces, so braces and
    parens in them never count.  Preprocessor lines (and their `\\`
    continuations) are blanked whole.
    """
    out = []
    in_block = False
    pp_cont = False
    for ln in lines:
        start_block = in_block
        is_pp = pp_cont or (not in_block and ln.lstrip().startswith("#"))
        code = []
        i, n = 0, len(ln)
        state = "B" if in_block else "N"
        while i < n:
            c = ln[i]
            if state == "B":
                if ln.startswith("*/", i):
                    state = "N"
                    code.append("  ")
                    i += 2
                    continue
                code.append(" ")
            elif state in ('"', "'"):
                if c == "\\":
                    code.append("  ")
                    i += 2
                    continue
                if c == state:
                    state = "N"
                    code.append(c)
                else:
                    code.append(" ")
            else:
                if ln.startswith("//", i):
                    code.append(" " * (n - i))
                    break
                if ln.startswith("/*", i):
                    state = "B"
                    code.append("  ")
                    i += 2
                    continue
                if c in ('"', "'"):
                    state = c
                code.append(c)
            i += 1
        in_block = state == "B"
        pp_cont = is_pp and ln.rstrip().endswith("\\")
        text = "".join(code)
        out.append(("" if is_pp else text, start_block, is_pp))
    return out


def comment_blocks(lines, lx):
    """[(start, end)] 0-based inclusive runs of whole-line `//` comments."""
    res, run = [], []
    for i, ln in enumerate(lines):
        _, in_block, is_pp = lx[i]
        ok = (LINE_COMMENT_RE.match(ln) and not in_block and not is_pp
              and not (i > 0 and lines[i - 1].rstrip().endswith("\\")))
        if ok:
            run.append(i)
        else:
            if run:
                res.append((run[0], run[-1]))
            run = []
    if run:
        res.append((run[0], run[-1]))
    return res


# --------------------------------------------------------------------------- definitions

MACRO_FUNCS = {
    "HANDLERS": ("Handle", "DataArray *_msg, bool _warn"),
    "CUSTOM_HANDLERS": ("Handle", "DataArray *_msg, bool _warn"),
    "PROPSYNCS": ("SyncProperty", "DataNode &_val, DataArray *_prop, int _i, PropOp _op"),
    "CUSTOM_PROPSYNC": (None, "{T} &o, DataNode &_val, DataArray *_prop, int _i, PropOp _op"),
    "SAVES": ("Save", "BinStream &bs"),
    "LOADS": ("Load", "BinStream &bs"),
    "COPYS": ("Copy", "const Hmx::Object *o, Hmx::Object::CopyType ty"),
}
BEGIN_RE = re.compile(r"^\s*BEGIN_(%s)\s*\(\s*(.+?)\s*\)\s*$" % "|".join(MACRO_FUNCS))
CONTROL = {"if", "for", "while", "switch", "catch", "return", "sizeof", "do", "else",
           "try", "__declspec", "defined", "__pragma", "decltype", "alignof"}
TRAIL_OK = re.compile(r"^(?:\s*(?:const|volatile|override|throw\s*\([^)]*\)|noexcept|"
                      r"__\w+(?:\s*\([^)]*\))?))*\s*$")
QUAL_RE = re.compile(
    r"((?:[A-Za-z_]\w*(?:<[^;{}()<>]*(?:<[^;{}()<>]*>[^;{}()<>]*)*>)?\s*::\s*)*"
    r"(?:operator\s*(?:\(\s*\)|[^\s(\w]+|\s\w[\w\s:*&<>]*)|~?[A-Za-z_]\w*))\s*$"
)


class Def:
    def __init__(self, name, params, sig, open_, close, kind):
        self.name, self.params, self.sig = name, params, sig
        self.open, self.close, self.kind = open_, close, kind

    def __repr__(self):
        return f"Def({self.name}, {self.sig}-{self.close})"


def _paren_groups(s):
    out, depth, start = [], 0, None
    for i, c in enumerate(s):
        if c == "(":
            if depth == 0:
                start = i
            depth += 1
        elif c == ")" and depth:
            depth -= 1
            if depth == 0:
                out.append((start, i))
    return out


def _func_shape(head):
    """(qualified_name, params, name_offset) if `head {` opens a function body."""
    for o, c in _paren_groups(head):
        trail = head[c + 1:]
        t = trail.strip()
        if not (TRAIL_OK.match(trail) or (t.startswith(":") and not t.startswith("::"))):
            continue
        pre = head[:o]
        m = QUAL_RE.search(pre)
        if not m:
            continue
        name = re.sub(r"\s+", "", m.group(1)) if "operator" not in m.group(1) else \
            re.sub(r"\s+", " ", m.group(1)).strip()
        if name == "DEF_DATA_FUNC":  # obj/DataFunc.h: DataNode name(DataArray *array)
            return head[o + 1:c].strip(), "DataArray *array", m.start(1)
        last = re.split(r"::", name)[-1].lstrip("~")
        if last in CONTROL or not last:
            return None
        before = pre[: m.start(1)]
        if re.search(r"(?<![=!<>])=(?!=)", before) or re.search(r"\breturn\b", before):
            return None
        return name, head[o + 1:c], m.start(1)
    return None


def find_defs(lines, lx):
    """All function definitions: brace-bodied ones plus BEGIN_x/END_x macro bodies."""
    nlines = len(lines)
    starts, text = [], []
    pos = 0
    for code, _, _ in lx:
        starts.append(pos)
        text.append(code)
        pos += len(code) + 1
    flat = "\n".join(text)

    def line_of(p):
        lo, hi = 0, nlines - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if starts[mid] <= p:
                lo = mid
            else:
                hi = mid - 1
        return lo

    # macro bodies first -- their braces are invisible, so the scanner below
    # must treat every `{` inside one as a plain block
    macro_regions, defs = [], []
    i = 0
    while i < nlines:
        m = BEGIN_RE.match(lx[i][0])
        if m:
            kind, arg = m.group(1), m.group(2)
            end_re = re.compile(r"^\s*END_%s\b" % kind)
            j = i + 1
            while j < nlines and not end_re.match(lx[j][0]):
                j += 1
            if j < nlines:
                meth, params = MACRO_FUNCS[kind]
                tname = re.sub(r"\s+", "", arg)
                name = "PropSync" if meth is None else f"{tname}::{meth}"
                defs.append(Def(name, params.format(T=tname), i, i, j, "macro:BEGIN_" + kind))
                macro_regions.append((i, j))
                i = j + 1
                continue
        i += 1

    def in_macro(ln):
        return any(a <= ln <= b for a, b in macro_regions)

    scopes = []  # (kind, name, open_line, close_line)
    stack = []   # (kind, name, open_line)
    head_start = 0
    p, n = 0, len(flat)
    while p < n:
        c = flat[p]
        if c == ";":
            head_start = p + 1
        elif c == "}":
            if stack:
                k, nm, ol = stack.pop()
                scopes.append((k, nm, ol, line_of(p)))
            head_start = p + 1
        elif c == "{":
            head = flat[head_start:p]
            hl = line_of(p)
            body_kind = None
            fs = None if in_macro(hl) else _func_shape(head)
            clean = re.sub(r"^\s*(?:(?:public|private|protected)\s*:\s*)+", "", head)
            if fs:
                body_kind = "func"
            elif in_macro(hl):
                body_kind = "block"
            elif re.search(r"\bnamespace\b", clean):
                nm = re.search(r"\bnamespace\s+(\w+)", clean)
                stack.append(("namespace", nm.group(1) if nm else "", hl))
                head_start = p + 1
                p += 1
                continue
            elif re.search(r'\bextern\s*"', clean):
                stack.append(("extern", "", hl))
                head_start = p + 1
                p += 1
                continue
            elif re.search(r"\b(class|struct|union)\b", clean) and "=" not in clean:
                nm = re.findall(r"\b(?:class|struct|union)\s+(?:__declspec\s*\([^)]*\)\s*)?(\w+)",
                                clean)
                stack.append(("class", nm[-1] if nm else "", hl))
                head_start = p + 1
                p += 1
                continue
            else:
                body_kind = "block"
            # skip the whole body (function or initializer/enum/other block)
            depth, q = 1, p + 1
            while q < n and depth:
                if flat[q] == "{":
                    depth += 1
                elif flat[q] == "}":
                    depth -= 1
                q += 1
            if body_kind == "func":
                name, params, noff = fs
                prefix = [nm for k, nm, _ in stack if k in ("namespace", "class") and nm]
                full = "::".join(prefix + [name])
                # signature start: first line of the head carrying code that is
                # not a lone macro-ish token (END_SAVES etc. leave no `;`)
                sig = line_of(head_start + noff)
                off = head_start
                for seg in head[:noff].split("\n"):
                    t = seg.strip()
                    if t and not re.fullmatch(r"[A-Z][A-Z0-9_]*(\s*\([^()]*\))?\s*;?", t):
                        sig = min(sig, line_of(off))
                        break
                    off += len(seg) + 1
                defs.append(Def(full, params, sig, hl, line_of(q - 1), "func"))
            head_start = q
            p = q
            continue
        p += 1
    # enclosing namespace/class for macro bodies
    for d in defs:
        if d.kind.startswith("macro:"):
            pre = [nm for k, nm, a, b in scopes if k in ("namespace", "class") and nm
                   and a <= d.sig <= b]
            if pre and d.name != "PropSync":
                d.name = "::".join(pre + [d.name])
    defs.sort(key=lambda d: d.sig)
    return defs


# --------------------------------------------------------------------------- symbols

def strip_templates(s):
    out, depth = [], 0
    for c in s:
        if c == "<":
            depth += 1
        elif c == ">" and depth:
            depth -= 1
        elif depth == 0:
            out.append(c)
    return "".join(out)


def split_params(s):
    s = s.strip()
    if s in ("", "void"):
        return []
    out, depth, cur = [], 0, []
    for c in s:
        if c in "<([":
            depth += 1
        elif c in ">)]" and depth:
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    out.append("".join(cur).strip())
    return out


def parse_demangled(d):
    """`public: virtual void __cdecl A::B::f(class X const *,int)` -> ('A::B::f', [params])."""
    s = d.strip()
    s = re.sub(r"\s+(const|volatile)\s*$", "", s)
    if not s.endswith(")"):
        return None, []
    depth, i = 0, len(s) - 1
    while i >= 0:
        if s[i] == ")":
            depth += 1
        elif s[i] == "(":
            depth -= 1
            if depth == 0:
                break
        i -= 1
    if i <= 0:
        return None, []
    pre, params = s[:i], s[i + 1:-1]
    if "operator" in pre:
        k = pre.rfind("operator")
        op = "operator" + pre[k + 8:].strip()
        pre = strip_templates(pre[:k])
        tok = pre.split()[-1] if pre.split() else ""
        name = (tok if tok.endswith("::") else "") + op
    else:
        pre = strip_templates(pre)
        toks = pre.split()
        name = toks[-1] if toks else ""
    return name.replace("`anonymous namespace'", ""), split_params(params)


def norm_name(n):
    n = strip_templates(n or "")
    n = re.sub(r"\s+", "", n)
    return re.sub(r"^(::)+", "", n.replace("::::", "::"))


class SymbolIndex:
    def __init__(self, root):
        root = Path(root)
        rep = json.loads((root / "build" / VERSION / "report.json").read_text())
        cfg = json.loads((root / "objdiff.json").read_text())
        u2src = {u["name"]: (u.get("metadata") or {}).get("source_path") for u in cfg["units"]}
        self.by_src, self.all = {}, []
        for u in rep["units"]:
            src = u2src.get(u["name"])
            for f in u.get("functions", []):
                dm = (f.get("metadata") or {}).get("demangled_name") or ""
                qn, ps = parse_demangled(dm)
                if not qn:
                    continue
                ent = (f["name"], norm_name(qn), ps, dm)
                self.all.append(ent)
                if src:
                    self.by_src.setdefault(src, []).append(ent)

    def resolve(self, src_rel, d):
        """(symbol_or_None, note).  note is '' for a clean unique match."""
        want = norm_name(d.name)
        pool = self.by_src.get(src_rel)
        scope = "unit"
        if pool is None or src_rel.endswith((".h", ".hpp", ".inl")):
            pool, scope = self.all, "whole report (header)"

        def match(ent):
            return ent[1] == want or ent[1].endswith("::" + want) or \
                (want.count("::") and want.endswith("::" + ent[1]))
        cands = {}
        for e in pool:
            if match(e):
                cands.setdefault(e[0], e)
        cands = list(cands.values())
        if not cands:
            return None, f"no function named `{d.name}` in report.json ({scope})"
        if len(cands) == 1:
            return cands[0][0], ""
        sp = split_params(re.sub(r"=[^,]*", "", d.params))
        same = [e for e in cands if len(e[2]) == len(sp)]
        if len(same) == 1:
            return same[0][0], ""
        pool2 = same or cands

        def score(e):
            sc = 0
            for a, b in zip(sp, e[2]):
                ids = [t for t in re.findall(r"[A-Za-z_]\w*", strip_templates(a))
                       if t not in ("const", "unsigned", "signed", "volatile", "struct",
                                    "class", "enum")]
                if len(ids) > 1:
                    ids = ids[:-1]  # drop the parameter name
                sc += sum(1 for t in ids if re.search(r"\b%s\b" % re.escape(t), b))
            return sc
        ranked = sorted(pool2, key=score, reverse=True)
        if len(ranked) == 1 or score(ranked[0]) > score(ranked[1]):
            return ranked[0][0], ""
        others = ", ".join(f"`{e[0]}`" for e in ranked[1:4])
        if len(ranked) > 3:
            return None, (f"ambiguous: {len(ranked)} overloads of `{d.name}` fit equally "
                          f"(e.g. `{ranked[0][0]}`, {others})")
        return ranked[0][0], (f"ambiguous: {len(ranked)} overloads of `{d.name}` fit equally; "
                              f"filed under the first, also possible: {others}")


# --------------------------------------------------------------------------- blocks

class Block:
    def __init__(self, start, end, keep_upto, lines):
        self.start, self.end = start, end          # whole block (0-based, inclusive)
        self.mstart = keep_upto                    # first moved line
        self.text = "\n".join(lines[self.mstart:end + 1])
        self.nmoved = end - self.mstart + 1
        self.where = ""
        self.symbol = None
        self.qualname = None
        self.note = ""
        self.refused = ""


def _paragraphs(blines):
    """Split block lines into paragraphs at empty `//` lines -> [(lo, hi)] (rel idx)."""
    out, lo = [], 0
    for i, ln in enumerate(blines):
        if ln.strip() in ("//", "///"):
            if i > lo:
                out.append((lo, i - 1))
            lo = i + 1
    if lo < len(blines):
        out.append((lo, len(blines) - 1))
    return out


def movable_blocks(lines, lx, min_lines=MIN_LINES):
    """Lane-note blocks to move (and ones refused), as Block objects."""
    res = []
    for s, e in comment_blocks(lines, lx):
        bl = lines[s:e + 1]
        if any(MARKER_RE.match(l) for l in bl):
            continue  # already migrated (stub present)
        if not NOTE_RE.search("\n".join(bl)) or not LANE_HISTORY_RE.search("\n".join(bl)):
            continue  # untagged prose ("inert", "MEASURED" ...) is documentation: leave it
        keep = s
        for lo, hi in _paragraphs(bl):
            if NOTE_RE.search("\n".join(bl[lo:hi + 1])):
                keep = s + lo
                break
        if e - keep + 1 < min_lines:
            continue
        b = Block(s, e, keep, lines)
        if DIRECTIVE_RE.search(b.text):
            b.refused = "carries a formatter/lint directive line"
        res.append(b)
    return res


def attribute(blocks, defs):
    for b in blocks:
        inside = [d for d in defs if d.sig <= b.mstart and b.end <= d.close]
        if inside:
            d = max(inside, key=lambda d: d.sig)  # innermost
            b.where = "Inside the body" if b.mstart > d.open else "Inside the signature"
            b.def_ = d
            continue
        nxt = [d for d in defs if d.sig > b.end]
        b.def_ = None
        if nxt:
            d = min(nxt, key=lambda d: d.sig)
            between = range(b.end + 1, d.sig)
            if all(not b._lines[k].strip() or LINE_COMMENT_RE.match(b._lines[k])
                   for k in between):
                b.where = "Above the definition"
                b.def_ = d
                continue
            b.nearest = d
        else:
            b.nearest = None
        b.where = "File-level"


# --------------------------------------------------------------------------- stubs

SPECULATIVE = re.compile(r"\?|\bhypothes|\bprobabl|\blikely\b|\bmaybe\b|\bmight\b|\bguess|"
                         r"not measured|\bunclear\b|open question|\bTODO\b|\bperhaps\b",
                         re.IGNORECASE)
SCORES = [
    (6, re.compile(r"load-bearing|\bdo not\b|\bdon't\b|\bmust\b|\bnever\b", re.I)),
    (4, re.compile(r"\bresidual is\b|whole residual|\bis the residual\b|\bRESIDUAL\b|"
                   r"\bmechanism\b|measured cause|root cause|\bthe cause\b|caused by", re.I)),
    (3, re.compile(r"\bimage\b.*\b(?:we|ours|our)\b", re.I)),
]
VERDICT_RE = re.compile(
    r"(?:\bnot\s+(?:a\s+)?)?\bbehaviou?r(?:al)?\s+bug\b|\bnot\s+a\s+(?:real\s+)?bug\b|"
    r"\bdo\s+not\s+(?:re-?open|retry|re-?try)\b|\bdon't\s+(?:re-?open|retry)\b",
    re.IGNORECASE)
CONTEXTUAL = re.compile(
    r"^(?:It|Its|This|That|These|Those|So|But|And|Then|Also|Both|Here|Same|Such|They|"
    r"Which|Or|Thus|Hence|Again|Still|Instead|Otherwise|\d)\b|\b(?:above|below|"
    r"previous|earlier|the note|this note|that note|as before|see\b)", re.IGNORECASE)
ABBREV = re.compile(r"\b(e\.g|i\.e|vs|cf|etc|approx|incl)\.")


def _plain(text, mark_entries=False):
    """Comment text without `//`, whitespace collapsed.  With `mark_entries`, a
    \x01 precedes every line that opens a new lane entry, so a sentence never
    runs from one lane's note into the next."""
    body = [re.sub(r"^\s*///?\s?", "", l) for l in text.splitlines()]
    if mark_entries:
        body = [("\x01" + l) if re.match(r"\(?(?:Lane\s+)?w(?:\d{1,3})?-[a-z0-9]", l) else l
                for l in body]
    return re.sub(r"[ \t]+", " ", " ".join(body)).strip()


def _tags_before(plain, pos):
    tags = [m for m in LANE_TAG_RE.finditer(plain) if m.start() <= pos]
    return tags[-1].group(0) if tags else None


def sentences(plain):
    prot = ABBREV.sub(lambda m: m.group(0).replace(".", "\x00"), plain)
    out, pos = [], 0
    for m in re.finditer(r"(?<=[.!])\s+(?=[A-Z`(\[\"'])|\s*\x01", prot):
        out.append((pos, prot[pos:m.start()].replace("\x00", ".")))
        pos = m.end()
    out.append((pos, prot[pos:].replace("\x00", ".")))
    return [(p, s.strip()) for p, s in out if s.strip()]


def _wrap(s, indent, nlines):
    width = WIDTH - len(indent) - len("// > ")
    w = textwrap.wrap(s, width=width, break_long_words=False, break_on_hyphens=False)
    return w if 0 < len(w) <= nlines and all(len(x) <= width for x in w) else None


def make_stub(b, indent, sidecar_rel):
    plain = _plain(b.text, mark_entries=True)
    out = [f"{indent}// Lane history ({b.nmoved} lines moved): {sidecar_rel}"]
    verdicts = []
    for m in VERDICT_RE.finditer(plain):
        tag = _tags_before(plain, m.start())
        v = re.sub(r"\s+", " ", m.group(0)) + (f" ({tag})" if tag else "")
        if v not in verdicts:
            verdicts.append(v)
    vline = None
    if verdicts:
        width = WIDTH - len(indent) - len("// > Verdicts: ")
        shown = []
        for v in verdicts:
            rest = len(verdicts) - len(shown) - 1
            cand = "; ".join(shown + [v]) + (f"; +{rest} more" if rest else "")
            if len(cand) > width:
                break
            shown.append(v)
        more = len(verdicts) - len(shown)
        vline = "Verdicts: " + "; ".join(shown) + (f"; +{more} more" if more else "")
        if not shown:
            vline = f"Verdicts: {len(verdicts)} recorded in the history"
    budget = 2 if vline else 3
    best = None
    sents = sentences(plain)
    for k, (pos, s) in enumerate(sents):
        bare = re.sub(r"^\(?(?:Lane\s+)?w(?:\d{1,3})?-[a-z0-9-]+\)?(?:\s*\([^)]*\))?:?\s*", "", s)
        if SPECULATIVE.search(s) or CONTEXTUAL.search(bare) or len(s) < 50 \
                or re.search(r"(?:\b\d+|:)\.?$", s):
            continue
        sc = 0
        if k == 0 and not re.match(r"\(?(?:Lane\s+)?w(?:\d{1,3})?-|MEASURED|REFUTED", s):
            sc = 5  # the block opens with prose, not a lane tag: usually what the code is
        for pts, rx in SCORES:
            if rx.search(s):
                sc = max(sc, pts)
        if sc < 3:
            continue
        sc += k / max(1, len(sents))  # later = newer, as a tie-break
        tag = _tags_before(plain, pos + len(s))
        q = s + (f" ({tag})" if tag and tag not in s else "")
        wrapped = _wrap(q, indent, budget)
        if wrapped and (best is None or sc > best[0]):
            best = (sc, wrapped)
    if best:
        out += [f"{indent}// > {w}" for w in best[1]]
    if vline:
        out.append(f"{indent}// > {vline}")
    return out


# --------------------------------------------------------------------------- sidecar

def sidecar_rel(src_rel):
    p = Path(src_rel)
    parts = p.parts[1:] if p.parts and p.parts[0] == "src" else p.parts
    return str(Path("docs/decomp/notes", *parts).with_suffix(".md"))


def render_sections(src_rel, blocks, head, today):
    """Markdown for the moved blocks, grouped by symbol in source order."""
    order, groups = [], {}
    for b in blocks:
        key = b.symbol or (f"{b.qualname}()" if b.qualname else FILE_LEVEL)
        if key not in groups:
            order.append(key)
            groups[key] = []
        groups[key].append(b)
    out = []
    for key in order:
        bs = groups[key]
        out.append(f"\n## `{key}`\n")
        first = bs[0]
        who = f"`{first.qualname}`" if first.qualname else "Not attributed to one function"
        out.append(f"{who} -- moved verbatim from `{src_rel}` on {today} by "
                   f"scripts/notes/migrate_notes.py (line numbers as of {head})\n")
        notes = []
        for b in bs:
            if b.note and b.note not in notes:
                notes.append(b.note)
        for n in notes:
            out.append(f"Attribution: {n}\n")
        for b in bs:
            extra = ""
            if b.where == "File-level":
                nd = getattr(b, "nearest", None)
                extra = (f"; nearest following definition: `{nd.name}`" if nd
                         else "; no definition follows it")
            out.append(f"{b.where} (was lines {b.mstart + 1}-{b.end + 1}{extra}):\n")
            out.append("```text\n" + b.text + "\n```\n")
    return "\n".join(out)


# --------------------------------------------------------------------------- driver

def plan_file(root, src_rel, index, min_lines, exclude):
    path = Path(root) / src_rel
    raw = path.read_text(encoding="utf-8")
    lines = raw.split("\n")
    lx = lex(lines)
    blocks = movable_blocks(lines, lx, min_lines)
    defs = find_defs(lines, lx)
    for b in blocks:
        b._lines = lines
    attribute(blocks, defs)
    for b in blocks:
        d = b.def_
        if d is not None:
            b.qualname = d.name
            b.symbol, b.note = index.resolve(src_rel, d)
            if d.kind.startswith("macro:") and not b.note:
                b.note = (f"definition is macro-generated ({d.kind[6:]}); symbol matched "
                          f"by the name the macro expands to")
            if b.symbol is None:
                b.note = b.note + "; filed under the source name"
        if b.symbol in exclude:
            b.refused = "pilot function (section already curated by hand)"
    return raw, lines, [b for b in blocks if not b.refused], [b for b in blocks if b.refused]


def apply_file(root, src_rel, raw, lines, moves, head, today):
    side_rel = sidecar_rel(src_rel)
    new_lines = list(lines)
    for b in sorted(moves, key=lambda b: b.mstart, reverse=True):
        indent = re.match(r"^\s*", lines[b.mstart]).group(0)
        new_lines[b.mstart:b.end + 1] = make_stub(b, indent, side_rel)
    side_path = Path(root) / side_rel
    old_side = side_path.read_text(encoding="utf-8") if side_path.exists() else \
        SIDECAR_INTRO.format(src=src_rel)
    new_side = old_side.rstrip("\n") + "\n" + render_sections(src_rel, moves, head, today)

    # ---- self-checks: nothing lost, nothing else touched ----------------------
    parsed = {}
    tmp = side_path.with_name(side_path.name + ".tmp-check")
    tmp.parent.mkdir(parents=True, exist_ok=True)
    tmp.write_text(new_side, encoding="utf-8")
    try:
        parsed = parse_sidecar(str(tmp))
    finally:
        tmp.unlink()
    for b in moves:
        key = b.symbol or (f"{b.qualname}()" if b.qualname else FILE_LEVEL)
        if not any(t == b.text for _, _, t in parsed.get(key, [])):
            raise SystemExit(f"SELF-CHECK FAILED: block at {src_rel}:{b.mstart + 1} not "
                             f"found verbatim under `{key}` -- nothing written")
    rebuilt, i = [], 0
    for b in sorted(moves, key=lambda b: b.mstart):
        rebuilt += lines[i:b.mstart]
        i = b.end + 1
    rebuilt += lines[i:]
    # remove stub lines from the new text and compare with old-minus-moved
    out_wo_stubs = [l for l in new_lines if not (MARKER_RE.match(l) or STUB_CONT_RE.match(l))]
    old_wo = [l for l in rebuilt if not (MARKER_RE.match(l) or STUB_CONT_RE.match(l))]
    if out_wo_stubs != old_wo:
        raise SystemExit(f"SELF-CHECK FAILED: {src_rel} would change outside the moved "
                         f"blocks -- nothing written")
    return "\n".join(new_lines), new_side, side_path


def git_head(root):
    try:
        return subprocess.run(["git", "-C", str(root), "rev-parse", "--short=9", "HEAD"],
                              capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return "unknown"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="+", help="source files (relative to the repo root)")
    ap.add_argument("--repo", help="tree root (default: two levels above this script's invocation path)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any file still carries a movable lane-note block")
    ap.add_argument("--min-lines", type=int, default=MIN_LINES)
    ap.add_argument("--exclude-symbol", action="append", default=[])
    ap.add_argument("--no-pilot-exclusions", action="store_true")
    ap.add_argument("--date", default=datetime.date.today().isoformat())
    ap.add_argument("--json", help="write a per-file summary here")
    ap.add_argument("--show", action="store_true",
                    help="print the source diff and each block's attribution")
    a = ap.parse_args(argv)
    if a.repo:
        root = Path(a.repo).resolve()
    else:
        sys.path.insert(0, str(HERE.parent))
        from project_root import project_root
        root = project_root(__file__, None, levels=2, warn=False)
    exclude = set(a.exclude_symbol) | (set() if a.no_pilot_exclusions else PILOT_SYMBOLS)
    rels = []
    for f in a.files:
        p = Path(f)
        p = p if p.is_absolute() else (Path.cwd() / p)
        try:
            rels.append(str(p.resolve().relative_to(root)))
        except ValueError:
            print(f"CANNOT RUN: {f} is not under {root}", file=sys.stderr)
            return 2
        if not p.exists():
            print(f"CANNOT RUN: no such file {f}", file=sys.stderr)
            return 2

    if a.check:
        bad = 0
        for rel in rels:
            lines = (root / rel).read_text(encoding="utf-8").split("\n")
            for b in movable_blocks(lines, lex(lines), a.min_lines):
                bad += 1
                print(f"{rel}:{b.mstart + 1}-{b.end + 1}: {b.nmoved}-line lane-note block "
                      f"in the source; move it to {sidecar_rel(rel)}"
                      f" (scripts/notes/migrate_notes.py {rel})")
        print(f"checked {len(rels)} file(s): {bad} lane-note wall(s) of >= {a.min_lines} lines")
        return 1 if bad else 0

    index = SymbolIndex(root)
    head = git_head(root)
    summary, any_refused = [], False
    for rel in rels:
        raw, lines, moves, refused = plan_file(root, rel, index, a.min_lines, exclude)
        rec = {"file": rel, "moved_blocks": len(moves),
               "moved_lines": sum(b.nmoved for b in moves),
               "refused": [(b.mstart + 1, b.refused) for b in refused],
               "unresolved": [(b.mstart + 1, b.qualname) for b in moves
                              if b.qualname and not b.symbol],
               "file_level": [b.mstart + 1 for b in moves if b.where == "File-level"],
               "ambiguous": [(b.mstart + 1, b.note) for b in moves if "ambiguous" in b.note]}
        for b in refused:
            any_refused = True
            print(f"REFUSED {rel}:{b.mstart + 1}: {b.refused}")
        if moves:
            new_src, new_side, side_path = apply_file(root, rel, raw, lines, moves, head, a.date)
            rec["stub_lines"] = sum(len(make_stub(b, "", sidecar_rel(rel))) for b in moves)
            rec["sidecar"] = str(side_path.relative_to(root))
            if a.show:
                import difflib
                for b in moves:
                    print(f"  {rel}:{b.mstart + 1}-{b.end + 1} {b.where} -> "
                          f"{b.symbol or b.qualname or FILE_LEVEL}"
                          f"{'  [' + b.note + ']' if b.note else ''}")
                sys.stdout.writelines(difflib.unified_diff(
                    raw.splitlines(True), new_src.splitlines(True), rel, rel, n=1))
            if not a.dry_run:
                side_path.parent.mkdir(parents=True, exist_ok=True)
                side_path.write_text(new_side, encoding="utf-8")
                (root / rel).write_text(new_src, encoding="utf-8")
        verb = "would move" if a.dry_run else "moved"
        print(f"{rel}: {verb} {rec['moved_blocks']} block(s) / {rec['moved_lines']} lines"
              f"{'; ' + str(len(rec['file_level'])) + ' file-level' if rec['file_level'] else ''}"
              f"{'; ' + str(len(rec['unresolved'])) + ' unresolved' if rec['unresolved'] else ''}"
              f"{'; ' + str(len(rec['ambiguous'])) + ' ambiguous' if rec['ambiguous'] else ''}")
        summary.append(rec)
    if a.json:
        Path(a.json).write_text(json.dumps(summary, indent=1))
    return 1 if any_refused else 0


if __name__ == "__main__":
    sys.exit(main())
