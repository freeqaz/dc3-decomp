#!/usr/bin/env python3
"""pointer_disp_scan.py — find WRONG FIELDS reached through a pointer that is
NOT `this`: displacement-only diffs off a node, an iterator, a loaded field, an
argument, a return value or a global.

WHY THIS EXISTS
===============
Taxonomy class 1 (wrong struct field / wrong offset, 121 historical bugs;
`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`) is only
partly `this`-relative.  `this_offset_scan.py` covers a displacement off a
register PROVEN to hold `this + K`.  Everything reached one pointer hop further
out -- `mNextShotIt->prev`, `node->next`, `arg->mFoo`, `GetBar()->mBaz` -- was
covered by nothing.

The motivating bug: `HamCamShot::SetPreFrame` rewinds a camera-shot list.  The
image steps the iterator through `Node::prev` (`lwz r11, 0x18(r11)`); ours
stepped through `Node::next` (`lwz r11, 0x14(r11)`), because
`ObjPtrList::iterator` had no `operator--` and the correct spelling did not
compile.  ONE `diff_arg` row, same opcode, same registers, different
displacement, off a NODE pointer -- at 97.34%, unnoticed.  See
`docs/decomp/patterns/missing-container-operator-forces-wrong-spelling.md`.

WHAT IT DOES
============
For every function defined in both the dtk-carved target object and ours:

  1. ALIGN the two instruction streams on a register- and displacement-blind
     key (primary opcode + extended opcode), so a row pairs "the same
     instruction" regardless of register allocation or immediates.
  2. KEEP a row when both sides are the same D/DS-form load/store (or an
     `addi` whose result is USED AS AN ADDRESS) and the displacement differs.
     A row carrying a relocation on either side is a global-identity question
     for the relocation-name tools and is counted, not examined.
  3. EVALUATE each side SYMBOLICALLY with one linear pass: every GPR holds
     `root + K` where root is `arg(n)` (incoming r3..r10), `sp`, a resolved
     global address, `ret(callee)`, or `ld(kind, root', K')` -- "the word loaded
     from root'+K'".  A load of a node's link is therefore
     `ld(w, ld(w, arg3, 0x2b4), 0x18)`, and the SAME root on both sides is what
     "the base registers correspond (modulo register permutation)" means here:
     correspondence is decided by VALUE, never by register number.
  4. NORMALISE ANCHORS by construction: the effective address is `root + K + D`,
     so `addi rX,rY,0x10; lwz 0x8(rX)` and `lwz 0x18(rY)` are the same address
     and the row lands in `anchor-normalised` (counted, not reported).  Globals
     are resolved to an absolute address through `config/373307D9/symbols.txt`
     (a `lbl_<addr>` name parses directly), which is the
     anchor-displacement-false-wrong-global normalisation: target anchors on
     `gNumHeaps` and reaches `gInitted` by displacement, we anchor on
     `gInitted` -- one absolute address, not a finding.
  5. BUCKET every kept row -- exactly one bucket, and the row ledger must
     balance (asserted).

WHY LINEAR, AND WHY IT IS SYMMETRIC
-----------------------------------
`this_offset_scan`'s lesson: a flow-insensitive proof that is ASYMMETRIC between
the two sides invents findings (whether a register is reused later is a
register-allocation choice).  This scanner never asks "is this register
reused"; it evaluates both streams with the same linear rule, and its only
cross-side question is "do the two BASE VALUES agree".  A linear pass is
unsound at a join (a register reassigned around a backward edge reads as its
fall-through value), but it is unsound IDENTICALLY on both sides, so it can
mis-type a base, never invent a difference between two identical streams.
After an unconditional branch the VOLATILE registers are reset to unknown
(the next instruction is reachable only by a jump); the non-volatiles carry.

WHAT IT CANNOT SEE  (read this before calling the class exhausted)
-----------------------------------------------------------------
* **A byte-identical function carries no displacement difference** -- proof of
  absence, counted as `byte-identical`.
* **A mislabelled LAYOUT.**  Both sides emit the same displacement for a field
  our header has at the wrong offset.  Needs RB2 DWARF / Ghidra, not a diff.
* **An upstream wrong pointer.**  If we loaded the node from the wrong field
  (`ld(this,0x14)` vs `ld(this,0x18)`), every row off that node lands in
  `base-provenance-differs`, and the wrong load itself is `this_offset_scan`'s
  row.  That bucket is a LEAD list, not a finding list.
* **Indexed addressing** (`lwzx`) has no displacement; not in scope.
* **Misalignment.**  The alignment is difflib over opcode keys.  Where code
  shape differs a lot, same-opcode rows can pair unrelated instructions; they
  then disagree on the base value and land in `base-provenance-differs`, which
  is why that bucket is large and is not a finding list.
* **TUs that do not build**: every target function in a unit with no built
  object is COUNTED in the denominator as `unit-has-no-built-object`.
* **Field NAMES are annotation.**  A base is typed only when its root is a
  `this` field with a pointer/iterator type in `struct_db.sqlite`, a typed
  argument, or a link of a known node layout; `this` bias is assumed 0 for the
  typing step (never for the finding).  An untyped candidate is still a
  candidate.

USAGE
-----
    python3 scripts/analysis/pointer_disp_scan.py --selftest
    python3 scripts/analysis/pointer_disp_scan.py
    python3 scripts/analysis/pointer_disp_scan.py --explain '<mangled>'
    python3 scripts/analysis/pointer_disp_scan.py --json out.json --show-leads 50

Exit code is `CoverageReport.emit()`'s.
"""
from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402
import this_offset_scan as tos  # noqa: E402  (decoder + symbol classification)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OBJDIFF = os.path.join(REPO, "objdiff.json")
DEFAULT_REPORT = os.path.join(REPO, "build", "373307D9", "report.json")
DEFAULT_SYMBOLS = os.path.join(REPO, "config", "373307D9", "symbols.txt")

REL_PPC_REL24 = 6
REL_PPC_REFHI = 16
REL_PPC_REFLO = 17
REL_PPC_PAIR = 18

OP_ADDI, OP_ADDIS, OP_RLWINM = 14, 15, 21
OP_LMW, OP_STMW = 46, 47
INT_LOADS = frozenset({32, 33, 34, 35, 40, 41, 42, 43})
STORES = frozenset({36, 37, 38, 39, 44, 45, 52, 53, 54, 55})
UPDATE_FORMS = frozenset(o for o, (_k, u) in tos.DFORM_MEM.items() if u)
VOLATILE = sorted(tos.VOLATILE_GPRS)
MAX_DEPTH = 5
PLACEHOLDER = ("fn_", "lbl_", "merged_", "sub_")
ABS = "abs"


# --------------------------------------------------------------------------- #
# COFF: bodies plus EVERY relocation, keyed by word index
# --------------------------------------------------------------------------- #
def function_bodies(path):
    """name -> (words, relocs) where relocs maps word index -> (type, symbol).

    Same slicing and EH-prefix trim as this_offset_scan.function_bodies; the
    difference is that every relocation is kept, not only the call targets,
    because a relocated displacement is a different question (and REFHI/REFLO
    carry the global a base register was built from).
    """
    data, secs, syms, by_index = tos.read_coff(path)
    if data is None:
        return {}
    by_sec = {s["idx"]: s for s in secs}
    per_sec = {}
    for s in syms:
        if s["section"] <= 0 or s["type"] != tos.IMAGE_SYM_DTYPE_FUNCTION:
            continue
        sec = by_sec.get(s["section"])
        if sec is None or not sec["name"].startswith(".text"):
            continue
        per_sec.setdefault(s["section"], []).append(s)
    relmaps = {}
    for sec in secs:
        if not sec["nrel"] or not sec["name"].startswith(".text"):
            continue
        m = {}
        for r in range(sec["nrel"]):
            ro = sec["relptr"] + r * 10
            if ro + 10 > len(data):
                break
            va, si, ty = struct.unpack_from("<IIH", data, ro)
            if ty == REL_PPC_PAIR:
                continue
            m[va & ~3] = (ty, by_index.get(si, {}).get("name", "?"))
        relmaps[sec["idx"]] = m
    out = {}
    for sec_idx in sorted(per_sec):
        group = per_sec[sec_idx]
        sec = by_sec[sec_idx]
        group.sort(key=lambda x: (x["value"], x["name"]))
        rmap = relmaps.get(sec_idx, {})
        for n, s in enumerate(group):
            start = s["value"]
            end = group[n + 1]["value"] if n + 1 < len(group) else sec["rawsize"]
            if end <= start or start % 4:
                continue
            if end - start >= 16:
                a = rmap.get(end - 8, (0, ""))[1]
                b = rmap.get(end - 4, (0, ""))[1]
                blob = data[sec["rawptr"] + end - 8: sec["rawptr"] + end]
                if blob == b"\x00" * 8 and (a.startswith("__CxxFrameHandler")
                                            or b.startswith("__ehfuncinfo$")):
                    end -= 8
            raw = data[sec["rawptr"] + start: sec["rawptr"] + end]
            words = [struct.unpack_from(">I", raw, o)[0]
                     for o in range(0, len(raw) - 3, 4)]
            relocs = {}
            for k in range(len(words)):
                r = rmap.get(start + k * 4)
                if r:
                    relocs[k] = r
            if s["name"] not in out or len(words) > len(out[s["name"]][0]):
                out[s["name"]] = (words, relocs)
    return out


def load_symtab(path):
    """symbols.txt name -> absolute address.  Empty when absent (declared)."""
    out = {}
    if not path or not os.path.exists(path):
        return out
    rx = re.compile(r"^(.+?) = [^:]+:0x([0-9A-Fa-f]+);")
    with open(path) as f:
        for line in f:
            m = rx.match(line)
            if m:
                out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def resolve_global(name, symtab):
    """A global's absolute address, or None.  `lbl_<hex>` parses directly."""
    if name.startswith("lbl_"):
        try:
            return int(name[4:], 16)
        except ValueError:
            return None
    return symtab.get(name)


# --------------------------------------------------------------------------- #
# Symbolic evaluation
# --------------------------------------------------------------------------- #
# A value is (root, off).  Roots:
#   ("arg", n)                 incoming GPR n (r3..r10)
#   ("sp",)                    the stack pointer (any frame-relative address)
#   ("abs",)                   a global resolved through symbols.txt; its
#                              absolute ADDRESS is carried in the offset
#   ("sym", name)              a global symbols.txt does not name
#   ("ret", callee)            r3 after a call
#   ("ld", kind, root, off)    the value loaded from root+off
#   ("const",)                 a small integer used as an address (rare)
#   ("r2",) / ("r13",)         the fixed registers
def _depth(root):
    d = 0
    while root and root[0] == "ld":
        d += 1
        root = root[2]
    return d


def _is_sp(root):
    while root and root[0] == "ld":
        root = root[2]
    return root == ("sp",)


