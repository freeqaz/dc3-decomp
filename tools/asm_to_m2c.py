#!/usr/bin/env python3
"""
Convert DC3 dtk assembly to m2c-compatible format.

Usage:
    python3 tools/asm_to_m2c.py <input.s> [-f function_name] [-o output.s]

Examples:
    # Convert entire file
    python3 tools/asm_to_m2c.py build/373307D9/asm/system/char/CharClip.s

    # Convert specific function
    python3 tools/asm_to_m2c.py build/373307D9/asm/system/char/CharClip.s -f AllocSize

    # Pipe to m2c
    python3 tools/asm_to_m2c.py build/373307D9/asm/system/char/CharClip.s -f AllocSize | \
        python3 ~/code/milohax/m2c/m2c.py -t ppc -
"""

import argparse
import re
import sys
from pathlib import Path


def demangle_msvc(name: str) -> str:
    """
    Simple MSVC demangling for readability.
    Full demangling would require a proper demangler like undname or demumble.
    """
    # Handle special names
    if name.startswith('__'):
        return name

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


def extract_function(content: str, func_pattern: str) -> str:
    """Extract a specific function from the assembly content."""
    lines = content.split('\n')
    result = []
    in_target = False
    found = False

    for line in lines:
        # Check for function start
        fn_match = re.match(r'\.fn\s+"([^"]+)"', line)
        if fn_match:
            mangled = fn_match.group(1)
            demangled = demangle_msvc(mangled)
            # Check if this matches our pattern
            if func_pattern.lower() in mangled.lower() or func_pattern.lower() in demangled.lower():
                in_target = True
                found = True
                result.append(line)
                continue

        if in_target:
            result.append(line)
            if line.startswith('.endfn'):
                break

    if not found:
        print(f"Warning: Function matching '{func_pattern}' not found", file=sys.stderr)
        return ""

    return '\n'.join(result)


def fixup_vmx128_instruction(instruction: str) -> str:
    """
    Fix VMX128 instructions that dtk disassembles with different argument counts.

    - vsel128: dtk outputs 3 args (VD, VA, VB), but m2c expects 4 (VD, VA, VB, VS)
      When VS == VD, dtk omits VS. We add it back.
    """
    # vsel128 VD, VA, VB -> vsel128 VD, VA, VB, VD
    match = re.match(r'^(vsel128)\s+(v\d+),\s*(v\d+),\s*(v\d+)$', instruction, re.IGNORECASE)
    if match:
        mnemonic, vd, va, vb = match.groups()
        return f'{mnemonic} {vd}, {va}, {vb}, {vd}'

    return instruction


def convert_asm(content: str) -> str:
    """Convert dtk assembly format to m2c format."""
    lines = content.split('\n')
    output = []
    in_function = False
    current_func = None

    for line in lines:
        # Function start: .fn "mangled_name", global
        fn_match = re.match(r'\.fn\s+"([^"]+)"', line)
        if fn_match:
            mangled = fn_match.group(1)
            demangled = demangle_msvc(mangled)
            output.append(f'.global {demangled}')
            output.append(f'{demangled}:')
            in_function = True
            current_func = demangled
            continue

        # Function end
        if line.startswith('.endfn'):
            in_function = False
            current_func = None
            output.append('')  # Blank line between functions
            continue

        # Local labels (preserve them with leading dot to match branch targets)
        # Source: .L_82348710:  -> Output: .L_82348710:
        label_match = re.match(r'^(\.L_[A-Fa-f0-9]+):?$', line.strip())
        if label_match:
            output.append(f'{label_match.group(1)}:')
            continue

        # Also handle labels without leading dot (add it for consistency)
        label_match2 = re.match(r'^(L_[A-Fa-f0-9]+):$', line.strip())
        if label_match2:
            output.append(f'.{label_match2.group(1)}:')
            continue

        # Instruction lines: /* addr offset hex */ instruction
        instr_match = re.match(r'/\*[^*]+\*/\s+(.+)$', line)
        if instr_match and in_function:
            instruction = instr_match.group(1).strip()
            # Keep symbol references with quotes intact for m2c
            # e.g., lis r10, "__real@3f800000"@ha stays as-is
            # Fixup VMX128 instructions that have different argument counts
            instruction = fixup_vmx128_instruction(instruction)
            output.append(f'\t{instruction}')
            continue

        # Skip directives we don't need
        if line.strip().startswith(('.section', '.balign', '.obj', '.endobj',
                                     '.4byte', '.rel', '.include', '.file', '#')):
            continue

    return '\n'.join(output)


def list_functions(content: str) -> list:
    """List all functions in the assembly file."""
    functions = []
    for match in re.finditer(r'\.fn\s+"([^"]+)"', content):
        mangled = match.group(1)
        demangled = demangle_msvc(mangled)
        functions.append((mangled, demangled))
    return functions


def main():
    parser = argparse.ArgumentParser(
        description="Convert DC3 dtk assembly to m2c-compatible format"
    )
    parser.add_argument(
        "input",
        help="Input assembly file",
    )
    parser.add_argument(
        "-f", "--function",
        help="Extract specific function (partial match)",
    )
    parser.add_argument(
        "-o", "--output",
        help="Output file (default: stdout)",
    )
    parser.add_argument(
        "-l", "--list",
        action="store_true",
        help="List all functions in the file",
    )
    args = parser.parse_args()

    # Read input
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: {args.input} not found", file=sys.stderr)
        sys.exit(1)

    with open(input_path) as f:
        content = f.read()

    # List mode
    if args.list:
        functions = list_functions(content)
        print(f"Functions in {args.input}:")
        for mangled, demangled in functions:
            print(f"  {demangled}")
            print(f"    -> {mangled}")
        print(f"\nTotal: {len(functions)} functions")
        return

    # Extract specific function if requested
    if args.function:
        content = extract_function(content, args.function)
        if not content:
            sys.exit(1)

    # Convert
    output = convert_asm(content)

    # Write output
    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == '__main__':
    main()
