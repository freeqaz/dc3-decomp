#!/usr/bin/env python3
"""Dump vtable layout from original COFF .obj files.

Reads COFF symbol and relocation tables to reconstruct vtable entries,
mapping each slot to the actual function symbol. ICF-merged symbols are
noted so you can identify which virtual function each slot corresponds to.

Usage:
    python3 scripts/dump_vtable.py <class_name> [--obj <path>]
    python3 scripts/dump_vtable.py RndFontBase
    python3 scripts/dump_vtable.py RndFont3d --obj build/373307D9/obj/system/rndobj/Font3d.obj

    # Show only the two slots loaded by a virtual call mismatch diff:
    python3 scripts/dump_vtable.py RndDrawable --diff-pair 0x14 0x18

    # Show only the slot at a specific byte offset:
    python3 scripts/dump_vtable.py RndDrawable --offset 0x14

If --obj is not given, searches build/373307D9/obj/ for a matching .obj file.

The "Annotation" column shows the virtual function name inferred from the
parent class chain or from the class header. It deliberately falls back to
no annotation (rather than guessing) when it can't prove the mapping.
"""

import argparse
import glob
import os
import re
import struct
import subprocess
import sys


# ---------------------------------------------------------------------------
# COFF parsing
# ---------------------------------------------------------------------------

def read_coff_symbols(data):
    """Parse COFF symbol table and string table."""
    machine, num_sections, timestamp, symtab_offset, num_symbols, opt_hdr_size, flags = \
        struct.unpack_from('<HHIIIHH', data, 0)

    # String table immediately after symbol table
    strtab_offset = symtab_offset + num_symbols * 18
    strtab_size = struct.unpack_from('<I', data, strtab_offset)[0]
    strtab = data[strtab_offset:strtab_offset + strtab_size]

    def get_name(offset):
        if data[offset:offset + 4] == b'\x00\x00\x00\x00':
            str_offset = struct.unpack_from('<I', data, offset + 4)[0]
            end = strtab.index(b'\x00', str_offset)
            return strtab[str_offset:end].decode('ascii', errors='replace')
        else:
            return data[offset:offset + 8].rstrip(b'\x00').decode('ascii', errors='replace')

    # Read all symbols
    symbols = []
    i = 0
    while i < num_symbols:
        sym_offset = symtab_offset + i * 18
        name = get_name(sym_offset)
        value, section, type_val, storage, aux_count = \
            struct.unpack_from('<IhHBB', data, sym_offset + 8)
        symbols.append({
            'index': i,
            'name': name,
            'value': value,
            'section': section,
            'type': type_val,
            'storage': storage,
            'aux_count': aux_count,
        })
        i += 1 + aux_count

    # Read section headers
    section_hdr_offset = 20 + opt_hdr_size
    sections = []
    for s in range(num_sections):
        hdr_off = section_hdr_offset + s * 40
        sec_name_raw = data[hdr_off:hdr_off + 8].rstrip(b'\x00')
        if sec_name_raw.startswith(b'/'):
            # Long section name - offset into string table
            str_off = int(sec_name_raw[1:].decode('ascii'))
            end = strtab.index(b'\x00', str_off)
            sec_name = strtab[str_off:end].decode('ascii', errors='replace')
        else:
            sec_name = sec_name_raw.decode('ascii', errors='replace')
        vsize, vaddr, raw_size, raw_offset, reloc_offset, linenum_offset, \
            num_relocs, num_linenums, characteristics = \
            struct.unpack_from('<IIIIIIHHI', data, hdr_off + 8)
        sections.append({
            'name': sec_name,
            'vsize': vsize,
            'raw_size': raw_size,
            'raw_offset': raw_offset,
            'reloc_offset': reloc_offset,
            'num_relocs': num_relocs,
            'characteristics': characteristics,
        })

    return symbols, sections


def find_vtable(data, symbols, sections, class_name):
    """Find vtable symbol and read its relocation entries (primary vtable).

    Preference order:
      1. ??_7<Class>@@6B@                  (true primary; exists for vbase classes)
      2. ??_7<Class>@@6BObject@Hmx@@@      (Object subobject vtable)
      3. ??_7<Class>@@6BObjectDir@@@       (if class derives from ObjectDir)
      4. First ??_7<Class>... seen
    """
    primary_name = f'??_7{class_name}@@6B@'
    object_sub_name = f'??_7{class_name}@@6BObject@Hmx@@@'
    objectdir_sub_name = f'??_7{class_name}@@6BObjectDir@@@'

    vtable_sym = None
    fallback = None
    for sym in symbols:
        if sym['name'] == primary_name:
            vtable_sym = sym
            break
        if sym['name'] == object_sub_name and vtable_sym is None:
            vtable_sym = sym
            # don't break — keep looking for primary
        if (vtable_sym is None and sym['name'] == objectdir_sub_name
                and fallback is None):
            fallback = sym
        if fallback is None and sym['name'].startswith(f'??_7{class_name}') and '6B' in sym['name']:
            fallback = sym

    if vtable_sym is None:
        vtable_sym = fallback
    if vtable_sym is None:
        return None, None

    # Find the section containing the vtable
    sec_idx = vtable_sym['section'] - 1  # 1-based
    if sec_idx < 0 or sec_idx >= len(sections):
        return vtable_sym, []

    section = sections[sec_idx]

    # Build symbol index lookup
    sym_by_idx = {}
    for sym in symbols:
        sym_by_idx[sym['index']] = sym

    # Read relocations for this section
    entries = []
    for r in range(section['num_relocs']):
        rel_off = section['reloc_offset'] + r * 10
        rva, sym_idx, rel_type = struct.unpack_from('<IIH', data, rel_off)
        target_sym = sym_by_idx.get(sym_idx, {'name': f'<unknown_{sym_idx}>'})
        entries.append({
            'offset': rva,
            'type': rel_type,
            'symbol': target_sym['name'],
        })

    # Sort by byte offset so slot 0 is at the start
    entries.sort(key=lambda e: e['offset'])
    return vtable_sym, entries


def demangle_symbol(mangled):
    """Try to demangle a MSVC mangled name."""
    try:
        result = subprocess.run(
            ['c++filt', '-n', mangled],
            capture_output=True, text=True, timeout=5
        )
        demangled = result.stdout.strip()
        if demangled != mangled:
            return demangled
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Basic manual demangling for common patterns
    if mangled.startswith('??_G'):
        # Scalar deleting destructor
        cls = mangled[4:].split('@@')[0]
        return f'{cls}::~{cls}() [scalar deleting]'
    if mangled.startswith('??_E'):
        # Vector deleting destructor (used by virtual inheritance thunks)
        cls = mangled[4:].split('@@')[0]
        return f'{cls}::~{cls}() [vector deleting]'
    if mangled.startswith('??1'):
        cls = mangled[3:].split('@@')[0]
        return f'{cls}::~{cls}()'
    if mangled.startswith('?'):
        parts = mangled[1:].split('@')
        if len(parts) >= 2:
            method = parts[0]
            cls = parts[1]
            return f'{cls}::{method}'

    return mangled


