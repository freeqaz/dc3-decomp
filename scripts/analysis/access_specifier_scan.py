#!/usr/bin/env python3
"""access_specifier_scan.py — find members we declared with the WRONG ACCESS.

WHY THIS EXISTS
===============
MSVC encodes member access in the mangled name.  A method we declare `public`
that the image declares `protected` is a DIFFERENT SYMBOL, not a differently-
annotated one:

    target (ham_xbox_r.map)        ours
    ??_GJsonObject@@MAAPAXI@Z      ??_GJsonObject@@UAAPAXI@Z      M=protected vs U=public
    ??_ERndVelocityBuffer@@EAA...  ??_ERndVelocityBuffer@@UAA...  E=private   vs U=public

That makes the class structurally invisible to every ruler this project owns:

  * objdiff pairs symbols BY NAME, so the two never pair at all -- the row is
    `unresolved-target`, not a mismatch, and contributes no instruction diff.
  * `run_symbol_sweep(kind="vtable_slots")` filters a slot as a benign ICF fold
    when both sides resolve to the SAME ADDRESS.  These do: the bodies are
    identical, only the NAME differs.  So the sweep drops them by design.
    `docs/analysis/dispatch-data-rescan-20260818.md` called the JsonObject /
    RndVelocityBuffer pair "Cosmetic ICF naming" for a day on exactly that
    reasoning, then corrected itself -- it is a real decomp bug.

The access specifier is not cosmetic.  It is a fact about the original source
that we got wrong, it changes what compiles against the class, and for a
virtual it can change the vtable (a method whose access we mis-declare can
still bind, but a signature we get wrong alongside it will not).

WHAT IT DOES
------------
Reads every defined symbol from our built COFF objects and every symbol named
in the linker map, reduces each to an ACCESS-BLIND KEY (the mangled name with
the access/storage character blanked), and reports keys where our set of
access characters and the target's are DISJOINT.

Disjoint, not merely different, is deliberate: a symbol legitimately appears
under more than one spelling across objects, and a key where the two sides
share ANY spelling is not evidence of anything.

FINDING THE ACCESS CHARACTER
============================
It is the character immediately after the `@@` that terminates the QUALIFIED
NAME.  Finding that `@@` requires TOKENISING the name, because `@@` occurs all
over a mangled symbol:

    A-H private   I-P protected   Q-X public

⚠ RETRACTED 2026-09-16 — THIS DOCSTRING USED TO ARGUE FOR THE BUG IT HAD.
It said, where the paragraph above now stands:

    "The access character is the one immediately after the FINAL `@@`, which is
     the separator between the qualified name and the function's type encoding.
     Templates embed `@@` inside the name, so rfind is correct and find is not."

The observation about templates is true.  The conclusion drawn from it is
false, and the false half was the load-bearing one.  `rfind` takes the LAST
`@@` in the whole string, and for any member function whose PARAMETER LIST
names a class, the last `@@` belongs to that parameter's type:

    ?Load@RndFlare@@UAAXAAVBinStream@@@Z
                  ^^ the real terminator (access 'U')
                                    ^^ what rfind found (reads 'Z')

`Z` is not an access code, so the symbol was dropped — and it was never in the
denominator either, because the universe was computed as "the symbols that
parsed".  A drop that is also invisible to the denominator is the exact defect
`coverage.py` exists to prevent, committed inside a file that four agent briefs
were citing as the reference implementation of that contract.

Plain `find` is ALSO wrong, which is why the retracted paragraph existed: in
`?end@?$vector@VBone@@V?$allocator@VBone@@@stlp_std@@@stlp_std@@QAA…` the FIRST
`@@` sits inside the template argument list.  Neither end of the string is the
answer; the answer is a parse.

Measured on 2026-09-16, over the 107,493 distinct `?`-mangled symbols our 989
built objects define:

    rfind("@@")  yields a valid access code :  42,811  (39.8%)
    find("@@")   yields a valid access code :  61,048  (56.8%)
    tokenised                               : 107,493  (100%, 0 failures)

and the scan's own coverage block moved from `examined 20,648 / 42,811 = 48.2%`
to the figure this run prints, against a denominator that is now every symbol
our objects define rather than only the ones that happened to parse.

HOW THE TOKENISER WORKS
-----------------------
`code_index()` walks the `@`-separated fragments of the qualified name.  A
fragment is an identifier, a back-reference digit, an anonymous-namespace tag,
a local-scope tag (`?<ordinal>?<enclosing symbol>`), a nested symbol, or a
template-id `?$Name@<args>@` — and a template argument list can only be skipped
by actually parsing the MSVC TYPE grammar, because the `@`s inside it belong to
qualified names nested arbitrarily deep.  So `_type()` exists.  It is deliberate
that every construct it does not understand raises `MangleError` with a reason
and is COUNTED, rather than returning a plausible wrong index.

INDEPENDENTLY VALIDATED.  objdiff's demangler spells the access out in words in
`report.json`'s `metadata.demangled_name`.  Over the 41,106 symbols where both
instruments have an opinion, the tokeniser's access CLASS and the demangler's
word agree 41,106 times and disagree 0 times.  `--selftest` re-runs that check.

WHAT IT CANNOT SEE  (read this before calling a class exhausted)
---------------------------------------------------------------
* **Only ~990 of 2,223 target objects currently have a built counterpart.**  A
  wrong access specifier in a TU that does not build yet is invisible here.
  The coverage block states the object count on every run; it is NOT a
  whole-binary census and must not be quoted as one.
* A member the image never emitted as a standalone symbol (inlined away, or an
  unreferenced template instantiation) has no target spelling to disagree with.
  Counted as `absent-from-target` rather than silently skipped.
* **Static DATA members carry access too** (codes 0/1/2 = private/protected/
  public static data) and are NOT compared here.  That is a deliberate scope
  decision, not an oversight: this scanner's comparator was built and validated
  for member FUNCTIONS, and widening it would make "what changed" un-
  attributable.  They are counted as `static-data-member-not-compared`, so the
  size of the un-scanned class is on the printout rather than in a footnote.
* `static` vs non-static and near/far live in the same character, so a
  disagreement is reported as an access disagreement even when the real defect
  is storage class.  The rendered row prints both raw characters so the reader
  can tell which it is.
* Virtual-thunk symbols spell their adjustor where the access code would go
  (`??_9Foo@@$B…`) and carry NO access information at all; counted as
  `virtual-thunk-no-access-code`.

USAGE
-----
    python3 scripts/analysis/access_specifier_scan.py --selftest   # validate first
    python3 scripts/analysis/access_specifier_scan.py
    python3 scripts/analysis/access_specifier_scan.py --json out.json --fail-on 1

`--selftest` asserts the comparator fires on a known-divergent pair AND is
silent on an agreeing one, asserts the tokeniser on the four shapes that broke
`rfind` and `find`, then -- when the real corpus is present -- asserts all four
documented live instances are still found and cross-checks the tokeniser
against objdiff's demangler.  A sweep whose instrument has not been shown to
fire on a known instance is not evidence of anything.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402
from coffx import read_coff  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_MAP = os.path.join(REPO, "orig", "373307D9", "ham_xbox_r.map")
DEFAULT_OBJ_ROOT = os.path.join(REPO, "build", "373307D9", "src")
DEFAULT_TARGET_OBJ_ROOT = os.path.join(REPO, "build", "373307D9", "obj")
DEFAULT_REPORT = os.path.join(REPO, "build", "373307D9", "report.json")

PRIVATE = set("ABCDEFGH")
PROTECTED = set("IJKLMNOP")
PUBLIC = set("QRSTUVWX")
ACCESS_CHARS = PRIVATE | PROTECTED | PUBLIC

SYMBOL_RE = re.compile(r"\?[A-Za-z0-9_?$@]+")

# The four instances documented as live at the time this scanner was written.
# --selftest requires every one of them to still be found, so that a future
# change which quietly stops detecting the class fails loudly instead of
# printing a smaller, cleaner-looking number.
KNOWN_LIVE = [
    "??_GJsonObject@@",
    "??_EJsonObject@@",
    "??_GRndVelocityBuffer@@",
    "??_ERndVelocityBuffer@@",
]


# --------------------------------------------------------------------------- #
# MSVC mangled-name tokeniser.
#
# The ONE job here is to find the `@@` that terminates the qualified name.
# Everything else is the machinery needed to skip a template argument list
# without guessing, because the `@`s inside one belong to nested qualified
# names and there is no bracket to count.
# --------------------------------------------------------------------------- #

class MangleError(ValueError):
    """A construct the tokeniser does not implement.  Always counted, never
    absorbed into a plausible wrong answer."""

    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


#: Single-character builtin types.  (Z is the ellipsis / end marker.)
PRIMITIVES = frozenset("CDEFGHIJKMNOXZ")
#: Function codes that carry a cv/storage char between the access code and the
#: calling convention.  Static members (CDKLST) and free functions (YZ) do not.
CV_BEARING = frozenset("ABEFIJMNQRUV")
#: Thunk codes: an adjustor number sits between the access code and the cv char.
THUNK_CODES = frozenset("GHOPWX")

_MAX_DEPTH = 80


def _num_value(s, i):
    """(value, next_index) for an MSVC encoded number.

    A bare digit `d` encodes d+1; anything else is base-16 in 'A'..'P'
    terminated by '@'.  A leading '?' negates.  Getting this wrong is not
    academic: reading `Y06` as "0 dimensions" instead of "1 dimension of 7"
    desynchronised every `MakeString` instantiation in the binary.
    """
    neg = False
    if i < len(s) and s[i] == "?":
        neg = True
        i += 1
    if i >= len(s):
        raise MangleError("truncated-number")
    if s[i].isdigit():
        v = int(s[i]) + 1
        return (-v if neg else v), i + 1
    j, v = i, 0
    while j < len(s) and "A" <= s[j] <= "P":
        v = v * 16 + (ord(s[j]) - 65)
        j += 1
    if j == i or j >= len(s) or s[j] != "@":
        raise MangleError("bad-number")
    return (-v if neg else v), j + 1


def _num(s, i):
    return _num_value(s, i)[1]


def _type(s, i, depth=0):
    """Index just past one type encoding starting at s[i]."""
    if depth > _MAX_DEPTH:
        raise MangleError("type-depth")
    if i >= len(s):
        raise MangleError("truncated-type")
    c = s[i]
    if c.isdigit():                                   # back-reference
        return i + 1
    if c in PRIMITIVES:
        return i + 1
    if c == "_":                                      # _N bool, _J __int64, ...
        if i + 1 >= len(s):
            raise MangleError("truncated-ext-primitive")
        return i + 2
    if c in "VUT":                                    # class / struct / union
        return _qname(s, i + 1, depth + 1)
    if c == "W":                                      # enum
        if s[i + 1:i + 2] != "4":
            raise MangleError("enum-kind")
        return _qname(s, i + 2, depth + 1)
    if c in "PQRS":                                   # pointer, with cv
        n = s[i + 1:i + 2]
        if n == "6":                                  # pointer to function
            return _fn(s, i + 2, "Y", depth + 1)
        if n == "8":                                  # pointer to member fn
            return _fn(s, _qname(s, i + 2, depth + 1), "Q", depth + 1)
        k = i + 1
        while k < len(s) and s[k] in "EIF":           # __ptr64 __restrict __unaligned
            k += 1
        n = s[k:k + 1]
        if n and "A" <= n <= "D":
            return _type(s, k + 1, depth + 1)
        if n and "Q" <= n <= "T":                     # pointer to member data
            return _qname(s, _type(s, k + 1, depth + 1), depth + 1)
        raise MangleError("ptr-mod")
    if c in "AB":                                     # reference, with cv
        n = s[i + 1:i + 2]
        if n and "A" <= n <= "D":
            return _type(s, i + 2, depth + 1)
        if n == "6":
            return _fn(s, i + 2, "Y", depth + 1)
        if n == "8":
            return _fn(s, _qname(s, i + 2, depth + 1), "Q", depth + 1)
        raise MangleError("ref-mod")
    if c == "Y":                                      # array
        n, j = _num_value(s, i + 1)
        if not 0 < n <= 64:
            raise MangleError("array-dim-count")
        for _ in range(n):
            j = _num(s, j)
        return _type(s, j, depth + 1)
    if c == "$":
        n = s[i + 1:i + 2]
        if n == "$":
            k = s[i + 2:i + 3]
            if k in "CQR":                            # cv char, then the type
                return _type(s, i + 4, depth + 1)
            if k == "A":                              # function type
                m = s[i + 3:i + 4]
                if m == "6":
                    return _fn(s, i + 4, "Y", depth + 1)
                if m == "8":
                    return _fn(s, _qname(s, i + 4, depth + 1), "Q", depth + 1)
                raise MangleError("dollar-dollar-A")
            if k == "B":
                return _type(s, i + 3, depth + 1)
            if k in "TVZ":                            # nullptr_t, empty pack
                return i + 3
            if k == "Y":
                return _qname(s, i + 3, depth + 1)
            raise MangleError("dollar-dollar")
        if n == "0":                                  # integral non-type arg
            return _num(s, i + 2)
        if n == "2":                                  # real non-type arg
            return _num(s, _num(s, i + 2))
        if n in "DFGHIJQ":
            return _num(s, i + 2)
        if n in "1E":                                 # address of a symbol
            if s[i + 2:i + 3] == "?":
                return _symbol(s, i + 2, depth + 1)
            raise MangleError("dollar-addr")
        if n == "S":
            return i + 2
        raise MangleError("dollar")
    raise MangleError("type-" + c)


def _fn(s, i, code, depth):
    """Index past a function encoding that begins AFTER its access code.

    `code` selects the shape: a non-static member carries a cv/storage char
    before the calling convention, a static member or free function does not,
    and a thunk carries an adjustor number first.  Reading the cv char as the
    calling convention is what made `QAAXVSymbol@@@Z` unparseable.
    """
    if code in THUNK_CODES:
        i = _num(s, i)                                # adjustor
    if code in CV_BEARING or code in THUNK_CODES:
        if i >= len(s):
            raise MangleError("truncated-cv")
        i += 1                                        # cv / storage
    i += 1                                            # calling convention
    if i > len(s):
        raise MangleError("truncated-cc")
    if s[i:i + 1] == "@":                             # ctor/dtor: no return type
        i += 1
    else:
        if s[i:i + 1] == "?":                         # cv-qualified return
            i += 2
        i = _type(s, i, depth)
    while i < len(s):
        c = s[i]
        if c == "Z":                                  # ...XZ  (void parameters)
            return i + 1
        if c == "@":                                  # ...@Z
            if s[i + 1:i + 2] != "Z":
                raise MangleError("fn-param-end")
            return i + 2
        i = _type(s, i, depth)
    raise MangleError("truncated-fn-params")


def _targs(s, i, depth):
    """Index past a template argument list's terminating '@'."""
    while True:
        if i >= len(s):
            raise MangleError("truncated-targs")
        if s[i] == "@":
            return i + 1
        i = _type(s, i, depth)