def evaluate(words, relocs, symtab):
    """One linear pass.  Returns per-instruction records:

      pre[i]    : {reg: value} snapshot restricted to the base register of a
                  memory op / addi source (only what a row needs)
      access[i] : (kind, is_store, root, ea_off) for every D/DS-form memory op
                  whose base value is known
      addi[i]   : {"src": value|None, "val": value|None, "uses": [...],
                   "passed": bool}  -- uses are (kind, root, off) of every
                  dereference whose base came from this addi (through copies
                  and further addis), passed is True if it reached a call as an
                  argument or was stored as a value
    """
    regs = {1: (("sp",), 0), 2: (("r2",), 0), 13: (("r13",), 0)}
    for n in range(3, 11):
        regs[n] = (("arg", n), 0)
    origin = {}                       # reg -> tuple of addi indices
    base_val = {}
    access = {}
    addi = {}

    def setreg(r, v, org=()):
        if r == 0 and v is not None:
            # r0 is a real register as a destination; only its use as a BASE
            # means literal zero, which is handled at the use site.
            pass
        if v is None:
            regs.pop(r, None)
        else:
            if _depth(v[0]) > MAX_DEPTH:
                regs.pop(r, None)
            else:
                regs[r] = v
        if org:
            origin[r] = org
        else:
            origin.pop(r, None)

    prev_uncond = False
    for i, w in enumerate(words):
        o = tos._op(w)
        rel = relocs.get(i)
        callee = rel[1] if rel and rel[0] == REL_PPC_REL24 else None
        helper = bool(callee and callee.startswith(tos.HELPER_PREFIXES))

        if prev_uncond:
            for r in VOLATILE:
                regs.pop(r, None)
                origin.pop(r, None)
            regs.pop(0, None)
        prev_uncond = False

        is_call = (not helper) and (w & 1) and o in (tos.OP_BRANCH, tos.OP_BC,
                                                     tos.OP_BCLR_BCCTR)
        if is_call:
            for areg in range(3, 11):
                for a in origin.get(areg, ()):
                    addi[a]["passed"] = True
                    v = regs.get(areg)
                    if v is not None:
                        addi[a]["escapes"].append(v)
                    else:
                        addi[a]["opaque_escape"] = True

        # -- memory ops ------------------------------------------------------ #
        if (o in tos.DFORM_MEM and o not in (OP_LMW, OP_STMW)) or o in tos.DSFORM_MEM:
            if o in tos.DFORM_MEM:
                kind = tos.DFORM_MEM[o][0]
                disp = tos._simm(w)
            else:
                kind = tos.DSFORM_MEM[o]
                disp = tos._ds(w)
            ra = tos._a(w)
            bv = None if ra == 0 else regs.get(ra)
            base_val[i] = bv
            is_store = o in STORES or (o == 62)
            if bv is not None:
                access[i] = (kind, is_store, bv[0], bv[1] + disp)
                for a in origin.get(ra, ()):
                    addi[a]["uses"].append((kind, bv[0], bv[1] + disp))
            if is_store:
                rs = tos._d(w)
                for a in origin.get(rs, ()):
                    addi[a]["passed"] = True
                    v = regs.get(rs)
                    if v is not None:
                        addi[a]["escapes"].append(v)
                    else:
                        addi[a]["opaque_escape"] = True
            # register effects
            if o in INT_LOADS or (o == 58 and (w & 3) in (0, 1, 2)):
                rd = tos._d(w)
                nv = None
                if bv is not None and rel is None:
                    nv = (("ld", kind, bv[0], bv[1] + disp), 0)
                if o in UPDATE_FORMS or (o == 58 and (w & 3) == 1):
                    setreg(ra, None if bv is None else (bv[0], bv[1] + disp))
                setreg(rd, nv)
            elif o in UPDATE_FORMS or (o == 62 and (w & 3) == 1):
                setreg(ra, None if bv is None else (bv[0], bv[1] + disp))
            continue

        # -- address formation --------------------------------------------- #
        if o == OP_ADDI:
            rd, ra, imm = tos._d(w), tos._a(w), tos._simm(w)
            if ra == 0:                                   # li
                setreg(rd, None)
                continue
            src = regs.get(ra)
            base_val[i] = src
            if rel and rel[0] == REL_PPC_REFLO:
                addr = resolve_global(rel[1], symtab)
                # a resolved global folds its address into the OFFSET, so two
                # anchors on neighbouring globals compare as one address space
                val = ((ABS,), addr + imm) if addr is not None else (("sym", rel[1]), imm)
            else:
                val = None if src is None else (src[0], src[1] + imm)
            addi[i] = {"src": src, "val": val, "uses": [], "passed": False,
                       "escapes": [], "opaque_escape": False, "reloc": rel}
            setreg(rd, val, origin.get(ra, ()) + (i,))
            continue
        if o == OP_ADDIS:
            rd, ra = tos._d(w), tos._a(w)
            if rel and rel[0] == REL_PPC_REFHI:
                addr = resolve_global(rel[1], symtab)
                setreg(rd, ((ABS,), addr) if addr is not None else (("sym", rel[1]), 0))
            else:
                setreg(rd, None)
            continue
        if o == tos.OP_X and ((w >> 1) & 0x3FF) == tos.X_OR and tos._d(w) == tos._b(w):
            rs, ra = tos._d(w), tos._a(w)             # mr ra, rs
            setreg(ra, regs.get(rs), origin.get(rs, ()))
            continue
        if o == OP_RLWINM:
            rs, ra = tos._d(w), tos._a(w)
            sh, mb, me = tos._b(w), (w >> 6) & 31, (w >> 1) & 31
            # `clrrwi rA,rS,n` with n <= 2 (and the n == 0 no-op the ObjPtrList
            # iterator step emits) keeps a pointer's value for addressing.
            if sh == 0 and mb == 0 and me >= 29:
                setreg(ra, regs.get(rs), origin.get(rs, ()))
                continue

        # -- everything else ------------------------------------------------- #
        if is_call:
            for r in VOLATILE:
                regs.pop(r, None)
                origin.pop(r, None)
            regs.pop(0, None)
            name = callee if callee else ("<indirect>" if o == tos.OP_BCLR_BCCTR
                                          else "<unnamed>")
            regs[3] = (("ret", name), 0)
            continue
        if helper:
            continue
        for r in tos._defs_gpr(w):
            regs.pop(r, None)
            origin.pop(r, None)
        # unconditional branch (b, not bl) or blr/bctr: the next instruction is
        # reachable only by a jump
        if (o == tos.OP_BRANCH and not (w & 1)) or \
           (o == tos.OP_BCLR_BCCTR and not (w & 1) and ((w >> 21) & 0x14) == 0x14):
            prev_uncond = True
    return base_val, access, addi


def root_eq(a, b):
    """True / False / None (cannot tell).  Placeholder names are wildcards."""
    if a is None or b is None:
        return None
    if a[0] != b[0]:
        if {a[0], b[0]} <= {"abs", "sym"}:
            return None                  # one side's global did not resolve
        return False
    t = a[0]
    if t == "ld":
        if a[1] != b[1] or a[3] != b[3]:
            return False
        return root_eq(a[2], b[2])
    if t == "ret":
        if a[1] == b[1] or a[1].startswith(PLACEHOLDER) or b[1].startswith(PLACEHOLDER):
            return True
        return False
    if t == "sym":
        return True if a[1] == b[1] else None
    return a == b


def fmt_root(root):
    if root is None:
        return "?"
    t = root[0]
    if t == "arg":
        return f"arg{root[1]}"
    if t == "sp":
        return "sp"
    if t == "abs":
        return "@abs"
    if t == "sym":
        return f"@{root[1]}"
    if t == "ret":
        return f"ret({root[1][:40]})"
    if t == "ld":
        return f"*{root[1]}({fmt_root(root[2])}{root[3]:+#x})"
    return t


# --------------------------------------------------------------------------- #
# Alignment
# --------------------------------------------------------------------------- #
def align_key(w):
    o = tos._op(w)
    if o in (31, 19, 59, 63):
        return (o, (w >> 1) & 0x3FF)
    if o == 30:
        return (o, (w >> 2) & 0x7)
    if o in (58, 62):
        return (o, w & 3)
    return (o,)


def aligned_pairs(tw, bw):
    sm = difflib.SequenceMatcher(None, [align_key(w) for w in tw],
                                 [align_key(w) for w in bw], autojunk=False)
    for a, b, n in sm.get_matching_blocks():
        for k in range(n):
            yield a + k, b + k


# --------------------------------------------------------------------------- #
# Typing (annotation only)
# --------------------------------------------------------------------------- #
# Template node layouts struct_db.sqlite does not carry (it has no template
# instantiations).  PPC layouts, from src/system/obj/Object.h and stlport.
KNOWN_LAYOUTS = {
    "ObjPtrList::Node": {0x0: ("__vptr", ""), 0x4: ("ObjRef::next", "ObjRef *"),
                         0x8: ("ObjRef::prev", "ObjRef *"),
                         0xc: ("mObject", "T *"), 0x10: ("mOwner", "ObjRefOwner *"),
                         0x14: ("next", "ObjPtrList::Node *"),
                         0x18: ("prev", "ObjPtrList::Node *")},
    "ObjPtrList": {0x0: ("__vptr", ""), 0x4: ("mSize", "int"),
                   0x8: ("mNodes", "ObjPtrList::Node *"),
                   0xc: ("mOwner", "ObjRefOwner *"), 0x10: ("mListMode", "int")},
    "ObjRef": {0x0: ("__vptr", ""), 0x4: ("next", "ObjRef *"),
               0x8: ("prev", "ObjRef *")},
    "std::_List_node": {0x0: ("_M_next", "std::_List_node *"),
                        0x4: ("_M_prev", "std::_List_node *"),
                        0x8: ("_M_data", "")},
    "std::_Rb_tree_node": {0x0: ("_M_color", "int"),
                           0x4: ("_M_parent", "std::_Rb_tree_node *"),
                           0x8: ("_M_left", "std::_Rb_tree_node *"),
                           0xc: ("_M_right", "std::_Rb_tree_node *"),
                           0x10: ("_M_value_field", "")},
}
LINK_PAIRS = [{"next", "prev"}, {"left", "right"}, {"first", "last"},
              {"head", "tail"}, {"begin", "end"}, {"parent", "child"}]


def _norm_field(n):
    n = n.rsplit("::", 1)[-1]
    for p in ("_M_", "m_", "m"):
        if n.startswith(p) and len(n) > len(p) and (p != "m" or n[len(p)].isupper()):
            n = n[len(p):]
            break
    return n.lower()


def is_link_pair(a, b):
    s = {_norm_field(a), _norm_field(b)}
    return any(s == p for p in LINK_PAIRS)


def type_to_layout(ts, layouts):
    """A member/param type string -> the layout its POINTEE (or itself, for an
    embedded object) resolves to, and whether it is a pointer."""
    if not ts:
        return None, False
    t = ts.replace("const ", "").replace("class ", "").replace("struct ", "").strip()
    if "ObjPtrList<" in t and (t.endswith("::iterator") or "Node" in t.rsplit(">", 1)[-1]):
        return "ObjPtrList::Node", True
    if t.startswith("ObjPtrList<"):
        return "ObjPtrList", t.endswith("*") or t.endswith("&")
    if ("std::list<" in t or t.startswith("list<")) and "iterator" in t:
        return "std::_List_node", True
    if ("std::map<" in t or "std::set<" in t) and "iterator" in t:
        return "std::_Rb_tree_node", True
    if t in KNOWN_LAYOUTS:
        return t, False
    ptr = t.endswith("*") or t.endswith("&")
    base = t.rstrip("*& ").strip()
    if "<" in base:
        return None, ptr
    for cand in (base, base.rsplit("::", 1)[-1]):
        if cand in layouts:
            return cand, ptr
    return None, ptr