# Parse a mangled MSVC function name to extract just the unqualified method name
# and the class/namespace where it was declared. Used to identify what virtual
# a slot's resolved symbol corresponds to.
_MANGLED_METHOD_RE = re.compile(r'^\?(?:\?_[GE])?(?P<method>[A-Za-z_][A-Za-z_0-9]*)@(?P<scope>(?:[A-Za-z_][A-Za-z_0-9]*@)*)@')


def parse_mangled_method(mangled):
    """Parse a mangled MSVC name into (method_name, class_path).

    class_path is innermost-first, e.g. for ?Foo@Bar@Baz@@... returns
    ('Foo', ['Bar', 'Baz']). Returns (None, None) for things like
    OnlyReturns or merged_Returns1 that aren't class methods.
    """
    if not mangled or not mangled.startswith('?'):
        return (None, None)
    # ??_G (scalar deleting dtor) / ??_E (vector deleting dtor)
    if mangled.startswith('??_G') or mangled.startswith('??_E'):
        # Pattern: ??_G<Class>@@... or ??_G<Class>@<NS>@... (with vtordisp thunk)
        body = mangled[4:]
        # The class is the first @-separated token before @@ or thunk
        m = re.match(r'^([A-Za-z_][A-Za-z_0-9]*)@', body)
        if not m:
            return (None, None)
        return ('~ctor', [m.group(1)])
    if mangled.startswith('??1'):
        body = mangled[3:]
        m = re.match(r'^([A-Za-z_][A-Za-z_0-9]*)@', body)
        if not m:
            return (None, None)
        return ('~ctor', [m.group(1)])
    # Regular method: ?Method@<Class>@<NS...>@@<sig>
    m = re.match(r'^\?([A-Za-z_][A-Za-z_0-9]*)@(.+?)@@', mangled)
    if not m:
        return (None, None)
    method = m.group(1)
    scope_part = m.group(2)
    # Split scope by @, innermost first
    scope = scope_part.split('@')
    return (method, scope)


# ---------------------------------------------------------------------------
# Ground-truth virtual layout for Hmx::Object
#
# Each entry: (name, is_const) — cv-qualifier matters because a derived
# class's `void Print() const` does NOT override `void Print()` (the binary
# treats them as separate slots).
# Order matches ??_7Object@Hmx@@6B@ in build/373307D9/obj/system/obj/Object.obj.
# ---------------------------------------------------------------------------
OBJECT_VIRTUALS_INFO = [
    ('~Object',         False),  # 0: scalar deleting dtor
    ('RefOwner',        True),   # 1 (const)
    ('Replace',         False),  # 2
    ('ClassName',       True),   # 3 (const, from OBJ_CLASSNAME macro)
    ('SetType',         False),  # 4 (from OBJ_SET_TYPE macro)
    ('Handle',          False),  # 5
    ('SyncProperty',    False),  # 6
    ('InitObject',      False),  # 7
    ('Save',            False),  # 8
    ('Copy',            False),  # 9
    ('Load',            False),  # 10
    ('PreSave',         False),  # 11
    ('PostSave',        False),  # 12
    ('Print',           False),  # 13 (NOT const — distinct from `Print() const`)
    ('Export',          False),  # 14
    ('SetTypeDef',      False),  # 15
    ('ObjectDef',       False),  # 16
    ('SetName',         False),  # 17
    ('DataDir',         False),  # 18
    ('PreLoad',         False),  # 19
    ('PostLoad',        False),  # 20
    ('FindPathName',    False),  # 21
]
OBJECT_VIRTUALS = [n for n, _c in OBJECT_VIRTUALS_INFO]


# RndHighlightable is a virtual base in DC3's class hierarchy. Its dtor
# overrides Hmx::Object's dtor, so the dtor lives in the Object subobject
# vtable. The RndHighlightable subobject vtable holds only `Highlight`,
# the one new virtual it introduces.
RNDHIGHLIGHTABLE_SUBOBJECT_VIRTUALS = [
    'Highlight',          # 0
]


# Manual overrides for classes whose headers are unparseable (templates, macros)
# or which need a curated layout. Key is the class name; value is the list of
# new virtuals (in declaration order, beyond the parent class's vtable).
MANUAL_VIRTUAL_LAYOUTS = {
    'Hmx::Object': OBJECT_VIRTUALS,
    'Object::Hmx': OBJECT_VIRTUALS,  # alias used in mangled symbols
    'Object': OBJECT_VIRTUALS,
}


# ---------------------------------------------------------------------------
# C++ header parser
# ---------------------------------------------------------------------------

# Permissive regex to find the declaration line of `class FOO : public BAR, ...`.
_CLASS_DECL_RE = re.compile(
    r'\bclass\s+(?P<name>[A-Za-z_][A-Za-z_0-9]*)\b\s*(?::\s*(?P<bases>[^{;]+))?\s*\{',
    re.MULTILINE,
)


def _strip_comments(text):
    """Strip /* ... */ and // ... comments from a C++ text fragment."""
    # Remove block comments
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Remove line comments
    text = re.sub(r'//[^\n]*', '', text)
    return text


def _split_top_level(text, sep=','):
    """Split text by `sep` but respect <> () [] {} nesting."""
    out = []
    depth_angle = 0
    depth_paren = 0
    depth_brace = 0
    depth_brack = 0
    cur = []
    for ch in text:
        if ch == '<':
            depth_angle += 1
        elif ch == '>':
            depth_angle -= 1
        elif ch == '(':
            depth_paren += 1
        elif ch == ')':
            depth_paren -= 1
        elif ch == '{':
            depth_brace += 1
        elif ch == '}':
            depth_brace -= 1
        elif ch == '[':
            depth_brack += 1
        elif ch == ']':
            depth_brack -= 1
        if ch == sep and depth_angle == 0 and depth_paren == 0 \
                and depth_brace == 0 and depth_brack == 0:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if cur:
        out.append(''.join(cur).strip())
    return out


def _find_class_body(text, class_name):
    """Find the body (text between `{` and matching `}`) of `class <class_name>`."""
    pattern = re.compile(
        r'\bclass\s+' + re.escape(class_name) + r'\b\s*(?::\s*([^{;]+))?\s*\{',
        re.MULTILINE,
    )
    m = pattern.search(text)
    if not m:
        return None, None
    bases_str = m.group(1) or ''
    body_start = m.end()
    # Find matching brace
    depth = 1
    i = body_start
    while i < len(text):
        ch = text[i]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return bases_str, text[body_start:i]
        i += 1
    return bases_str, None


