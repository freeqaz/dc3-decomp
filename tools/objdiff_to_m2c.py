#!/usr/bin/env python3
"""
Convert objdiff JSON output to m2c-compatible assembly format.

This script parses the JSON output from objdiff-cli (with --include-instructions)
and converts it to GNU-as style assembly that m2c can process.

Usage:
    # Pipe from objdiff-cli
    ./bin/objdiff-cli diff -p . "CharClip::SetFlags" -f json --include-instructions | \
        python3 tools/objdiff_to_m2c.py

    # From file
    python3 tools/objdiff_to_m2c.py -i function.json -o function.s

    # With custom symbol name
    ./bin/objdiff-cli diff -p . "CharClip::SetFlags" -f json --include-instructions | \
        python3 tools/objdiff_to_m2c.py --symbol CharClip_SetFlags

    # Full pipeline to m2c
    ./bin/objdiff-cli diff -p . "CharMirror::Load" -f json --include-instructions | \
        python3 tools/objdiff_to_m2c.py | \
        python3 ~/code/milohax/m2c/m2c.py -t ppc -

Examples:
    # Extract target binary disassembly and decompile
    ./bin/objdiff-cli diff -p . "Game::Poll" -f json --include-instructions 2>/dev/null | \
        python3 tools/objdiff_to_m2c.py | \
        python3 ~/code/milohax/m2c/m2c.py -t ppc -
"""

import argparse
import json
import re
import sys
from typing import Optional


def symbol_to_label(name: str) -> str:
    """
    Convert a symbol name to a valid assembly label.

    Handles:
    - C++ demangled names like "CharClip::SetFlags"
    - MSVC mangled names like "?SetFlags@CharClip@@QAAXH@Z"
    - Special names like "__savegprlr_29"

    Returns a valid C identifier usable as an assembly label.
    """
    if not name:
        return 'unknown'

    # Handle special names (compiler intrinsics)
    if name.startswith('__'):
        return name

    # Handle C++ demangled names with full signatures
    # e.g. "private: class MoveFrame * __cdecl MoveDir::ClosestMoveFrame(void)"
    # Extract the qualified name (Class::Method) before the parameter list
    if '::' in name:
        # Strip parameter list
        paren_idx = name.find('(')
        if paren_idx != -1:
            name = name[:paren_idx]
        # Take the last space-separated token(s) containing ::
        # This strips return type, access specifier, calling convention
        tokens = name.split()
        qualified = [t for t in tokens if '::' in t]
        if qualified:
            label = qualified[-1].replace('::', '_')
        else:
            label = tokens[-1].replace('::', '_')
        # Sanitize any remaining invalid chars
        label = re.sub(r'[^a-zA-Z0-9_]', '_', label)
        label = re.sub(r'_+', '_', label).strip('_')
        return label or 'unknown'

    # Handle MSVC mangled names like "?SetFlags@CharClip@@QAAXH@Z"
    if name.startswith('?'):
        # Remove leading ? or ??
        clean = re.sub(r'^\?\??', '', name)

        # Extract function and class names before @@
        parts = clean.split('@')
        if len(parts) >= 2:
            func_name = parts[0]
            class_name = parts[1] if parts[1] and not parts[1].startswith('@') else None
            if class_name:
                return f"{class_name}_{func_name}"
            return func_name

        # Fallback: replace @ with _ and clean up
        clean = clean.replace('@', '_')
        clean = re.sub(r'_+$', '', clean)
        return clean or 'unknown'

    # Handle template names with < >
    name = name.replace('<', '_').replace('>', '_').replace(',', '_')
    name = re.sub(r'_+', '_', name)  # Collapse multiple underscores
    name = name.strip('_')

    # Replace any remaining invalid chars
    name = re.sub(r'[^a-zA-Z0-9_]', '_', name)

    return name or 'unknown'


def parse_branch_targets(instructions: list) -> set:
    """
    Identify branch target addresses from branch instructions.
    Returns a set of addresses that are branch targets.
    """
    targets = set()
    branch_opcodes = {
        'b', 'bl', 'ba', 'bla',
        'bc', 'bcl', 'bca', 'bcla',
        'bclr', 'bclrl', 'bcctr', 'bcctrl',
        'beq', 'bne', 'blt', 'bgt', 'ble', 'bge',
        'beqlr', 'bnelr', 'bltlr', 'bgtlr', 'blelr', 'bgelr',
        'bdnz', 'bdz', 'bdnzl', 'bdzl',
    }

    for instr in instructions:
        target = instr.get('target', {})
        opcode = target.get('opcode', '')

        # Check if this is a branch instruction
        if opcode in branch_opcodes or opcode.startswith('b'):
            args = target.get('args', '')
            # Look for hex addresses like 0x17dc or cr6, 0x17dc
            # Branch targets in objdiff are shown as absolute addresses
            match = re.search(r'0x([0-9a-fA-F]+)$', args)
            if match:
                addr = int(match.group(1), 16)
                targets.add(addr)

    return targets


