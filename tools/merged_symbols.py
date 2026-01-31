#!/usr/bin/env python3
"""
Merged Symbol Lookup Tool for DC3 Decompilation.

When objdiff reports a LINKER_MERGED pattern with a symbol like 'merged_82331360',
this tool looks up the original symbol names at that address from the linker map file.

The linker uses Identical COMDAT Folding (ICF) to merge functions with identical
machine code to a single address. This means multiple symbol names can point to
the same code location (e.g., scalar and vector deleting destructors).

Usage:
    ./tools/merged_symbols.py 82331360              # Lookup single address
    ./tools/merged_symbols.py merged_82331360       # Also accepts merged_ prefix
    ./tools/merged_symbols.py --batch report.json   # Lookup all merged symbols from report
    ./tools/merged_symbols.py --stats               # Show statistics on merged symbols
"""

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# Default paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_MAP_FILE = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"
DEFAULT_REPORT_FILE = PROJECT_ROOT / "build" / "373307D9" / "report.json"


class MergedSymbolLookup:
    """Lookup merged symbol addresses from linker map file."""

    def __init__(self, map_file: Path):
        """Initialize with path to linker map file."""
        self.map_file = map_file
        self._address_to_symbols: Dict[str, List[Dict[str, str]]] = {}
        self._loaded = False

    def _ensure_loaded(self):
        """Load the map file if not already loaded."""
        if self._loaded:
            return

        if not self.map_file.exists():
            raise FileNotFoundError(f"Map file not found: {self.map_file}")

        # Parse the map file
        # Format: 0005:00001360       ??_GObjRef@@UAAPAXI@Z      82331360 f i App.obj
        # Columns: segment:offset, symbol_name, address, flags, source
        pattern = re.compile(
            r'^\s*\d{4}:[0-9a-fA-F]+\s+'  # segment:offset
            r'(\S+)\s+'                    # symbol name
            r'([0-9a-fA-F]{8})\s+'         # address (8 hex digits)
            r'(.*?)$'                       # rest (flags, source)
        )

        with open(self.map_file, 'r') as f:
            for line in f:
                match = pattern.match(line)
                if match:
                    symbol = match.group(1)
                    address = match.group(2).upper()
                    rest = match.group(3).strip()

                    # Parse flags and source
                    parts = rest.split()
                    flags = ""
                    source = ""

                    # "f i" means COMDAT-folded
                    if len(parts) >= 2 and parts[0] == 'f' and parts[1] == 'i':
                        flags = "f i"
                        if len(parts) > 2:
                            source = parts[2]
                    elif len(parts) >= 1:
                        if parts[0] == 'f':
                            flags = "f"
                            if len(parts) > 1:
                                source = parts[1]
                        else:
                            source = parts[0]

                    if address not in self._address_to_symbols:
                        self._address_to_symbols[address] = []

                    self._address_to_symbols[address].append({
                        'symbol': symbol,
                        'flags': flags,
                        'source': source,
                        'is_comdat_folded': 'f i' in rest,
                    })

        self._loaded = True

    def lookup(self, address: str) -> Optional[List[Dict[str, str]]]:
        """
        Look up symbols at a given address.

        Args:
            address: Address in hex (e.g., '82331360' or 'merged_82331360')

        Returns:
            List of symbol info dicts, or None if not found
        """
        self._ensure_loaded()

        # Normalize address - strip 'merged_' prefix and uppercase
        if address.lower().startswith('merged_'):
            address = address[7:]
        address = address.upper().lstrip('0x')

        return self._address_to_symbols.get(address)

    def get_merged_addresses(self) -> Dict[str, List[Dict[str, str]]]:
        """
        Get all addresses that have multiple symbols (merged).

        Returns:
            Dict mapping address to list of symbol info
        """
        self._ensure_loaded()
        return {
            addr: symbols
            for addr, symbols in self._address_to_symbols.items()
            if len(symbols) > 1
        }

    def get_comdat_folded_count(self) -> int:
        """Get count of COMDAT-folded symbols."""
        self._ensure_loaded()
        count = 0
        for symbols in self._address_to_symbols.values():
            for sym in symbols:
                if sym['is_comdat_folded']:
                    count += 1
        return count

    def demangle(self, symbol: str) -> str:
        """
        Attempt to demangle a MSVC mangled symbol name.

        This is a basic demangler that handles common patterns.
        For full demangling, consider using an external tool.
        """
        # Basic patterns
        if symbol.startswith('??_G'):
            # Scalar deleting destructor
            class_name = symbol[4:].split('@')[0]
            return f"{class_name}::`scalar deleting destructor'(unsigned int)"
        elif symbol.startswith('??_E'):
            # Vector deleting destructor
            class_name = symbol[4:].split('@')[0]
            return f"{class_name}::`vector deleting destructor'(unsigned int)"
        elif symbol.startswith('??0'):
            # Constructor
            class_name = symbol[3:].split('@')[0]
            return f"{class_name}::{class_name}(...)"
        elif symbol.startswith('??1'):
            # Destructor
            class_name = symbol[3:].split('@')[0]
            return f"{class_name}::~{class_name}()"
        elif symbol.startswith('?'):
            # Regular member function
            parts = symbol[1:].split('@')
            if len(parts) >= 2:
                method = parts[0]
                class_name = parts[1]
                return f"{class_name}::{method}(...)"

        # Can't demangle, return original
        return symbol


