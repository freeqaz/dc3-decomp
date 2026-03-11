#!/usr/bin/env python3
"""Compare IL representations between two source file variants.

Shows which IL operations differ between two versions of the same source,
helping identify exactly where a code change affects the compiler's
intermediate representation and thus the generated PPC code.

Usage:
    # Compare two standalone files:
    python3 msvc-src/tools/il_diff.py file_a.cpp file_b.cpp

    # Compare with function filter:
    python3 msvc-src/tools/il_diff.py file_a.cpp file_b.cpp -f MyFunction
"""
import argparse
import sys
from pathlib import Path
from difflib import unified_diff

# Import from same directory
sys.path.insert(0, str(Path(__file__).resolve().parent))
from il_parser import ILFile, capture_il


def il_ops_to_lines(func, symbols):
    """Convert IL operations to comparable text lines."""
    lines = []
    for op in func.operations:
        opname = op['name']
        operands = op.get('operands', [])
        ops_parts = []
        for entry in operands:
            kind = entry[0]
            val = entry[1]
            typ = entry[2] if len(entry) > 2 else None
            if kind == 'var':
                vname = symbols.get_name(val) if symbols else f'tok_{val:04x}'
                type_suffix = f':{typ}' if (typ and typ != 'int') else ''
                ops_parts.append(f'{vname}{type_suffix}')
            elif kind == 'lit':
                ops_parts.append(str(val))
            elif kind == 'ref':
                rname = symbols.get_name(val) if symbols else f'ref_{val:04x}'
                ops_parts.append(f'&{rname}')
            elif kind == 'val':
                vname = symbols.get_name(val) if symbols else f'tok_{val:04x}'
                ops_parts.append(vname)
            else:
                ops_parts.append(f'{kind}:{val}')
        ops_str = ', '.join(ops_parts)

        if opname == 'LABEL':
            lines.append(f".L{op['label']:02x}:")
        elif opname == 'ASSIGN':
            target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
            lines.append(f"  {target} = {ops_str}" if ops_str else f"  ASSIGN → {target}")
        elif opname == 'RETURN':
            target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
            lines.append(f"  RETURN {target}")
        elif opname in ('SWITCH', 'SWITCH_TABLE', 'CASE'):
            if opname == 'SWITCH':
                target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
                lines.append(f"  SWITCH({ops_str}) → {target}")
            elif opname == 'SWITCH_TABLE':
                rtype = op.get('result_type', '')
                default = symbols.get_name(op['default_target']) if symbols else f'tok_{op["default_target"]:04x}'
                lines.append(f"  SWITCH_TABLE:{rtype} default={default}")
            elif opname == 'CASE':
                target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
                lines.append(f"  CASE {op.get('value', '?')} → {target}")
        elif opname == 'COND_BRANCH':
            lines.append(f"  COND_BRANCH [{ops_str}]")
        elif opname in ('CALL_START', 'CALL_EXEC'):
            ret_type = op.get('return_type', '?')
            lines.append(f"  {opname}({ops_str}) → {ret_type}")
        elif opname == 'CAST':
            ret_type = op.get('result_type', '?')
            lines.append(f"  CAST({ops_str}) → {ret_type}" if ops_str else f"  CAST → {ret_type}")
        elif opname in ('GOTO', 'FALLTHROUGH'):
            lines.append(f"  {opname}")
        else:
            lines.append(f"  {opname}({ops_str})" if ops_str else f"  {opname}")
    return lines


def main():
    parser = argparse.ArgumentParser(description='Compare IL between source variants')
    parser.add_argument('file_a', help='First source file')
    parser.add_argument('file_b', help='Second source file')
    parser.add_argument('--function', '-f', help='Filter to specific function')
    parser.add_argument('--output-dir', default='/tmp/claude-1000', help='Working directory')
    args = parser.parse_args()

    output_dir_a = str(Path(args.output_dir) / 'il_diff_a')
    output_dir_b = str(Path(args.output_dir) / 'il_diff_b')
    Path(output_dir_a).mkdir(parents=True, exist_ok=True)
    Path(output_dir_b).mkdir(parents=True, exist_ok=True)

    # Capture IL for both
    print(f"Capturing IL for {Path(args.file_a).name}...", file=sys.stderr)
    base_a = capture_il(args.file_a, output_dir=output_dir_a)
    print(f"Capturing IL for {Path(args.file_b).name}...", file=sys.stderr)
    base_b = capture_il(args.file_b, output_dir=output_dir_b)

    if not base_a or not base_b:
        print("ERROR: IL capture failed", file=sys.stderr)
        sys.exit(1)

    il_a = ILFile(base_a)
    il_b = ILFile(base_b)

    # Build function name → function maps
    funcs_a = {f.name: f for f in il_a.functions}
    funcs_b = {f.name: f for f in il_b.functions}

    all_names = sorted(set(list(funcs_a.keys()) + list(funcs_b.keys())))

    total_diff = 0
    for name in all_names:
        if args.function and args.function not in name:
            continue

        if name in funcs_a and name in funcs_b:
            lines_a = il_ops_to_lines(funcs_a[name], il_a.symbols)
            lines_b = il_ops_to_lines(funcs_b[name], il_b.symbols)

            if lines_a == lines_b:
                continue

            total_diff += 1
            print(f"\n{'=' * 70}")
            print(f"  CHANGED: {name}")
            print(f"{'=' * 70}")

            diff = list(unified_diff(
                lines_a, lines_b,
                fromfile=Path(args.file_a).name,
                tofile=Path(args.file_b).name,
                lineterm=''
            ))
            for line in diff:
                print(line)

        elif name in funcs_a:
            total_diff += 1
            print(f"\n--- REMOVED: {name}")
        elif name in funcs_b:
            total_diff += 1
            print(f"\n+++ ADDED: {name}")

    if total_diff == 0:
        print("No IL differences found.")
    else:
        print(f"\n{total_diff} function(s) differ in IL.")


if __name__ == '__main__':
    main()