def quote_symbol(sym: str) -> str:
    """
    Quote a symbol name if it contains characters that need escaping.
    MSVC mangled names contain ? and @ which need quoting.
    """
    # Check if quoting is needed
    if '?' in sym or '@' in sym or '$' in sym or '<' in sym or '>' in sym:
        # Don't double-quote
        if sym.startswith('"') and sym.endswith('"'):
            return sym
        return f'"{sym}"'
    return sym


def _is_reloc_symbol(s: str) -> bool:
    """Check if a string looks like a relocation symbol appended by objdiff."""
    # MSVC mangled names, labels, merged symbols
    return (s.startswith('?') or s.startswith('merged_') or
            s.startswith('lbl_') or s.startswith('jumptable_') or
            s.startswith('switch_') or s.startswith('__jtbl') or
            (s.startswith('"') and '@' in s))


def format_instruction(instr: dict) -> str:
    """
    Format a single instruction from objdiff JSON to assembly.

    Handles:
    - Standard instructions
    - Relocations (lis/addi with @ha/@l suffixes)
    - Branch targets
    - Extra relocation info appended by objdiff (strip it for mr, etc.)
    - Quoting MSVC mangled symbols
    """
    target = instr.get('target', {})
    opcode = target.get('opcode', '')
    args = target.get('args', '')

    if not opcode:
        return ''

    # Handle lis with relocation - needs @ha suffix
    # objdiff shows: "lis r11, ?TheDebug@@3VDebug@@A"
    # m2c needs: lis r11, "?TheDebug@@3VDebug@@A"@ha
    if opcode == 'lis' and args:
        # Check if this looks like a symbol reference (not just a number)
        parts = args.split(', ', 1)
        if len(parts) == 2:
            reg, operand = parts
            # If operand is a symbol (starts with ? or letter, not 0x number)
            if operand and not operand.startswith('0x') and not operand.lstrip('-').isdigit():
                # Quote and add @ha suffix for high-adjusted address
                return f"{opcode} {reg}, {quote_symbol(operand)}@ha"

    # Handle addi with relocation - needs @l suffix
    # objdiff shows: "addi r29, r11, ?TheDebug@@3VDebug@@A"
    # m2c needs: addi r29, r11, "?TheDebug@@3VDebug@@A"@l
    # Also handles: "addi r7, r28, 0x4, lbl_82017228" -> "addi r7, r28, 0x4"
    if opcode in ('addi', 'subi') and args:
        parts = args.split(', ')
        if len(parts) >= 3:
            # Check if we have 4 parts with relocation info appended
            if len(parts) == 4:
                # Format: "addi r7, r28, 0x4, lbl_82017228"
                # Strip the relocation info, keep: "addi r7, r28, 0x4"
                return f"{opcode} {parts[0]}, {parts[1]}, {parts[2]}"
            # Check if third part is a symbol
            last = parts[-1]
            if last and not last.startswith('0x') and not last.lstrip('-').isdigit():
                # Reconstruct with @l suffix and quote
                prefix = ', '.join(parts[:-1])
                return f"{opcode} {prefix}, {quote_symbol(last)}@l"

    # Handle mr with extra relocation info
    # objdiff shows: "mr r7, r28, lbl_82017228" or "mr r3, r29, ?TheDebug@@3VDebug@@A"
    # mr only takes 2 register operands
    if opcode == 'mr' and args:
        parts = args.split(', ')
        if len(parts) >= 3:
            # Strip extra relocation info
            return f"{opcode} {parts[0]}, {parts[1]}"

    # Handle bl/b with symbol targets - quote if needed
    if opcode in ('bl', 'b') and args:
        # Check if target is a symbol (not an address or label)
        if not args.startswith('0x') and not args.startswith('.L_') and not args.startswith('__'):
            return f"{opcode} {quote_symbol(args)}"

    # Convert memory operands from objdiff format to GNU-as format
    # objdiff: "lwz r11, 0x4c, r3" -> GNU-as: "lwz r11, 0x4c(r3)"
    # This applies to load/store instructions with offset(base) format.
    # Indexed ops (ending in 'x') use 3 registers: "lbzx rD, rA, rB" - no conversion needed.
    memory_ops = {
        'lwz', 'lbz', 'lhz', 'lha', 'lfs', 'lfd', 'lmw',
        'stw', 'stb', 'sth', 'stfs', 'stfd', 'stmw',
        'lwzu', 'lbzu', 'lhzu', 'lfsu', 'lfdu',
        'stwu', 'stbu', 'sthu', 'stfsu', 'stfdu',
        # PPC64 load/store doubleword
        'ld', 'std', 'ldu', 'stdu',
    }

    if opcode in memory_ops and args:
        parts = args.split(', ')
        if len(parts) == 3:
            reg_dest, offset, reg_base = parts
            # Check if offset is a symbol reference (not numeric)
            # e.g. "lwz r4, ?gNullStr@@3PBDB, r11" -> "lwz r4, "?gNullStr..."@l(r11)"
            if offset and not offset.startswith('0x') and not offset.lstrip('-').isdigit():
                return f"{opcode} {reg_dest}, {quote_symbol(offset)}@l({reg_base})"
            # Format: "lwz r11, 0x4c, r3" -> "lwz r11, 0x4c(r3)"
            return f"{opcode} {reg_dest}, {offset}({reg_base})"
        elif len(parts) == 4:
            # Format with relocation: "lwz r11, 0x3c, r10, ?TheTaskMgr..." -> "lwz r11, 0x3c(r10)"
            reg_dest, offset, reg_base, _reloc = parts
            return f"{opcode} {reg_dest}, {offset}({reg_base})"

    # Indexed memory ops (ending in 'x') use 3 registers: "lbzx rD, rA, rB"
    # objdiff may append relocation info as a 4th part - strip it
    indexed_memory_ops = {
        'lwzx', 'lbzx', 'lhzx', 'lhax', 'lfsx', 'lfdx',
        'stwx', 'stbx', 'sthx', 'stfsx', 'stfdx',
        'lwbrx', 'lhbrx', 'stwbrx', 'sthbrx',
        'lwarx', 'stwcx.',
    }

    if opcode in indexed_memory_ops and args:
        parts = args.split(', ')
        if len(parts) == 4:
            # Strip relocation info: "lbzx r0, r12, r4, ??_C@..." -> "lbzx r0, r12, r4"
            return f"{opcode} {parts[0]}, {parts[1]}, {parts[2]}"

    # General relocation stripping: objdiff appends symbol info to many instructions
    # e.g. "add r12, r12, r0, ?SongInfoAudioTypeToSym..." -> "add r12, r12, r0"
    if args:
        parts = args.split(', ')
        if len(parts) >= 2 and _is_reloc_symbol(parts[-1]):
            cleaned = ', '.join(parts[:-1])
            return f"{opcode} {cleaned}"

    # Standard instruction formatting
    if args:
        return f"{opcode} {args}"
    else:
        return opcode


