"""RB2 DWARF local variable lookup for decomp sessions.

Parses the rb2_dump.cpp file to extract function signatures, local variables,
and reference tables. Provides fast lookup by Class::Method name.

Usage:
    python3 scripts/orchestrator/rb2_locals.py "ClipCollide::Collide"
    python3 scripts/orchestrator/rb2_locals.py "ClipCollide"
    python3 scripts/orchestrator/rb2_locals.py "Collide"
"""

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_RB2_DUMP = Path.home() / "code/milohax/rb3/doc/rb2_dump.cpp"

# --- Regex patterns ---

RANGE_RE = re.compile(r'^// Range: (0x[0-9A-Fa-f]+) -> (0x[0-9A-Fa-f]+)')

# Function signature: return_type name(params) { or }
# Handles operators, destructors, class::method, free functions
FUNC_RE = re.compile(
    r'^(.+?)\s+'                             # return type
    r'((?:\w+::)?(?:~?\w+|operator\s*.+?))'  # name (class::method, ~dtor, operator<<)
    r'\(([^)]*)\)\s*\{?\s*$'                 # parameter list
)

# Parameter with register annotation: type name /* rN */
PARAM_RE = re.compile(
    r'(.+?)\s+'          # type
    r'(\w+)'             # name
    r'\s*/\*\s*'         # /* delimiter
    r'(r\d+|f\d+)'      # register
    r'\s*\*/'            # */ delimiter
)

# Local variable: type name; // location
LOCAL_RE = re.compile(
    r'^\s+'                                          # leading whitespace
    r'(.+?)\s+'                                      # type
    r'([\w\[\]]+)'                                   # name (may include [N])
    r';\s*//\s*'                                     # ; //
    r'(r\d+(?:\+0x[0-9A-Fa-f]+)?|f\d+)'             # location
)

# Reference line: // -> ...
REF_RE = re.compile(r'^\s+//\s*->\s*(.+)')

# Compile unit header
UNIT_RE = re.compile(r'^\s*Compile unit:\s*(.+)')

# Static variable between functions (skip these)
STATIC_RE = re.compile(r'^static\s+.+;\s*//\s*size:')


@dataclass
class LocalVar:
    name: str
    type: str
    location: str

    @property
    def is_gpr(self) -> bool:
        return bool(re.match(r'^r\d+$', self.location))

    @property
    def is_fpr(self) -> bool:
        return bool(re.match(r'^f\d+$', self.location))

    @property
    def is_stack(self) -> bool:
        return '+' in self.location

    @property
    def is_callee_saved(self) -> bool:
        m = re.match(r'^r(\d+)$', self.location)
        if m:
            return int(m.group(1)) >= 13
        m = re.match(r'^f(\d+)$', self.location)
        if m:
            return int(m.group(1)) >= 14
        return False

    @property
    def reg_tag(self) -> str:
        if self.is_stack:
            return ""
        if self.is_callee_saved:
            return " (callee-saved)"
        return " (volatile)"


@dataclass
class Param:
    name: str
    type: str
    register: str


@dataclass
class RB2Function:
    class_name: str | None
    method_name: str
    full_name: str
    return_type: str
    params: list[Param] = field(default_factory=list)
    locals: list[LocalVar] = field(default_factory=list)
    references: list[str] = field(default_factory=list)
    source_file: str = ""
    addr_start: int = 0
    addr_end: int = 0

    @property
    def code_size(self) -> int:
        return self.addr_end - self.addr_start

    def format_output(self) -> str:
        lines = []
        lines.append(f"## {self.full_name}")
        if self.source_file:
            lines.append(f"Source: {self.source_file}")
        if self.addr_start:
            lines.append(
                f"Range: 0x{self.addr_start:08X} -> 0x{self.addr_end:08X} "
                f"({self.code_size} bytes)"
            )
        lines.append("")

        # Parameters
        if self.params:
            lines.append("### Parameters")
            for p in self.params:
                lines.append(f"  {p.name:<16s} {p.type:<28s} {p.register}")
            lines.append("")

        # GPR locals
        gpr_locals = [v for v in self.locals if v.is_gpr]
        if gpr_locals:
            lines.append("### Local Variables (GPR)")
            for v in gpr_locals:
                lines.append(
                    f"  {v.name:<16s} {v.type:<28s} {v.location}{v.reg_tag}"
                )
            lines.append("")

        # FPR locals
        fpr_locals = [v for v in self.locals if v.is_fpr]
        if fpr_locals:
            lines.append("### Local Variables (FPR)")
            for v in fpr_locals:
                lines.append(
                    f"  {v.name:<16s} {v.type:<28s} {v.location}{v.reg_tag}"
                )
            lines.append("")

        # Stack locals
        stack_locals = [v for v in self.locals if v.is_stack]
        if stack_locals:
            lines.append("### Local Variables (Stack)")
            for v in stack_locals:
                lines.append(
                    f"  {v.name:<16s} {v.type:<28s} {v.location}"
                )
            lines.append("")

        # References
        if self.references:
            lines.append("### References")
            for r in self.references:
                lines.append(f"  {r}")
            lines.append("")

        return "\n".join(lines)


