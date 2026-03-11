#!/usr/bin/env python3
"""Compare IL representation with PPC output for the same source file.

Shows how IL operations map to PPC instructions, helping identify
where source changes will affect codegen.

Usage:
    python3 msvc-src/tools/il_annotate.py source.cpp [--function name]
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# Import from same directory
sys.path.insert(0, str(Path(__file__).resolve().parent))
from il_parser import ILFile, capture_il, _get_ninja_compile_cmd

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
CL_EXE = str(PROJECT_ROOT / "build/compilers/X360/16.00.11886.00/cl.exe")


def find_wibo():
    candidates = [
        str(PROJECT_ROOT / "build/tools/wibo"),
        str(PROJECT_ROOT / "../wibo/build/release/wibo"),
    ]
    for c in candidates:
        if Path(c).exists():
            return str(Path(c).resolve())
    raise FileNotFoundError("wibo not found")


def to_winpath(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def compile_with_listing(source_path, output_dir):
    """Compile and get PPC listing, using ninja command if available."""
    cod_path = str(Path(output_dir) / 'output.cod')
    win_cod = to_winpath(cod_path)
    win_source = to_winpath(source_path)

    ninja_cmd = _get_ninja_compile_cmd(source_path)

    if ninja_cmd:
        wibo_path, cl_args, env_vars, cwd, cl_path = ninja_cmd
        # Filter out /Fo, /showIncludes, source file; add listing flags
        filtered_args = []
        for arg in cl_args:
            if arg.startswith('/Fo'):
                continue
            if arg == '/showIncludes':
                continue
            if arg.endswith('.cpp') or arg.endswith('.c'):
                continue
            filtered_args.append(arg)

        cmd = [wibo_path, cl_path] + filtered_args + [
            '/FAcs', f'/Fa{win_cod}',
            f'/Fo{win_cod.replace(".cod", ".obj")}',
            win_source,
        ]

        env = os.environ.copy()
        env.update(env_vars)
        env['WIBO_FS_CACHE'] = '1'
        run_cwd = str(PROJECT_ROOT / cwd) if not Path(cwd).is_absolute() else cwd
    else:
        wibo = find_wibo()
        cmd = [
            wibo, CL_EXE,
            '/Ox', '/GS-', '/c',
            '/FAcs', f'/Fa{win_cod}',
            f'/Fo{win_cod.replace(".cod", ".obj")}',
            win_source,
        ]
        env = os.environ.copy()
        env['WIBO_FS_CACHE'] = '1'
        run_cwd = str(output_dir)

    subprocess.run(cmd, capture_output=True, text=True, env=env,
                   cwd=run_cwd, timeout=60)

    if Path(cod_path).exists():
        return open(cod_path).read()
    return None


def parse_functions_from_listing(listing):
    """Extract per-function assembly from .cod listing."""
    functions = {}
    current_name = None
    current_lines = []

    for line in listing.splitlines():
        m = re.match(r'^(\?[^\s]+)\s+PROC\s+NEAR', line)
        if m:
            if current_name:
                functions[current_name] = current_lines
            current_name = m.group(1)
            current_lines = []
            continue

        m = re.match(r'^(\?[^\s]+)\s+ENDP', line)
        if m:
            if current_name:
                functions[current_name] = current_lines
            current_name = None
            current_lines = []
            continue

        if current_name:
            current_lines.append(line)

    return functions


def format_il_ops(func, symbols):
    """Format IL operations for display."""
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
                vname = symbols.get_name(val) if symbols else f'0x{val:04x}'
                type_suffix = f':{typ}' if (typ is not None and typ != 'int') else ''
                ops_parts.append(f'{vname}{type_suffix}')
            elif kind == 'lit':
                ops_parts.append(str(val))
            elif kind == 'ref':
                rname = symbols.get_name(val) if symbols else f'ref(0x{val:04x})'
                ops_parts.append(f'&{rname}')
            else:
                ops_parts.append(f'{kind}:{val}')
        ops_str = ', '.join(ops_parts)

        if opname == 'LABEL':
            lines.append(f"  .L{op['label']:02x}:")
        elif opname == 'ASSIGN':
            target = symbols.get_name(op['target']) if symbols else f'0x{op["target"]:04x}'
            if ops_str:
                lines.append(f"    {target} = {ops_str}")
            else:
                lines.append(f"    ASSIGN → {target}")
        elif opname == 'RETURN':
            target = symbols.get_name(op['target']) if symbols else f'0x{op["target"]:04x}'
            lines.append(f"    RETURN {target}")
        elif opname == 'SWITCH':
            target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
            lines.append(f"    SWITCH({ops_str}) → {target}")
        elif opname == 'SWITCH_TABLE':
            rtype = op.get('result_type', '')
            default = symbols.get_name(op['default_target']) if symbols else f'tok_{op["default_target"]:04x}'
            lines.append(f"    SWITCH_TABLE:{rtype} default={default}")
        elif opname == 'CASE':
            target = symbols.get_name(op['target']) if symbols else f'tok_{op["target"]:04x}'
            lines.append(f"    CASE {op.get('value', '?')} → {target}")
        elif opname in ('COND_BRANCH', 'GOTO', 'FALLTHROUGH'):
            lines.append(f"    {opname}")
        elif opname in ('CALL_START', 'CALL_EXEC'):
            ret_type = op.get('return_type', '?')
            lines.append(f"    {opname}({ops_str}) → {ret_type}")
        elif opname == 'CAST':
            ret_type = op.get('result_type', '?')
            if ops_str:
                lines.append(f"    CAST({ops_str}) → {ret_type}")
            else:
                lines.append(f"    CAST → {ret_type}")
        elif opname in ('MEMBER_PTR', 'DEREF', 'STORE', 'VCALL_SETUP', 'VCALL_BIND'):
            rtype = op.get('result_type', '')
            extra = f' → {rtype}' if rtype else ''
            lines.append(f"    {opname}({ops_str}){extra}")
        else:
            if ops_str:
                lines.append(f"    {opname}({ops_str})")
            else:
                lines.append(f"    {opname}")
    return lines


def main():
    parser = argparse.ArgumentParser(description='IL + PPC annotated compilation')
    parser.add_argument('source', help='C++ source file')
    parser.add_argument('--function', '-f', help='Filter to specific function')
    parser.add_argument('--output-dir', default='/tmp/claude-1000', help='Working directory')
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Capture IL
    il_base = capture_il(args.source, output_dir=str(output_dir))
    if not il_base:
        print("ERROR: Failed to capture IL", file=sys.stderr)
        sys.exit(1)

    il = ILFile(il_base)

    # Compile with listing
    listing = compile_with_listing(args.source, str(output_dir))
    ppc_functions = parse_functions_from_listing(listing) if listing else {}

    # Display
    for func in il.functions:
        name = func.name
        if args.function and args.function not in name:
            continue

        print(f"{'=' * 70}")
        print(f"  {name}")
        print(f"{'=' * 70}")

        # IL operations
        il_lines = format_il_ops(func, il.symbols)
        print("\n  IL Operations:")
        for line in il_lines:
            print(f"    {line}")

        # PPC assembly
        if name in ppc_functions:
            ppc_insn_pat = re.compile(r'\s+[0-9a-f]+\s')
            insn_count = sum(1 for l in ppc_functions[name] if ppc_insn_pat.match(l))
            print(f"\n  PPC Assembly ({insn_count} instructions):")
            for line in ppc_functions[name]:
                if line.strip():
                    print(f"    {line.rstrip()}")
        else:
            print("\n  PPC Assembly: (not found in listing)")

        print()


if __name__ == '__main__':
    main()