def _sptok(s, i):
    """Index past a special-name TOKEN at s[i] == '?'  ('?0', '?_G', '?_R0').

    A top-level symbol spells it '?' + token (`??0Foo@@…`); a template-id
    spells it bare (`??$?0G@…` is '?' + '?$' + '?0' + args).
    """
    k = s[i + 1:i + 2]
    if not k:
        raise MangleError("truncated-special")
    if k != "_":
        return i + 2                                  # ?0..?9, ?A..?Z
    m = s[i + 2:i + 3]
    if not m:
        raise MangleError("truncated-special")
    if m in ("_", "R"):
        return i + 4                                  # ?__E, ?_R0
    return i + 3                                      # ?_7, ?_E, ?_G


def _template_id(s, i, depth):
    """Index past a template-id fragment `?$Name@<args>@` at s[i:i+2] == '?$'."""
    i += 2
    if i < len(s) and s[i] == "?":
        i = _sptok(s, i)                              # e.g. ?$?0 -> operator=
    else:
        j = s.find("@", i)
        if j < 0:
            raise MangleError("truncated-template-name")
        i = j + 1
    return _targs(s, i, depth + 1)


def _qname(s, i, depth=0):
    """Index just past the '@@' terminating the qualified name starting at s[i].

    The qualified name is a list of fragments, each followed by '@', with one
    extra '@' closing the list -- so the terminator is an EMPTY fragment.
    """
    if depth > _MAX_DEPTH:
        raise MangleError("qname-depth")
    while True:
        if i >= len(s):
            raise MangleError("truncated-qname")
        c = s[i]
        if c == "@":                                  # empty fragment: the end
            return i + 1
        if c.isdigit():                               # back-reference fragment
            i += 1
            continue
        if c == "?":
            n = s[i + 1:i + 2]
            if n == "$":
                i = _template_id(s, i, depth)
                continue
            if n == "A":                              # ?A@ / ?A0x1234@ anon ns
                j = s.find("@", i)
                if j < 0:
                    raise MangleError("truncated-anon-ns")
                i = j + 1
                continue
            if n.isdigit() or ("A" <= n <= "P"):
                # local scope: '?' <ordinal> '?' <enclosing mangled name>
                try:
                    j = _num(s, i + 1)
                except MangleError:
                    j = -1
                if j > 0 and s[j:j + 1] == "?":
                    i = _symbol(s, j + 1, depth + 1)
                    continue
            # A fragment that is itself a whole mangled symbol, as in the
            # dynamic initialiser ??__E?gFoo@Bar@@0V?$vector@…@@@@YAXXZ.
            # Unlike a local-scope fragment (self-delimiting), this one carries
            # its own '@' terminator: without consuming it, the LIST terminator
            # is read as the fragment's and the code character comes back '@'.
            # That was 96 symbols reported as `unrecognized-code-char` -- counted
            # rather than hidden, which is how it was found at all.
            i = _symbol(s, i, depth + 1)
            if s[i:i + 1] != "@":
                raise MangleError("nested-symbol-fragment-end")
            i += 1
            continue
        j = s.find("@", i)                            # plain identifier
        if j < 0:
            raise MangleError("truncated-fragment")
        i = j + 1