def field_at(layout, off, layouts, depth=0):
    """(dotted name, type string) of the member covering `off`, descending
    into embedded objects.  None when the layout has no member there."""
    lay = KNOWN_LAYOUTS.get(layout) or layouts.get(layout)
    if not lay or depth > 4:
        return None
    if off in lay:
        name, ts = lay[off]
        # an embedded object whose own offset-0 member is what is touched
        sub, ptr = type_to_layout(ts, layouts)
        if sub and not ptr and sub != layout:
            inner = field_at(sub, 0, layouts, depth + 1)
            if inner:
                return (f"{name}.{inner[0]}", inner[1])
        return (name, ts)
    below = [k for k in lay if k <= off]
    if not below:
        return None
    k = max(below)
    name, ts = lay[k]
    sub, ptr = type_to_layout(ts, layouts)
    if sub and not ptr:
        inner = field_at(sub, off - k, layouts, depth + 1)
        if inner:
            return (f"{name}.{inner[0]}", inner[1])
    return None


def split_params(demangled):
    if not demangled or "(" not in demangled:
        return None
    s = demangled[demangled.index("(") + 1:]
    depth, cur, out = 0, "", []
    for ch in s:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            if depth == 0:
                break
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [p for p in out if p and p != "void"]


def type_of_root(root, ctx, layouts):
    """Layout name the root POINTS AT, or None."""
    if root is None:
        return None
    t = root[0]
    if t == "arg":
        n = root[1]
        if ctx["member"] and n == 3:
            return ctx["class"] if ctx["class"] in layouts else None
        params = ctx["params"]
        if params is None:
            return None
        idx = n - (4 if ctx["member"] else 3)
        if idx < 0 or idx >= len(params):
            return None
        if any(p in ("float", "double") for p in params[:idx + 1]):
            return None                  # FPR/GPR slot accounting: do not guess
        lay, ptr = type_to_layout(params[idx], layouts)
        return lay if ptr else None
    if t == "ld" and root[1] == "w":
        inner = type_of_root(root[2], ctx, layouts)
        if inner is None:
            return None
        f = field_at(inner, root[3], layouts)
        if f is None:
            return None
        lay, ptr = type_to_layout(f[1], layouts)
        if lay and (ptr or lay == "ObjPtrList::Node"):
            return lay
    return None


# --------------------------------------------------------------------------- #
# Per-function comparison
# --------------------------------------------------------------------------- #
BUCKETS = [
    # artifacts / other lanes -- counted, not reported as findings
    "relocated-displacement", "stack-frame", "this-relative",
    "anchor-normalised", "addi-not-an-address", "reordered",
    "contradicted-by-100pct",
    # leads -- reported on request, never findings
    "base-unknown", "base-provenance-differs", "permuted-unobserved",
    # candidates -- every one needs adjudication
    "cand-adjacent-links", "cand-wrong-field-typed", "cand-chain-step",
    "cand-novel-address", "cand-permuted",
]
CANDIDATE_BUCKETS = [b for b in BUCKETS if b.startswith("cand-")]


FPR_LOADS = frozenset({48, 49, 50, 51})
FPR_STORES = frozenset({52, 53, 54, 55})
REACH_BUDGET = 200000
POLY_OPS = frozenset({20, 21, 25, 28, 29, 30, 31})
POLY_CAP = 64
POLY_DEGREE = 8

# --------------------------------------------------------------------------- #
# Value-level equivalence: is a displacement row a SCHEDULING reorder?
#
# The first cut of this check was "both sides touch both addresses", the
# `this_offset_scan` multiplicity rule.  It is unsafe for this purpose: a real
# field SWAP -- `CharUpperTwist::Load`'s 3-cycle of bone references, a shipped
# class-7 bug -- also touches every address on both sides.  The second cut
# followed each loaded value to its FIRST CONSUMER and demanded the same
# consumer on both sides; it was sound but useless on float code, where the
# consumers are themselves reordered and regallocated (MakeScale: 11 rows, all
# scheduling, all reported).
#
# What decides it is VALUE FLOW: evaluate each side symbolically into
# hash-consed expression trees (loads as ld(kind, address), arithmetic as
# (op, operands) with commutative operands sorted), and collect every EFFECT
# -- a store to non-stack memory, a call with its fresh argument values, a
# compare, a return.  A row is a reorder iff every effect that CONSUMES its
# loaded value (or, for a store, the store effect itself) has an identical
# partner on the other side.  A swap whose values cross over changes the
# effects and stays a candidate; a reorder does not.
#
# Symmetric by construction (one evaluator, one intern table, both sides).
# Conservative where it is crude: an effect that differs for an UNRELATED
# reason (an unmatched call elsewhere in a 97% function) keeps a row a
# candidate rather than excusing it.
# --------------------------------------------------------------------------- #
COMMUTATIVE_59_63 = {21, 25}                   # fadd(s), fmul(s)
X_COMMUTATIVE = {266, 28, 444, 316, 235, 284, 476, 124, 10, 138}
X_LOGICAL = {28, 60, 444, 412, 316, 124, 476, 284, 24, 536, 792, 824, 26, 954,
             922, 986, 27, 539, 794, 826, 827, 58}
X_ARITH = {266, 40, 235, 75, 11, 491, 459, 104, 10, 138, 8, 136, 202, 234, 232,
           200, 233, 489, 457, 73, 9}
X_LOADS = {23: "w", 87: "b", 279: "h", 343: "h", 21: "d", 341: "w", 55: "w",
           119: "b", 311: "h", 535: "fs", 599: "fd", 567: "fs", 631: "fd"}
X_FLOAD = {535, 599, 567, 631}
X_STORES = {151: "w", 215: "b", 407: "h", 149: "d", 663: "fs", 727: "fd",
            983: "w", 183: "w", 247: "b", 439: "h", 695: "fs", 759: "fd"}
X_FSTORE = {663, 727, 983, 695, 759}


class Interner:
    """Hash-consing shared by BOTH sides of one comparison, so equal
    expressions get equal ids and effect multisets compare by id."""

    def __init__(self):
        self.ids = {}
        self.kids = []
        self.nodes = []

    def __call__(self, *node):
        i = self.ids.get(node)
        if i is None:
            i = len(self.kids)
            self.ids[node] = i
            self.kids.append(tuple(x for x in node[1:] if isinstance(x, Ref)))
            self.nodes.append(node)
        return Ref(i)

    def show(self, r, depth=4):
        node = self.nodes[r]
        if depth <= 0:
            return f"#{int(r)}"
        parts = []
        for x in node[1:]:
            if isinstance(x, Ref):
                parts.append(self.show(x, depth - 1))
            elif isinstance(x, int) and not isinstance(x, bool):
                parts.append(hex(x) if abs(x) > 9 else str(x))
            else:
                parts.append(str(x)[:48])
        return f"{node[0]}(" + ", ".join(parts) + ")"


class Ref(int):
    """An interned expression id (distinguishable from an immediate)."""


