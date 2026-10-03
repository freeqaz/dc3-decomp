"""Milo editor schemas -> per-class property read lists.

The shipped ``system/run/**/*objects.dta`` files carry the Milo editor's
``(editor ...)`` block for every engine class: the property names a class
exposes through ``SyncProperty`` and their types, including structs, vectors
and arrays, plus ``(superclasses ...)``. That is the only class-generic,
already-on-disk description of "every property of an object", so the loader
probe (``loader.py``) uses it to decide what to read.

It is a *list of names to ask for*, never ground truth:

* a property the schema names but the C++ class does not sync answers
  ``{$o has (path)}`` false on BOTH sides and is recorded ``<absent>``;
* a property the schema omits is simply not read (a coverage gap, stated in
  the golden's manifest as the schema hash);
* the engine on each side decides the value. A property one side has and the
  other lacks is therefore a real finding (a SyncProperty difference).

Preprocessing is deliberately crude: ``#define NAME (body)`` macros are
expanded (``TRANSFORM``, ``VECTOR3``, ``COLOR``, ...); ``#ifdef``/``#ifndef``
/``#endif`` lines are dropped so every conditional block is included (an
extra name costs one ``<absent>`` read); ``#include`` is ignored because every
``*objects.dta`` file is read anyway.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

#: Where the shipped editor schemas live (relative to the repo root).
DEFAULT_SCHEMA_ROOT = Path("orig-assets/extracted/(..)/(..)/system/run")

_TOK = re.compile(r'''
      (?P<ws>\s+)
    | (?P<comment>;[^\n]*)
    | (?P<open>[(\[{])
    | (?P<close>[)\]}])
    | (?P<string>"(?:[^"\\]|\\.)*")
    | (?P<qsym>'[^']*')
    | (?P<atom>[^\s()\[\]{};"']+)
''', re.VERBOSE)


class Atom(str):
    """A bare DTA atom (symbol / number), distinct from a quoted string."""


def _tokenize(text: str) -> list:
    stack: list[list] = [[]]
    pos = 0
    while pos < len(text):
        m = _TOK.match(text, pos)
        if not m:
            raise ValueError(f"cannot tokenize at {pos}: {text[pos:pos + 40]!r}")
        pos = m.end()
        k = m.lastgroup
        if k in ("ws", "comment"):
            continue
        if k == "open":
            stack.append([])
        elif k == "close":
            done = stack.pop()
            stack[-1].append(done)
        elif k == "string":
            stack[-1].append(m.group()[1:-1])
        elif k == "qsym":
            stack[-1].append(Atom(m.group()[1:-1]))
        else:
            stack[-1].append(Atom(m.group()))
    while len(stack) > 1:  # tolerate a truncated file: close what is open
        done = stack.pop()
        stack[-1].append(done)
    return stack[0]


def _preprocess(text: str) -> tuple[str, dict[str, str]]:
    """Split ``#define`` bodies out of a file; drop other directives."""
    defines: dict[str, str] = {}
    out_lines: list[str] = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        s = lines[i].strip()
        if s.startswith("#define"):
            name = s.split()[1]
            # The body is the next balanced form, possibly over many lines.
            # Count parens outside "strings" and ;comments: help text such as
            # "(0 is highest res lod)" must not end (or extend) the body.
            body, depth, started, in_str = [], 0, False, False
            i += 1
            while i < len(lines):
                ln = lines[i]
                body.append(ln)
                for c in ln:
                    if in_str:
                        if c == '"':
                            in_str = False
                    elif c == '"':
                        in_str = True
                    elif c == ";":
                        break
                    elif c == "(":
                        depth += 1
                        started = True
                    elif c == ")":
                        depth -= 1
                i += 1
                if started and depth <= 0 and not in_str:
                    break
            defines[name] = "\n".join(body)
            continue
        if s.startswith("#"):
            # #include / #ifdef / #ifndef / #endif (possibly with a trailing
            # form after #endif, e.g. "#endif)"): keep anything after the
            # directive word that is structural.
            word = s.split()[0]
            rest = s[len(word):]
            if word.startswith("#endif"):
                rest = s[len("#endif"):]
                out_lines.append(rest)
            i += 1
            continue
        out_lines.append(lines[i])
        i += 1
    return "\n".join(out_lines), defines