def _symbol(s, i, depth=0):
    """Index past a whole nested mangled symbol starting at s[i] == '?'."""
    j = code_index(s, i, depth)
    ch = s[j]
    if "A" <= ch <= "Z":
        return _fn(s, j + 1, ch, depth)
    if ch in "012345":                                # data: <type><cv>
        return _type(s, j + 1, depth) + 1
    if ch in "67":                                    # vftable / vbtable
        k = j + 1
        if k < len(s) and s[k] in "ABCD":
            k += 1
        if k < len(s) and s[k] == "@":
            k += 1
        return k
    if ch in "89":
        return j + 1
    raise MangleError("nested-code")


def code_index(s, i=0, depth=0):
    """Index of the ACCESS/STORAGE CODE character of the mangled name at s[i].

    Raises MangleError -- never returns a guess.  This is the whole point of
    the file: the previous `rfind("@@") + 2` could not fail, it could only be
    wrong, and a wrong index that happens to land on a letter in A-X is
    indistinguishable from a parse.
    """
    if i >= len(s) or s[i] != "?":
        raise MangleError("not-mangled")
    if s[i + 1:i + 2] == "?" and s[i + 2:i + 3] != "$":
        i = _sptok(s, i + 1)                          # '?' + special token
    else:
        i += 1
    j = _qname(s, i, depth)
    if j >= len(s):
        raise MangleError("truncated-after-qname")
    return j


