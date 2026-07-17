#!/usr/bin/env python3
"""
Parse MSVC PPC IL (.ex) files from c1xx.dll → c2.dll intermediate format.

The MSVC Xbox 360 compiler uses a typed intermediate language between:
  c1xx.dll (C++ front-end) → IL files → c2.dll (PPC back-end)

IL files are normally deleted after compilation. Capture them by making
c2.dll fail early: compile with /d2nop (causes "unrecognized flag" error
before c2 reads/deletes the files).

Usage:
    # Capture IL:
    python3 msvc-src/tools/il_parser.py capture test.cpp
    # Parse a captured .ex file:
    python3 msvc-src/tools/il_parser.py parse _CL_xxxxxxxx
    # Full pipeline: capture + parse:
    python3 msvc-src/tools/il_parser.py analyze test.cpp

See msvc-src/docs/IL_FORMAT.md for complete format documentation.
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

# --- Opcode table ---

OPCODES = {
    # Arithmetic (0x02-0x0A)
    0x02: 'ADD',
    0x03: 'SUB',
    0x04: 'MUL',
    0x05: 'DIV',
    0x06: 'MOD',
    0x08: 'NEG',
    0x09: 'SHL',
    0x0A: 'SHR',
    # Bitwise (0x0B-0x0E)
    0x0B: 'AND',
    0x0C: 'OR',
    0x0D: 'XOR',
    0x0E: 'NOT',
    # Compound assignment
    0x0F: 'ADD_ASSIGN',
    # Logical
    0x1A: 'LOGICAL_NOT',
    0x1B: 'LOGICAL_OR',
    0x1C: 'LOGICAL_AND',
    # Comparison (0x1F-0x24)
    0x1F: 'EQ',
    0x20: 'NE',
    0x21: 'LE',
    0x22: 'LT',
    0x23: 'GE',
    0x24: 'GT',
    # Pointer/memory
    0x27: 'MEMBER_PTR',
    0x28: 'PTR_ADD',
    0x30: 'DEREF',
    0x32: 'STORE',
    0x36: 'SUB_ASSIGN',
    # Virtual dispatch
    0x67: 'VCALL_SETUP',
    0x9A: 'VCALL_BIND',
    # Type conversion
    0x2C: 'CAST',
    # Control flow
    0x38: 'COND_BRANCH',
    # Switch
    0x3B: 'SWITCH',
    0x3C: 'SWITCH_TABLE',
    0x3D: 'CASE',
}

UNARY_OPS = {0x08, 0x0E}  # NEG, NOT

# Known type encodings (prefix encodes size: 82=1B, 84=2B, 86=4B, 88=8B)
KNOWN_TYPES = {
    # 1-byte types (prefix 82)
    b'\x82\x11\x70': 'char',
    b'\x82\x12\x20': 'uchar',
    b'\x82\x12\x30': 'bool',
    # 2-byte types (prefix 84)
    b'\x84\x21\x11': 'short',
    b'\x84\x22\x21': 'ushort',
    # 4-byte types (prefix 86)
    b'\x86\x41\x74': 'int',
    b'\x86\x42\x75': 'uint',
    b'\x86\x45\x40': 'float',
    # 8-byte types (prefix 88)
    b'\x88\x85\x41': 'double',
}

# Structural markers
FUNC_END = b'\x4f\x12'
# Block Entry marker: 42 45 0E 06 01 01 01 0D 08 00
BE_MARKER = b'\x42\x45\x0e\x06'
IL_SUFFIXES = ('ex', 'gl', 'sy', 'in', 'db')

# --- Default paths ---

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_CL = str(PROJECT_ROOT / "build/compilers/X360/16.00.11886.00/cl.exe")
DEFAULT_WIBO = str(PROJECT_ROOT / "build/tools/wibo")


def find_wibo():
    """Find wibo binary."""
    candidates = [
        DEFAULT_WIBO,
        str(PROJECT_ROOT / "../wibo/build/release/wibo"),
        str(PROJECT_ROOT / "../wibo/build/wibo"),
    ]
    for c in candidates:
        if Path(c).exists():
            return str(Path(c).resolve())
    raise FileNotFoundError("wibo not found")


def to_winpath(path):
    """Convert Linux path to wibo Windows path."""
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def _utc_now_iso():
    """Return current UTC timestamp in ISO-8601 format."""
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def _sanitize_bundle_name(name):
    """Return a filesystem-safe bundle name."""
    return re.sub(r'[^A-Za-z0-9_.-]+', '_', name).strip('._') or 'il_bundle'


def _bundle_manifest_path(bundle_base):
    """Return the manifest path for a bundle base path."""
    return Path(str(Path(bundle_base)) + '.manifest.json')


def build_bundle_manifest(
    bundle_base,
    *,
    source_path=None,
    bundle_name=None,
    il_base=None,
    command=None,
    run_cwd=None,
    cl_path=None,
    wibo_path=None,
):
    """Build manifest metadata for a captured IL bundle."""
    bundle_base = Path(bundle_base)
    files = {}
    for suffix in IL_SUFFIXES:
        path = Path(str(bundle_base) + suffix)
        files[suffix] = {
            'path': path.name,
            'exists': path.exists(),
            'size': path.stat().st_size if path.exists() else 0,
        }

    return {
        'bundle_name': bundle_name or bundle_base.parent.name or bundle_base.name,
        'captured_at': _utc_now_iso(),
        'source_path': str(Path(source_path).resolve()) if source_path else None,
        'bundle_base': str(bundle_base),
        'il_base': il_base or bundle_base.name,
        'run_cwd': str(Path(run_cwd).resolve()) if run_cwd else None,
        'compiler_path': str(cl_path) if cl_path else None,
        'wibo_path': str(wibo_path) if wibo_path else None,
        'command': list(command) if command else None,
        'files': files,
    }


def write_bundle_manifest(bundle_base, manifest, bundle_dir=None):
    """Write bundle manifest JSON adjacent to the captured files."""
    manifest_path = Path(bundle_dir) / 'manifest.json' if bundle_dir else _bundle_manifest_path(bundle_base)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + '\n',
        encoding='utf-8',
    )
    return manifest_path


def read_bundle_manifest(path):
    """Read a bundle manifest from a bundle dir, base path, or manifest path."""
    candidate = Path(path)
    if candidate.is_dir():
        manifest_path = candidate / 'manifest.json'
    elif candidate.name == 'manifest.json':
        manifest_path = candidate
    else:
        manifest_path = _bundle_manifest_path(candidate)
    if not manifest_path.exists():
        return None
    return json.loads(manifest_path.read_text(encoding='utf-8'))


def resolve_bundle_base(path):
    """Resolve a bundle dir, manifest path, base path, or file path to the base."""
    candidate = Path(path)
    if candidate.is_dir():
        manifest = read_bundle_manifest(candidate)
        if manifest and manifest.get('bundle_base'):
            return manifest['bundle_base']
        for suffix in IL_SUFFIXES:
            matches = sorted(candidate.glob(f'*{suffix}'))
            if matches:
                return str(matches[0])[:-len(suffix)]
        return str(candidate / candidate.name)
    if candidate.name == 'manifest.json':
        manifest = read_bundle_manifest(candidate)
        if manifest and manifest.get('bundle_base'):
            return manifest['bundle_base']
        return str(candidate.parent / candidate.parent.name)
    for suffix in IL_SUFFIXES:
        if str(candidate).endswith(suffix):
            return str(candidate)[:-len(suffix)]
    return str(candidate)


# --- Type parsing ---

def try_parse_type(data, pos):
    """Try to parse a type marker at the given position.

    Returns (type_name, bytes_consumed) or (None, 0).

    Type encoding:
        86 XX XX     — 4-byte types (int, float, pointers)
        88 XX XX     — 8-byte types (double)
        86 43 XX XX  — pointer types (4 bytes: 86 43 + 2 extra)
    """
    if pos >= len(data):
        return None, 0

    prefix = data[pos]

    # Type prefix encodes size: 82=1B, 84=2B, 86=4B, 88=8B
    # a6 prefix = class/struct pointer type (vtable context)
    if prefix == 0xA6 and pos + 3 < len(data):
        return f'class({data[pos+1]:02x}{data[pos+2]:02x}{data[pos+3]:02x})', 4

    if prefix in (0x82, 0x84, 0x86, 0x88) and pos + 2 < len(data):
        # Check for pointer type: 86 43 XX XX (4 bytes)
        if prefix == 0x86 and data[pos + 1] == 0x43 and pos + 3 < len(data):
            key3 = bytes(data[pos:pos + 3])
            return KNOWN_TYPES.get(key3, f'ptr({data[pos+2]:02x}{data[pos+3]:02x})'), 4

        key = bytes(data[pos:pos + 3])
        size_names = {0x82: '1B', 0x84: '2B', 0x86: '4B', 0x88: '8B'}
        fallback = f'type_{size_names[prefix]}({data[pos+1]:02x}{data[pos+2]:02x})'
        return KNOWN_TYPES.get(key, fallback), 3

    return None, 0


def try_parse_result_type(data, pos):
    """Try to parse result type annotation: 41 + type_marker."""
    if pos < len(data) and data[pos] == 0x41:
        typename, consumed = try_parse_type(data, pos + 1)
        if typename:
            return typename, 1 + consumed
    return None, 0


# --- IL Parsing ---

class ILFunction:
    """Parsed IL function."""

    def __init__(self, index, header_offset, body_offset, end_offset, body_data, token_width=2):
        self.index = index
        self.header_offset = header_offset
        self.body_offset = body_offset
        self.end_offset = end_offset
        self.body = body_data
        self.tw = token_width  # 2 for test files, 4 for real compilations
        self.name = f'func_{index}'
        self.params = []
        self.result_var = None
        self.operations = []
        self._parse_body()

    def _read_token(self, data, pos):
        """Read a token at the given position. Returns (token_value, bytes_consumed)."""
        if self.tw == 4 and pos + 3 < len(data):
            return (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3], 4
        elif pos + 1 < len(data):
            return (data[pos] << 8) | data[pos+1], 2
        return 0, 0

    def _parse_body(self):
        """Parse the function body into structured operations."""
        data = self.body
        i = 0
        tw = self.tw

        # Skip leading label if present: 4F 01 NN
        if i + 2 < len(data) and data[i] == 0x4F and data[i+1] == 0x01:
            # Label before SS — record it but don't add as operation yet
            i += 3

        # Check for function index byte before SS
        # In test files: index_byte SS; in real files: may start directly with SS
        if i + 2 < len(data) and data[i+1:i+3] == b'\x53\x53':
            self.index = data[i]
            i += 1
        elif i < len(data) and data[i:i+2] != b'\x53\x53':
            # Skip unknown bytes until SS
            while i + 1 < len(data) and data[i:i+2] != b'\x53\x53':
                i += 1

        # Parse SS (Start Statement) block
        if i + 1 < len(data) and data[i:i+2] == b'\x53\x53':
            i += 2
            if i < len(data) and data[i] == 0x26:
                i += 1
                if i + tw - 1 < len(data):
                    self.result_var, _ = self._read_token(data, i)
                    i += tw
                if i < len(data) and data[i] == 0x46:
                    i += 1
                while i + tw < len(data) and data[i] == 0x2D:
                    i += 1
                    token, consumed = self._read_token(data, i)
                    self.params.append(token)
                    i += consumed

        # Parse LO (Load Operands) block
        if i + 3 < len(data) and data[i:i+3] == b'\x4c\x4f\x11':
            i += 3
            # Skip optional 0x53 after LO
            if i < len(data) and data[i] == 0x53:
                i += 1

        # Parse operations
        self._parse_operations(data, i)

    def _parse_operations(self, data, start):
        """Parse the operation stream."""
        i = start
        loads = []  # Pending operands: list of (kind, value)

        while i < len(data):
            byte = data[i]

            # Label definition: 4F 01 NN
            if byte == 0x4F and i + 2 < len(data) and data[i+1] == 0x01:
                label = data[i+2]
                self.operations.append({
                    'type': 'label',
                    'name': 'LABEL',
                    'label': label,
                })
                loads = []
                i += 3
                # Skip optional 0x53 after label
                if i < len(data) and data[i] == 0x53:
                    i += 1
                continue

            # Load variable: B9 token type
            if byte == 0xB9 and i + self.tw < len(data):
                token, tc = self._read_token(data, i + 1)
                typename, consumed = try_parse_type(data, i + 1 + tc)
                if typename:
                    loads.append(('var', token, typename))
                    i += 1 + tc + consumed
                    continue
                else:
                    loads.append(('var', token, '?'))
                    i += 1 + tc
                    continue

            # Literal: 33 type NN (or 33 type 80 NN NN NN NN for values > 127)
            if byte == 0x33:
                typename, consumed = try_parse_type(data, i + 1)
                if typename and i + 1 + consumed < len(data):
                    val_pos = i + 1 + consumed
                    if data[val_pos] == 0x80 and val_pos + 4 < len(data):
                        # Multi-byte: 0x80 prefix + 4-byte LE value
                        value = struct.unpack_from('<I', data, val_pos + 1)[0]
                        # Interpret as signed if type is signed
                        if typename in ('int', 'short', 'char') and value >= 0x80000000:
                            value = value - 0x100000000
                        loads.append(('lit', value, typename))
                        i = val_pos + 5
                    else:
                        value = data[val_pos]
                        # Interpret as signed for small negative values
                        if typename in ('int', 'short', 'char') and value >= 0x80:
                            value = value - 0x100
                        loads.append(('lit', value, typename))
                        i = val_pos + 1
                    continue

            # Variable reference: 26 token (in compound ops and calls)
            if byte == 0x26 and i + self.tw < len(data):
                token, tc = self._read_token(data, i + 1)
                loads.append(('ref', token, None))
                i += 1 + tc
                continue

            # Value reference: 29 token (in return/goto)
            if byte == 0x29 and i + self.tw < len(data):
                token, tc = self._read_token(data, i + 1)
                loads.append(('val', token, None))
                i += 1 + tc
                continue

            # CALL_START: BD type metadata(6 bytes)
            if byte == 0xBD:
                typename, consumed = try_parse_type(data, i + 1)
                if typename:
                    # Skip 6-byte metadata after type
                    meta_start = i + 1 + consumed
                    metadata = data[meta_start:meta_start + 6] if meta_start + 6 <= len(data) else b''
                    self.operations.append({
                        'type': 'call_start',
                        'name': 'CALL_START',
                        'return_type': typename,
                        'operands': list(loads),
                    })
                    loads = []
                    i = meta_start + 6
                    continue

            # CALL_EXECUTE: 55 type 4C
            if byte == 0x55:
                typename, consumed = try_parse_type(data, i + 1)
                if typename:
                    self.operations.append({
                        'type': 'call_exec',
                        'name': 'CALL_EXEC',
                        'return_type': typename,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + consumed
                    # Skip optional 4C
                    if i < len(data) and data[i] == 0x4C:
                        i += 1
                    continue

            # Conditional branch: CB ... (ternary select)
            if byte == 0x43 and i + 1 < len(data) and data[i+1] == 0x42:
                # Parse CB operands (true_val, false_val after CB)
                i += 2
                cb_operands = list(loads)
                loads = []
                # Read the two branch values
                true_vals = []
                false_vals = []
                # Simple: skip to the next meaningful opcode
                self.operations.append({
                    'type': 'op',
                    'name': 'CB',
                    'operands': cb_operands,
                })
                continue

            # Arithmetic/bitwise/comparison/switch opcodes
            if byte in OPCODES:
                opname = OPCODES[byte]

                # SWITCH: 3B token — evaluate expression into dispatch var
                if byte == 0x3B and i + self.tw < len(data):
                    token, tc = self._read_token(data, i + 1)
                    self.operations.append({
                        'type': 'switch',
                        'name': 'SWITCH',
                        'target': token,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + tc
                    continue

                # SWITCH_TABLE: 3C type token — dispatch table with default
                if byte == 0x3C:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        default_pos = i + 1 + consumed
                        if default_pos + self.tw - 1 < len(data):
                            default_token, tc = self._read_token(data, default_pos)
                            self.operations.append({
                                'type': 'switch_table',
                                'name': 'SWITCH_TABLE',
                                'result_type': typename,
                                'default_target': default_token,
                                'operands': list(loads),
                            })
                            loads = []
                            i = default_pos + tc
                            continue

                # CASE: 3D token — maps preceding literal to target label
                if byte == 0x3D and i + self.tw < len(data):
                    token, tc = self._read_token(data, i + 1)
                    # The case value should be the last literal loaded
                    case_val = loads[-1][1] if loads and loads[-1][0] == 'lit' else '?'
                    self.operations.append({
                        'type': 'case',
                        'name': 'CASE',
                        'target': token,
                        'value': case_val,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + tc
                    continue

                # MEMBER_PTR: 27 type — pointer + offset → typed member pointer
                if byte == 0x27:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        self.operations.append({
                            'type': 'op',
                            'name': 'MEMBER_PTR',
                            'result_type': typename,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 1 + consumed
                        continue

                # VCALL_SETUP: 67 flags token — virtual call setup
                if byte == 0x67:
                    if i + 1 + self.tw < len(data):
                        flags = data[i+1]
                        token, tc = self._read_token(data, i + 2)
                        self.operations.append({
                            'type': 'op',
                            'name': 'VCALL_SETUP',
                            'target': token,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 2 + tc
                        continue

                # VCALL_BIND: 9A type — virtual method resolution
                if byte == 0x9A:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        self.operations.append({
                            'type': 'op',
                            'name': 'VCALL_BIND',
                            'result_type': typename,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 1 + consumed
                        continue

                # CAST: 2C type 00
                if byte == 0x2C:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        self.operations.append({
                            'type': 'op',
                            'name': 'CAST',
                            'result_type': typename,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 1 + consumed
                        # Skip trailing 00
                        if i < len(data) and data[i] == 0x00:
                            i += 1
                        continue

                # COND_BRANCH: 38 token
                if byte == 0x38 and i + self.tw < len(data):
                    target, tc = self._read_token(data, i + 1)
                    self.operations.append({
                        'type': 'branch',
                        'name': 'COND_BRANCH',
                        'target': target,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + tc
                    continue

                # PTR_ADD: 28 XX XX (always 2 flag bytes, not token)
                if byte == 0x28 and i + 2 < len(data):
                    flags = (data[i+1] << 8) | data[i+2]
                    self.operations.append({
                        'type': 'op',
                        'name': 'PTR_ADD',
                        'flags': flags,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 3
                    continue

                # DEREF: 30 type
                if byte == 0x30:
                    typename, consumed = try_parse_type(data, i + 1)
                    self.operations.append({
                        'type': 'op',
                        'name': 'DEREF',
                        'result_type': typename,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + consumed
                    continue

                # STORE: 32 type 4B
                if byte == 0x32:
                    typename, consumed = try_parse_type(data, i + 1)
                    self.operations.append({
                        'type': 'op',
                        'name': 'STORE',
                        'result_type': typename,
                        'operands': list(loads),
                    })
                    loads = []
                    i += 1 + consumed
                    # Skip optional 4B
                    if i < len(data) and data[i] == 0x4B:
                        i += 1
                    continue

                # ADD_ASSIGN: 0F type 4B
                if byte == 0x0F:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        self.operations.append({
                            'type': 'op',
                            'name': 'ADD_ASSIGN',
                            'result_type': typename,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 1 + consumed
                        if i < len(data) and data[i] == 0x4B:
                            i += 1
                        continue

                # SUB_ASSIGN: 36 type 4B
                if byte == 0x36:
                    typename, consumed = try_parse_type(data, i + 1)
                    if typename:
                        self.operations.append({
                            'type': 'op',
                            'name': 'SUB_ASSIGN',
                            'result_type': typename,
                            'operands': list(loads),
                        })
                        loads = []
                        i += 1 + consumed
                        if i < len(data) and data[i] == 0x4B:
                            i += 1
                        continue

                # Standard binary/unary op
                self.operations.append({
                    'type': 'op',
                    'name': opname,
                    'operands': list(loads),
                })
                loads = []
                i += 1
                continue

            # Assignment: 3A token
            if byte == 0x3A and i + self.tw < len(data):
                token, tc = self._read_token(data, i + 1)
                self.operations.append({
                    'type': 'assign',
                    'name': 'ASSIGN',
                    'target': token,
                    'operands': list(loads),
                })
                loads = []
                i += 1 + tc
                continue

            # Result type annotation: 41 + type
            if byte == 0x41:
                typename, consumed = try_parse_result_type(data, i)
                if typename:
                    i += consumed
                    continue

            # Terminate: 54 XX [29 token]
            if byte == 0x54 and i + 1 < len(data):
                kind = data[i+1]
                if kind == 0x02 and i + 2 + self.tw < len(data) and data[i+2] == 0x29:
                    # RETURN: 54 02 29 token
                    token, tc = self._read_token(data, i + 3)
                    self.operations.append({
                        'type': 'return',
                        'name': 'RETURN',
                        'target': token,
                    })
                    i += 3 + tc
                    continue
                elif kind == 0x04:
                    # GOTO: 54 04 [29 XX XX | 3A XX XX ...]
                    self.operations.append({
                        'type': 'goto',
                        'name': 'GOTO',
                        'operands': list(loads),
                    })
                    loads = []
                    i += 2
                    continue
                elif kind == 0x03:
                    # FALLTHROUGH: 54 03
                    self.operations.append({
                        'type': 'fallthrough',
                        'name': 'FALLTHROUGH',
                    })
                    loads = []
                    i += 2
                    continue
                else:
                    i += 2
                    continue

            # Block end marker: 4B
            if byte == 0x4B:
                i += 1
                continue

            # Statement marker: 53
            if byte == 0x53:
                i += 1
                continue

            # Unknown — skip
            i += 1

    def __repr__(self):
        return f"ILFunction({self.name}, {len(self.operations)} ops)"

    def to_dict(self, symbols=None):
        """Return a JSON-serializable representation of the function."""
        def operand_to_dict(entry):
            kind = entry[0]
            value = entry[1]
            operand = {
                'kind': kind,
                'value': value,
            }
            if len(entry) > 2 and entry[2] is not None:
                operand['type'] = entry[2]
            if symbols and kind in ('var', 'ref', 'val'):
                operand['name'] = symbols.get_name(value)
            return operand

        operations = []
        for op in self.operations:
            item = dict(op)
            if 'operands' in item:
                item['operands'] = [operand_to_dict(e) for e in item['operands']]
            for field in ('target', 'default_target'):
                if field in item and symbols and isinstance(item[field], int):
                    item[f'{field}_name'] = symbols.get_name(item[field])
            operations.append(item)

        return {
            'index': self.index,
            'name': self.name,
            'header_offset': self.header_offset,
            'body_offset': self.body_offset,
            'end_offset': self.end_offset,
            'result_var': self.result_var,
            'result_var_name': symbols.get_name(self.result_var) if symbols and self.result_var is not None else None,
            'params': list(self.params),
            'param_names': [symbols.get_name(p) for p in self.params] if symbols else [],
            'operation_count': len(self.operations),
            'operations': operations,
        }


class ILSymbols:
    """Parsed symbol table from .sy file."""

    def __init__(self, data, token_width=2):
        self.data = data
        self.tw = token_width
        self.symbols = {}
        self._parse()

    def _parse(self):
        """Extract name strings from symbol data.

        Pattern: ... 01 01 [token] 00 [name] 00 ...
        Token is 2 bytes (test files) or 4 bytes (real compilations).
        """
        i = 0
        tw = self.tw
        while i < len(self.data):
            if self.data[i] == 0x00 and i + 1 < len(self.data):
                j = i + 1
                name_start = j
                while j < len(self.data) and 0x20 <= self.data[j] < 0x7F:
                    j += 1
                if j > name_start and j < len(self.data) and self.data[j] == 0x00:
                    name = self.data[name_start:j].decode('ascii')
                    if i >= tw:
                        if tw == 4:
                            token = (self.data[i-4] << 24) | (self.data[i-3] << 16) | (self.data[i-2] << 8) | self.data[i-1]
                        else:
                            token = (self.data[i-2] << 8) | self.data[i-1]
                        self.symbols[token] = name
            i += 1

    def get_name(self, token):
        return self.symbols.get(token, f'tok_{token:08x}' if token > 0xFFFF else f'tok_{token:04x}')

    def to_dict(self):
        """Return JSON-serializable symbol mapping."""
        return {
            f'{token:08x}' if token > 0xFFFF else f'{token:04x}': name
            for token, name in sorted(self.symbols.items())
        }


class ILGlobals:
    """Parsed global info from .gl file."""

    def __init__(self, data):
        self.data = data
        self.functions = []
        self.source_path = None
        self._parse()

    def _parse(self):
        """Extract function names and source path."""
        i = 0
        while i < len(self.data):
            if self.data[i] == ord('?'):
                j = i
                while j < len(self.data) and self.data[j] != 0:
                    j += 1
                name = self.data[i:j].decode('ascii', errors='replace')
                # Only accept valid MSVC mangled names: ?identifier@@...
                if len(name) > 2 and name[1].isalpha() and '@@' in name:
                    self.functions.append(name)
                i = j + 1
            else:
                i += 1

        for match in re.finditer(rb'[a-z]:\\[^\x00]+\.cpp\x00', self.data, re.IGNORECASE):
            self.source_path = match.group(0)[:-1].decode('ascii', errors='replace')

    def to_dict(self):
        """Return JSON-serializable global metadata."""
        return {
            'functions': list(self.functions),
            'source_path': self.source_path,
        }


class ILImports:
    """Parsed import/type metadata from .in file."""

    def __init__(self, data):
        self.data = data
        self.known_types = []
        self.class_types = []
        self.strings = []
        self._parse()

    def _parse(self):
        if not self.data:
            return

        for offset in range(len(self.data) - 2):
            key = bytes(self.data[offset:offset + 3])
            type_name = KNOWN_TYPES.get(key)
            if type_name:
                self.known_types.append({
                    'offset': offset,
                    'encoding': key.hex(),
                    'type': type_name,
                })

        for offset in range(len(self.data) - 3):
            if self.data[offset] == 0xA6:
                chunk = self.data[offset:offset + 4]
                self.class_types.append({
                    'offset': offset,
                    'encoding': chunk.hex(),
                })

        self.strings = _extract_ascii_strings(self.data)

    def to_dict(self):
        return {
            'known_types': self.known_types,
            'class_types': self.class_types,
            'strings': self.strings,
        }


class ILDebugInfo:
    """Parsed debug metadata from .db file."""

    def __init__(self, data, source_path=None):
        self.data = data
        self.source_path = source_path
        self.strings = []
        self.line_candidates = []
        self._parse()

    def _parse(self):
        if not self.data:
            return

        self.strings = _extract_ascii_strings(self.data)

        max_line = None
        if self.source_path:
            try:
                max_line = len(Path(self.source_path).read_text(encoding='utf-8', errors='replace').splitlines())
            except OSError:
                max_line = None

        candidates = set()
        for i in range(len(self.data) - 1):
            value = self.data[i] | (self.data[i + 1] << 8)
            if value <= 0:
                continue
            if max_line is not None and value > max_line:
                continue
            if max_line is None and value > 512:
                continue
            candidates.add(value)

        self.line_candidates = sorted(candidates)

    def to_dict(self):
        return {
            'strings': self.strings,
            'line_candidates': self.line_candidates,
        }


def _extract_ascii_strings(data, min_len=3):
    """Extract printable ASCII strings from a byte buffer."""
    strings = []
    current = []
    for byte in data:
        if 32 <= byte < 127:
            current.append(chr(byte))
            continue
        if len(current) >= min_len:
            strings.append(''.join(current))
        current = []
    if len(current) >= min_len:
        strings.append(''.join(current))
    return strings


class ILFile:
    """Complete IL file set (ex + gl + sy + in + db)."""

    def __init__(self, base_path):
        """Load all IL files for a given base name (without suffix)."""
        self.base = base_path
        self.ex_data = self._read(base_path + 'ex')
        self.gl_data = self._read(base_path + 'gl')
        self.sy_data = self._read(base_path + 'sy')
        self.in_data = self._read(base_path + 'in')
        self.db_data = self._read(base_path + 'db')

        self.token_width = self._detect_token_width()
        self.globals = ILGlobals(self.gl_data) if self.gl_data else None
        self.symbols = ILSymbols(self.sy_data, self.token_width) if self.sy_data else None
        self.imports = ILImports(self.in_data) if self.in_data else None
        self.debug = ILDebugInfo(self.db_data, self.globals.source_path if self.globals else None) if self.db_data else None
        self.functions = self._parse_functions()

    def _read(self, path):
        try:
            return Path(path).read_bytes()
        except FileNotFoundError:
            return None

    def _detect_token_width(self):
        """Detect token width from .ex file structure.

        Test files (5B 80 header) use 2-byte tokens.
        Real files (4F 02 header) use 4-byte tokens.
        Detection: find first '4F 02', measure bytes until next '4F'.
        """
        if not self.ex_data or len(self.ex_data) < 8:
            return 2

        data = self.ex_data
        # Find first 4F 02 sequence
        idx = data.find(b'\x4F\x02')
        if idx == -1:
            return 2

        # Count bytes between 02 and next 4F
        scan = idx + 2
        while scan < len(data) and data[scan] != 0x4F:
            scan += 1

        width = scan - (idx + 2)
        if width in (2, 4):
            return width
        # Default to 2 for unrecognized formats
        return 2

    def _parse_functions(self):
        if not self.ex_data:
            return []

        data = self.ex_data
        results = []
        func_idx = 0

        # Find all BE (Block Entry) markers — each precedes a function
        be_positions = []
        pos = 0
        while pos < len(data):
            idx = data.find(BE_MARKER, pos)
            if idx == -1:
                break
            be_positions.append(idx)
            pos = idx + 1

        for be_idx, be_pos in enumerate(be_positions):
            # After BE marker + block data + 0F, find SS (Start Statement)
            # Pattern: BE ... 0F [4F 1F ...] or [4F 02 module_id 4F 01 NN]
            # Then SS (53 53) starts the function body
            search_start = be_pos + 4  # Skip BE header
            ss_idx = data.find(b'\x53\x53', search_start)
            if ss_idx == -1:
                continue

            # Verify this SS belongs to this BE (not the next one)
            next_be = be_positions[be_idx + 1] if be_idx + 1 < len(be_positions) else len(data)
            if ss_idx >= next_be:
                continue

            # Find function end (4F 12) after this SS
            end_idx = data.find(FUNC_END, ss_idx)
            if end_idx == -1:
                end_idx = len(data)
            # Make sure we don't cross into the next function's BE block
            if end_idx > next_be:
                end_idx = next_be

            # Extract function body: starts right before SS marker
            # Include the label preceding SS if present
            body_start = ss_idx
            # Check for label: 4F 01 NN before SS
            if body_start >= 3 and data[body_start - 3] == 0x4F and data[body_start - 2] == 0x01:
                body_start -= 3

            body_data = data[body_start:end_idx]
            func = ILFunction(func_idx, be_pos, body_start, end_idx, body_data, self.token_width)

            if self.globals and func_idx < len(self.globals.functions):
                func.name = self.globals.functions[func_idx]
            else:
                func.name = f'func_{func_idx}'

            results.append(func)
            func_idx += 1

        return results

    def dump(self, verbose=False):
        """Print parsed IL summary."""
        if self.globals and self.globals.source_path:
            print(f"Source: {self.globals.source_path}")
        print(f"Functions: {len(self.functions)}")
        if self.globals:
            print(f"Symbols: {', '.join(self.globals.functions)}")
        print()

        for func in self.functions:
            name = func.name
            params = []
            for p in func.params:
                pname = self.symbols.get_name(p) if self.symbols else f'0x{p:04x}'
                params.append(pname)

            print(f"  {name}({', '.join(params)}):")

            for op in func.operations:
                opname = op['name']
                operands = op.get('operands', [])

                if opname == 'LABEL':
                    print(f"  .L{op['label']:02x}:")
                    continue

                ops_parts = []
                for entry in operands:
                    kind = entry[0]
                    val = entry[1]
                    typ = entry[2] if len(entry) > 2 else None
                    if kind == 'var':
                        vname = self.symbols.get_name(val) if self.symbols else f'0x{val:04x}'
                        if typ and typ != 'int':
                            ops_parts.append(f'{vname}:{typ}')
                        else:
                            ops_parts.append(vname)
                    elif kind == 'lit':
                        ops_parts.append(str(val))
                    elif kind == 'ref':
                        rname = self.symbols.get_name(val) if self.symbols else f'ref(0x{val:04x})'
                        ops_parts.append(f'&{rname}')
                    elif kind == 'val':
                        vname = self.symbols.get_name(val) if self.symbols else f'0x{val:04x}'
                        ops_parts.append(vname)
                    else:
                        ops_parts.append(f'{kind}:{val}')

                ops_str = ', '.join(ops_parts)

                if opname == 'ASSIGN':
                    target = self.symbols.get_name(op['target']) if self.symbols else f'0x{op["target"]:04x}'
                    if ops_str:
                        print(f"    {target} = {ops_str}")
                    else:
                        print(f"    ASSIGN → {target}")
                elif opname == 'RETURN':
                    target = self.symbols.get_name(op['target']) if self.symbols else f'0x{op["target"]:04x}'
                    print(f"    RETURN {target}")
                elif opname == 'COND_BRANCH':
                    target = op.get('target', '?')
                    print(f"    COND_BRANCH .L{target:04x} [{ops_str}]" if isinstance(target, int) else f"    COND_BRANCH ? [{ops_str}]")
                elif opname == 'CALL_START':
                    ret_type = op.get('return_type', '?')
                    print(f"    CALL_START({ops_str}) → {ret_type}")
                elif opname == 'CALL_EXEC':
                    ret_type = op.get('return_type', '?')
                    print(f"    CALL_EXEC({ops_str}) → {ret_type}")
                elif opname == 'SWITCH':
                    target = self.symbols.get_name(op['target']) if self.symbols else f'tok_{op["target"]:04x}'
                    if ops_str:
                        print(f"    SWITCH({ops_str}) → {target}")
                    else:
                        print(f"    SWITCH → {target}")
                elif opname == 'SWITCH_TABLE':
                    rtype = op.get('result_type', '')
                    default = self.symbols.get_name(op['default_target']) if self.symbols else f'tok_{op["default_target"]:04x}'
                    print(f"    SWITCH_TABLE:{rtype} default={default}")
                elif opname == 'CASE':
                    target = self.symbols.get_name(op['target']) if self.symbols else f'tok_{op["target"]:04x}'
                    case_val = op.get('value', '?')
                    print(f"    CASE {case_val} → {target}")
                elif opname in ('GOTO', 'FALLTHROUGH'):
                    if ops_str:
                        print(f"    {opname} [{ops_str}]")
                    else:
                        print(f"    {opname}")
                elif opname in ('DEREF', 'STORE', 'ADD_ASSIGN', 'SUB_ASSIGN', 'PTR_ADD'):
                    rtype = op.get('result_type', '')
                    extra = f' → {rtype}' if rtype else ''
                    print(f"    {opname}({ops_str}){extra}")
                elif opname == 'CB':
                    print(f"    CB({ops_str})")
                else:
                    if ops_str:
                        print(f"    {opname}({ops_str})")
                    else:
                        print(f"    {opname}")

            if verbose:
                print(f"    --- Raw body ({len(func.body)} bytes) ---")
                for off in range(0, len(func.body), 16):
                    chunk = func.body[off:off+16]
                    hexpart = ' '.join(f'{b:02x}' for b in chunk)
                    ascpart = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    print(f"      {off:04x}: {hexpart:<48s} {ascpart}")
            print()

    def to_dict(self):
        """Return a JSON-serializable representation of the IL bundle."""
        files = {}
        for suffix, data in (
            ('ex', self.ex_data),
            ('gl', self.gl_data),
            ('sy', self.sy_data),
            ('in', self.in_data),
            ('db', self.db_data),
        ):
            files[suffix] = {
                'path': Path(self.base + suffix).name,
                'present': data is not None,
                'size': len(data) if data is not None else 0,
            }

        return {
            'base': self.base,
            'token_width': self.token_width,
            'files': files,
            'globals': self.globals.to_dict() if self.globals else None,
            'symbols': self.symbols.to_dict() if self.symbols else {},
            'imports': self.imports.to_dict() if self.imports else None,
            'debug': self.debug.to_dict() if self.debug else None,
            'functions': [func.to_dict(self.symbols) for func in self.functions],
        }


# --- IL Capture ---

def _get_ninja_compile_cmd(source_path):
    """Extract the compile command for a source file from ninja.

    Returns (wibo_path, cl_args, env_vars, cwd) or None if not found.
    """
    source_path = str(Path(source_path).resolve())
    source_name = Path(source_path).name

    try:
        result = subprocess.run(
            ['ninja', '-t', 'commands'],
            capture_output=True, text=True,
            cwd=str(PROJECT_ROOT),
            timeout=30,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    for line in result.stdout.splitlines():
        if source_name not in line:
            continue
        # Line format: cd <dir> && <wibo_path> [ENV_VARS] <cl_path> <flags> <source>
        m = re.match(r'^cd\s+(\S+)\s+&&\s+(.+)$', line)
        if not m:
            continue
        cwd = m.group(1)
        rest = m.group(2)

        # Extract wibo path and env vars
        parts = rest.split()
        wibo_path = parts[0]
        env_vars = {}
        cl_start = 1
        for idx, part in enumerate(parts[1:], 1):
            if '=' in part and not part.startswith('/'):
                k, v = part.split('=', 1)
                env_vars[k] = v.strip("'")
            else:
                cl_start = idx
                break

        cl_path = parts[cl_start]
        cl_args = parts[cl_start + 1:]

        return wibo_path, cl_args, env_vars, cwd, cl_path

    return None


def capture_il(
    source_path,
    output_dir=None,
    cl_path=None,
    wibo_path=None,
    bundle_name=None,
):
    """Compile a source file and capture the IL files.

    Uses /d2nop to make c2.dll fail immediately, leaving IL files intact.
    For project source files, extracts the real compile command from ninja
    (including includes, PCH, flags). For standalone test files, uses basic flags.

    Returns the base path of the captured IL files.
    If bundle_name is provided, files are placed in a dedicated bundle
    directory under output_dir and a manifest is written there.
    """
    if output_dir is None:
        output_dir = '/tmp/claude-1000'

    source_path = str(Path(source_path).resolve())
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Try to get real compile command from ninja for project files
    ninja_cmd = _get_ninja_compile_cmd(source_path)

    if ninja_cmd:
        wibo_path_n, cl_args, env_vars, cwd, cl_path_n = ninja_cmd
        # Use ninja's wibo and cl paths
        if wibo_path is None:
            wibo_path = wibo_path_n
        if cl_path is None:
            cl_path = cl_path_n

        # Filter out /Fo (output), /showIncludes, source file — replace with IL capture flags
        filtered_args = []
        skip_next = False
        for arg in cl_args:
            if skip_next:
                skip_next = False
                continue
            if arg.startswith('/Fo'):
                continue
            if arg == '/showIncludes':
                continue
            if arg.endswith('.cpp') or arg.endswith('.c'):
                continue
            filtered_args.append(arg)

        win_source = to_winpath(source_path)
        win_obj = to_winpath(str(output_dir / 'il_capture.obj'))

        cmd = [wibo_path, cl_path] + ['/Bd', '/d2nop'] + filtered_args + [
            f'/Fo{win_obj}', win_source
        ]

        env = os.environ.copy()
        env.update(env_vars)
        env['WIBO_FS_CACHE'] = '1'

        # Resolve cwd relative to PROJECT_ROOT
        run_cwd = str(PROJECT_ROOT / cwd) if not Path(cwd).is_absolute() else cwd
    else:
        # Standalone file — basic flags
        if cl_path is None:
            cl_path = DEFAULT_CL
        if wibo_path is None:
            wibo_path = find_wibo()

        win_source = to_winpath(source_path)
        win_obj = to_winpath(str(output_dir / 'il_capture.obj'))

        cmd = [
            wibo_path, cl_path,
            '/Bd', '/d2nop',
            '/Ox', '/GS-',
            '/c',
            f'/Fo{win_obj}',
            win_source,
        ]

        env = os.environ.copy()
        env['WIBO_FS_CACHE'] = '1'
        run_cwd = str(output_dir)

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=env,
        cwd=run_cwd,
        timeout=60,
    )

    il_base = None
    il_src_dir = None
    for line in result.stdout.splitlines() + result.stderr.splitlines():
        # The compiler reports the IL temp path as `-il <dir><sep>_CL_<hash>`.
        # On the current wibo build the prefix is a real tmp path with a
        # backslash separator (e.g. `<tmpdir>\_CL_2596b77c`), so strip any
        # leading directory before the _CL_ basename and remember the directory
        # the files actually landed in (drift fix 2026-07-10; older builds
        # emitted a bare `_CL_<hash>` with files in CWD).
        m = re.search(r'-il\s+(\S*?)(_CL_[0-9a-f]+)', line)
        if m:
            il_base = m.group(2)
            prefix = m.group(1).replace('\\', '/')
            if prefix:
                il_src_dir = os.path.dirname(prefix + il_base) or None
            break

    if il_base is None:
        print("ERROR: Could not find IL base name in compiler output", file=sys.stderr)
        if result.stderr:
            # Show first few lines of errors
            for line in result.stderr.splitlines()[:5]:
                print(f"  {line}", file=sys.stderr)
        return None

    # IL files are written to the compiler's temp dir (reported via -il) or CWD.
    # Move them to output_dir if they landed elsewhere.
    candidates = []
    if il_src_dir:
        candidates.append(str(Path(il_src_dir) / il_base))
    candidates.append(str(Path(run_cwd) / il_base))
    candidates.append(str(output_dir / il_base))

    il_path = str(output_dir / il_base)
    for cand in candidates:
        if not Path(cand + 'ex').exists():
            continue
        if str(Path(cand).parent.resolve()) != str(output_dir.resolve()):
            import shutil
            for s in IL_SUFFIXES:
                src = Path(cand + s)
                if src.exists():
                    shutil.move(str(src), str(output_dir / (il_base + s)))
            il_path = str(output_dir / il_base)
        else:
            il_path = cand
        break
    else:
        print("WARNING: IL .ex file not found", file=sys.stderr)

    for s in IL_SUFFIXES:
        p = Path(il_path + s)
        if not p.exists():
            print(f"WARNING: {p} not found", file=sys.stderr)

    final_base = Path(il_path)
    if bundle_name:
        safe_name = _sanitize_bundle_name(bundle_name)
        bundle_dir = output_dir / safe_name
        bundle_dir.mkdir(parents=True, exist_ok=True)
        bundled_base = bundle_dir / il_base
        import shutil
        for suffix in IL_SUFFIXES:
            src = Path(str(final_base) + suffix)
            dst = Path(str(bundled_base) + suffix)
            if src.exists():
                shutil.copy2(src, dst)
        final_base = bundled_base

    manifest = build_bundle_manifest(
        final_base,
        source_path=source_path,
        bundle_name=bundle_name,
        il_base=il_base,
        command=cmd,
        run_cwd=run_cwd,
        cl_path=cl_path,
        wibo_path=wibo_path,
    )
    write_bundle_manifest(final_base, manifest, bundle_dir=final_base.parent if bundle_name else None)
    return str(final_base)


# --- Main CLI ---

def cmd_capture(args):
    il_base = capture_il(
        args.source,
        output_dir=args.output_dir,
        cl_path=args.cl,
        wibo_path=args.wibo if args.wibo else None,
        bundle_name=args.bundle_name,
    )
    if il_base:
        manifest_path = Path(il_base).parent / 'manifest.json' if args.bundle_name else _bundle_manifest_path(il_base)
        print(f"IL files captured: {il_base}[ex|gl|sy|in|db]")
        if manifest_path.exists():
            print(f"Manifest: {manifest_path}")
        return il_base
    return None


def cmd_parse(args):
    base = resolve_bundle_base(args.file)
    il = ILFile(base)
    il.dump(verbose=args.verbose)


def cmd_analyze(args):
    il_base = capture_il(
        args.source,
        output_dir=args.output_dir,
        cl_path=args.cl,
        wibo_path=args.wibo if args.wibo else None,
        bundle_name=args.bundle_name,
    )
    if il_base:
        print(f"IL files: {il_base}")
        manifest_path = Path(il_base).parent / 'manifest.json' if args.bundle_name else _bundle_manifest_path(il_base)
        if manifest_path.exists():
            print(f"Manifest: {manifest_path}")
        print()
        il = ILFile(il_base)
        il.dump(verbose=args.verbose)


def cmd_list_bundle(args):
    base = resolve_bundle_base(args.path)
    manifest = read_bundle_manifest(args.path)
    print(f"Bundle base: {base}")
    if manifest:
        print(f"Bundle name: {manifest.get('bundle_name')}")
        print(f"Captured:    {manifest.get('captured_at')}")
        if manifest.get('source_path'):
            print(f"Source:      {manifest.get('source_path')}")
        if manifest.get('run_cwd'):
            print(f"Run CWD:     {manifest.get('run_cwd')}")
        for suffix in IL_SUFFIXES:
            info = manifest.get('files', {}).get(suffix, {})
            status = 'ok' if info.get('exists') else 'missing'
            print(f"  {suffix}: {status:7s} {info.get('size', 0):6d}B  {info.get('path', '')}")
    else:
        print("Manifest:    <missing>")
        for suffix in IL_SUFFIXES:
            path = Path(str(base) + suffix)
            if path.exists():
                print(f"  {suffix}: ok      {path.stat().st_size:6d}B  {path.name}")
            else:
                print(f"  {suffix}: missing      0B  {path.name}")

    if args.functions:
        print()
        il = ILFile(base)
        print(f"Functions: {len(il.functions)}")
        for func in il.functions:
            print(f"  {func.name} ({len(func.operations)} ops)")


def cmd_export_json(args):
    """Export a parsed IL bundle as normalized JSON."""
    base = resolve_bundle_base(args.path)
    il = ILFile(base)
    data = il.to_dict()
    manifest = read_bundle_manifest(args.path)
    if manifest:
        data['manifest'] = manifest

    output_path = Path(args.output) if args.output else Path(base).parent / 'bundle.json'
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(data, indent=2, sort_keys=True) + '\n',
        encoding='utf-8',
    )
    print(f"Wrote {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description='MSVC PPC IL parser — capture and analyze intermediate language files'
    )
    parser.add_argument('--cl', default=DEFAULT_CL, help='Path to cl.exe')
    parser.add_argument('--wibo', default=None, help='Path to wibo (auto-detected)')

    sub = parser.add_subparsers(dest='command')

    p_cap = sub.add_parser('capture', help='Capture IL files from source')
    p_cap.add_argument('source', help='C++ source file')
    p_cap.add_argument('--output-dir', default='/tmp/claude-1000', help='Output directory')
    p_cap.add_argument('--bundle-name', help='Store capture in a named bundle directory')

    p_parse = sub.add_parser('parse', help='Parse a captured IL file')
    p_parse.add_argument('file', help='IL file base name or .ex path')
    p_parse.add_argument('-v', '--verbose', action='store_true', help='Show raw hex')

    p_analyze = sub.add_parser('analyze', help='Capture + parse in one step')
    p_analyze.add_argument('source', help='C++ source file')
    p_analyze.add_argument('--output-dir', default='/tmp/claude-1000', help='Output directory')
    p_analyze.add_argument('--bundle-name', help='Store capture in a named bundle directory')
    p_analyze.add_argument('-v', '--verbose', action='store_true', help='Show raw hex')

    p_list = sub.add_parser('list-bundle', help='Show bundle metadata and contents')
    p_list.add_argument('path', help='Bundle dir, manifest path, base path, or .ex path')
    p_list.add_argument('--functions', action='store_true', help='Also list parsed functions')

    p_json = sub.add_parser('export-json', help='Export a parsed IL bundle as normalized JSON')
    p_json.add_argument('path', help='Bundle dir, manifest path, base path, or .ex path')
    p_json.add_argument('--output', help='Output JSON path (default: bundle dir/bundle.json)')

    args = parser.parse_args()

    if args.command == 'capture':
        cmd_capture(args)
    elif args.command == 'parse':
        cmd_parse(args)
    elif args.command == 'analyze':
        cmd_analyze(args)
    elif args.command == 'list-bundle':
        cmd_list_bundle(args)
    elif args.command == 'export-json':
        cmd_export_json(args)
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
