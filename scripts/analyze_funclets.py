#!/usr/bin/env python3
"""
Analyze unpaired fn_<addr> funclets in DC3 decomp report.json.
Classifies WHY each funclet is unpaired:
  - CLASS A: static-init/dtor (??__E/??__F) with matching bytes in base
  - CLASS B: large COMDAT function (size > 200B) - name lost during split
  - CLASS C: XDK/non-authorable unit (expected non-pairable)
  - CLASS D: other / truly orphaned
"""
import struct
import re
import os
import json
from collections import defaultdict

REPORT_JSON = "build/373307D9/report.json"
FN_PATTERN = re.compile(r'^fn_[0-9a-fA-F]{8}$')


def read_coff(filepath):
    """Parse a COFF object file, returning (data, sections, symbols)."""
    with open(filepath, 'rb') as f:
        data = f.read()
    machine, nsections, timestamp, sym_ptr, num_syms, opt_size, chars = \
        struct.unpack_from('<HHiIIHH', data, 0)
    str_table_offset = sym_ptr + num_syms * 18

    section_offset = 20 + opt_size
    sections = []
    for i in range(nsections):
        sh = data[section_offset + i * 40: section_offset + i * 40 + 40]
        if len(sh) < 40:
            break
        name_raw = sh[:8]
        if name_raw[0:1] == b'/':
            try:
                str_off = int(name_raw.lstrip(b'/').rstrip(b'\x00').decode('ascii'))
                end_idx = data.index(b'\x00', str_table_offset + str_off)
                name = data[str_table_offset + str_off:end_idx].decode('ascii', errors='replace')
            except Exception:
                name = name_raw.decode('ascii', errors='replace')
        else:
            name = name_raw.rstrip(b'\x00').decode('ascii', errors='replace')
        vsize, vaddr, raw_size, raw_offset, reloc_offset, lineno_offset, n_relocs, n_linenos, flags = \
            struct.unpack_from('<IIIIIIHHI', sh, 8)
        sections.append({
            'idx': i + 1,
            'name': name,
            'raw_size': raw_size,
            'raw_offset': raw_offset,
            'n_relocs': n_relocs,
            'reloc_offset': reloc_offset,
            'flags': flags
        })

    symbols = []
    i = 0
    sym_offset = sym_ptr
    while i < num_syms:
        entry = data[sym_offset:sym_offset + 18]
        if len(entry) < 18:
            break
        name_bytes = entry[:8]
        value = struct.unpack_from('<I', entry, 8)[0]
        section_num = struct.unpack_from('<h', entry, 12)[0]
        sym_type = struct.unpack_from('<H', entry, 14)[0]
        storage_class = entry[16]
        aux_count = entry[17]
        if name_bytes[:4] == b'\x00\x00\x00\x00':
            str_off_v = struct.unpack_from('<I', name_bytes, 4)[0]
            end_idx = data.index(b'\x00', str_table_offset + str_off_v)
            name = data[str_table_offset + str_off_v:end_idx].decode('ascii', errors='replace')
        else:
            name = name_bytes.rstrip(b'\x00').decode('ascii', errors='replace')
        symbols.append({
            'name': name, 'value': value, 'section': section_num,
            'class': storage_class, 'type': sym_type
        })
        sym_offset += 18 * (1 + aux_count)
        i += 1 + aux_count
    return data, sections, symbols


def get_relocs_for_section(data, sec):
    """Return list of reloc offsets in the section."""
    offsets = []
    if sec['n_relocs'] == 0:
        return offsets
    reloc_offset = sec['reloc_offset']
    for i in range(sec['n_relocs']):
        r = data[reloc_offset + i * 10: reloc_offset + i * 10 + 10]
        if len(r) < 10:
            break
        virt_addr = struct.unpack_from('<I', r, 0)[0]
        offsets.append(virt_addr)
    return offsets


def mask_bytes(raw_bytes, reloc_offsets, base_addr=0):
    """Zero 4-byte instruction words at relocation sites."""
    b = bytearray(raw_bytes)
    for addr in reloc_offsets:
        off = addr - base_addr
        if 0 <= off < len(b):
            end = min(off + 4, len(b))
            for j in range(off, end):
                b[j] = 0
    return bytes(b)


def get_sym_bytes_masked(data, sects, sym):
    """Get masked bytes for a symbol given section info."""
    sec_num = sym['section']
    if sec_num <= 0 or sec_num > len(sects):
        return None
    sec = sects[sec_num - 1]
    start = sec['raw_offset'] + sym['value']
    size = sec['raw_size']
    raw = data[start:start + size]
    reloc_offs = get_relocs_for_section(data, sec)
    return mask_bytes(raw, reloc_offs, sym['value'])


def similarity(a, b):
    if len(a) != len(b) or len(a) == 0:
        return 0.0
    return sum(1 for x, y in zip(a, b) if x == y) / len(a)