class RB2LocalsDB:
    """Indexed database of RB2 function local variable tables."""

    def __init__(self, dump_path: Path = DEFAULT_RB2_DUMP):
        self.dump_path = dump_path
        self._functions: dict[str, list[RB2Function]] = {}
        self._class_index: dict[str, list[str]] = {}
        self._method_index: dict[str, list[str]] = {}
        self._parsed = False

    def parse(self) -> None:
        if self._parsed:
            return
        if not self.dump_path.exists():
            raise FileNotFoundError(f"RB2 dump not found: {self.dump_path}")
        content = self.dump_path.read_text(errors="replace")
        self._parse_content(content)
        self._build_indices()
        self._parsed = True

    def _parse_content(self, content: str) -> None:
        lines = content.split('\n')
        i = 0
        current_unit = ""
        pending_range = None  # (addr_start, addr_end)

        while i < len(lines):
            line = lines[i]

            # Track compile unit
            unit_m = UNIT_RE.match(line)
            if unit_m:
                current_unit = unit_m.group(1).strip()
                i += 1
                continue

            # Skip static variable declarations between functions
            if STATIC_RE.match(line):
                i += 1
                continue

            # Track range lines
            range_m = RANGE_RE.match(line)
            if range_m:
                pending_range = (
                    int(range_m.group(1), 16),
                    int(range_m.group(2), 16),
                )
                i += 1
                continue

            # Try to match function signature
            func_m = FUNC_RE.match(line)
            if func_m and pending_range is not None:
                return_type = func_m.group(1).strip()
                raw_name = func_m.group(2).strip()
                param_str = func_m.group(3).strip()

                # Parse class::method
                if '::' in raw_name:
                    parts = raw_name.split('::', 1)
                    class_name = parts[0]
                    method_name = parts[1]
                    full_name = raw_name
                else:
                    class_name = None
                    method_name = raw_name
                    full_name = raw_name

                # Parse parameters
                params = []
                if param_str:
                    # Split on comma, but be careful with template params
                    # Simple approach: find all PARAM_RE matches
                    for pm in PARAM_RE.finditer(param_str):
                        params.append(Param(
                            name=pm.group(2),
                            type=pm.group(1).strip(),
                            register=pm.group(3),
                        ))

                func = RB2Function(
                    class_name=class_name,
                    method_name=method_name,
                    full_name=full_name,
                    return_type=return_type,
                    params=params,
                    source_file=current_unit,
                    addr_start=pending_range[0],
                    addr_end=pending_range[1],
                )
                pending_range = None

                # Check if it's an empty function on one line: ... { }
                if line.rstrip().endswith('{}') or line.rstrip().endswith('{ }'):
                    self._add_function(func)
                    i += 1
                    continue

                # Parse body
                i += 1
                while i < len(lines):
                    body_line = lines[i]

                    # Check for closing brace
                    if body_line.startswith('}'):
                        i += 1
                        break

                    # Local variable
                    local_m = LOCAL_RE.match(body_line)
                    if local_m:
                        func.locals.append(LocalVar(
                            type=local_m.group(1).strip(),
                            name=local_m.group(2),
                            location=local_m.group(3),
                        ))
                        i += 1
                        continue

                    # Reference
                    ref_m = REF_RE.match(body_line)
                    if ref_m:
                        func.references.append(ref_m.group(1).strip())
                        i += 1
                        continue

                    # Skip comment lines (// Local variables, // References, etc.)
                    i += 1

                self._add_function(func)
                continue

            # No match, consume line
            pending_range = None
            i += 1

    def _add_function(self, func: RB2Function) -> None:
        if func.full_name not in self._functions:
            self._functions[func.full_name] = []
        self._functions[func.full_name].append(func)

    def _build_indices(self) -> None:
        for full_name, funcs in self._functions.items():
            for func in funcs:
                # Class index
                if func.class_name:
                    if func.class_name not in self._class_index:
                        self._class_index[func.class_name] = []
                    if full_name not in self._class_index[func.class_name]:
                        self._class_index[func.class_name].append(full_name)

                # Method index
                if func.method_name not in self._method_index:
                    self._method_index[func.method_name] = []
                if full_name not in self._method_index[func.method_name]:
                    self._method_index[func.method_name].append(full_name)

    def _demangle_input(self, function_name: str) -> str:
        """Handle MSVC mangled input like ?Method@Class@@..."""
        if "@" in function_name and "?" in function_name:
            parts = function_name.lstrip("?").split("@")
            if len(parts) >= 2:
                method = parts[0]
                class_name = parts[1]
                if class_name and method:
                    return f"{class_name}::{method}"
        return function_name

    def lookup(self, function_name: str) -> list[RB2Function]:
        """Look up functions by name.

        Accepts:
          "Class::Method" - exact match
          "ClassName"     - all methods of that class
          "MethodName"    - all classes with that method
          "?Method@Class@@..." - MSVC mangled (auto-demangled)
        """
        self.parse()

        # Handle mangled input
        function_name = self._demangle_input(function_name)

        # 1. Exact "Class::Method" match
        if "::" in function_name:
            return self._functions.get(function_name, [])

        # 2. Try as class name first (all methods)
        if function_name in self._class_index:
            results = []
            for full_name in sorted(self._class_index[function_name]):
                results.extend(self._functions[full_name])
            return results

        # 3. Try as method name (across all classes)
        if function_name in self._method_index:
            results = []
            for full_name in sorted(self._method_index[function_name]):
                results.extend(self._functions[full_name])
            return results

        # 4. Fallback: case-insensitive substring search
        results = []
        fn_lower = function_name.lower()
        for full_name in sorted(self._functions.keys()):
            if fn_lower in full_name.lower():
                results.extend(self._functions[full_name])
        return results[:50]

    @property
    def function_count(self) -> int:
        self.parse()
        return sum(len(v) for v in self._functions.values())

    @property
    def class_count(self) -> int:
        self.parse()
        return len(self._class_index)


# Singleton
_db: RB2LocalsDB | None = None


def get_db(dump_path: Path = DEFAULT_RB2_DUMP) -> RB2LocalsDB:
    global _db
    if _db is None:
        _db = RB2LocalsDB(dump_path)
    return _db


MW_WARNING = (
    "---\n"
    "NOTE: RB2 = MetroWerks EABI PPC (Wii). DC3 = MSVC PPC (Xbox 360).\n"
    "Register assignments differ -- use variable NAMES and TYPES as ground truth,\n"
    "not specific register numbers."
)


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 rb2_locals.py <function_name>")
        print("  Examples: ClipCollide::Collide, ClipCollide, Collide")
        sys.exit(1)

    function_name = sys.argv[1]
    db = get_db()
    results = db.lookup(function_name)

    if not results:
        print(f"No RB2 DWARF data found for: {function_name}")
        print("(Function may be DC3-only or named differently in RB2)")
        sys.exit(0)

    for i, func in enumerate(results[:20]):
        if i > 0:
            print("\n" + "=" * 60 + "\n")
        print(func.format_output())

    if len(results) > 20:
        print(f"\n... and {len(results) - 20} more matches")

    print(MW_WARNING)


if __name__ == "__main__":
    main()