# --------------------------------------------------------------------------- #
# Classification
# --------------------------------------------------------------------------- #
#: Shapes that are mangled names but structurally cannot carry member access.
#: Named and counted rather than filtered, so the printout says how big they are.
STRING_LITERAL_PREFIX = "??_C@"
RTTI_PREFIX = "??_R"

#: Drop reason per non-access code character.  '$' is a virtual thunk whose
#: adjustor occupies the slot; 0/1/2 are static DATA members, which DO carry
#: access but are a deliberate, counted non-goal (see the docstring).
CODE_DROP_REASON = {
    "Y": "free-function-no-access",
    "Z": "free-function-no-access",
    "0": "static-data-member-not-compared",
    "1": "static-data-member-not-compared",
    "2": "static-data-member-not-compared",
    "3": "data-storage-code-no-member-access",
    "4": "data-storage-code-no-member-access",
    "5": "data-storage-code-no-member-access",
    "6": "data-storage-code-no-member-access",
    "7": "data-storage-code-no-member-access",
    "8": "data-storage-code-no-member-access",
    "9": "data-storage-code-no-member-access",
    "$": "virtual-thunk-no-access-code",
}


def access_class(ch: str) -> str:
    if ch in PRIVATE:
        return "private"
    if ch in PROTECTED:
        return "protected"
    if ch in PUBLIC:
        return "public"
    return "?"