def effects_eval(words, relocs, symtab, E, sigs=None, ret_kind=None):
    """(effects, load_val, store_eff, addi_cons).

    effects    list[Ref]: every store to non-stack memory, call (with its fresh
               argument values), compare and return.  Calls carry an ORDINAL
               and loads/stores carry the EPOCH (calls so far), because call
               order is a side effect: a serializer that reads `&mA` then `&mB`
               is not the same program as one that reads `&mB` then `&mA`, and a
               multiset comparison would call them equal.
    load_val   {i: Ref}  the value a load at i produced
    store_eff  {i: Ref}  the effect a store at i produced
    addi_cons  {i: [("ld", j) | ("st", j) | ("call", Ref)]}  what the address an
               addi at i formed was USED for (through copies and further addis)
    """
    origin = {}
    addi_cons = {}
    epoch = 0
    sp = E("sp")
    g = {1: sp, 2: E("r2"), 13: E("r13")}
    for n in range(3, 11):
        g[n] = E("arg", n)
    f = {n: E("farg", n) for n in range(1, 14)}
    ctr = E("ctr0")
    stack = {}
    mem = {}                                  # non-stack EA Ref -> (kind, value)
    fresh_g, fresh_f = set(), set()
    effects, load_val, store_eff = [], {}, {}
    is_sp = {sp}

    def G(r):
        v = g.get(r)
        if v is None:
            v = E("undef-g", r)
            g[r] = v
        return v

    def F(r):
        v = f.get(r)
        if v is None:
            v = E("undef-f", r)
            f[r] = v
        return v

    offs = {}                                 # Ref -> (base Ref, int off)

    def add(x, k):
        b, o = offs.get(x, (x, 0))
        if o + k == 0:
            return b
        r = E("+", b, o + k)
        offs[r] = (b, o + k)
        if b in is_sp:
            is_sp.add(r)
        return r

    polys = {}                                # Ref -> {monomial: coef}

    # /fp:fast lets MSVC reassociate float arithmetic freely, so two
    # correct builds routinely compute a*b + c*d - e in different orders
    # (MakeScale, the Multiply/Invert family).  Normalise fadd/fsub/fmul/
    # fmadd/fmsub/fnmadd/fnmsub/fneg into a polynomial over leaf values:
    # a reassociation leaves it unchanged, a SWAP of two leaves does not.
    # Capped: past POLY_CAP monomials the op stays opaque (deterministic,
    # merely unnormalised).
    def poly_of(x):
        return polys.get(x) or {(x,): 1}

    def padd(p, q, sq):
        r = dict(p)
        for m, k in q.items():
            r[m] = r.get(m, 0) + sq * k
        return {m: k for m, k in r.items() if k}

    def pmul(p, q):
        if len(p) * len(q) > POLY_CAP:
            return None
        r = {}
        for m1, k1 in p.items():
            for m2, k2 in q.items():
                m = tuple(sorted(m1 + m2))
                if len(m) > POLY_DEGREE:
                    return None
                r[m] = r.get(m, 0) + k1 * k2
        return {m: k for m, k in r.items() if k}

    def mkpoly(p):
        if len(p) > POLY_CAP:
            p = None
        if p is None:
            return E("poly-overflow")
        if len(p) == 1:
            (m, k), = p.items()
            if k == 1 and len(m) == 1:
                return m[0]
        flat = []
        for m in sorted(p):
            flat.append(p[m])
            flat.append(len(m))
            flat.extend(m)
        r = E("poly", *flat)
        polys[r] = p
        return r

    lins = {}                                 # Ref -> (const, {term: coef})

    def lin_of(x):
        if x in lins:
            return lins[x]
        b, o = offs.get(x, (x, 0))
        return (o, {b: 1})

    def lin(c, terms):
        """Integer sums are exact mod 2**32, so `(size - p) + buf` and
        `(buf - p) + size` are the same value (DecompressChunk).  Canonicalise
        add/subf/neg into a sorted linear form.  NOT applied to floats."""
        terms = {t: k for t, k in terms.items() if k}
        if len(terms) == 1:
            (t, k), = terms.items()
            if k == 1:
                return add(t, c)
        r = E("lin", c, *[x for t in sorted(terms) for x in (t, terms[t])])
        lins[r] = (c, terms)
        return r

    def lin_combine(x, y, sy):
        cx, tx = lin_of(x)
        cy, ty = lin_of(y)
        t = dict(tx)
        for k, v in ty.items():
            t[k] = t.get(k, 0) + sy * v
        return lin(cx + sy * cy, t)

    def canon_arg(v):
        return E("spaddr") if v in is_sp else v

    def setg(r, v):
        g[r] = v
        if 3 <= r <= 10:
            fresh_g.add(r)

    def setf(r, v):
        f[r] = v
        if 1 <= r <= 13:
            fresh_f.add(r)

    def glob(rel, imm):
        addr = resolve_global(rel[1], symtab)
        if addr is None:
            return E("sym", rel[1], imm)
        return add(E("abs"), addr + imm)

    def call_args(callee=None):
        """The argument registers of a call, as values.

        With the callee's signature known (report.json's demangled names), the
        EXACT registers: slot k is r(3+k) for an integer/pointer and the next
        f-register for a float -- which still consumes GPR slot k (measured:
        `CharInterest::ComputeScore(V3&,V3&,V3&,float,int,bool)` reads its int
        from r8 and its bool from r9, with r7 skipped).  Without a signature
        (an indirect call, an external, a by-value return, varargs), EVERY
        freshly written volatile -- strict, so a scratch difference keeps a row
        a candidate rather than excusing it.
        """
        regs = None
        if callee in CRT_FLOAT_ARGS:
            regs = [("f", k) for k in range(1, CRT_FLOAT_ARGS[callee] + 1)]
        elif callee in CRT_GPR_ARGS:
            regs = [("g", 3 + k) for k in range(CRT_GPR_ARGS[callee])]
        elif callee and callee.startswith("??$MakeString@"):
            # MakeString takes a format and every argument BY REFERENCE: all
            # GPR, no float slot, so the fresh run from r3 is exactly its args
            regs = []
            r = 3
            while r <= 10 and r in fresh_g:
                regs.append(("g", r))
                r += 1
        elif callee and sigs:
            regs = arg_regs(sigs.get(callee))
        if regs is None:
            return (tuple((r, canon_arg(g[r])) for r in sorted(fresh_g) if r in g)
                    + tuple((100 + r, f[r]) for r in sorted(fresh_f) if r in f))
        out = []
        for cls, r in regs:
            if cls == "g":
                out.append((r, canon_arg(G(r))))
            else:
                out.append((100 + r, F(r)))
        return tuple(out)

    prev_uncond = False
    for i, w in enumerate(words):
        o = tos._op(w)
        rel = relocs.get(i)
        callee = rel[1] if rel and rel[0] == REL_PPC_REL24 else None
        if callee and callee.startswith(tos.HELPER_PREFIXES):
            continue
        if prev_uncond:
            for r in VOLATILE:
                g[r] = E("reset-g", r)
                origin.pop(r, None)
            prev_uncond = False
        D, A, B = tos._d(w), tos._a(w), tos._b(w)
        # any GPR this instruction writes loses its addi origin unless a
        # handler below re-establishes it (addi / mr / the no-op rlwinm), which
        # read the SOURCE's origin from before the pop
        org_a, org_d = origin.get(A, ()), origin.get(D, ())
        if not (o == tos.OP_BRANCH or o == tos.OP_BCLR_BCCTR):
            for r in tos._defs_gpr(w):
                origin.pop(r, None)

        # ---- D/DS-form memory ------------------------------------------ #
        if (o in tos.DFORM_MEM and o not in (OP_LMW, OP_STMW)) or o in tos.DSFORM_MEM:
            if o in tos.DFORM_MEM:
                kind, disp = tos.DFORM_MEM[o][0], tos._simm(w)
            else:
                kind, disp = tos.DSFORM_MEM[o], tos._ds(w)
            if rel and rel[0] == REL_PPC_REFLO:
                ea = glob(rel, disp)
            elif A == 0:
                ea = E("absaddr", disp)
            else:
                ea = add(G(A), disp)
            store = o in STORES or o == 62
            for a in origin.get(A, ()):
                addi_cons[a].append(("st" if store else "ld", i))
            if store:
                val = F(D) if o in FPR_STORES else G(D)
                if ea in is_sp:
                    stack[ea] = (kind, val)
                else:
                    e = E("st", epoch, kind, ea, canon_arg(val))
                    effects.append(e)
                    store_eff[i] = e
                    # store-to-load forwarding, the way MSVC does it: a later
                    # load of the SAME address sees this value.  A store through
                    # a DIFFERENT base might alias, so it evicts every entry
                    # not provably disjoint (same base, different offset).
                    # (InsertFakeArmPos: the image reloads elbow.z, we forward
                    # the shoulder.z it was just set from -- one value.)
                    root_ = offs.get(ea, (ea, 0))[0]
                    for k_ in [k_ for k_ in mem if offs.get(k_, (k_, 0))[0] != root_]:
                        del mem[k_]
                    mem[ea] = (kind, val)
                    if o not in FPR_STORES:
                        for a in origin.get(D, ()):
                            addi_cons[a].append(("st", i))
            else:
                if ea in is_sp:
                    # a stack slot is keyed by ADDRESS: an int spilled with
                    # `std` and reloaded with `lfd` for fcfid is one value
                    # moving between register files, not two
                    sk = stack.get(ea)
                    lo = None
                    if sk is None and kind == "w":
                        # the low word of a double spilled by `stfd` (fctiwz
                        # then `lwz 0x54(r1)` after `stfd 0x50(r1)`)
                        b_, o_ = offs.get(ea, (ea, 0))
                        hi = stack.get(add(b_, o_ - 4))
                        if hi is not None and hi[0] in ("fd", "d"):
                            lo = E("lo32", hi[1])
                    if lo is not None:
                        val = lo
                    elif sk is None:
                        val = E("stk", kind)
                    elif sk[0] == kind:
                        val = sk[1]
                    else:
                        val = E("cvt", sk[0], kind, sk[1])
                elif ea in mem and mem[ea][0] == kind:
                    val = mem[ea][1]
                else:
                    val = E("ld", epoch, kind, ea)
                load_val[i] = val
                if o in FPR_LOADS:
                    setf(D, val)
                else:
                    setg(D, val)
                    origin.pop(D, None)
            upd = (o in UPDATE_FORMS or (o in (58, 62) and (w & 3) == 1))
            if upd and A:
                g[A] = ea if not (rel and rel[0] == REL_PPC_REFLO) else ea
            continue
        if o in (OP_LMW, OP_STMW):
            if o == OP_LMW:
                for r in range(D, 32):
                    g[r] = E("lmw", r)
            continue

        # ---- integer D-form -------------------------------------------- #
        if o == OP_ADDI:
            if rel and rel[0] == REL_PPC_REFLO:
                setg(D, glob(rel, tos._simm(w)))
                origin.pop(D, None)
            elif A == 0:
                setg(D, E("imm", tos._simm(w)))
                origin.pop(D, None)
            else:
                setg(D, add(G(A), tos._simm(w)))
                addi_cons[i] = []
                origin[D] = org_a + (i,)
            continue
        if o == OP_ADDIS:
            if rel and rel[0] == REL_PPC_REFHI:
                setg(D, glob(rel, 0))
            elif A == 0:
                setg(D, E("imm", tos._simm(w) << 16))
            else:
                setg(D, add(G(A), tos._simm(w) << 16))
            continue
        if o in (7, 8, 12, 13):                        # mulli subfic addic addic.
            setg(D, E("dimm", o, G(A), tos._simm(w)))
            continue
        if o in (10, 11):                              # cmpli / cmpi
            effects.append(E("cmpi", o, G(A), w & 0xFFFF))
            continue
        # A RECORD form (`rlwinm.`, `andi.`, `and.` ...) compares its result
        # with zero into cr0: a branch condition, and therefore observable.
        if o in (24, 25, 26, 27, 28, 29):              # ori oris xori xoris andi andis
            if o == 24 and (w & 0xFFFF) == 0:
                g[A] = G(D)                            # nop / mr-like
            else:
                setg(A, E("limm", o, G(D), w & 0xFFFF))
                if o in (28, 29):
                    effects.append(E("cmp0", G(A)))
            continue
        if o == OP_RLWINM:
            sh, mb, me = B, (w >> 6) & 31, (w >> 1) & 31
            if sh == 0 and mb == 0 and me == 31:
                setg(A, G(D))
                if org_d:
                    origin[A] = org_d
            else:
                setg(A, E("rlwinm", G(D), sh, mb, me))
            if w & 1:
                effects.append(E("cmp0", G(A)))
            continue
        if o == 20:
            setg(A, E("rlwimi", G(D), G(A), B, (w >> 6) & 31, (w >> 1) & 31))
            if w & 1:
                effects.append(E("cmp0", G(A)))
            continue
        if o == 23:
            setg(A, E("rlwnm", G(D), G(B), (w >> 6) & 31, (w >> 1) & 31))
            if w & 1:
                effects.append(E("cmp0", G(A)))
            continue
        if o == 30:
            setg(A, E("rld", G(D), w & 0xFFFF))
            continue

        # ---- float --------------------------------------------------------- #
        if o in (59, 63):
            xo5 = (w >> 1) & 31
            C = (w >> 6) & 31
            if xo5 in POLY_OPS and not (o == 63 and xo5 in (16, 17)):
                a_, b_, c_ = F(A), F(B), F(C)
                pa, pb, pc = poly_of(a_), poly_of(b_), poly_of(c_)
                if xo5 == 20:
                    r = padd(pa, pb, -1)
                elif xo5 == 21:
                    r = padd(pa, pb, 1)
                elif xo5 == 25:
                    r = pmul(pa, pc)
                else:
                    ac = pmul(pa, pc)
                    r = None if ac is None else padd(ac, pb, 1 if xo5 in (29, 31) else -1)
                    if r is not None and xo5 in (30, 31):
                        r = {m: -k for m, k in r.items()}
                if r is not None:
                    setf(D, mkpoly(r))
                    continue
            if xo5 >= 18 and not (o == 63 and xo5 in (16, 17)):
                if xo5 in (18, 20, 21):
                    ops = [F(A), F(B)]
                elif xo5 == 25:
                    ops = [F(A), F(C)]
                elif xo5 in (22, 24, 26):
                    ops = [F(B)]
                else:                                  # fsel / fmadd family
                    ops = [F(A), F(C), F(B)]
                if xo5 in COMMUTATIVE_59_63 or xo5 >= 28:
                    ops[:2] = sorted(ops[:2])
                setf(D, E("fa", o, xo5, *ops))
                continue
            xo = (w >> 1) & 0x3FF
            if o == 63 and xo in (0, 32):
                effects.append(E("fcmp", F(A), F(B)))
                continue
            if o == 63 and xo in (72, 12):             # fmr, frsp
                setf(D, F(B))
                continue
            if o == 63 and xo == 40:                   # fneg
                setf(D, mkpoly({m: -k for m, k in poly_of(F(B)).items()}))
                continue
            setf(D, E("fx", o, xo, F(B)))
            continue

        # ---- X-form -------------------------------------------------------- #
        if o == 31:
            xo = (w >> 1) & 0x3FF
            if xo in (0, 32):
                effects.append(E("cmp", xo, G(A), G(B)))
                continue
            if xo in X_LOADS:
                v = E("ldx", X_LOADS[xo], G(A) if A else E("zero"), G(B))
                (setf if xo in X_FLOAD else setg)(D, v)
                continue
            if xo in X_STORES:
                val = F(D) if xo in X_FSTORE else G(D)
                effects.append(E("stx", X_STORES[xo], G(A) if A else E("zero"),
                                 G(B), canon_arg(val)))
                mem.clear()                    # an indexed store may alias anything
                continue
            if xo == 444 and D == B:                   # mr
                g[A] = G(D)
                if org_d:
                    origin[A] = org_d
                if 3 <= A <= 10:
                    fresh_g.add(A)
                continue
            if xo == 467 and ((w >> 11) & 0x3FF) == 0x120:   # mtctr
                ctr = G(D)
                continue
            if xo in X_LOGICAL:
                ops = [G(D), G(B)] if xo not in (824, 826, 827, 26, 954, 922, 986) \
                    else [G(D), B]
                if xo in X_COMMUTATIVE:
                    ops = sorted(ops)
                setg(A, E("xl", xo, *ops))
                if w & 1:
                    effects.append(E("cmp0", G(A)))
                continue
            if xo == 266:                              # add
                setg(D, lin_combine(G(A), G(B), 1))
                if w & 1:
                    effects.append(E("cmp0", G(D)))
                continue
            if xo == 40:                               # subf: rB - rA
                setg(D, lin_combine(G(B), G(A), -1))
                if w & 1:
                    effects.append(E("cmp0", G(D)))
                continue
            if xo == 104:                              # neg
                c, t = lin_of(G(A))
                setg(D, lin(-c, {k: -v for k, v in t.items()}))
                continue
            if xo in X_ARITH:
                ops = [G(A), G(B)] if xo not in (104, 202, 234, 232, 200) else [G(A)]
                if xo in X_COMMUTATIVE:
                    ops = sorted(ops)
                setg(D, E("xa", xo, *ops))
                continue
            for r in tos._defs_gpr(w):
                g[r] = E("x?", xo, r)
            continue

        # ---- branches -------------------------------------------------------- #
        if o == tos.OP_BRANCH:
            if w & 1:
                args = call_args(callee)
                name = callee or "<unnamed>"
                if name.startswith(PLACEHOLDER):
                    name = "*"
                elif name.startswith("??$MakeString@"):
                    # the instantiation is a wrong-CALLEE question with its own
                    # tools; for value flow, every MakeString formats its args
                    name = "MakeString"
                flat = [x for pair in args for x in pair]
                ce = E("call", epoch, name, *flat)
                effects.append(ce)
                for r in range(3, 11):
                    for a in origin.get(r, ()):
                        addi_cons[a].append(("call", ce))
                ret = E("ret", epoch, name, *flat)
                fret = E("fret", epoch, name, *flat)
                epoch += 1
                origin.clear()
                # a callee can write any stack slot whose address escaped (the
                # Normalize(cross, cross) out-parameter): what a slot holds
                # after the call is "whatever was there, as modified by call N"
                stack = {a: (k, E("postcall", epoch, v)) for a, (k, v) in stack.items()}
                mem.clear()
                for r in VOLATILE:
                    g[r] = E("clob-g", r)
                for r in range(0, 14):
                    f[r] = E("clob-f", r)
                g[3] = ret
                f[1] = fret
                fresh_g.clear()
                fresh_f.clear()
            else:
                prev_uncond = True
            continue
        if o == tos.OP_BCLR_BCCTR:
            xo = (w >> 1) & 0x3FF
            always = ((w >> 21) & 0x14) == 0x14
            if xo == 528 and (w & 1):                  # bctrl
                args = call_args()
                flat = [x for pair in args for x in pair]
                ce = E("icall", epoch, ctr, *flat)
                effects.append(ce)
                for r in range(3, 11):
                    for a in origin.get(r, ()):
                        addi_cons[a].append(("call", ce))
                ret = E("iret", epoch, ctr, *flat)
                epoch += 1
                origin.clear()
                # a callee can write any stack slot whose address escaped (the
                # Normalize(cross, cross) out-parameter): what a slot holds
                # after the call is "whatever was there, as modified by call N"
                stack = {a: (k, E("postcall", epoch, v)) for a, (k, v) in stack.items()}
                mem.clear()
                for r in VOLATILE:
                    g[r] = E("clob-g", r)
                for r in range(0, 14):
                    f[r] = E("clob-f", r)
                g[3] = ret
                f[1] = E("ifret", epoch, ctr, *flat)
                fresh_g.clear()
                fresh_f.clear()
            elif xo == 16 and always:                  # blr
                # only the register the signature returns in is observable;
                # a void function's leftover f1 is scratch (Multiply(V3,Quat))
                if ret_kind == "void":
                    effects.append(E("return"))
                elif ret_kind == "f":
                    effects.append(E("return", F(1)))
                elif ret_kind == "g":
                    effects.append(E("return", G(3)))
                else:
                    effects.append(E("return", G(3), F(1)))
                prev_uncond = True
            elif xo == 528 and always:                 # bctr (switch / tail)
                effects.append(E("jump", ctr))
                prev_uncond = True
            continue
        # anything else: whatever it defines becomes opaque
        for r in tos._defs_gpr(w):
            g[r] = E("op?", o, r)
    return effects, load_val, store_eff, addi_cons