def _parse_bases(bases_str):
    """Parse a base-class spec like 'public virtual RndHighlightable, public Foo'
    into a list of dicts: [{'name': 'RndHighlightable', 'virtual': True, 'access': 'public'}, ...].
    """
    if not bases_str.strip():
        return []
    result = []
    for piece in _split_top_level(bases_str):
        piece = piece.strip()
        if not piece:
            continue
        is_virtual = False
        access = 'private'  # default for `class`
        toks = piece.split()
        # Eat leading access/virtual qualifiers
        i = 0
        while i < len(toks):
            t = toks[i]
            if t in ('public', 'protected', 'private'):
                access = t
                i += 1
            elif t == 'virtual':
                is_virtual = True
                i += 1
            else:
                break
        if i >= len(toks):
            continue
        # Remaining is a (potentially templated/namespaced) type name
        type_name = ' '.join(toks[i:])
        # Strip template args for matching simplicity
        bare = re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>', '', type_name).strip()
        # Remove namespace qualifiers? Keep them but track bare last part too.
        result.append({
            'name': bare,
            'qualified_name': type_name,
            'virtual': is_virtual,
            'access': access,
        })
    return result


# Regex for `virtual ... methodName(` declarations inside a class body.
# Group "name" is the method name. We tolerate ~Dtor, operator overloads (skip).
_VIRTUAL_DECL_RE = re.compile(
    r'(?ms)'                                  # multiline, dotall
    r'(?<![A-Za-z_0-9])virtual\b'             # `virtual` keyword
    r'(?P<sig>[^;{=]*?)'                      # return type + name + params
    r'(?P<name>~?[A-Za-z_][A-Za-z_0-9]*)'     # method name
    r'\s*\((?P<params>[^)]*)\)'               # parameters
    r'[^;{]*?'                                # cv-qualifiers, etc.
    r'(?P<term>[;{=])'                        # decl terminator
)