def classify_name(name: str):
    """(key, access_char, drop_reason) for one symbol name.

    Exactly one of (key, access_char) / drop_reason is non-None, so a caller
    cannot forget to account for a name.
    """
    if not name.startswith("?"):
        return None, None, "not-an-msvc-mangled-name"
    if name.startswith(STRING_LITERAL_PREFIX):
        return None, None, "string-literal"
    if name.startswith(RTTI_PREFIX):
        return None, None, "rtti-descriptor"
    try:
        i = code_index(name)
    except MangleError as e:
        return None, None, "unparsable-mangled-name:" + e.reason
    ch = name[i]
    if ch in ACCESS_CHARS:
        return name[:i] + "\x00" + name[i + 1:], ch, None
    return None, None, CODE_DROP_REASON.get(ch, "unrecognized-code-char")


def access_blind_key(name: str):
    """(key, access_char) for a mangled member symbol, else None.

    Thin wrapper over `classify_name` kept because the selftest, the tests and
    `load_target` all want the "is this a comparable member symbol" question
    without the drop bookkeeping.
    """
    key, ch, _reason = classify_name(name)
    return None if key is None else (key, ch)


# --------------------------------------------------------------------------- #
# Corpus
# --------------------------------------------------------------------------- #
def load_target(map_path: str):
    """access-blind key -> set of access chars seen in the linker map."""
    tgt = {}
    with open(map_path, errors="replace") as f:
        for line in f:
            for tok in SYMBOL_RE.findall(line):
                k = access_blind_key(tok)
                if k:
                    tgt.setdefault(k[0], set()).add(k[1])
    return tgt


def load_our_symbol_names(obj_root: str):
    """(sorted distinct defined symbol names, n_objects, n_unreadable).

    THE DENOMINATOR PASS.  It does no classification at all -- it does not even
    look at the leading '?' -- so `universe` cannot be `examined + drops` by
    construction and coverage.py's arithmetic check is a real check.  The old
    universe was `len(ours)`, i.e. "the symbols my parser accepted", which made
    every parse failure invisible to the very block that exists to expose it.
    """
    names = set()
    n_obj = 0
    n_unreadable = 0
    for path in sorted(glob.glob(os.path.join(obj_root, "**", "*.obj"), recursive=True)):
        n_obj += 1
        try:
            with open(path, "rb") as f:
                _secs, syms = read_coff(f.read())
        except OSError:
            n_unreadable += 1
            continue
        if not syms:
            n_unreadable += 1
            continue
        for s in syms:
            name = getattr(s, "name", "") or ""
            if name:
                names.add(name)
    return sorted(names), n_obj, n_unreadable