def _unmatched(a, b):
    ca, cb = {}, {}
    for x in a:
        ca[x] = ca.get(x, 0) + 1
    for x in b:
        cb[x] = cb.get(x, 0) + 1
    return {x for x, n in ca.items() if n > cb.get(x, 0)}


def _reaches(E, roots, target):
    """True if `target` is a sub-expression of any of `roots`, or the search
    budget ran out (conservative: an unfinished search never excuses a row)."""
    seen = set()
    stack = list(roots)
    steps = 0
    while stack:
        x = stack.pop()
        if x == target:
            return True
        if x in seen:
            continue
        seen.add(x)
        steps += 1
        if steps > REACH_BUDGET:
            return True
        stack.extend(E.kids[x])
    return False


def arg_regs(demangled):
    """[('g'|'f', regno), ...] for a demangled MSVC signature, or None when the
    register assignment cannot be stated with confidence."""
    if not demangled or "..." in demangled or "__cdecl" not in demangled:
        return None
    head, rest = demangled.split("__cdecl", 1)
    head = head.strip()
    member = head.startswith(ACCESS_WORDS) and " static " not in f" {head} "
    ret = head.split(":", 1)[1] if head.startswith(ACCESS_WORDS) else head
    ret = ret.replace("virtual", "").strip()
    if (("class " in ret or "struct " in ret or "union " in ret)
            and not ret.endswith(("*", "&"))):
        return None                        # hidden return pointer: do not guess
    params = split_params(rest)
    if params is None:
        return None
    slots = (["g"] if member else []) + [
        "f" if p in ("float", "double") else "g" for p in params]
    if len(slots) > 8:
        return None
    out, fnext = [], 1
    for k, c in enumerate(slots):
        if c == "g":
            out.append(("g", 3 + k))
        else:
            out.append(("f", fnext))
            fnext += 1
    return out


ACCESS_WORDS = ("public: ", "protected: ", "private: ")

# C runtime math the report does not describe (it is not a decomp unit): all
# double-in-FPR, count of arguments.  Anything absent falls back to strict.
CRT_FLOAT_ARGS = {n: 1 for n in ("acos", "asin", "atan", "cos", "sin", "tan",
                                 "sqrt", "exp", "log", "log10", "floor", "ceil",
                                 "fabs", "cosh", "sinh", "tanh")}
CRT_FLOAT_ARGS.update({"atan2": 2, "pow": 2, "fmod": 2})
CRT_GPR_ARGS = {"memcpy": 3, "memset": 3, "memmove": 3, "memcmp": 3, "strlen": 1,
                "strcmp": 2, "stricmp": 2, "_stricmp": 2, "strcpy": 2,
                "strncpy": 3, "strcat": 2, "strchr": 2, "strrchr": 2, "strstr": 2,
                "strncmp": 3, "_strnicmp": 3}


def ret_kind_of(demangled):
    """'void' | 'f' | 'g' | None -- which register a function returns in."""
    if not demangled or "__cdecl" not in demangled:
        return None
    head = demangled.split("__cdecl", 1)[0].strip()
    if head.startswith(ACCESS_WORDS):
        head = head.split(":", 1)[1]
    head = head.replace("virtual", "").replace("static", "").strip()
    if head == "void":
        return "void"
    if head in ("float", "double"):
        return "f"
    if not head or (("class " in head or "struct " in head or "union " in head)
                    and not head.endswith(("*", "&"))):
        return None
    return "g"


class ValueFlow:
    def __init__(self, tw, trel, bw, brel, symtab, sigs=None, ret_kind=None):
        self.E = Interner()
        (self.t_eff, self.t_ld, self.t_st,
         self.t_addi) = effects_eval(tw, trel, symtab, self.E, sigs, ret_kind)
        (self.b_eff, self.b_ld, self.b_st,
         self.b_addi) = effects_eval(bw, brel, symtab, self.E, sigs, ret_kind)
        self.t_un = _unmatched(self.t_eff, self.b_eff)
        self.b_un = _unmatched(self.b_eff, self.t_eff)
        self.t_all = set(self.t_eff)
        self.b_all = set(self.b_eff)

    def _load_ok(self, side, i):
        ld, allx, un = ((self.t_ld, self.t_all, self.t_un) if side == "t"
                        else (self.b_ld, self.b_all, self.b_un))
        v = ld.get(i)
        return (v is not None and _reaches(self.E, allx, v)
                and not _reaches(self.E, un, v))

    def _cons_ok(self, side, cons):
        st, un = (self.t_st, self.t_un) if side == "t" else (self.b_st, self.b_un)
        for kind, x in cons:
            if kind == "ld" and not self._load_ok(side, x):
                return False
            if kind == "st" and (x not in st or st[x] in un):
                return False
            if kind == "call" and x in un:
                return False
        return True

    def addi_benign(self, ti, bi):
        """An address-forming addi is value-equivalent iff everything done
        THROUGH the address it formed -- loads, stores, passing it to a call --
        is matched on the other side.  Needs at least one consumer per side:
        an address the linear pass never sees used is not thereby excused."""
        tc, bc = self.t_addi.get(ti), self.b_addi.get(bi)
        if not tc or not bc:
            return False
        return self._cons_ok("t", tc) and self._cons_ok("b", bc)

    def benign(self, ti, bi):
        """True iff the row's difference reaches no unmatched effect."""
        if ti in self.t_st or bi in self.b_st:
            et, eb = self.t_st.get(ti), self.b_st.get(bi)
            return (et is not None and eb is not None
                    and et not in self.t_un and eb not in self.b_un)
        vt, vb = self.t_ld.get(ti), self.b_ld.get(bi)
        if vt is None or vb is None:
            return False
        # A value that reaches NO effect is not thereby proven dead: the pass is
        # linear, so a use across a back edge (a loop step consumed at the loop
        # head) is invisible to it.  Unobserved is not excused.
        if not (_reaches(self.E, set(self.t_eff), vt)
                and _reaches(self.E, set(self.b_eff), vb)):
            return False
        return (not _reaches(self.E, self.t_un, vt)
                and not _reaches(self.E, self.b_un, vb))


# --------------------------------------------------------------------------- #
# Second, independent reorder test: FIRST-CONSUMER correspondence.
#
# The value-flow test above is exact but global: in a 97% function an effect
# that differs for an UNRELATED reason keeps a pure scheduling swap a candidate.
# This test is local and equally sound for the swap question: a load of address
# X is excused only if OUR load of X feeds the instruction that the TARGET's
# load of X feeds (mapped through the alignment), in the same operand slot --
# and symmetrically for our address.  A crossed-over swap sends X to the other
# consumer and fails; a reorder does not.  Commutative consumers (fadd/fmul/
# add/and/or/xor/mullw) accept either source slot.
# --------------------------------------------------------------------------- #
DATAFLOW_WINDOW = 48