def _expand(form, macros: dict[str, list]):
    if isinstance(form, list):
        out = []
        for x in form:
            if isinstance(x, Atom) and x in macros:
                out.extend(macros[x])
            else:
                out.append(_expand(x, macros))
        return out
    return form


@dataclass
class ClassSchema:
    name: str
    superclasses: list[str]
    editor: list            # raw (name type ...) entries, macros expanded
    types: dict[str, list]  # type name -> its own (editor ...) entries
    source: str


def load_schemas(root: Path) -> tuple[dict[str, ClassSchema], str]:
    """Parse every ``*objects.dta`` + macro file under ``root``.

    Returns ``(schemas, sha256)`` where the hash covers the exact bytes read,
    so a golden can state which schema decided its field list.
    """
    files = sorted(p for p in root.rglob("*.dta")
                   if p.name.endswith("objects.dta") or p.name.endswith("macros.dta"))
    if not files:
        raise FileNotFoundError(f"no *objects.dta under {root}")
    h = hashlib.sha256()
    texts: dict[Path, str] = {}
    raw_defines: dict[str, str] = {}
    for p in files:
        b = p.read_bytes()
        h.update(str(p.relative_to(root)).encode() + b"\0" + b)
        body, defs = _preprocess(b.decode("latin-1"))
        texts[p] = body
        raw_defines.update(defs)
    macros: dict[str, list] = {}
    for name, body in raw_defines.items():
        try:
            parsed = _tokenize(body)
        except ValueError:
            continue
        # "#define X\n(body)" -> X splices the CONTENTS of (body).
        macros[name] = parsed[0] if parsed and isinstance(parsed[0], list) else parsed
    # Macros may reference macros (BOX -> VECTOR3): expand to a fixed point.
    for _ in range(4):
        macros = {k: _expand(v, macros) for k, v in macros.items()}

    schemas: dict[str, ClassSchema] = {}
    for p, body in texts.items():
        if p.name.endswith("macros.dta"):
            continue
        for form in _tokenize(body):
            if not (isinstance(form, list) and form and isinstance(form[0], Atom)):
                continue
            form = _expand(form, macros)
            name = str(form[0])
            sup: list[str] = []
            editor: list = []
            types: dict[str, list] = {}
            for sec in form[1:]:
                if not (isinstance(sec, list) and sec and isinstance(sec[0], str)):
                    continue
                if sec[0] == "superclasses":
                    sup = [str(x) for x in sec[1:] if isinstance(x, str)]
                elif sec[0] == "editor":
                    editor = [e for e in sec[1:] if isinstance(e, list) and e]
                elif sec[0] == "types":
                    for t in sec[1:]:
                        if isinstance(t, list) and t and isinstance(t[0], str):
                            for tsec in t[1:]:
                                if isinstance(tsec, list) and tsec and tsec[0] == "editor":
                                    types[str(t[0])] = [e for e in tsec[1:]
                                                        if isinstance(e, list) and e]
            prev = schemas.get(name)
            if prev and not editor and not sup:
                continue  # a later bare (Class (types ...)) stub; keep the full one
            schemas[name] = ClassSchema(name, sup, editor, types,
                                        str(p.relative_to(root)))
    return schemas, h.hexdigest()


# ---------------------------------------------------------------------------
# Leaf extraction
# ---------------------------------------------------------------------------

#: Editor scalar type -> probe field kind. ``sym`` reads go through ``sprint``
#: so a schema that says ``symbol`` for what is really an int cannot fault.
SCALAR_KINDS = {
    "float": "float", "int": "int", "bitfield": "int", "bool": "bool",
    "symbol": "sym", "string": "sym", "file": "sym", "object": "obj",
    "color": "int",
}
SKIP_TYPES = {"script"}