def main():
    with open(REPORT_JSON) as f:
        report = json.load(f)

    # Collect unpaired fn_<addr> by unit
    unpaired_by_unit = defaultdict(list)
    for unit in report.get('units', []):
        uname = unit.get('name', '')
        for fn in unit.get('functions', []):
            fname = fn.get('name', '')
            if FN_PATTERN.match(fname):
                fmp = fn.get('fuzzy_match_percent')
                bsz = int(fn.get('base_size') or 0)
                if (fmp is None or fmp < 1.0) and bsz == 0:
                    unpaired_by_unit[uname].append(fname)

    total = sum(len(v) for v in unpaired_by_unit.values())
    print(f"Total unpaired fn_<addr> funclets: {total}")
    print()

    class_a = []  # static-init/dtor, bytes match
    class_b = []  # large COMDAT function (>200B), no match possible
    class_c = []  # XDK/non-authorable
    class_d = []  # other / truly orphaned

    for unit, fn_names in sorted(unpaired_by_unit.items()):
        is_xdk = 'xdk' in unit or 'binkxenon' in unit
        if is_xdk:
            for fn in fn_names:
                class_c.append((unit, fn, 'xdk/bink unit'))
            continue

        unit_short = unit.replace('default/', '')
        tgt_path = f'build/373307D9/obj/{unit_short}.obj'
        base_path = f'build/373307D9/src/{unit_short}.obj'

        if not os.path.exists(tgt_path):
            for fn in fn_names:
                class_d.append((unit, fn, 'target obj missing'))
            continue
        if not os.path.exists(base_path):
            for fn in fn_names:
                class_d.append((unit, fn, 'base obj missing'))
            continue

        tgt_data, tgt_sects, tgt_syms = read_coff(tgt_path)
        base_data, base_sects, base_syms = read_coff(base_path)

        # Build base symbol index by size
        base_init_dtor_by_size = defaultdict(list)
        for sym in base_syms:
            n = sym['name']
            if ('??__E' in n or '??__F' in n) and sym['section'] > 0:
                sec = base_sects[sym['section'] - 1]
                masked = get_sym_bytes_masked(base_data, base_sects, sym)
                if masked:
                    base_init_dtor_by_size[len(masked)].append((n, masked))

        # Also build __unwind$ base candidates by size
        base_unwind_by_size = defaultdict(list)
        for sym in base_syms:
            n = sym['name']
            if ('__unwind$' in n or '__catch$' in n) and sym['section'] > 0:
                sec = base_sects[sym['section'] - 1]
                masked = get_sym_bytes_masked(base_data, base_sects, sym)
                if masked:
                    base_unwind_by_size[len(masked)].append((n, masked))

        for fn_name in fn_names:
            sym = next((s for s in tgt_syms if s['name'] == fn_name), None)
            if not sym or sym['section'] <= 0:
                class_d.append((unit, fn_name, 'no target symbol'))
                continue

            sec = tgt_sects[sym['section'] - 1]
            sec_size = sec['raw_size']
            masked_tgt = get_sym_bytes_masked(tgt_data, tgt_sects, sym)

            if sec_size > 200:
                class_b.append((unit, fn_name, f'.text$dup size={sec_size}B (large COMDAT)'))
                continue

            # Try to find a matching base symbol
            found_match = None
            # Check ??__E/??__F
            for bname, bmasked in base_init_dtor_by_size.get(sec_size, []):
                sim = similarity(masked_tgt, bmasked)
                if sim >= 0.5:
                    found_match = (bname, sim, 'static-init/dtor')
                    break
            # Also check __unwind$ if not found
            if not found_match:
                for bname, bmasked in base_unwind_by_size.get(sec_size, []):
                    sim = similarity(masked_tgt, bmasked)
                    if sim >= 0.5:
                        found_match = (bname, sim, '__unwind (should be paired?)')
                        break

            if found_match:
                bname, sim, kind = found_match
                class_a.append((unit, fn_name, sec_size, bname, sim, kind))
            else:
                class_d.append((unit, fn_name, f'no base match at size={sec_size} in {sec["name"]}'))

    # Report
    print(f"CLASS A (static-init/dtor with matching base bytes): {len(class_a)}")
    for unit, fn, sz, bname, sim, kind in class_a:
        bname_trunc = bname[:60] + '...' if len(bname) > 60 else bname
        print(f"  {fn} (size={sz}) <-> {bname_trunc} (sim={sim:.1%}) [{kind}]")
        print(f"    unit: {unit}")

    print()
    print(f"CLASS B (large COMDAT fn >200B, name lost during split): {len(class_b)}")
    for unit, fn, reason in class_b:
        print(f"  {fn} ({reason})")
        print(f"    unit: {unit}")

    print()
    print(f"CLASS C (XDK/non-authorable, expected non-pairable): {len(class_c)}")
    by_unit = defaultdict(list)
    for unit, fn, reason in class_c:
        by_unit[unit].append(fn)
    for unit, fns in sorted(by_unit.items()):
        print(f"  {unit}: {len(fns)} funclets")

    print()
    print(f"CLASS D (other / truly orphaned): {len(class_d)}")
    for unit, fn, reason in class_d:
        print(f"  {fn}: {reason}")
        print(f"    unit: {unit}")

    print()
    print("=== SUMMARY ===")
    print(f"Total: {total}")
    print(f"  A (pairable via ??__E/??__F extension): {len(class_a)}")
    print(f"  B (large COMDAT, unpairable without name recovery): {len(class_b)}")
    print(f"  C (XDK/bink, non-authorable, expected): {len(class_c)}")
    print(f"  D (other/orphaned): {len(class_d)}")


if __name__ == '__main__':
    main()