def _commutative(w):
    o = tos._op(w)
    if o in (59, 63):
        return ((w >> 1) & 31) in (21, 25)
    if o == 31:
        return ((w >> 1) & 0x3FF) in X_COMMUTATIVE
    return False


def _mentions(w, reg, cls):
    o = tos._op(w)
    if cls == "f":
        if o in (59, 63):
            fields = (21, 16, 11, 6)
        elif o in FPR_STORES or o in FPR_LOADS:
            fields = (21,)
        else:
            return None
    else:
        if o in (59, 63):
            return None
        if o in FPR_STORES or o in FPR_LOADS:
            fields = (16,)
        else:
            fields = (21, 16, 11)
    for sh in fields:
        if (w >> sh) & 31 == reg:
            if sh != 21 and _commutative(w):
                return "comm"
            return sh
    return None


def _writes(w, reg, cls):
    o = tos._op(w)
    if cls == "f":
        return (o in (59, 63) or o in FPR_LOADS) and tos._d(w) == reg
    if o in INT_LOADS or o in (OP_ADDI, OP_ADDIS, 58):
        return tos._d(w) == reg
    if o in (31, 21, 24, 25, 26, 27, 28, 29, 20, 23, 30):
        return reg in (tos._a(w), tos._d(w))
    return False


def _flow(words, i):
    w = words[i]
    o = tos._op(w)
    cls = "f" if (o in FPR_LOADS or o in FPR_STORES) else "g"
    reg = tos._d(w)
    if o in FPR_STORES or o in STORES or o == 62:
        for j in range(i - 1, max(-1, i - 1 - DATAFLOW_WINDOW), -1):
            if _writes(words[j], reg, cls):
                return (j, "def")
        return None
    for j in range(i + 1, min(len(words), i + 1 + DATAFLOW_WINDOW)):
        sh = _mentions(words[j], reg, cls)
        if sh is not None:
            return (j, sh)
    return None


def _first_consumer_reorder(row, root, bv_root, tw, bw, tacc, bacc, t2o):
    ti, bi = row["t_idx"], row["o_idx"]
    tf, bf = _flow(tw, ti), _flow(bw, bi)
    if tf is None or bf is None or ti not in tacc or bi not in bacc:
        return False
    kind, is_store = tacc[ti][0], tacc[ti][1]

    def find(acc, words, ea, root_, want):
        for k in sorted(acc):
            kk, st, r, off = acc[k]
            if kk == kind and st == is_store and off == ea and root_eq(r, root_) is True:
                f = _flow(words, k)
                if f is not None and want(f):
                    return True
        return False
    ok1 = find(bacc, bw, row["t_ea"], bv_root,
               lambda f: f[0] == t2o.get(tf[0]) and f[1] == tf[1])
    ok2 = find(tacc, tw, row["o_ea"], root,
               lambda f: t2o.get(f[0]) == bf[0] and f[1] == bf[1])
    return ok1 and ok2


def compare_function(tw, trel, bw, brel, symtab, ctx, layouts, norm, sigs=None):
    """Returns a list of row dicts, each with a 'bucket'."""
    tbase, tacc, taddi = evaluate(tw, trel, symtab)
    bbase, bacc, baddi = evaluate(bw, brel, symtab)
    rows = []
    vf = None                     # built lazily: most functions never need it
    t2o = None
    for ti, bi in aligned_pairs(tw, bw):
        a, b = tw[ti], bw[bi]
        if a == b:
            continue
        o = tos._op(a)
        if tos._op(b) != o:
            continue
        is_mem = (o in tos.DFORM_MEM and o not in (OP_LMW, OP_STMW)) or o in tos.DSFORM_MEM
        is_addi = o == OP_ADDI and tos._a(a) != 0 and tos._a(b) != 0
        if not (is_mem or is_addi):
            continue
        if o in tos.DSFORM_MEM:
            td, bd = tos._ds(a), tos._ds(b)
        else:
            td, bd = tos._simm(a), tos._simm(b)
        if td == bd:
            continue                          # register-only difference
        if tos._a(a) == 0 or tos._a(b) == 0:
            continue                          # absolute addressing, no base
        row = {"t_idx": ti, "o_idx": bi, "op": o, "t_disp": td, "o_disp": bd,
               "t_word": f"{a:08x}", "o_word": f"{b:08x}",
               "t_rd": tos._d(a), "o_rd": tos._d(b),
               "t_ra": tos._a(a), "o_ra": tos._a(b)}
        rows.append(row)
        if ti in trel or bi in brel:
            row["bucket"] = "relocated-displacement"
            continue
        tv, bv = tbase.get(ti), bbase.get(bi)
        row["t_base"] = fmt_root(tv[0]) + (f"{tv[1]:+#x}" if tv else "") if tv else "?"
        row["o_base"] = fmt_root(bv[0]) + (f"{bv[1]:+#x}" if bv else "") if bv else "?"
        if (tv and _is_sp(tv[0])) or (bv and _is_sp(bv[0])):
            row["bucket"] = "stack-frame"
            continue
        if tv is None or bv is None:
            row["bucket"] = "base-unknown"
            continue
        eq = root_eq(tv[0], bv[0])
        if eq is None:
            row["bucket"] = "base-unknown"
            continue
        if eq is False:
            row["bucket"] = "base-provenance-differs"
            continue
        # same base value on both sides
        root = tv[0]
        t_ea, b_ea = tv[1] + td, bv[1] + bd
        kind = ("addr" if is_addi else
                tos.DFORM_MEM[o][0] if o in tos.DFORM_MEM else tos.DSFORM_MEM[o])
        row.update(kind=kind, t_ea=t_ea, o_ea=b_ea, delta=b_ea - t_ea)
        if ctx["member"] and root == ("arg", 3):
            row["bucket"] = "this-relative"      # this_offset_scan's lane
            continue
        if t_ea == b_ea:
            row["bucket"] = "anchor-normalised"
            continue
        if is_addi:
            tu, bu = taddi[ti], baddi[bi]
            if not (tu["uses"] or tu["passed"] or bu["uses"] or bu["passed"]):
                row["bucket"] = "addi-not-an-address"
                continue
            # anchor: the two addis differ but every address reached through
            # them agrees -> one address, two spellings
            def _reach_set(u):
                return sorted([(k, fmt_root(r), off) for (k, r, off) in u["uses"]]
                              + [("esc", fmt_root(r), off) for (r, off) in u["escapes"]])
            tset, bset = _reach_set(tu), _reach_set(bu)
            if (tset and tset == bset and not tu["opaque_escape"]
                    and not bu["opaque_escape"]):
                row["bucket"] = "anchor-normalised"
                continue

            # a SCHEDULING reorder of two addis: each side computes the other's
            # address somewhere, off the same base, and uses it identically
            def _uses(u):
                return (sorted((k, fmt_root(r), off) for (k, r, off) in u["uses"]),
                        u["passed"])

            def _has(addis, root_, ea, want):
                return any(v["val"] is not None and v["val"][1] == ea
                           and root_eq(v["val"][0], root_) is True
                           and _uses(v) == want for v in addis.values())
            if (_has(baddi, tv[0], t_ea, _uses(tu))
                    and _has(taddi, bv[0], b_ea, _uses(bu))):
                row["bucket"] = "reordered"
                continue
        if norm is not None and norm >= 100.0:
            row["bucket"] = "contradicted-by-100pct"
            continue
        if vf is None:
            vf = ValueFlow(tw, trel, bw, brel, symtab, sigs, ctx.get("ret"))
        if (vf.addi_benign(ti, bi) if is_addi else vf.benign(ti, bi)):
            row["bucket"] = "reordered"
            continue
        if not is_addi:
            if t2o is None:
                t2o = dict(aligned_pairs(tw, bw))
            if _first_consumer_reorder(row, root, bv[0], tw, bw, tacc, bacc, t2o):
                row["bucket"] = "reordered"
                row["reorder_test"] = "first-consumer"
                continue
        # -- candidate: type it ----------------------------------------------- #
        lay = type_of_root(root, ctx, layouts)
        row["base_type"] = lay
        tf = field_at(lay, t_ea, layouts) if lay else None
        of = field_at(lay, b_ea, layouts) if lay else None
        row["t_field"] = tf[0] if tf else None
        row["o_field"] = of[0] if of else None
        chain = (not is_addi and o in INT_LOADS
                 and row["t_rd"] == row["t_ra"] and row["o_rd"] == row["o_ra"])
        row["chain_step"] = chain
        # Does each side touch the OTHER side's address anywhere, off the same
        # base value?  If yes, the row is a PERMUTATION of addresses whose value
        # flow differs -- a crossed-over swap (the CharUpperTwist::Load shape)
        # OR float reassociation that expression equality cannot prove equal.
        # If no, one side reads an address the other never reads at all: the
        # SetPreFrame shape, and the strongest signal this tool produces.
        def _touches(acc, addis, root_, ea):
            if is_addi:        # an address: formed, or dereferenced, anywhere
                return (any(off == ea and root_eq(r, root_) is True
                            for (_k, _st, r, off) in acc.values())
                        or any(v["val"] is not None and v["val"][1] == ea
                               and root_eq(v["val"][0], root_) is True
                               for v in addis.values()))
            return any(kk == kind and off == ea and root_eq(r, root_) is True
                       for (kk, _st, r, off) in acc.values())
        if is_addi:
            # an addi's ADDRESSES are what it reaches (every dereference through
            # it, and its own value if it escapes), not its value -- an anchor
            # `subi r30,r11,0x44` is never itself an address anyone touches
            def _reached(u, _ea):
                return ([off for (_k, _r, off) in u["uses"]]
                        + [off for (_r, off) in u["escapes"]])
            permuted = (all(_touches(bacc, baddi, bv[0], x)
                            for x in _reached(taddi[ti], t_ea))
                        and all(_touches(tacc, taddi, root, x)
                                for x in _reached(baddi[bi], b_ea)))
        else:
            permuted = (_touches(bacc, baddi, bv[0], t_ea)
                        and _touches(tacc, taddi, root, b_ea))
        row["permuted"] = permuted
        if tf and of and is_link_pair(tf[0], of[0]):
            row["bucket"] = "cand-adjacent-links"
        elif permuted and vf is not None and not vf.t_un and not vf.b_un:
            # every effect the linear pass can see matches; the row's value was
            # simply never observed (a use across a back edge or a jump).
            # Not excused -- the pass is blind there -- but not evidence either
            row["bucket"] = "permuted-unobserved"
        elif permuted:
            row["bucket"] = "cand-permuted"
        elif tf and of:
            row["bucket"] = "cand-wrong-field-typed"
        elif chain:
            row["bucket"] = "cand-chain-step"
        else:
            row["bucket"] = "cand-novel-address"
    return rows


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
def load_layouts(path):
    raw = tos.load_struct_db(path)
    return {c: {off: (m[0], m[1] or "") for off, m in mem.items()}
            for c, mem in raw.items()}


def function_ctx(unit, name, demangled):
    kind, cls = tos.classify_symbol(name)
    dm = demangled.get((unit, name))
    if kind == "template-or-complex":
        r = tos.member_from_demangled(dm)
        if r:
            kind, cls = r
    return {"member": kind == "member", "class": cls,
            "params": split_params(dm), "kind": kind, "ret": ret_kind_of(dm)}