def classify_ours(names, cov: CoverageReport):
    """(key -> access chars, key -> n names, reasons, examples).

    Every name in `names` is either mapped to a key or routed through
    `cov.drop()`.  Names that map to a key are accounted for later, by the
    comparator, because whether they were COMPARED depends on the target side.
    """
    ours = {}
    key_names = {}
    reasons = {}
    examples = {}
    for name in names:
        key, ch, reason = classify_name(name)
        if reason is not None:
            # `unparsable-mangled-name:<why>` is split so the coverage block
            # shows one line per shape, which is what makes an un-implemented
            # construct visible instead of merely absent.
            head = reason.split(":", 1)[0]
            reasons[reason] = reasons.get(reason, 0) + 1
            examples.setdefault(reason, name)
            cov.drop(head if head != reason else reason)
            continue
        ours.setdefault(key, set()).add(ch)
        key_names[key] = key_names.get(key, 0) + 1
    return ours, key_names, reasons, examples


def compare(ours: dict, tgt: dict, cov: CoverageReport, key_names=None):
    """Returns (findings, n_agree, n_partial). Routes every discard through cov.

    Accounting is per NAME, not per key: `key_names[key]` is how many distinct
    mangled spellings collapsed into that key, and the coverage denominator is
    a count of names.  With no `key_names` (the synthetic tests) each key
    stands for one name.
    """
    findings = []
    n_agree = 0
    n_partial = 0
    for key in sorted(ours):
        weight = 1 if key_names is None else key_names.get(key, 1)
        our_acc = ours[key]
        tgt_acc = tgt.get(key)
        if not tgt_acc:
            cov.drop("absent-from-target", weight,
                     note="inlined away or never emitted standalone by the image")
            continue
        cov.examine(weight)
        if our_acc == tgt_acc:
            n_agree += 1
            continue
        if our_acc & tgt_acc:
            # Shares a spelling -- not evidence. Counted, not reported.
            n_partial += 1
            continue
        findings.append({
            "symbol": key.replace("\x00", "?"),
            "ours": sorted(our_acc),
            "target": sorted(tgt_acc),
            "ours_access": access_class(sorted(our_acc)[0]),
            "target_access": access_class(sorted(tgt_acc)[0]),
        })
    return findings, n_agree, n_partial


# --------------------------------------------------------------------------- #
# Selftest
# --------------------------------------------------------------------------- #
#: The four shapes the retracted docstring got wrong, with the character each
#: wrong spelling reads.  Used by --selftest and pinned in
#: scripts/analysis/tests/test_access_specifier_scan.py.
PARSE_PINS = [
    # (symbol, correct access char, what rfind reads, what find reads)
    ("?SendDoneImpl@StreamReceiver360@@UAA_NXZ", "U", "U", "U"),
    ("?Load@RndFlare@@UAAXAAVBinStream@@@Z", "U", "Z", "U"),
    ("?end@?$vector@DV?$StlNodeAlloc@D@stlpmtx_std@@@stlpmtx_std@@QAAPADXZ",
     "Q", "Q", "D"),
    ("?kMThd@MidiChunkID@@2V1@B", None, None, None),   # static data: code '2'
]