def format_lookup_result(
    address: str,
    symbols: List[Dict[str, str]],
    lookup: MergedSymbolLookup,
    verbose: bool = False
) -> str:
    """Format lookup result for display."""
    lines = []

    if len(symbols) == 1:
        lines.append(f"Address 0x{address}: 1 symbol (not merged)")
    else:
        lines.append(f"Address 0x{address}: {len(symbols)} symbols merged by ICF")

    lines.append("")

    for i, sym in enumerate(symbols, 1):
        mangled = sym['symbol']
        demangled = lookup.demangle(mangled)
        source = sym.get('source', '')

        if verbose:
            lines.append(f"  {i}. {demangled}")
            lines.append(f"     Mangled: {mangled}")
            if source:
                lines.append(f"     Source:  {source}")
            lines.append("")
        else:
            src_suffix = f" ({source})" if source else ""
            lines.append(f"  {i}. {demangled}{src_suffix}")

    return "\n".join(lines)


def extract_merged_addresses_from_report(report_path: Path) -> List[str]:
    """Extract all merged_* addresses from a report.json file."""
    if not report_path.exists():
        raise FileNotFoundError(f"Report file not found: {report_path}")

    addresses = set()
    pattern = re.compile(r'merged_([0-9a-fA-F]+)')

    with open(report_path, 'r') as f:
        content = f.read()

    for match in pattern.finditer(content):
        addresses.add(match.group(1).upper())

    return sorted(addresses)


def cmd_lookup(args):
    """Handle single address lookup."""
    lookup = MergedSymbolLookup(args.map_file)

    symbols = lookup.lookup(args.address)

    if symbols is None:
        print(f"No symbols found at address: {args.address}", file=sys.stderr)
        sys.exit(1)

    # Normalize address for display
    address = args.address.upper()
    if address.lower().startswith('merged_'):
        address = address[7:]
    address = address.lstrip('0x')

    if args.json:
        result = {
            'address': address,
            'symbol_count': len(symbols),
            'is_merged': len(symbols) > 1,
            'symbols': [
                {
                    'mangled': sym['symbol'],
                    'demangled': lookup.demangle(sym['symbol']),
                    'source': sym.get('source', ''),
                    'is_comdat_folded': sym.get('is_comdat_folded', False),
                }
                for sym in symbols
            ]
        }
        print(json.dumps(result, indent=2))
    else:
        print(format_lookup_result(address, symbols, lookup, args.verbose))