def iter_units(objdiff):
    with open(objdiff) as f:
        units = json.load(f)["units"]
    for u in sorted(units, key=lambda x: x.get("name", "")):
        tp, bp = u.get("target_path"), u.get("base_path")
        tp = (tp if os.path.isabs(tp) else os.path.join(REPO, tp)) if tp else None
        bp = (bp if os.path.isabs(bp) else os.path.join(REPO, bp)) if bp else None
        yield u.get("name", ""), tp, bp


def scan(args, cov, only=None):
    symtab = load_symtab(args.symbols)
    layouts = load_layouts(args.struct_db)
    norms, demangled = tos.load_report(args.report)
    # callee signatures for exact call-argument registers, by mangled name;
    # sorted so a name defined in two units resolves the same way every run
    sigs = {}
    for (_u, nm), dm in sorted(demangled.items()):
        sigs.setdefault(nm, dm)

    units = list(iter_units(args.objdiff))
    target = []                          # (unit, name, body, base_path)
    n_units_no_target = 0
    for unit, tp, bp in units:
        if not tp or not os.path.exists(tp):
            n_units_no_target += 1
            continue
        tb = function_bodies(tp)
        for name in sorted(tb):
            if only and name != only:
                continue
            target.append((unit, name, tb[name], bp))
    cov.universe(len(target), "function bodies in the target objects of every "
                              "objdiff.json unit that has one")

    rows_all = []
    bucket_rows = {b: 0 for b in BUCKETS}
    funcs_with = {b: set() for b in BUCKETS}
    n_byte_identical = 0
    cache_unit, cache_bodies = None, None
    for unit, name, (tw, trel), bp in target:
        if not bp or not os.path.exists(bp):
            cov.drop("unit-has-no-built-object",
                     note="the TU does not build yet: a wrong field there is invisible")
            continue
        if cache_unit != unit:
            cache_unit, cache_bodies = unit, function_bodies(bp)
        if name not in cache_bodies:
            cov.drop("not-defined-in-our-object",
                     note="target function with no body in our object "
                          "(unwritten, inlined away, or emitted elsewhere)")
            continue
        bw, brel = cache_bodies[name]
        cov.examine()
        if tw == bw:
            n_byte_identical += 1
            continue
        ctx = function_ctx(unit, name, demangled)
        rows = compare_function(tw, trel, bw, brel, symtab, ctx, layouts,
                                norms.get((unit, name)), sigs)
        for r in rows:
            r["unit"], r["symbol"] = unit, name
            r["norm"] = norms.get((unit, name))
            bucket_rows[r["bucket"]] += 1
            funcs_with[r["bucket"]].add((unit, name))
        rows_all.extend(rows)

    # The row ledger must balance: every kept row is in exactly one bucket.
    assert sum(bucket_rows.values()) == len(rows_all), "row ledger does not balance"
    cov.extra("rows_in_scope", len(rows_all))
    cov.extra("buckets_rows", bucket_rows)
    cov.extra("buckets_functions", {b: len(s) for b, s in funcs_with.items()})
    cov.extra("byte_identical", n_byte_identical)
    cov.extra("units_without_target_object", n_units_no_target)
    cov.note(f"{n_byte_identical} examined functions are byte-identical: a proof "
             f"of absence for this class, not a blind spot")
    cov.note(f"{len(rows_all)} displacement-only rows in scope; every one is in "
             f"exactly one bucket (ledger asserted)")
    if not symtab:
        cov.note("symbols.txt unavailable -- globals are compared by NAME only, "
                 "so the anchor normalisation for globals is OFF")
    if not layouts:
        cov.note("struct_db.sqlite unavailable -- candidate TYPING is off; the "
                 "candidate/lead split is unaffected")
    return rows_all, bucket_rows, funcs_with


def _default_struct_db():
    return tos._default_struct_db()


def _explain_effects(args):
    """The value-flow adjudication surface: effects present on one side only."""
    symtab = load_symtab(args.symbols)
    for unit, tp, bp in iter_units(args.objdiff):
        if not (tp and bp and os.path.exists(tp) and os.path.exists(bp)):
            continue
        tb = function_bodies(tp)
        if args.explain not in tb:
            continue
        bb = function_bodies(bp)
        if args.explain not in bb:
            continue
        _n, demangled = tos.load_report(args.report)
        sigs = {}
        for (_u, nm), dm in sorted(demangled.items()):
            sigs.setdefault(nm, dm)
        vf = ValueFlow(*tb[args.explain], *bb[args.explain], symtab, sigs,
                       ret_kind_of(sigs.get(args.explain)))
        for side, eff, un in (("target", vf.t_eff, vf.t_un), ("ours", vf.b_eff, vf.b_un)):
            print(f"  effects only in {side} ({len(un)} distinct of {len(eff)}):")
            for e in sorted(un):
                print(f"      {vf.E.show(e, args.explain_depth)}")
        return


def print_row(r):
    fld = ""
    if r.get("t_field") or r.get("o_field"):
        fld = f"  [{r.get('base_type')}: {r.get('t_field')} vs {r.get('o_field')}]"
    ea = ""
    if "t_ea" in r:
        ea = f"  ea {r['t_ea']:+#x} vs {r['o_ea']:+#x} (delta {r['delta']:+d})"
    print(f"      [{r['bucket']}] t#{r['t_idx']} {r['t_word']} vs o#{r['o_idx']} "
          f"{r['o_word']}  base t={r.get('t_base', '?')} o={r.get('o_base', '?')}"
          f"{ea}{fld}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--objdiff", default=DEFAULT_OBJDIFF)
    ap.add_argument("--report", default=DEFAULT_REPORT)
    ap.add_argument("--symbols", default=DEFAULT_SYMBOLS)
    ap.add_argument("--struct-db", default=_default_struct_db())
    ap.add_argument("--json", default=None)
    ap.add_argument("--show-leads", type=int, default=0, metavar="N",
                    help="also print the first N lead rows (base-unknown / "
                         "provenance-differs). Display only; counts are complete.")
    ap.add_argument("--show-this", action="store_true",
                    help="also print the this-relative rows (this_offset_scan's lane)")
    ap.add_argument("--explain", default=None, metavar="SYMBOL",
                    help="print every in-scope row of one function with both "
                         "sides' base values. The adjudication surface.")
    ap.add_argument("--explain-depth", type=int, default=4,
                    help="expression depth printed by --explain")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest(args)
    if not os.path.exists(args.objdiff):
        print(f"INCONCLUSIVE: objdiff.json not found: {args.objdiff}")
        return EXIT_NO_INPUT

    cov = CoverageReport("pointer_disp_scan", args=args)
    cov.require_examined("no function pair was comparable")
    rows, bucket_rows, funcs_with = scan(args, cov, only=args.explain)

    if args.explain:
        print(f"explain {args.explain}: {len(rows)} in-scope rows")
        for r in rows:
            print_row(r)
        _explain_effects(args)
        cov.emit()
        return 0 if rows or cov.as_dict()["examined"] else EXIT_NO_INPUT

    cands = [r for r in rows if r["bucket"] in CANDIDATE_BUCKETS]
    nfun = len({(r["unit"], r["symbol"]) for r in cands})
    ex = cov.as_dict()
    print(f"NON-THIS DISPLACEMENT CANDIDATES: {len(cands)} rows in {nfun} functions "
          f"(of {len(rows)} in-scope displacement-only rows; {ex['examined']} "
          f"function pairs examined of a universe of {ex['universe']})")
    labels = {
        "relocated-displacement": "relocated (global identity -- reloc tools' lane)",
        "stack-frame": "stack frame (r1-derived; frame layout, artifact)",
        "this-relative": "this-relative (this_offset_scan's lane)",
        "anchor-normalised": "anchor-normalised (same address, two spellings)",
        "addi-not-an-address": "addi never used as an address (det-arith's lane)",
        "reordered": "reordered (value-equivalent: every effect using it matches)",
        "contradicted-by-100pct": "contradicted by report.json = 100.0 (MY artifact)",
        "base-unknown": "LEAD: a base value unknown/unresolvable on a side",
        "base-provenance-differs": "LEAD: the two bases hold different values",
        "permuted-unobserved": "LEAD: permuted, all effects match, row's value never observed",
        "cand-adjacent-links": "CANDIDATE: adjacent links of one node (direction bug shape)",
        "cand-wrong-field-typed": "CANDIDATE: wrong field of a typed base",
        "cand-chain-step": "CANDIDATE: untyped p = p->field step",
        "cand-novel-address": "CANDIDATE: an address the other side never touches off this base",
        "cand-permuted": "CANDIDATE (weak): addresses permuted, value flow differs (swap OR reassociation)",
    }
    for b in BUCKETS:
        print(f"  {bucket_rows[b]:6d} rows / {len(funcs_with[b]):5d} fns  {labels[b]}")
    print()
    cur = None
    for r in sorted(cands, key=lambda r: (r["bucket"], r["unit"], r["symbol"], r["t_idx"])):
        key = (r["bucket"], r["symbol"])
        if key != cur:
            cur = key
            norm = r["norm"]
            print(f"  {r['symbol']}   norm={'n/a' if norm is None else f'{norm:.4f}'}"
                  f"   unit={r['unit']}")
        print_row(r)
    if args.show_this:
        print("\n  this-relative rows:")
        for r in rows:
            if r["bucket"] == "this-relative":
                print(f"  {r['symbol']}")
                print_row(r)
    if args.show_leads:
        leads = [r for r in rows if r["bucket"] in ("base-unknown",
                                                     "base-provenance-differs",
                                                     "permuted-unobserved")]
        print(f"\n  leads: showing {min(args.show_leads, len(leads))} of {len(leads)}")
        for r in leads[: args.show_leads]:
            print(f"  {r['symbol']}")
            print_row(r)
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"rows": rows, "buckets_rows": bucket_rows,
                       "_coverage": cov.as_dict()}, fh, indent=1, sort_keys=True)
    return cov.emit()


# --------------------------------------------------------------------------- #
# Selftest
# --------------------------------------------------------------------------- #
def _lwz(d, a, imm):
    return (32 << 26) | (d << 21) | (a << 16) | (imm & 0xFFFF)


def _stw(s, a, imm):
    return (36 << 26) | (s << 21) | (a << 16) | (imm & 0xFFFF)


def _addi(d, a, imm):
    return (14 << 26) | (d << 21) | (a << 16) | (imm & 0xFFFF)


def _mr(d, s):
    return (31 << 26) | (s << 21) | (d << 16) | (s << 11) | (444 << 1)


def _clrrwi0(d, s):
    return (21 << 26) | (s << 21) | (d << 16) | (0 << 11) | (0 << 6) | (31 << 1)


def _b():
    return (18 << 26)