def convert_objdiff_json(data: dict, symbol_override: Optional[str] = None) -> str:
    """
    Convert objdiff JSON to m2c assembly format.

    Args:
        data: Parsed JSON from objdiff-cli
        symbol_override: Optional symbol name to use instead of extracted one

    Returns:
        m2c-compatible assembly string
    """
    output = []

    # Get symbol name
    symbol = symbol_override or data.get('symbol', 'unknown')
    label = symbol_to_label(symbol)

    # Get instructions
    instructions = data.get('instructions', [])
    if not instructions:
        print(f"Warning: No instructions found for {symbol}", file=sys.stderr)
        return ""

    # Find branch targets to create labels
    branch_targets = parse_branch_targets(instructions)

    # Build address-to-index map for the target side
    # Use the target addresses since we're extracting target binary
    addr_map = {}
    for idx, instr in enumerate(instructions):
        target = instr.get('target', {})
        addr_str = target.get('address', '')
        if addr_str:
            try:
                addr = int(addr_str, 16)
                addr_map[addr] = idx
            except ValueError:
                pass

    # Emit function header
    output.append(f".global {label}")
    output.append(f"{label}:")

    # Emit instructions
    for instr in instructions:
        target = instr.get('target', {})
        addr_str = target.get('address', '')

        # Check if this address is a branch target - emit label
        if addr_str:
            try:
                addr = int(addr_str, 16)
                if addr in branch_targets:
                    output.append(f".L_{addr:08X}:")
            except ValueError:
                pass

        # Format the instruction
        asm = format_instruction(instr)
        if asm:
            # Convert branch target addresses to labels
            # Look for branch to hex address pattern
            match = re.search(r'(0x[0-9a-fA-F]+)$', asm)
            if match:
                target_addr_str = match.group(1)
                try:
                    target_addr = int(target_addr_str, 16)
                    if target_addr in branch_targets:
                        # Replace address with label reference
                        asm = asm[:match.start()] + f".L_{target_addr:08X}"
                except ValueError:
                    pass

            output.append(f"\t{asm}")

    return '\n'.join(output)


def main():
    parser = argparse.ArgumentParser(
        description="Convert objdiff JSON output to m2c-compatible assembly",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        "-i", "--input",
        help="Input JSON file (default: stdin)",
    )
    parser.add_argument(
        "-o", "--output",
        help="Output assembly file (default: stdout)",
    )
    parser.add_argument(
        "--symbol",
        help="Override the symbol name for the function label",
    )
    parser.add_argument(
        "--use-base",
        action="store_true",
        help="Use base (compiled) instructions instead of target (original binary)",
    )
    args = parser.parse_args()

    # Read input
    try:
        if args.input:
            with open(args.input) as f:
                content = f.read()
        else:
            content = sys.stdin.read()
    except Exception as e:
        print(f"Error reading input: {e}", file=sys.stderr)
        sys.exit(1)

    # Parse JSON
    try:
        data = json.loads(content)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        sys.exit(1)

    # If --use-base is specified, swap target/base in instructions
    if args.use_base:
        for instr in data.get('instructions', []):
            instr['target'], instr['base'] = instr.get('base', {}), instr.get('target', {})

    # Convert
    output = convert_objdiff_json(data, args.symbol)

    if not output:
        sys.exit(1)

    # Write output
    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
            f.write('\n')
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == '__main__':
    main()