@dataclass(frozen=True)
class Leaf:
    path: tuple            # property path, e.g. ("local_xfm", "x") or ("lods",)
    kind: str              # float|int|bool|sym|obj|size
    elem: object = None    # for kind == "size": the element type spec


def _type_of(entry: list):
    """``(name TYPE opts...)`` -> TYPE (an Atom or a list), or None."""
    return entry[1] if len(entry) > 1 else None


def leaves_for_type(tspec, path: tuple, out: list[Leaf], depth: int = 0) -> None:
    if depth > 6 or tspec is None:
        return
    if isinstance(tspec, str):
        t = str(tspec)
        if t in SKIP_TYPES:
            return
        kind = SCALAR_KINDS.get(t)
        if kind:
            out.append(Leaf(path, kind))
        return
    if isinstance(tspec, list) and tspec and isinstance(tspec[0], str):
        head = str(tspec[0])
        if head in ("struct", "vector"):
            subs = [str(x[0]) for x in tspec[1:]
                    if isinstance(x, list) and x and isinstance(x[0], str)]
            if set(subs) in ({"r", "g", "b"}, {"r", "g", "b", "a"}):
                # COLOR: Hmx::Color's PropSync is a LEAF (packed int) and
                # MILO_ASSERTs on a sub-path (obj/PropSync.cpp:33), so read
                # the packed value at the parent path.
                out.append(Leaf(path, "int"))
                return
            for sub in tspec[1:]:
                if isinstance(sub, list) and sub and isinstance(sub[0], str):
                    leaves_for_type(_type_of(sub), path + (str(sub[0]),), out, depth + 1)
        elif head == "array":
            elem = tspec[1] if len(tspec) > 1 else None
            out.append(Leaf(path, "size", _freeze(elem)))


def _freeze(x):
    if isinstance(x, list):
        return tuple(_freeze(y) for y in x)
    return str(x) if x is not None else None


def _thaw(x):
    if isinstance(x, tuple):
        return [_thaw(y) for y in x]
    return x


def class_chain(schemas: dict[str, ClassSchema], cls: str) -> list[str]:
    """``cls`` and every superclass, nearest first, each once."""
    seen: list[str] = []
    todo = [cls]
    while todo:
        c = todo.pop(0)
        if c in seen or c not in schemas:
            continue
        seen.append(c)
        todo.extend(schemas[c].superclasses)
    return seen


def class_leaves(schemas: dict[str, ClassSchema], cls: str,
                 obj_type: str = "") -> list[Leaf]:
    """Every readable leaf for an object of ``cls`` (and DTA type ``obj_type``)."""
    out: list[Leaf] = []
    seen_names: set[str] = set()
    for c in class_chain(schemas, cls):
        sch = schemas[c]
        entries = list(sch.editor)
        if obj_type and obj_type in sch.types:
            entries += sch.types[obj_type]
        for e in entries:
            if not isinstance(e[0], str):
                continue
            name = str(e[0])
            if name in seen_names or " " in name:
                continue
            seen_names.add(name)
            leaves_for_type(_type_of(e), (name,), out)
    return out


def element_leaves(elem_spec, path: tuple) -> list[Leaf]:
    out: list[Leaf] = []
    leaves_for_type(_thaw(elem_spec), path, out)
    return out


if __name__ == "__main__":  # pragma: no cover - inspection helper
    import sys
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SCHEMA_ROOT
    s, digest = load_schemas(root)
    print(f"{len(s)} classes, sha256 {digest[:16]}")
    for c in sys.argv[2:]:
        print(c, class_chain(s, c))
        for leaf in class_leaves(s, c):
            print("  ", leaf.path, leaf.kind, leaf.elem if leaf.kind == "size" else "")
    if len(sys.argv) <= 2:
        print(json.dumps({k: v.superclasses for k, v in list(s.items())[:20]}, indent=1))