def selftest(args):
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    member = {"member": True, "class": "Foo", "params": [], "kind": "member"}
    free = {"member": False, "class": None, "params": ["Foo *"], "kind": "free"}
    lay = {"Foo": {0x0: ("__vptr", ""), 0x10: ("mNode", "Bar *"),
                   0x14: ("mNodeIt", "ObjPtrList<Foo>::iterator")},
           "Bar": {0x0: ("mA", "int"), 0x4: ("mB", "int"), 0x8: ("mNext", "Bar *"),
                   0xc: ("mPrev", "Bar *")}}

    def run(t, o, ctx=member, norm=50.0, tr=None, orl=None):
        return compare_function(t, tr or {}, o, orl or {}, {}, ctx, lay, norm)

    # the SetPreFrame shape: iterator node from a this field, step through prev
    t = [_mr(31, 3), _lwz(11, 31, 0x14), _clrrwi0(11, 11), _lwz(11, 11, 0x18),
         _stw(11, 31, 0x14)]
    o = [_mr(31, 3), _lwz(11, 31, 0x14), _clrrwi0(11, 11), _lwz(11, 11, 0x14),
         _stw(11, 31, 0x14)]
    rows = run(t, o)
    check("SetPreFrame shape: ONE row, bucket cand-adjacent-links "
          "(prev vs next of an ObjPtrList node)",
          len(rows) == 1 and rows[0]["bucket"] == "cand-adjacent-links"
          and rows[0]["t_field"] == "prev" and rows[0]["o_field"] == "next")
    check("negative control: correcting our step clears it", run(t, list(t)) == [])

    # register permutation alone: no row
    o2 = [_mr(30, 3), _lwz(10, 30, 0x14), _clrrwi0(10, 10), _lwz(10, 10, 0x18),
          _stw(10, 30, 0x14)]
    check("a pure register permutation yields no row", run(t, o2) == [])
    # ...and correspondence is by VALUE: permuted registers + wrong disp still found
    o3 = [_mr(30, 3), _lwz(10, 30, 0x14), _clrrwi0(10, 10), _lwz(10, 10, 0x14),
          _stw(10, 30, 0x14)]
    r3 = run(t, o3)
    check("permuted registers do not hide the displacement (value correspondence)",
          len(r3) == 1 and r3[0]["bucket"] == "cand-adjacent-links")

    # anchor normalisation: addi r9,r11,0x10 ; lwz 0x8(r9)  vs  lwz 0x18(r11)
    t = [_lwz(11, 3, 0x10), _addi(9, 11, 0x10), _lwz(4, 9, 0x8)]
    o = [_lwz(11, 3, 0x10), _addi(9, 11, 0x8), _lwz(4, 9, 0x10)]
    rows = run(t, o)
    check("a different anchor reaching the SAME address -> anchor-normalised, "
          "both rows",
          len(rows) == 2 and all(r["bucket"] == "anchor-normalised" for r in rows))

    # stack slot
    t = [_lwz(4, 1, 0x50)]
    o = [_lwz(4, 1, 0x54)]
    check("an r1 slot is stack-frame, never a field",
          [r["bucket"] for r in run(t, o)] == ["stack-frame"])
    t = [_addi(31, 1, 0x60), _lwz(4, 31, 0x8)]
    o = [_addi(31, 1, 0x60), _lwz(4, 31, 0xc)]
    check("an r1-derived frame pointer is stack-frame too",
          [r["bucket"] for r in run(t, o)] == ["stack-frame"])

    # this-relative is the sibling lane
    t = [_lwz(4, 3, 0x10)]
    o = [_lwz(4, 3, 0x14)]
    check("this+K in a member function -> this-relative (not ours)",
          [r["bucket"] for r in run(t, o)] == ["this-relative"])
    check("...but in a FREE function r3 is an argument -> typed candidate",
          [r["bucket"] for r in run(t, o, ctx=free)] == ["cand-wrong-field-typed"])

    # different base values
    t = [_lwz(11, 3, 0x10), _lwz(4, 11, 0x8)]
    o = [_lwz(11, 3, 0x14), _lwz(4, 11, 0xc)]
    rows = run(t, o)
    check("different base values -> base-provenance-differs lead for the inner "
          "row (the outer this row is the sibling's)",
          [r["bucket"] for r in rows] == ["this-relative", "base-provenance-differs"])

    # relocated
    t = [_lwz(4, 11, 0x10)]
    o = [_lwz(4, 11, 0x14)]
    check("a relocated displacement is counted, not examined",
          [r["bucket"] for r in run(t, o, tr={0: (REL_PPC_REFLO, "x")})]
          == ["relocated-displacement"])
    check("an unknown base (never defined) is a lead, not a candidate",
          [r["bucket"] for r in run(t, o)] == ["base-unknown"])

    # a call clobbers volatile bases; r3 becomes ret(callee)
    t = [_lwz(11, 3, 0x10), (18 << 26) | 1, _lwz(4, 11, 0x8)]
    o = [_lwz(11, 3, 0x10), (18 << 26) | 1, _lwz(4, 11, 0xc)]
    check("a call clobbers r11: the row after it is base-unknown",
          [r["bucket"] for r in run(t, o)] == ["base-unknown"])
    t = [(18 << 26) | 1, _lwz(4, 3, 0x8)]
    o = [(18 << 26) | 1, _lwz(4, 3, 0xc)]
    rows = run(t, o, tr={0: (REL_PPC_REL24, "?Get@X@@QAAPAVY@@XZ")},
               orl={0: (REL_PPC_REL24, "?Get@X@@QAAPAVY@@XZ")})
    check("r3 after a call is ret(callee): same callee -> untyped candidate",
          [r["bucket"] for r in rows] == ["cand-novel-address"])
    rows = run(t, o, tr={0: (REL_PPC_REL24, "?Get@X@@QAAPAVY@@XZ")},
               orl={0: (REL_PPC_REL24, "?Other@X@@QAAPAVY@@XZ")})
    check("...different real callees -> base-provenance-differs",
          [r["bucket"] for r in rows] == ["base-provenance-differs"])
    rows = run(t, o, tr={0: (REL_PPC_REL24, "fn_82000000")},
               orl={0: (REL_PPC_REL24, "?Other@X@@QAAPAVY@@XZ")})
    check("...a placeholder callee name is a wildcard",
          [r["bucket"] for r in rows] == ["cand-novel-address"])

    # scheduling reorder: the two loads swap places but each VALUE still goes
    # to the same consumer in the same slot (r4 <- 0x8 feeds the store to 0x20)
    t = [_lwz(11, 3, 0x10), _lwz(4, 11, 0x8), _lwz(5, 11, 0xc),
         _stw(4, 3, 0x20), _stw(5, 3, 0x24)]
    o = [_lwz(11, 3, 0x10), _lwz(5, 11, 0xc), _lwz(4, 11, 0x8),
         _stw(4, 3, 0x20), _stw(5, 3, 0x24)]
    rows = run(t, o)
    check("a pure scheduling swap (values reach the same consumers) -> reordered x2",
          [r["bucket"] for r in rows] == ["reordered"] * 2)
    # the CharUpperTwist shape: same two addresses, values reach DIFFERENT places
    o = [_lwz(11, 3, 0x10), _lwz(4, 11, 0xc), _lwz(5, 11, 0x8),
         _stw(4, 3, 0x20), _stw(5, 3, 0x24)]
    rows = run(t, o)
    check("a real SWAP (both addresses touched, values cross over) stays a "
          "candidate -- a 'both sides touch both' rule would hide it",
          len(rows) == 2 and all(r["bucket"] in CANDIDATE_BUCKETS for r in rows))
    check("the same rows at report.json 100.0 are contradicted (my artifact)",
          [r["bucket"] for r in run(t, [_lwz(11, 3, 0x10), _lwz(4, 11, 0x14),
                                       _lwz(5, 11, 0xc)], norm=100.0)]
          == ["contradicted-by-100pct"])

    # chain step, untyped
    t = [_lwz(11, 4, 0x20), _lwz(11, 11, 0x0)]
    o = [_lwz(11, 4, 0x20), _lwz(11, 11, 0x4)]
    check("untyped p = p->f step with a different f -> cand-chain-step",
          [r["bucket"] for r in run(t, o)] == ["cand-chain-step"])

    # addi never used as an address: the arithmetic lane's
    t = [_lwz(11, 3, 0x10), _addi(4, 11, 1), _stw(4, 3, 0x10)]
    o = [_lwz(11, 3, 0x10), _addi(4, 11, 2), _stw(4, 3, 0x10)]
    check("an addi stored as a VALUE is an address escape -> candidate, and an "
          "addi never used at all is det-arith's",
          [r["bucket"] for r in run(t, o)] == ["cand-novel-address"]
          and [r["bucket"] for r in run([_lwz(11, 3, 0x10), _addi(4, 11, 1)],
                                        [_lwz(11, 3, 0x10), _addi(4, 11, 2)])]
          == ["addi-not-an-address"])

    # volatile reset after an unconditional branch
    t = [_lwz(11, 3, 0x10), _b(), _lwz(4, 11, 0x8)]
    o = [_lwz(11, 3, 0x10), _b(), _lwz(4, 11, 0xc)]
    check("after an unconditional b, volatile bases are unknown",
          [r["bucket"] for r in run(t, o)] == ["base-unknown"])

    # global anchor normalisation through symbols.txt
    t = [(15 << 26) | (11 << 21), _addi(11, 11, 0), _lwz(4, 11, -0x13)]
    o = [(15 << 26) | (11 << 21), _addi(11, 11, 0), _lwz(4, 11, 0x0)]
    tr = {0: (REL_PPC_REFHI, "?gNumHeaps@@3HA"), 1: (REL_PPC_REFLO, "?gNumHeaps@@3HA")}
    orl = {0: (REL_PPC_REFHI, "gInitted"), 1: (REL_PPC_REFLO, "gInitted")}
    symtab = {"?gNumHeaps@@3HA": 0x830E56EC, "gInitted": 0x830E56EC - 0x13}
    rows = compare_function(t, tr, o, orl, symtab, member, lay, 50.0)
    check("anchor-displacement-false-wrong-global: two anchors, one absolute "
          "address -> anchor-normalised",
          [r["bucket"] for r in rows] == ["anchor-normalised"])

    check("link-pair naming: mNext/mPrev, _M_left/_M_right, next/prev",
          is_link_pair("mNext", "mPrev") and is_link_pair("_M_left", "_M_right")
          and is_link_pair("next", "prev") and not is_link_pair("mNext", "mSize"))

    # -- live: the known positive -------------------------------------------- #
    name = "?SetPreFrame@HamCamShot@@UAAXMM@Z"
    tp = os.path.join(REPO, "build", "373307D9", "obj", "system", "hamobj", "HamCamShot.obj")
    bp = os.path.join(REPO, "build", "373307D9", "src", "system", "hamobj", "HamCamShot.obj")
    if os.path.exists(tp) and os.path.exists(bp):
        tb, bb = function_bodies(tp), function_bodies(bp)
        layouts = load_layouts(args.struct_db)
        ctx = {"member": True, "class": "HamCamShot", "params": ["float", "float"],
               "kind": "member"}
        rows = compare_function(*tb[name], *bb[name], load_symtab(args.symbols),
                                ctx, layouts, 97.0)
        c = [r for r in rows if r["bucket"] in CANDIDATE_BUCKETS]
        live = [r for r in c if r["t_ea"] == 0x18 and r["o_ea"] == 0x14]
        if live:
            print(f"  (live SetPreFrame row: {live[0]['bucket']} "
                  f"{live[0].get('t_field')} vs {live[0].get('o_field')})")
            check("live: SetPreFrame's prev-vs-next row is an adjacent-links "
                  "candidate", live[0]["bucket"] == "cand-adjacent-links")
        else:
            fixed = [r for r in rows if r.get("t_ea") == 0x18]
            print("  NOTE  live SetPreFrame row absent -- is the fix-camshot fix "
                  f"merged into this tree? rows={[r['bucket'] for r in rows]} {fixed}")
            print("        This is NOT a pass for the positive control.")
    else:
        print("  SKIP  live SetPreFrame check (objects absent) -- NOT a pass")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