def selftest(args=None) -> int:
    """Fire on a known divergence; stay silent on agreement. Then the real corpus."""
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    # -- comparator, on synthetic input -------------------------------------
    cov = CoverageReport("selftest", allow_truncation=True)
    cov.universe(4, "synthetic keys")
    ours = {
        "?A@C@@\x00AAXXZ": {"U"},      # public,    target protected -> FINDING
        "?B@C@@\x00AAXXZ": {"Q"},      # agrees
        "?C@C@@\x00AAXXZ": {"U", "M"}, # partial overlap -> NOT a finding
        "?D@C@@\x00AAXXZ": {"U"},      # absent from target
    }
    tgt = {
        "?A@C@@\x00AAXXZ": {"M"},
        "?B@C@@\x00AAXXZ": {"Q"},
        "?C@C@@\x00AAXXZ": {"M"},
    }
    found, n_agree, n_partial = compare(ours, tgt, cov)
    syms = [f["symbol"] for f in found]
    check("fires on a disjoint access pair", syms == ["?A@C@@?AAXXZ"])
    check("silent on an exact agreement", n_agree == 1)
    check("partial overlap counted, not reported", n_partial == 1)
    check("absent-from-target is dropped, not examined",
          cov.as_dict()["dropped"].get("absent-from-target") == 1)
    check("denominator balances", cov.unaccounted == 0)

    # Negative control: correct our side, the finding MUST disappear.
    cov2 = CoverageReport("selftest-neg", allow_truncation=True)
    cov2.universe(1, "synthetic keys")
    fixed, _, _ = compare({"?A@C@@\x00AAXXZ": {"M"}}, {"?A@C@@\x00AAXXZ": {"M"}}, cov2)
    check("negative control: fixing our access clears the finding", fixed == [])

    # -- the tokeniser, on the four shapes the old docstring got wrong ------
    for sym, want, from_rfind, from_find in PARSE_PINS:
        got = access_blind_key(sym)
        got_ch = None if got is None else got[1]
        check(f"tokenises {sym[:52]:52s} -> {want}", got_ch == want)
        if want is not None and from_rfind != want:
            i = sym.rfind("@@")
            check("  ...and rfind would have read "
                  f"{sym[i + 2]!r}, which is why it was dropped",
                  sym[i + 2] != want)
        if want is not None and from_find != want:
            i = sym.find("@@")
            check(f"  ...and find would have read {sym[i + 2]!r}", sym[i + 2] != want)
    check("a static data member is NOT reported as a member-function access",
          access_blind_key("?kMThd@MidiChunkID@@2V1@B") is None)
    check("a global data symbol is ignored", access_blind_key("?BITMAP_REV@@3EA") is None)
    check("a free function is ignored",
          access_blind_key("?PathName@@YAPBDPBVObject@Hmx@@@Z") is None)
    check("real dtor manglings parse to the documented access",
          access_blind_key("??_GJsonObject@@MAAPAXI@Z")[1] == "M"
          and access_blind_key("??_ERndVelocityBuffer@@EAAPAXI@Z")[1] == "E")
    check("an unparsable name RAISES rather than returning a guess",
          isinstance(_raises(lambda: code_index("?broken@")), MangleError))

    # -- the real corpus, when present --------------------------------------
    map_path = getattr(args, "map", None) or DEFAULT_MAP
    obj_root = getattr(args, "obj_root", None) or DEFAULT_OBJ_ROOT
    report = getattr(args, "report", None) or DEFAULT_REPORT
    if os.path.exists(map_path) and os.path.isdir(obj_root):
        cov3 = CoverageReport("selftest-live", allow_truncation=True)
        tgt_live = load_target(map_path)
        names, n_obj, _ = load_our_symbol_names(obj_root)
        cov3.universe(len(names), "our defined symbols")
        ours_live, key_names, reasons, _ex = classify_ours(names, cov3)
        live, _, _ = compare(ours_live, tgt_live, cov3, key_names)
        got = {f["symbol"] for f in live}
        for known in KNOWN_LIVE:
            check(f"still detects documented live instance {known}",
                  any(s.startswith(known) for s in got))
        check("live corpus was non-empty", n_obj > 0 and len(tgt_live) > 0)
        n_unparsable = sum(n for r, n in reasons.items()
                           if r.startswith("unparsable-mangled-name"))
        check(f"tokeniser parses every mangled symbol our objects define "
              f"({n_unparsable} failures)", n_unparsable == 0)

        # INDEPENDENT INSTRUMENT: objdiff's demangler spells access in words.
        agree, disagree, ex = cross_check_demangler(report)
        if agree + disagree == 0:
            print("  SKIP  demangler cross-check (report.json absent)")
        else:
            check(f"objdiff's demangler agrees on all {agree} symbols it "
                  f"and the tokeniser both decide ({disagree} disagreements)",
                  disagree == 0)
            for e in ex[:5]:
                print(f"        MISMATCH {e}")
    else:
        print("  SKIP  live-corpus checks (map or built objects absent)")
        print("        This is NOT a pass: the comparator was exercised, the")
        print("        extractor was not. Build the tree and re-run.")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def _raises(fn):
    try:
        fn()
    except Exception as e:                      # noqa: BLE001 - the value IS the result
        return e
    return None


