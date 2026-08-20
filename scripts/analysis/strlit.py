#!/usr/bin/env python3
"""Read MSVC string-literal COMDATs out of COFF objects, and decode ??_C@ names."""
import struct, sys, os

# ---------- mangled name decode ----------
_SPECIAL = {
    '0':',', '1':'/', '2':'\\', '3':':', '4':'.', '5':' ', '6':'\n', '7':'\t',
    '8':"'", '9':'-',
}
def _dec_char(s, i):
    """decode one source char at s[i]; return (char, next_i)"""
    c = s[i]
    if c != '?':
        return c, i+1
    n = s[i+1]
    if n in _SPECIAL:
        return _SPECIAL[n], i+2
    if n == '$':
        a, b = s[i+2], s[i+3]
        return chr(((ord(a)-ord('A'))<<4) | (ord(b)-ord('A'))), i+4
    if 'a' <= n <= 'z':
        return chr(0xE1 + (ord(n)-ord('a'))), i+2
    if 'A' <= n <= 'Z':
        return chr(0xC1 + (ord(n)-ord('A'))), i+2
    raise ValueError('bad escape %r at %d in %s' % (n, i, s))

def dec_num(s, i):
    """MSVC encoded number: '0'-'9' => 1..10 ; 'A'-'P' hex digits terminated by '@'"""
    if s[i].isdigit():
        return int(s[i]) + 1, i+1
    v = 0
    while s[i] != '@':
        v = v*16 + (ord(s[i]) - ord('A'))
        i += 1
    return v, i+1

def decode_strlit(name):
    """??_C@_<0|1><num>@<hash>@<text>@ -> (is_wide, declared_len, hash, visible_text)"""
    assert name.startswith('??_C@_'), name
    i = 6
    wide = name[i] == '1'
    i += 1
    ln, i = dec_num(name, i)
    j = name.index('@', i)
    h = name[i:j]
    i = j + 1
    out = []
    while i < len(name) and name[i] != '@':
        c, i = _dec_char(name, i)
        out.append(c)
    truncated = (i >= len(name)) or (name[i] != '@') or (len(out) == 32)
    return wide, ln, h, ''.join(out), truncated

# ---------- COFF ----------
def read_coff_symbols(path):
    d = open(path,'rb').read()
    machine, nsec, ts, symptr, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', d, 0)
    secs = []
    off = 20 + optsz
    for k in range(nsec):
        raw = d[off:off+40]
        nm = raw[0:8]
        vsz, va, szraw, ptrraw, ptrrel, ptrln, nrel, nln, flags = struct.unpack_from('<IIIIIIHHI', raw, 8)
        secs.append(dict(name=nm, size=szraw, ptr=ptrraw, flags=flags))
        off += 40
    strtab_off = symptr + nsym*18
    strtab_len = struct.unpack_from('<I', d, strtab_off)[0] if strtab_off+4 <= len(d) else 4
    strtab = d[strtab_off:strtab_off+strtab_len]
    syms = {}
    i = 0
    while i < nsym:
        rec = d[symptr+i*18: symptr+i*18+18]
        if rec[0:4] == b'\x00\x00\x00\x00':
            so = struct.unpack_from('<I', rec, 4)[0]
            e = strtab.index(b'\x00', so)
            name = strtab[so:e].decode('latin-1')
        else:
            name = rec[0:8].rstrip(b'\x00').decode('latin-1')
        value, secnum, typ, sclass, naux = struct.unpack_from('<IhHBB', rec, 8)
        if name.startswith('??_C@') and 1 <= secnum <= len(secs):
            s = secs[secnum-1]
            syms.setdefault(name, (s, value))
        i += 1 + naux
    return d, secs, syms

def get_bytes(path, name):
    d, secs, syms = read_coff_symbols(path)
    if name not in syms:
        return None
    s, val = syms[name]
    return d[s['ptr']+val : s['ptr']+val+s['size']], s['size']

if __name__ == '__main__':
    if sys.argv[1] == 'decode':
        for n in sys.argv[2:]:
            w, ln, h, t, tr = decode_strlit(n)
            print(("L" if w else " "), ln, h, repr(t), "TRUNC" if tr else "")
    else:
        obj, name = sys.argv[1], sys.argv[2]
        b, sz = get_bytes(obj, name)
        print(sz, repr(b))