def _extract_class_body_virtuals(body, class_name):
    """From a class body, extract the names of methods declared `virtual` in
    declaration order. Skips `override` decls (since those reuse a parent slot)
    and skips OBJ_CLASSNAME / OBJ_SET_TYPE macro expansions.

    Returns list of dicts: [{'name': 'UpdateSphere', 'override': False, 'pure': bool}].
    """
    if not body:
        return []
    body = _strip_comments(body)

    # Process the body sequentially, tracking nested class/struct bodies so we
    # don't grab their virtuals.
    results = []
    pos = 0
    n = len(body)
    while pos < n:
        # Skip nested classes/structs: when we see `class X {` or `struct X {`
        # at the current scope, skip to the matching close brace.
        nested = re.match(r'\s*(?:class|struct|union)\b[^;{]*\{', body[pos:])
        if nested:
            # Advance past the nested body
            start = pos + nested.end() - 1  # at the `{`
            depth = 1
            i = start + 1
            while i < n and depth > 0:
                if body[i] == '{':
                    depth += 1
                elif body[i] == '}':
                    depth -= 1
                i += 1
            pos = i
            continue

        # Look for `virtual` at top level of this body
        m = re.search(
            r'(?<![A-Za-z_0-9])virtual\b(?P<rest>[^;{]*?)(?P<term>[;{=])',
            body[pos:],
        )
        if not m:
            break
        decl_start = pos + m.start()
        decl_end = pos + m.end()
        decl = body[decl_start:decl_end]
        rest = m.group('rest')
        # Parse out method name + parameters
        # The method name is the last identifier immediately before the `(`
        paren_idx = rest.find('(')
        if paren_idx == -1:
            # No `(`? Not a function decl — could be `virtual ~Foo();` (yes
            # has parens). If no paren, skip.
            pos = decl_end
            continue
        before_paren = rest[:paren_idx]
        # The name is the trailing identifier (allow `~Name`)
        name_match = re.search(r'(~?[A-Za-z_][A-Za-z_0-9]*)\s*$', before_paren)
        if not name_match:
            pos = decl_end
            continue
        name = name_match.group(1)
        # Skip operator overloads
        if name == 'operator':
            pos = decl_end
            continue
        # Check for `override` keyword after the closing paren but before the term.
        # Approximate: look for `override` in the snippet after the params.
        # We have `rest` after `virtual`; we need to find what came AFTER the `)`.
        # Walk through `rest` to find matching `)` for the `(` at paren_idx.
        depth = 0
        i = paren_idx
        while i < len(rest):
            if rest[i] == '(':
                depth += 1
            elif rest[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        after_paren = rest[i + 1:]
        is_override = bool(re.search(r'\boverride\b', after_paren))
        # `final` doesn't change slot semantics, ignore.
        is_pure = '=' in decl[decl_start - decl_start:]  # `= 0`
        is_pure = '= 0' in (after_paren + decl[decl_end - decl_start - 1:])

        # Detect `const` cv-qualifier on the method (immediately after `)`)
        # Look for `const` as a standalone keyword in `after_paren`.
        is_const = bool(re.search(r'\bconst\b', after_paren))
        results.append({
            'name': name,
            'override': is_override,
            'pure': is_pure,
            'const': is_const,
        })

        # If the declaration is an inline definition (terminated by `{`),
        # skip past the body.
        if m.group('term') == '{':
            # Find the matching close brace starting at decl_end - 1
            i = decl_end - 1
            depth = 1
            i += 1
            while i < n and depth > 0:
                if body[i] == '{':
                    depth += 1
                elif body[i] == '}':
                    depth -= 1
                i += 1
            pos = i
        else:
            pos = decl_end

    return results


def _find_macro_uses(body):
    """Return the set of macro names used at the top of a class body that
    declare virtuals (OBJ_CLASSNAME, OBJ_SET_TYPE). Used to know that
    ClassName/SetType were declared in this class (overriding Object's).
    """
    macros = set()
    if 'OBJ_CLASSNAME' in body:
        macros.add('OBJ_CLASSNAME')
    if 'OBJ_SET_TYPE' in body:
        macros.add('OBJ_SET_TYPE')
    return macros


# ---------------------------------------------------------------------------
# Header lookup: find a header file for a given class name
# ---------------------------------------------------------------------------

# Cache: class_name -> header_path
_HEADER_CACHE = {}


def _search_dirs():
    """Project source directories where headers live."""
    roots = []
    for sub in ('src', 'include'):
        if os.path.isdir(sub):
            roots.append(sub)
    return roots


def find_header_for_class(class_name):
    """Locate the header file that declares `class <class_name>`.

    Search strategy:
    1. Cached result.
    2. Walk src/ and include/ looking for `class <class_name>` declarations
       (skipping forward declarations like `class Foo;`).
    """
    if class_name in _HEADER_CACHE:
        return _HEADER_CACHE[class_name]
    pattern = re.compile(
        r'\bclass\s+' + re.escape(class_name) + r'\b\s*(?::|{)',
    )
    fwd_pattern = re.compile(
        r'\bclass\s+' + re.escape(class_name) + r'\b\s*;',
    )
    candidates = []
    for root in _search_dirs():
        for dirpath, _dirs, files in os.walk(root):
            for fname in files:
                if not (fname.endswith('.h') or fname.endswith('.hpp')):
                    continue
                full = os.path.join(dirpath, fname)
                try:
                    with open(full, 'r', encoding='utf-8', errors='replace') as f:
                        text = f.read()
                except OSError:
                    continue
                if pattern.search(text):
                    candidates.append(full)
                elif fwd_pattern.search(text):
                    # forward decl only — skip
                    continue
    if candidates:
        # Prefer a header whose basename matches the class name
        for c in candidates:
            base = os.path.splitext(os.path.basename(c))[0]
            if base == class_name:
                _HEADER_CACHE[class_name] = c
                return c
        _HEADER_CACHE[class_name] = candidates[0]
        return candidates[0]
    _HEADER_CACHE[class_name] = None
    return None


# Cache: class_name -> {'bases': [...], 'virtuals': [...], 'header': path}
_CLASS_INFO_CACHE = {}


def get_class_info(class_name):
    """Parse the header for `class_name` and return its base list and
    declaration-order virtuals.

    Returns dict with keys:
        'bases': [{'name': str, 'virtual': bool, 'access': str}, ...]
        'virtuals': [{'name': str, 'override': bool, 'pure': bool}, ...]
        'macros': set of macro names that injected virtuals
        'header': path to header (or None)
    Or None if the class can't be located.
    """
    if class_name in _CLASS_INFO_CACHE:
        return _CLASS_INFO_CACHE[class_name]

    header = find_header_for_class(class_name)
    if not header:
        _CLASS_INFO_CACHE[class_name] = None
        return None

    try:
        with open(header, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        _CLASS_INFO_CACHE[class_name] = None
        return None

    text = _strip_comments(text)
    bases_str, body = _find_class_body(text, class_name)
    if body is None:
        _CLASS_INFO_CACHE[class_name] = None
        return None

    bases = _parse_bases(bases_str) if bases_str else []
    virtuals = _extract_class_body_virtuals(body, class_name)
    macros = _find_macro_uses(body)

    info = {
        'bases': bases,
        'virtuals': virtuals,
        'macros': macros,
        'header': header,
    }
    _CLASS_INFO_CACHE[class_name] = info
    return info


# ---------------------------------------------------------------------------
# Virtual function layout resolver
# ---------------------------------------------------------------------------

# Cache: class_name -> list of {'name': str, 'declared_in': str, 'override': bool}
_LAYOUT_CACHE = {}

# Cache: class_name -> bool (does its primary inheritance chain include
# a virtual base anywhere?)
_HAS_VBASE_CACHE = {}


def class_has_vbase_in_chain(class_name, visited=None):
    """Return True if `class_name` or any of its non-virtual ancestors has
    a virtual base in its declaration. Cached.
    """
    if visited is None:
        visited = set()
    if class_name in visited:
        return False
    visited = visited | {class_name}
    if class_name in _HAS_VBASE_CACHE:
        return _HAS_VBASE_CACHE[class_name]
    norm = _normalize_class_name(class_name)
    if norm == 'Hmx::Object':
        _HAS_VBASE_CACHE[class_name] = False
        return False
    info = get_class_info(norm)
    if info is None:
        _HAS_VBASE_CACHE[class_name] = False
        return False
    for b in info['bases']:
        if b['virtual']:
            _HAS_VBASE_CACHE[class_name] = True
            return True
    # Recurse into non-virtual primary base
    for b in info['bases']:
        if not b['virtual'] and '<' not in b['name']:
            if class_has_vbase_in_chain(_normalize_class_name(b['name']), visited):
                _HAS_VBASE_CACHE[class_name] = True
                return True
    _HAS_VBASE_CACHE[class_name] = False
    return False


def _normalize_class_name(name):
    """Normalize a class name (strip namespace, template args)."""
    name = re.sub(r'<[^<>]*(?:<[^<>]*>[^<>]*)*>', '', name).strip()
    # Strip leading namespace if it matches a known mapping
    if name == 'Hmx::Object' or name == 'Object::Hmx':
        return 'Hmx::Object'
    # Drop ::Foo namespace prefix to get the leaf class
    if '::' in name:
        return name.split('::')[-1]
    return name


def get_object_subobject_layout():
    """Return the 22-virtual layout of Hmx::Object (the inherited subobject)."""
    return [
        {'name': n, 'declared_in': 'Hmx::Object', 'override': False, 'const': c}
        for n, c in OBJECT_VIRTUALS_INFO
    ]


def build_primary_vtable_layout(class_name, visited=None):
    """Recursively build the expected virtual layout for a class's PRIMARY
    inheritance chain (i.e. the chain ending at Hmx::Object via the first
    non-virtual base).

    Returns a list of dicts: [{'name': str, 'declared_in': str, 'override': bool}].
    Each entry represents one vtable slot.

    Slots are ordered: [parent's slots, then this class's NEW virtuals in
    declaration order].

    Returns None if the class is unparseable.
    """
    if visited is None:
        visited = set()
    if class_name in visited:
        return None
    visited = visited | {class_name}

    if class_name in _LAYOUT_CACHE:
        return _LAYOUT_CACHE[class_name]

    norm = _normalize_class_name(class_name)

    # Root: Hmx::Object
    if norm == 'Hmx::Object':
        layout = get_object_subobject_layout()
        _LAYOUT_CACHE[class_name] = layout
        return layout

    info = get_class_info(norm)
    if info is None:
        _LAYOUT_CACHE[class_name] = None
        return None

    # Find the primary (first non-virtual) base. For classes with virtual
    # inheritance from RndHighlightable, the primary vtable does NOT include
    # the virtual base's slots — those live in a separate subobject vtable
    # (??_7Class@@6BObject@Hmx@@@). So for those classes, the primary layout
    # is empty parent + only the class's new virtuals.
    primary_base = None
    has_virtual_base = False
    for b in info['bases']:
        if b['virtual']:
            has_virtual_base = True
            continue
        # Skip template instantiations we can't resolve
        if '<' in b['name'] or '>' in b['name']:
            continue
        primary_base = b
        break

    if primary_base is None:
        # No non-virtual base. Layout = (class's own new virtuals only).
        # Note: any virtuals declared here that override a virtual-base
        # method belong to the virtual base's subobject vtable, not the primary.
        # So we filter to entries that are NOT overrides of the virtual base.
        parent_layout = []
        # Identify virtual base's virtuals if any
        vb_virtual_names = set()
        if has_virtual_base:
            for b in info['bases']:
                if not b['virtual']:
                    continue
                vb_layout = build_primary_vtable_layout(
                    _normalize_class_name(b['name']), visited
                )
                if vb_layout:
                    for entry in vb_layout:
                        vb_virtual_names.add(entry['name'])
            # Object virtuals are always in the picture for Hmx classes with a vbase
            for name in OBJECT_VIRTUALS:
                vb_virtual_names.add(name)
        # Add macro-declared virtuals to the "override" set (ClassName/SetType)
        # so they don't appear in the primary vtable
        if 'OBJ_CLASSNAME' in info['macros']:
            vb_virtual_names.add('ClassName')
        if 'OBJ_SET_TYPE' in info['macros']:
            vb_virtual_names.add('SetType')

        new_layout = []
        for v in info['virtuals']:
            if v['override']:
                continue
            # A virtual destructor on a class with no non-virtual base:
            # - If there's a virtual base, the dtor lives in the vbase's
            #   subobject vtable (filtered above via OBJECT_VIRTUALS).
            # - If there's no base at all (true root class like Splash), the
            #   dtor is slot 0 of this class's own primary vtable.
            if v['name'].startswith('~'):
                if has_virtual_base:
                    continue
                # True root class — dtor is a real slot
                new_layout.append({
                    'name': v['name'],
                    'declared_in': norm,
                    'override': False,
                })
                continue
            if v['name'] in vb_virtual_names:
                continue
            new_layout.append({
                'name': v['name'],
                'declared_in': norm,
                'override': False,
            })
        layout = parent_layout + new_layout
        _LAYOUT_CACHE[class_name] = layout
        return layout

    # Have a non-virtual primary base. Recurse.
    parent_norm = _normalize_class_name(primary_base['name'])
    parent_layout = build_primary_vtable_layout(parent_norm, visited)
    if parent_layout is None:
        _LAYOUT_CACHE[class_name] = None
        return None

    # Build a set of (name, const?) pairs from the parent layout.
    # In MSVC PPC, a derived virtual that differs only in cv-qualifier from
    # the parent virtual is a *new* virtual (separate slot), not an override.
    parent_sigs = {(e['name'], e.get('const', False)) for e in parent_layout}
    parent_name_only = {e['name'] for e in parent_layout}
    # OBJ_CLASSNAME / OBJ_SET_TYPE expand to ClassName/SetType virtuals.
    # ClassName is const, SetType is non-const.
    if 'OBJ_CLASSNAME' in info['macros']:
        parent_sigs.add(('ClassName', True))
        parent_name_only.add('ClassName')
    if 'OBJ_SET_TYPE' in info['macros']:
        parent_sigs.add(('SetType', False))
        parent_name_only.add('SetType')

    # If the inheritance chain contains a virtual base (typically
    # `virtual RndHighlightable` -> `virtual Hmx::Object`), then Object's
    # virtuals are reachable through the vbase subobject vtable. Overrides
    # of them DON'T add slots to the primary vtable.
    if class_has_vbase_in_chain(class_name):
        for n, c in OBJECT_VIRTUALS_INFO:
            parent_sigs.add((n, c))
            parent_name_only.add(n)
        # Also any vbase's own virtuals
        for b in info['bases']:
            if b['virtual']:
                vb_layout = build_primary_vtable_layout(
                    _normalize_class_name(b['name']), visited
                )
                if vb_layout:
                    for entry in vb_layout:
                        parent_sigs.add((entry['name'], entry.get('const', False)))
                        parent_name_only.add(entry['name'])

    # Collect virtuals from SECONDARY bases (signature-aware).
    secondary_sigs = set()
    for b in info['bases']:
        if b['virtual']:
            continue
        if b is primary_base:
            continue
        if '<' in b['name'] or '>' in b['name']:
            continue
        sec_layout = build_primary_vtable_layout(
            _normalize_class_name(b['name']), visited
        )
        if sec_layout:
            for entry in sec_layout:
                secondary_sigs.add((entry['name'], entry.get('const', False)))

    # Build new layout
    layout = list(parent_layout)
    for v in info['virtuals']:
        sig = (v['name'], v.get('const', False))
        if sig in parent_sigs:
            # Same name AND same cv-qualifier as a parent virtual — override
            continue
        if sig in secondary_sigs:
            # Override of a secondary base's virtual — lives in that
            # base's subobject vtable, not the primary.
            continue
        if v['override']:
            continue
        if v['name'].startswith('~'):
            # Destructor — reuses parent's dtor slot
            continue
        layout.append({
            'name': v['name'],
            'declared_in': norm,
            'override': False,
            'const': v.get('const', False),
        })

    _LAYOUT_CACHE[class_name] = layout
    return layout


def build_subobject_layout_for_base(class_name, base_name):
    """Build the expected layout for the `??_7<class>@@6B<base>@@@` vtable.

    The slots correspond to the BASE class's virtual layout. So this is just
    `build_primary_vtable_layout(base_name)`, plus a trailing RTTI slot.
    """
    return build_primary_vtable_layout(base_name)


# ---------------------------------------------------------------------------
# Vtable enumeration (existing API, extended)
# ---------------------------------------------------------------------------

def find_obj_file(class_name):
    """Search for the .obj file containing a class's vtable."""
    search_names = [class_name]

    # Strip common prefixes
    if class_name.startswith('Rnd'):
        search_names.append(class_name[3:])  # RndFontBase -> FontBase
    if class_name.startswith('Ham'):
        search_names.append(class_name[3:])

    obj_dir = 'build/373307D9/obj'
    for name in search_names:
        pattern = os.path.join(obj_dir, '**', f'{name}.obj')
        matches = glob.glob(pattern, recursive=True)
        if matches:
            # Prefer the one NOT under obj/obj/ (avoid duplicate)
            for m in matches:
                if '/obj/obj/' not in m:
                    return m
            return matches[0]

    return None


# Known ICF merge patterns - functions with identical machine code
ICF_HINTS = {
    'OnlyReturns': 'returns void/this (empty function or return this)',
}


def classify_icf(symbol, offset=None, all_entries=None):
    """Try to classify ICF-merged symbols based on context."""
    if symbol == 'OnlyReturns':
        return 'empty/returns'
    if symbol.startswith('merged_'):
        return f'merged ({symbol[7:]})'
    return None


def is_icf_symbol(symbol):
    """True if the symbol is an ICF-merged placeholder (its name doesn't
    identify the actual function bound to this slot)."""
    if symbol == 'OnlyReturns':
        return True
    if symbol.startswith('merged_'):
        return True
    return False


def is_class_method(symbol, class_name):
    """True if `symbol` is a mangled method that belongs to `class_name`."""
    method, scope = parse_mangled_method(symbol)
    if not scope:
        return False
    if not scope:
        return False
    # scope[0] is innermost class
    return scope[0] == class_name


def get_vtable_layout(class_name, obj_path=None, project_root=None):
    """Get vtable layout as a list of dicts with offset, slot, symbol, demangled."""
    if project_root:
        old_cwd = os.getcwd()
        os.chdir(project_root)

    try:
        if not obj_path:
            obj_path = find_obj_file(class_name)
            if not obj_path:
                return []

        with open(obj_path, 'rb') as f:
            data = f.read()

        symbols, sections = read_coff_symbols(data)
        vtable_sym, entries = find_vtable(data, symbols, sections, class_name)

        if vtable_sym is None or not entries:
            return []

        result = []
        for i, entry in enumerate(entries):
            result.append({
                'slot': i,
                'offset': entry['offset'],
                'symbol': entry['symbol'],
                'demangled': demangle_symbol(entry['symbol']),
            })
        return result
    finally:
        if project_root:
            os.chdir(old_cwd)


def lookup_vtable_offset(class_name, offset, obj_path=None, project_root=None):
    """Look up which virtual function is at a given vtable offset."""
    layout = get_vtable_layout(class_name, obj_path, project_root)
    for entry in layout:
        if entry['offset'] == offset:
            return entry
    return None


def enumerate_all_vtables(data, symbols, sections):
    """Find all vtable symbols (??_7) and their RTTI sub-object offsets."""
    sym_by_idx = {}
    for sym in symbols:
        sym_by_idx[sym['index']] = sym

    vtables = []
    for sym in symbols:
        name = sym['name']
        if not name.startswith('??_7') or '6B' not in name:
            continue

        base_name = ''
        m = re.match(r'\?\?_7\w+@@6B(.+?)@@@?$', name)
        if m:
            base_name = m.group(1).replace('@', '::')

        sec_idx = sym['section'] - 1
        if sec_idx < 0 or sec_idx >= len(sections):
            continue

        section = sections[sec_idx]

        entries = []
        for r in range(section['num_relocs']):
            rel_off = section['reloc_offset'] + r * 10
            rva, sym_idx, rel_type = struct.unpack_from('<IIH', data, rel_off)
            target_sym = sym_by_idx.get(sym_idx, {'name': f'<unknown_{sym_idx}>'})
            entries.append({
                'offset': rva,
                'type': rel_type,
                'symbol': target_sym['name'],
            })

        sub_object_offset = None
        for entry in entries:
            if entry['symbol'].startswith('??_R4'):
                r4_sym = None
                for s in symbols:
                    if s['name'] == entry['symbol']:
                        r4_sym = s
                        break
                if r4_sym and r4_sym['section'] > 0:
                    r4_sec_idx = r4_sym['section'] - 1
                    if r4_sec_idx < len(sections):
                        r4_section = sections[r4_sec_idx]
                        r4_data_off = r4_section['raw_offset'] + r4_sym['value']
                        if r4_data_off + 8 <= len(data):
                            sub_object_offset = struct.unpack_from('>I', data, r4_data_off + 4)[0]

        vtables.append({
            'symbol': name,
            'base_name': base_name,
            'sub_object_offset': sub_object_offset,
            'section_idx': sec_idx,
            'entries': entries,
        })

    return vtables


def resolve_vcall(class_name, sub_object_offset, vtable_slot, obj_path=None, project_root=None):
    """Resolve a virtual function call through a sub-object vtable."""
    if project_root:
        old_cwd = os.getcwd()
        os.chdir(project_root)

    try:
        if not obj_path:
            obj_path = find_obj_file(class_name)
            if not obj_path:
                return {'error': f'Could not find .obj file for {class_name}'}

        with open(obj_path, 'rb') as f:
            data = f.read()

        symbols, sections = read_coff_symbols(data)
        vtables = enumerate_all_vtables(data, symbols, sections)

        if not vtables:
            return {'error': f'No vtable symbols found for {class_name} in {obj_path}'}

        if vtable_slot >= 100:
            vtable_slot = vtable_slot // 4

        matched = None
        for vt in vtables:
            if vt['sub_object_offset'] == sub_object_offset:
                matched = vt
                break

        if matched is None:
            available = []
            for vt in vtables:
                available.append({
                    'symbol': vt['symbol'],
                    'base_name': vt['base_name'],
                    'sub_object_offset': vt['sub_object_offset'],
                })
            return {
                'error': f'No vtable at sub-object offset {sub_object_offset} for {class_name}',
                'available_vtables': available,
            }

        entries = matched['entries']

        func_entries = [e for e in entries if not e['symbol'].startswith('??_R4')]
        if vtable_slot >= len(func_entries):
            return {
                'error': f'Slot {vtable_slot} out of range (vtable has {len(func_entries)} function slots)',
                'vtable_symbol': matched['symbol'],
                'base_name': matched['base_name'],
            }

        target_entry = func_entries[vtable_slot]
        target_sym = target_entry['symbol']
        demangled = demangle_symbol(target_sym)

        all_slots = []
        for i, e in enumerate(func_entries):
            slot_info = {
                'slot': i,
                'offset': f'0x{i * 4:02x}',
                'symbol': e['symbol'],
                'demangled': demangle_symbol(e['symbol']),
            }
            icf = classify_icf(e['symbol'], e['offset'], entries)
            if icf:
                slot_info['note'] = f'ICF: {icf}'
            all_slots.append(slot_info)

        confidence = 'high'
        icf = classify_icf(target_sym, target_entry['offset'], entries)
        if icf:
            confidence = 'medium'

        result = {
            'resolved_function': demangled,
            'raw_symbol': target_sym,
            'vtable_symbol': matched['symbol'],
            'base_name': matched['base_name'],
            'sub_object_offset': sub_object_offset,
            'slot': vtable_slot,
            'slot_offset_hex': f'0x{vtable_slot * 4:02x}',
            'confidence': confidence,
            'all_slots': all_slots,
            'obj_file': obj_path,
        }
        if icf:
            result['icf_note'] = icf

        return result

    finally:
        if project_root:
            os.chdir(old_cwd)


# ---------------------------------------------------------------------------
# Annotation engine
# ---------------------------------------------------------------------------

def annotate_vtable(class_name, vtable_symbol, entries):
    """Return one annotation string per virtual function slot (NOT per entry).

    Entries that are RTTI Complete Object Locators (??_R4) are skipped because
    they're metadata, not virtual function slots. Callers should walk `entries`
    and only consume an annotation for non-R4 entries.

    For a vtable `??_7Class@@6B<Base>@@@`:
    - If base specified: slots map to <Base>'s virtual layout.
    - If no base specified (primary vtable for a virtually-inheriting class):
      slots map to Class's NEW virtuals in declaration order.

    Returns:
        annotations: list of strings, one per non-R4 entry (may be empty)
    """
    # Filter out R4 entries — they're not virtual function slots
    func_entries = [e for e in entries if not e['symbol'].startswith('??_R4')]
    n = len(func_entries)
    annotations = [''] * n

    # Parse the vtable symbol to find the base it describes
    # ??_7Class@@6B@           -> primary (no base)
    # ??_7Class@@6BBase@@@     -> base subobject
    # ??_7Class@@6BBase@NS@@@  -> base subobject with namespaced base
    m_primary = re.match(r'\?\?_7(\w+)@@6B@$', vtable_symbol)
    m_base = re.match(r'\?\?_7(\w+)@@6B(.+?)@@@?$', vtable_symbol)

    base_class = None
    is_primary = False
    if m_primary:
        is_primary = True
    elif m_base:
        # Extract base name from mangled scope: "Object@Hmx" -> "Hmx::Object"
        scope = m_base.group(2)
        parts = scope.split('@')
        # Reverse so namespace comes first: ['Object', 'Hmx'] -> 'Hmx::Object'
        base_class = '::'.join(reversed(parts))
    else:
        return annotations

    # Build the expected layout for this vtable's "described" class.
    if is_primary:
        # Primary vtable of `class_name`:
        # - For a class with virtual inheritance from RndHighlightable: this
        #   vtable contains only the class's OWN new virtuals.
        # - For non-virtual-inheritance classes (UIListLabel et al.), this
        #   vtable contains the full layout starting from Object.
        info = get_class_info(class_name)
        if info is None:
            return annotations

        # Detect: does this class have ANY virtual base?
        has_vbase = any(b['virtual'] for b in info['bases'])

        if has_vbase:
            # Primary vtable holds class's new virtuals only
            new_layout = []
            # Compute the set of (name, const) sigs inherited from virtual
            # base(s) and Object.
            inherited_sigs = set(OBJECT_VIRTUALS_INFO)
            for b in info['bases']:
                if b['virtual']:
                    pl = build_primary_vtable_layout(_normalize_class_name(b['name']))
                    if pl:
                        for entry in pl:
                            inherited_sigs.add(
                                (entry['name'], entry.get('const', False))
                            )
            if 'OBJ_CLASSNAME' in info['macros']:
                inherited_sigs.add(('ClassName', True))
            if 'OBJ_SET_TYPE' in info['macros']:
                inherited_sigs.add(('SetType', False))

            for v in info['virtuals']:
                if v['name'].startswith('~'):
                    continue
                if v['override']:
                    continue
                sig = (v['name'], v.get('const', False))
                if sig in inherited_sigs:
                    continue
                new_layout.append(v['name'])

            for i in range(min(n, len(new_layout))):
                annotations[i] = f'[new in {class_name}] {new_layout[i]}'
        else:
            # Flat layout: use the full primary chain
            layout = build_primary_vtable_layout(class_name)
            if layout:
                for i in range(min(n, len(layout))):
                    entry = layout[i]
                    if entry['declared_in'] == class_name:
                        annotations[i] = f'[new in {class_name}] {entry["name"]}'
                    elif entry['declared_in'] == 'Hmx::Object':
                        annotations[i] = f'[Hmx::Object] {entry["name"]}'
                    else:
                        annotations[i] = f'[inherited from {entry["declared_in"]}] {entry["name"]}'
    elif base_class:
        # Subobject vtable: slots correspond to base_class's primary layout.
        # Special-cased: RndHighlightable subobject vtable has only [Highlight]
        norm_base = _normalize_class_name(base_class)
        if norm_base == 'RndHighlightable':
            layout = RNDHIGHLIGHTABLE_SUBOBJECT_VIRTUALS
            for i in range(min(n, len(layout))):
                annotations[i] = f'[RndHighlightable subobject] {layout[i]}'
        else:
            # If this is the Object subobject vtable AND the class has no
            # separate `??_7Class@@6B@`, then this IS the primary vtable for
            # the class (non-virtual-inheritance case). In that case slots
            # beyond Hmx::Object's 22 are the class's own new virtuals.
            base_layout = build_primary_vtable_layout(norm_base)
            class_layout = build_primary_vtable_layout(class_name)
            # Prefer the class layout if it extends the base layout
            if (class_layout and base_layout
                    and len(class_layout) >= len(base_layout)
                    and all(class_layout[i]['name'] == base_layout[i]['name']
                            for i in range(len(base_layout)))):
                layout = class_layout
            else:
                layout = base_layout
            if layout:
                for i in range(min(n, len(layout))):
                    entry = layout[i]
                    if entry['declared_in'] == class_name:
                        annotations[i] = f'[new in {class_name}] {entry["name"]}'
                    elif entry['declared_in'] == 'Hmx::Object':
                        annotations[i] = f'[Hmx::Object] {entry["name"]}'
                    else:
                        annotations[i] = f'[inherited from {entry["declared_in"]}] {entry["name"]}'

    return annotations


# ---------------------------------------------------------------------------
# CLI helpers
# ---------------------------------------------------------------------------

def _format_row(slot, offset, symbol, annotation, sym_col_width=60):
    offset_hex = f'0x{offset:04x}'
    sym_disp = symbol if len(symbol) <= sym_col_width else symbol[:sym_col_width - 3] + '...'
    return f'[{slot:3d}]  {offset_hex}  {sym_disp:<{sym_col_width}}  {annotation}'


def _print_full_dump(class_name, vtable_sym, entries, annotations, raw=False, demangle=False):
    print(f'Vtable: {vtable_sym["name"]} (section {vtable_sym["section"]}, {len(entries)} entries)')
    print()
    print(f'{"Slot":>4}  {"Offset":>6}  {"Symbol":<60}  Annotation')
    print('-' * 120)
    slot_idx = 0
    for i, entry in enumerate(entries):
        sym = entry['symbol']
        if demangle and not raw:
            display = demangle_symbol(sym)
        else:
            display = sym
        # ??_R4 is the RTTI Complete Object Locator, not a virtual function slot.
        if sym.startswith('??_R4'):
            annotation = '[RTTI Complete Object Locator]'
            offset_hex = f'0x{entry["offset"]:04x}'
            print(f'[ R4]  {offset_hex}  {display:<60}  {annotation}')
            continue
        annotation = annotations[slot_idx] if slot_idx < len(annotations) else ''
        # If ICF-merged, note it
        icf = classify_icf(sym)
        if icf and annotation:
            annotation += f'  ({sym} — ICF merged, real impl may differ)'
        elif icf:
            annotation = f'({sym} — ICF merged)'
        print(_format_row(slot_idx, entry['offset'], display, annotation))
        slot_idx += 1


def _print_slot(class_name, slot_idx, entry, annotation, label_prefix=''):
    sym = entry['symbol']
    demangled = demangle_symbol(sym)
    icf = classify_icf(sym)
    offset_hex = f'0x{entry["offset"]:04x}'
    slot_label = f'slot {slot_idx}' if slot_idx is not None else 'RTTI'
    line = f'{label_prefix}{slot_label} ({offset_hex}): {demangled}'
    if annotation:
        line += f'    {annotation}'
    print(line)
    if icf:
        print(f'   ICF note: this symbol ({sym}) is ICF-merged. '
              f'The slot maps to the virtual indicated above by the annotation.')


def _run_resolve(argv):
    """Handle 'resolve' subcommand."""
    parser = argparse.ArgumentParser(prog='dump_vtable.py resolve',
                                     description='Resolve a virtual call')
    parser.add_argument('resolve_class', help='Most-derived class name')
    parser.add_argument('offset', type=lambda x: int(x, 0), help='Sub-object offset')
    parser.add_argument('slot', type=int, help='Vtable slot index')
    parser.add_argument('--obj', dest='resolve_obj', help='Path to .obj file')
    args = parser.parse_args(argv)

    result = resolve_vcall(args.resolve_class, args.offset, args.slot, obj_path=args.resolve_obj)
    if 'error' in result:
        print(f"Error: {result['error']}")
        if 'available_vtables' in result:
            print("\nAvailable vtables:")
            for vt in result['available_vtables']:
                print(f"  offset={vt['sub_object_offset']}  base={vt['base_name']:<30s}  {vt['symbol']}")
        sys.exit(1)

    print(f"Resolved: {result['resolved_function']}")
    print(f"Vtable:   {result['vtable_symbol']}")
    print(f"Base:     {result['base_name']}")
    print(f"Slot:     [{result['slot']}] at {result['slot_offset_hex']}")
    print(f"Confidence: {result['confidence']}")
    if 'icf_note' in result:
        print(f"ICF Note: {result['icf_note']}")
    print(f"\nAll slots in this vtable:")
    for s in result['all_slots']:
        marker = ' <<' if s['slot'] == result['slot'] else ''
        note = f"  ({s['note']})" if 'note' in s else ''
        print(f"  [{s['slot']:3d}] {s['offset']}  {s['demangled']}{note}{marker}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'resolve':
        _run_resolve(sys.argv[2:])
        return

    parser = argparse.ArgumentParser(description='Dump vtable layout from original COFF .obj files')
    parser.add_argument('class_name', help='Class name (e.g., RndFontBase, RndFont3d)')
    parser.add_argument('--obj', help='Path to .obj file (auto-detected if not given)')
    parser.add_argument('--demangle', '-d', action='store_true', help='Demangle symbol names in the Symbol column')
    parser.add_argument('--raw', action='store_true', help='Show raw mangled symbol names (default)')
    parser.add_argument('--offset', type=lambda x: int(x, 0),
                        help='Show only the slot at this byte offset (e.g., 0x14)')
    parser.add_argument('--diff-pair', nargs=2, type=lambda x: int(x, 0),
                        metavar=('OFFSET_TGT', 'OFFSET_SRC'),
                        help='Compare two slots side by side (e.g. --diff-pair 0x14 0x18). '
                             'Useful when objdiff shows a vcall with mismatched offsets.')
    args = parser.parse_args()

    obj_path = args.obj
    if not obj_path:
        obj_path = find_obj_file(args.class_name)
        if not obj_path:
            print(f"Error: Could not find .obj file for {args.class_name}")
            print(f"Try: python3 {sys.argv[0]} {args.class_name} --obj <path_to_obj>")
            sys.exit(1)

    print(f"Reading: {obj_path}")

    with open(obj_path, 'rb') as f:
        data = f.read()

    symbols, sections = read_coff_symbols(data)
    vtable_sym, entries = find_vtable(data, symbols, sections, args.class_name)

    if vtable_sym is None:
        print(f"Error: No vtable symbol found for {args.class_name}")
        print(f"Available ??_7 symbols:")
        for sym in symbols:
            if '??_7' in sym['name']:
                print(f"  {sym['name']}")
        sys.exit(1)

    # Compute annotations once (one per non-R4 slot)
    annotations = annotate_vtable(args.class_name, vtable_sym['name'], entries)

    # Build a "slot index" parallel to entries (None for R4 rows)
    slot_indices = []
    next_slot = 0
    for e in entries:
        if e['symbol'].startswith('??_R4'):
            slot_indices.append(None)
        else:
            slot_indices.append(next_slot)
            next_slot += 1

    def _find_by_offset(off):
        for i, e in enumerate(entries):
            if e['offset'] == off:
                return i
        return None

    def _annotation_at(entry_idx):
        slot = slot_indices[entry_idx]
        if slot is None or slot >= len(annotations):
            return ''
        return annotations[slot]

    # --diff-pair: show only the two slots side-by-side
    if args.diff_pair:
        off_tgt, off_src = args.diff_pair
        idx_tgt = _find_by_offset(off_tgt)
        idx_src = _find_by_offset(off_src)
        print(f'Vtable: {vtable_sym["name"]}')
        if idx_tgt is None:
            print(f'  (no slot at TGT offset 0x{off_tgt:04x})')
        else:
            ann = _annotation_at(idx_tgt)
            slot_num = slot_indices[idx_tgt]
            _print_slot(args.class_name, slot_num, entries[idx_tgt], ann,
                        label_prefix='TGT ')
        if idx_src is None:
            print(f'  (no slot at SRC offset 0x{off_src:04x})')
        else:
            ann = _annotation_at(idx_src)
            slot_num = slot_indices[idx_src]
            _print_slot(args.class_name, slot_num, entries[idx_src], ann,
                        label_prefix='SRC ')
        if idx_tgt is not None and idx_src is not None:
            print()
            tgt_sym = entries[idx_tgt]['symbol']
            src_sym = entries[idx_src]['symbol']
            tgt_ann = _annotation_at(idx_tgt) or '(unknown)'
            src_ann = _annotation_at(idx_src) or '(unknown)'
            tgt_slot = slot_indices[idx_tgt]
            src_slot = slot_indices[idx_src]
            print(f'Summary:')
            print(f'  Target binary calls slot {tgt_slot} (0x{off_tgt:04x}): {tgt_ann}')
            print(f'  Decomp source  calls slot {src_slot} (0x{off_src:04x}): {src_ann}')
            print(f'  If the source code expected to dispatch to "{tgt_ann}",')
            print(f'  change the source to call that virtual (the target says slot {tgt_slot}).')
            if classify_icf(tgt_sym) or classify_icf(src_sym):
                print()
                print(f'  Note: at least one slot resolves to an ICF-merged symbol')
                print(f'  (TGT={tgt_sym!r}, SRC={src_sym!r}).')
                print(f'  Trust the annotation column over the raw symbol name.')
        return

    # --offset: show only one slot
    if args.offset is not None:
        idx = _find_by_offset(args.offset)
        if idx is None:
            print(f'No slot at offset 0x{args.offset:04x} in {vtable_sym["name"]}')
            sys.exit(1)
        ann = _annotation_at(idx)
        slot_num = slot_indices[idx]
        _print_slot(args.class_name, slot_num, entries[idx], ann)
        return

    # Default: full dump
    print()
    demangle = args.demangle and not args.raw
    _print_full_dump(args.class_name, vtable_sym, entries, annotations,
                     raw=args.raw, demangle=demangle)


if __name__ == '__main__':
    main()