def cross_check_demangler(report_path: str):
    """(n_agree, n_disagree, examples) against objdiff's `demangled_name`.

    The demangler is a genuinely independent decode of the same string, so this
    is the check that the tokeniser is RIGHT rather than merely total.  A
    `[thunk]:` prefix is stripped: it precedes the access word, it does not
    replace it.
    """
    if not os.path.exists(report_path):
        return 0, 0, []
    with open(report_path) as f:
        rep = json.load(f)
    agree = disagree = 0
    examples = []
    seen = set()
    for u in sorted(rep.get("units", []), key=lambda x: x.get("name", "")):
        for fn in u.get("functions", []):
            name = fn.get("name", "")
            if not name.startswith("?") or name in seen:
                continue
            seen.add(name)
            dm = (fn.get("metadata") or {}).get("demangled_name")
            if not dm:
                continue
            if dm.startswith("[thunk]:"):
                dm = dm[len("[thunk]:"):].strip()
            want = None
            for word in ("public:", "protected:", "private:"):
                if dm.startswith(word):
                    want = word[:-1]
                    break
            try:
                i = code_index(name)
            except MangleError:
                continue
            got = access_class(name[i])
            got = got if got != "?" else None
            if got == want:
                agree += 1
            else:
                disagree += 1
                if len(examples) < 10:
                    examples.append((name, name[i], got, want, dm[:70]))
    return agree, disagree, examples


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--map", default=DEFAULT_MAP, help="MSVC linker map (target truth)")
    ap.add_argument("--obj-root", default=DEFAULT_OBJ_ROOT, help="our built COFF objects")
    ap.add_argument("--target-obj-root", default=DEFAULT_TARGET_OBJ_ROOT,
                    help="target objects, counted only to state build coverage")
    ap.add_argument("--report", default=DEFAULT_REPORT,
                    help="report.json, used ONLY by --selftest's demangler cross-check")
    ap.add_argument("--json", default=None, help="write findings to this path")
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT only; the counts above it are complete "
                         "(prints 'showing N of M')")
    ap.add_argument("--fail-on", type=int, default=0,
                    help="exit 1 when findings >= N (0 disables)")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest(args)

    # Rule 4: a missing input is never a clean verdict.
    if not os.path.exists(args.map):
        print(f"INCONCLUSIVE: linker map not found: {args.map}")
        print("The target side of this comparison is unavailable; this run checked nothing.")
        return EXIT_NO_INPUT
    if not os.path.isdir(args.obj_root):
        print(f"INCONCLUSIVE: built objects not found: {args.obj_root}")
        print("Run `ninja` first; an unbuilt tree cannot be compared against the map.")
        return EXIT_NO_INPUT

    cov = CoverageReport("access_specifier_scan", args=args)
    cov.require_examined("no symbol was comparable against the map")

    tgt = load_target(args.map)
    names, n_obj, n_unreadable = load_our_symbol_names(args.obj_root)
    cov.universe(len(names), "distinct symbols defined by our built objects")
    ours, key_names, reasons, examples = classify_ours(names, cov)

    n_mangled = sum(1 for n in names if n.startswith("?"))
    n_target_obj = len(glob.glob(os.path.join(args.target_obj_root, "**", "*.obj"),
                                 recursive=True)) if os.path.isdir(args.target_obj_root) else 0
    if n_target_obj:
        cov.note(f"our build covers {n_obj} of {n_target_obj} target objects "
                 f"({100.0 * n_obj / n_target_obj:.1f}%) -- a wrong access specifier in a TU "
                 f"that does not build yet is NOT visible to this scan")
    if n_unreadable:
        cov.note(f"{n_unreadable} object(s) unreadable or symbol-less")
    cov.note("target truth is the linker map; the access char is the one after the "
             "'@@' that TERMINATES THE QUALIFIED NAME, found by tokenising (rfind "
             "lands on a class parameter's '@@'; find lands inside a template arg list)")

    n_unparsable = sum(n for r, n in reasons.items()
                       if r.startswith("unparsable-mangled-name"))
    if n_unparsable:
        cov.note(f"{n_unparsable} mangled symbol(s) the tokeniser cannot parse -- "
                 f"listed below by construct; each is a SHAPE THIS SCAN CANNOT SEE")
        for reason in sorted(r for r in reasons if r.startswith("unparsable-")):
            cov.note(f"    {reason}: {reasons[reason]}  e.g. {examples[reason][:90]}")
    if "unrecognized-code-char" in reasons:
        cov.note(f"unrecognized-code-char e.g. {examples['unrecognized-code-char'][:90]}")

    cov.extra("our_objects", n_obj)
    cov.extra("target_objects", n_target_obj)
    cov.extra("target_keys", len(tgt))
    cov.extra("mangled_symbols", n_mangled)
    cov.extra("access_bearing_keys", len(ours))
    cov.extra("drop_reasons_detail", dict(sorted(reasons.items())))

    findings, n_agree, n_partial = compare(ours, tgt, cov, key_names)
    cov.extra("agree_exactly", n_agree)
    cov.extra("partial_overlap", n_partial)
    cov.extra("findings", len(findings))
    cov.note(f"examined {cov.as_dict()['examined']} of {n_mangled} MANGLED symbols "
             f"({100.0 * cov.as_dict()['examined'] / n_mangled:.2f}%) -- the universe "
             f"above is every defined symbol, mangled or not, so the arithmetic check "
             f"is computed over a population this scanner did not choose")

    print(f"ACCESS-SPECIFIER DIVERGENCES: {len(findings)}")
    print(f"  compared against map : {n_agree + n_partial + len(findings)}  (keys)")
    print(f"  agree exactly        : {n_agree}")
    print(f"  partial overlap      : {n_partial}  (share a spelling -- not evidence)")
    print()
    shown = findings if args.limit <= 0 else findings[: args.limit]
    if args.limit > 0 and len(shown) < len(findings):
        print(f"  showing {len(shown)} of {len(findings)}")
    for f in shown:
        print(f"  ours={''.join(f['ours'])}({f['ours_access']:9s}) "
              f"target={''.join(f['target'])}({f['target_access']:9s})  {f['symbol']}")

    # Distinct declarations: ??_E and ??_G are two thunks of ONE declaration.
    decls = sorted({re.sub(r"^\?\?_[EG]", "??_*", f["symbol"]) for f in findings})
    print(f"\n  distinct declarations behind those rows: {len(decls)}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"findings": findings, "_coverage": cov.as_dict()}, fh, indent=2)

    rc = cov.emit()
    if rc:
        return rc
    if args.fail_on and len(findings) >= args.fail_on:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