def cmd_batch(args):
    """Handle batch lookup from report.json."""
    lookup = MergedSymbolLookup(args.map_file)
    addresses = extract_merged_addresses_from_report(args.report)

    if not addresses:
        print("No merged_* symbols found in report.", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(addresses)} merged addresses in report\n")

    results = []
    for addr in addresses:
        symbols = lookup.lookup(addr)
        if symbols:
            results.append({
                'address': addr,
                'symbols': symbols,
            })

    if args.json:
        output = []
        for r in results:
            output.append({
                'address': r['address'],
                'symbol_count': len(r['symbols']),
                'symbols': [
                    {
                        'mangled': sym['symbol'],
                        'demangled': lookup.demangle(sym['symbol']),
                        'source': sym.get('source', ''),
                    }
                    for sym in r['symbols']
                ]
            })
        print(json.dumps(output, indent=2))
    else:
        for r in results:
            print(format_lookup_result(r['address'], r['symbols'], lookup, args.verbose))
            print()


def cmd_stats(args):
    """Show statistics on merged symbols."""
    lookup = MergedSymbolLookup(args.map_file)

    # Load and analyze
    merged = lookup.get_merged_addresses()
    comdat_count = lookup.get_comdat_folded_count()

    # Count by merge degree
    merge_counts = defaultdict(int)
    for addr, symbols in merged.items():
        merge_counts[len(symbols)] += 1

    # Categorize by symbol type
    destructor_merges = 0
    getter_merges = 0
    template_merges = 0

    for addr, symbols in merged.items():
        sym_names = [s['symbol'] for s in symbols]

        if any('??_G' in s or '??_E' in s for s in sym_names):
            destructor_merges += 1
        if any('GetObj' in s for s in sym_names):
            getter_merges += 1
        if any('$' in s for s in sym_names):  # Template instantiation
            template_merges += 1

    print("## Merged Symbol Statistics\n")
    print(f"Total COMDAT-folded symbols: {comdat_count:,}")
    print(f"Unique merged addresses: {len(merged):,}")
    print()
    print("### Merge Degree Distribution")
    print("| Symbols at Address | Count |")
    print("|-------------------|-------|")
    for degree in sorted(merge_counts.keys()):
        print(f"| {degree} | {merge_counts[degree]:,} |")
    print()
    print("### Common Merge Types")
    print(f"- Destructor merges (??_G/??_E): {destructor_merges:,}")
    print(f"- GetObj template merges: {getter_merges:,}")
    print(f"- Other template merges: {template_merges:,}")

    # Show some examples
    if args.examples:
        print("\n### Examples of Merged Addresses")
        count = 0
        for addr, symbols in sorted(merged.items(), key=lambda x: -len(x[1])):
            if count >= 5:
                break
            print()
            print(format_lookup_result(addr, symbols, lookup, False))
            count += 1


def main():
    parser = argparse.ArgumentParser(
        description="Look up merged symbol addresses from the linker map file.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    parser.add_argument(
        '--map-file', '-m',
        type=Path,
        default=DEFAULT_MAP_FILE,
        help=f"Path to linker map file (default: {DEFAULT_MAP_FILE})"
    )

    # Allow address as positional argument for direct lookup
    parser.add_argument(
        'address',
        nargs='?',
        help='Address to look up (e.g., 82331360 or merged_82331360)'
    )
    parser.add_argument('--json', '-j', action='store_true', help='Output as JSON')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show full details')
    parser.add_argument('--batch', '-b', action='store_true', help='Batch lookup from report.json')
    parser.add_argument('--stats', '-s', action='store_true', help='Show statistics')
    parser.add_argument('--examples', '-e', action='store_true', help='Show example merges (with --stats)')
    parser.add_argument(
        '--report', '-r',
        type=Path,
        default=DEFAULT_REPORT_FILE,
        help=f"Path to report.json for batch mode (default: {DEFAULT_REPORT_FILE})"
    )

    args = parser.parse_args()

    # Determine which command to run
    if args.stats:
        args.command = 'stats'
    elif args.batch:
        args.command = 'batch'
    elif args.address:
        args.command = 'lookup'
    else:
        parser.print_help()
        sys.exit(1)

    try:
        if args.command == 'lookup':
            cmd_lookup(args)
        elif args.command == 'batch':
            cmd_batch(args)
        elif args.command == 'stats':
            cmd_stats(args)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == '__main__':
    main()
