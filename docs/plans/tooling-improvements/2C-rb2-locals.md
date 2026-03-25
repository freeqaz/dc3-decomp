# 2C: `rb2_locals` — RB2 Local Variable Lookup

> **Implementation**: Skill (`/rb2-locals`) + parser module in `scripts/orchestrator/rb2_locals.py`. NOT an MCP tool — skills are preferred for new functionality.

## Motivation

The RB2 DWARF dump files contain local variable tables with register/stack assignments for every function in the Rock Band 2 Wii build. This data is invaluable during decomp sessions because it reveals:

- Variable names, types, and counts for each function
- Which variables the compiler assigned to callee-saved registers (r13-r31, f14-f31) vs stack slots
- Parameter register assignments (confirms calling convention)
- Static/global references used by each function

On the ClipCollide session, having this data made the initial `Collide()` implementation nearly correct in one pass, saving 30+ minutes of trial-and-error. Currently, accessing this data requires manual glob + file read during sessions. An MCP tool would make it instant.

## Tool Definition

### Name and Parameters

```python
Tool(
    name="rb2_locals",
    description=(
        "Look up local variable tables from RB2 DWARF debug info. "
        "Returns variable names, types, and register/stack locations for "
        "functions in the shared Milo engine. Useful for initial implementations "
        "and understanding variable layout. Note: RB2 uses MetroWerks EABI PPC (Wii), "
        "not MSVC PPC (Xbox 360) — register assignments are suggestive, not exact."
    ),
    inputSchema={
        "type": "object",
        "properties": {
            "function_name": {
                "type": "string",
                "description": (
                    "Function name to search for. Accepts: "
                    "'Class::Method' (exact match), "
                    "'ClassName' (all methods), "
                    "'MethodName' (grep across all classes). "
                    "Examples: 'ClipCollide::Collide', 'ClipCollide', 'Collide'"
                ),
            },
        },
        "required": ["function_name"],
    },
)
```

## Data Source: RB2 DWARF Dump Format

### File Locations

Two equivalent data sources exist:

1. **Single file**: `~/code/milohax/rb3/doc/rb2_dump.cpp` (218,130 lines, 13,879 functions)
2. **Split directory**: `~/code/milohax/rb3/doc/rb2_dump/` (1,241 files mirroring original source tree)

The single file is easier to parse (one grep target). The split directory is useful for browsing by subsystem. Both contain identical data.

**Recommendation**: Parse the single file for simplicity. It loads in <1s and grep is instant.

### Format Grammar

Each compile unit begins with a header block:

```
/*
    Compile unit: C:\rockband2\system\src\char\ClipCollide.cpp
    Producer: MW EABI PPC C-Compiler
    Language: C++
    Code range: 0x8039F34C -> 0x803A3A44
*/
```

Functions follow the pattern:

```
// Range: 0x803A0754 -> 0x803A0BB4
void ClipCollide::Collide(class ClipCollide * const this /* r28 */) {
    // Local variables
    class RndDrawable * w; // r31
    char * names[3]; // r1+0x7C
    class RndTransformable * meshes[3]; // r1+0x70
    class Vector3 points[3]; // r1+0xB0
    int i; // r29
    class CharServoBone * b; // r30
    float delta; // f31
    float f; // f30
    int i; // r29
    class Vector3 p; // r1+0x60
    class Segment s; // r1+0x90
    float dist; // r1+0x10
    class Plane plane; // r1+0x50
    class RndDrawable * d; // r26
    class Vector3 pos; // r1+0x40
    unsigned char punt; // r27
    class RndMesh * mesh; // r0

    // References
    // -> struct [anonymous] __RTTI__7RndMesh;
    // -> struct [anonymous] __RTTI__9ObjectDir;
    // -> struct [anonymous] __RTTI__11RndDrawable;
}
```

Functions with no body may appear inline:

```
void ClipCollide::ClearReport(class ClipCollide * const this /* r31 */) {}
```

### Variable Location Types

Each local variable has a location comment `// <location>` with one of these forms:

| Format | Meaning | Example |
|--------|---------|---------|
| `rN` | GPR register (N=0-31) | `int i; // r29` |
| `fN` | FPR register (N=0-31) | `float delta; // f31` |
| `r1+0xNN` | Stack frame offset | `class Vector3 p; // r1+0x60` |

Register ranges of interest:
- **r0**: return value or scratch (volatile)
- **r3-r10**: parameter passing / volatile
- **r13-r31**: callee-saved (preserved across calls)
- **f0-f13**: volatile FPR
- **f14-f31**: callee-saved FPR
- **r1+0xNN**: stack-allocated local (offset from stack pointer)

### Parameter Annotations

Function parameters have register annotations in the signature itself:

```
void ClipCollide::Collide(class ClipCollide * const this /* r28 */) {
```

```
void ClipCollide::PickReport(
    class ClipCollide * const this /* r30 */,
    const char * r /* r27 */
) {
```

Note: These are the *callee-saved register* that holds the parameter after the prologue, not necessarily the ABI parameter register. MW EABI PPC passes the first 8 GPR args in r3-r10 and first 8 FPR args in f1-f8. The dump shows where the compiler *saved* the parameter, not where it arrived.

### References Section

After local variables, functions may have a `// References` block listing global/static symbols accessed:

```
    // References
    // -> struct [anonymous] __RTTI__7RndMesh;
    // -> struct [anonymous] __RTTI__9ObjectDir;
    // -> class Debug TheDebug;
    // -> const char * kAssertStr;
    // -> static class Symbol _s;
```

### Duplicate Variable Names

The DWARF dump preserves scoped variable names, so the same name can appear multiple times (different scopes):

```
    int i; // r29       ← outer loop
    ...
    int i; // r29       ← inner loop (same register, different scope)
```

```
    class DataNode r; // r1+0x70    ← one branch
    class DataNode r; // r1+0x68    ← another branch
    class DataNode r; // r1+0x60    ← yet another
```

## Output Format

The tool should return a structured text block with three sections:

### 1. Function Header

```
## ClipCollide::Collide
Source: C:\rockband2\system\src\char\ClipCollide.cpp
Range: 0x803A0754 -> 0x803A0BB4 (1120 bytes)
```

### 2. Parameters

```
### Parameters
  this          ClipCollide*         r28 (callee-saved → r3 on entry)
```

For functions with multiple parameters:

```
### Parameters
  this          ClipCollide*         r30 (callee-saved → r3 on entry)
  r             const char*          r27 (callee-saved → r4 on entry)
```

### 3. Local Variables

Sorted by location type (registers first, then stack), with a clear table:

```
### Local Variables (GPR)
  w             RndDrawable*         r31
  i             int                  r29
  b             CharServoBone*       r30
  d             RndDrawable*         r26
  punt          unsigned char        r27
  mesh          RndMesh*             r0 (volatile)

### Local Variables (FPR)
  delta         float                f31
  f             float                f30

### Local Variables (Stack)
  names[3]      char*                r1+0x7C
  meshes[3]     RndTransformable*    r1+0x70
  points[3]     Vector3              r1+0xB0
  p             Vector3              r1+0x60
  s             Segment              r1+0x90
  dist          float                r1+0x10
  plane         Plane                r1+0x50
  pos           Vector3              r1+0x40

### References
  __RTTI__7RndMesh
  __RTTI__9ObjectDir
  __RTTI__11RndDrawable
```

### 4. Calling Convention Warning

Always append:

```
---
⚠ RB2 = MetroWerks EABI PPC (Wii). DC3 = MSVC PPC (Xbox 360).
Register assignments differ — use variable NAMES and TYPES as ground truth,
not specific register numbers. MW callee-saved GPR allocation often starts
from r31 descending (similar to MSVC), but parameter passing, stack frame
layout, and volatile register usage will differ.
```

### Multiple Matches

When `function_name` matches multiple functions (e.g., searching by class name or overloaded methods), return all matches with separators. Cap at 20 functions and note truncation.

### No Match

```
No RB2 DWARF data found for: HamDirector::Poll
(HamDirector is DC3-only — not present in RB2)
```

## MW EABI vs MSVC PPC Calling Convention Differences

This section documents the key differences that users must be aware of when interpreting RB2 register assignments for DC3 work.

### What Transfers Directly (High Value)

| Data | Transferability | Notes |
|------|----------------|-------|
| Variable **names** | Direct | Shared codebase, identical naming |
| Variable **types** | Direct | Same classes, same layout |
| Variable **count** | Direct | Same function, same logic |
| Stack vs register decision | Suggestive | Large structs always on stack; small values in registers — same across compilers |
| Parameter order | Direct | Same function signature |
| References (globals/RTTIs) | Direct | Reveals which globals the function touches |

### What Does NOT Transfer (Pitfalls)

| Data | Transferability | Why |
|------|----------------|-----|
| Specific GPR number (e.g., r29) | **NOT direct** | Different allocators assign different registers. MW uses r31-descending for callee-saved; MSVC uses linear scan with different priority tables. |
| Specific FPR number (e.g., f31) | **NOT direct** | Same reason — allocator differences. |
| Stack frame offsets (r1+0x60) | **NOT direct** | Different frame layouts, different alignment, different spill areas. |
| Volatile vs callee-saved choice | Suggestive only | MW and MSVC have different spill cost heuristics. A variable in r0 (volatile) in MW might be in r31 (callee-saved) in MSVC or vice versa. |
| Parameter register at function entry | Different | MW EABI: r3-r10 for GPR, f1-f8 for FPR. MSVC Xbox 360: same r3-r10/f1-f13 but different rules for struct passing and alignment. |

### Practical Guidance

The tool output should frame register data as **hints about variable importance**:

- **Callee-saved register (r13-r31, f14-f31)**: Variable is live across function calls. Expect it in a callee-saved register in DC3 too, but not necessarily the *same* one.
- **Volatile register (r0-r12, f0-f13)**: Variable is short-lived or used once. May be in a different volatile register or even optimized away in DC3.
- **Stack (r1+0xNN)**: Variable is a struct, array, or spilled. Almost certainly on stack in DC3 too, but at a different offset.

## Implementation Plan

### Files to Modify

1. **`scripts/orchestrator/mcp_server.py`** — Add tool registration and handler
2. **`scripts/orchestrator/rb2_locals.py`** (new) — Parser module

### New Module: `scripts/orchestrator/rb2_locals.py`

```python
"""RB2 DWARF local variable lookup for decomp sessions.

Parses the rb2_dump.cpp file to extract function signatures, local variables,
and reference tables. Provides fast lookup by Class::Method name.
"""

import re
from pathlib import Path
from dataclasses import dataclass, field

DEFAULT_RB2_DUMP = Path.home() / "code/milohax/rb3/doc/rb2_dump.cpp"


@dataclass
class LocalVar:
    """A local variable from DWARF debug info."""
    name: str           # Variable name
    type: str           # C++ type string
    location: str       # Register or stack location (e.g., "r31", "f30", "r1+0x60")

    @property
    def is_gpr(self) -> bool:
        """Is this in a GPR (not stack, not FPR)?"""
        return bool(re.match(r'^r\d+$', self.location))

    @property
    def is_fpr(self) -> bool:
        """Is this in a floating-point register?"""
        return bool(re.match(r'^f\d+$', self.location))

    @property
    def is_stack(self) -> bool:
        """Is this on the stack?"""
        return self.location.startswith('r1+')

    @property
    def is_callee_saved(self) -> bool:
        """Is this in a callee-saved register?"""
        m = re.match(r'^r(\d+)$', self.location)
        if m:
            return int(m.group(1)) >= 13
        m = re.match(r'^f(\d+)$', self.location)
        if m:
            return int(m.group(1)) >= 14
        return False

    @property
    def is_volatile(self) -> bool:
        """Is this in a volatile register?"""
        return (self.is_gpr or self.is_fpr) and not self.is_callee_saved


@dataclass
class Param:
    """A function parameter with register annotation."""
    name: str
    type: str
    register: str  # e.g., "r28", "f30"


@dataclass
class RB2Function:
    """A function entry from the RB2 DWARF dump."""
    class_name: str | None    # None for free functions
    method_name: str          # Bare method name
    full_name: str            # "Class::Method" or just "Function"
    return_type: str
    params: list[Param] = field(default_factory=list)
    locals: list[LocalVar] = field(default_factory=list)
    references: list[str] = field(default_factory=list)
    source_file: str = ""     # Compile unit path
    addr_start: int = 0
    addr_end: int = 0

    @property
    def code_size(self) -> int:
        return self.addr_end - self.addr_start

    def format_output(self) -> str:
        """Format this function's data for MCP output."""
        # ... (see Output Format section above)


class RB2LocalsDB:
    """Indexed database of RB2 function local variable tables."""

    def __init__(self, dump_path: Path = DEFAULT_RB2_DUMP):
        self.dump_path = dump_path
        self._functions: dict[str, list[RB2Function]] = {}  # full_name -> [funcs]
        self._class_index: dict[str, list[str]] = {}  # class_name -> [full_names]
        self._method_index: dict[str, list[str]] = {}  # method_name -> [full_names]
        self._parsed = False

    def parse(self) -> None:
        """Parse the dump file and build indices."""
        if self._parsed:
            return
        content = self.dump_path.read_text(errors="replace")
        self._parse_content(content)
        self._build_indices()
        self._parsed = True

    def lookup(self, function_name: str) -> list[RB2Function]:
        """Look up functions by name.

        Accepts:
          "Class::Method" - exact match
          "ClassName"     - all methods of that class
          "MethodName"    - all classes with that method
        """
        self.parse()
        # ... matching logic ...

    def _parse_content(self, content: str) -> None:
        """Parse all function entries from the dump."""
        # ... regex-based parser ...

    def _build_indices(self) -> None:
        """Build class_name and method_name reverse indices."""
        # ...
```

### Key Parsing Logic

The parser processes the dump file line-by-line, tracking state:

```
State machine:
  IDLE
    → see "// Range: ..." line → record addr_start, addr_end
    → see function signature line → extract name, params, enter IN_FUNCTION

  IN_FUNCTION
    → see "// Local variables" → enter IN_LOCALS
    → see "// References" → enter IN_REFERENCES
    → see "}" (closing brace at column 0) → emit function, back to IDLE

  IN_LOCALS
    → see "    type name; // location" → add LocalVar
    → see "// References" → enter IN_REFERENCES
    → see "}" → emit function, back to IDLE

  IN_REFERENCES
    → see "    // -> ..." → add reference
    → see "}" → emit function, back to IDLE
```

#### Regex Patterns

```python
# Range line
RANGE_RE = re.compile(r'^// Range: (0x[0-9A-Fa-f]+) -> (0x[0-9A-Fa-f]+)')

# Function signature with optional param annotations
# Captures: return_type, class::method or function, parameter list
FUNC_RE = re.compile(
    r'^(.+?)\s+'                      # return type
    r'((?:\w+::)?\w+|operator\s*.+?)'  # function name (may include operator overloads)
    r'\(([^)]*)\)\s*\{?\s*$'           # parameter list
)

# Parameter with register annotation
PARAM_RE = re.compile(
    r'(.+?)\s+'          # type
    r'(\w+)'             # name
    r'\s*/\*\s*'         # /* delimiter
    r'(r\d+|f\d+)'      # register
    r'\s*\*/'            # */ delimiter
)

# Local variable line
LOCAL_RE = re.compile(
    r'^\s+'              # leading whitespace
    r'(.+?)\s+'          # type
    r'([\w\[\]]+)'       # name (may include array brackets)
    r';\s*//\s*'         # semicolon + comment start
    r'(r\d+(?:\+0x[0-9A-Fa-f]+)?|f\d+)'  # location
)

# Reference line
REF_RE = re.compile(r'^\s+//\s*->\s*(.+)')

# Compile unit header
UNIT_RE = re.compile(r'^\s*Compile unit:\s*(.+)')
```

### MCP Server Integration

In `mcp_server.py`:

1. **Import**: Add `from orchestrator.rb2_locals import RB2LocalsDB` at top
2. **Tool registration**: Add `Tool(...)` entry in `list_tools()` (see schema above)
3. **Dispatch**: Add `elif name == "rb2_locals": return await self._rb2_locals(arguments)` in `call_tool()`
4. **Lazy init**: Create `self._rb2_locals_db: RB2LocalsDB | None = None` on the server, parse on first call

Handler implementation:

```python
async def _rb2_locals(self, args: dict) -> list[TextContent]:
    """Handle rb2_locals tool call."""
    function_name = args.get("function_name", "")
    if not function_name:
        return [TextContent(type="text", text="No function_name provided.")]

    # Lazy-init parser
    if self._rb2_locals_db is None:
        dump_path = Path.home() / "code/milohax/rb3/doc/rb2_dump.cpp"
        if not dump_path.exists():
            return [TextContent(type="text", text=f"RB2 dump not found: {dump_path}")]
        self._rb2_locals_db = RB2LocalsDB(dump_path)

    results = self._rb2_locals_db.lookup(function_name)

    if not results:
        return [TextContent(
            type="text",
            text=f"No RB2 DWARF data found for: {function_name}\n"
                 f"(Function may be DC3-only or named differently in RB2)"
        )]

    # Format output
    output_parts = []
    for func in results[:20]:  # Cap at 20
        output_parts.append(func.format_output())

    if len(results) > 20:
        output_parts.append(f"\n... and {len(results) - 20} more matches")

    output_parts.append(
        "\n---\n"
        "NOTE: RB2 = MetroWerks EABI PPC (Wii). DC3 = MSVC PPC (Xbox 360).\n"
        "Register assignments differ -- use variable NAMES and TYPES as ground truth,\n"
        "not specific register numbers. MW callee-saved GPR allocation often starts\n"
        "from r31 descending (similar to MSVC), but parameter passing, stack frame\n"
        "layout, and volatile register usage will differ."
    )

    return [TextContent(type="text", text="\n\n".join(output_parts))]
```

### Search Strategy

The `lookup()` method should support three query patterns:

```python
def lookup(self, function_name: str) -> list[RB2Function]:
    self.parse()

    # 1. Exact "Class::Method" match
    if "::" in function_name:
        return self._functions.get(function_name, [])

    # 2. Try as class name first (all methods)
    if function_name in self._class_index:
        results = []
        for full_name in self._class_index[function_name]:
            results.extend(self._functions[full_name])
        return results

    # 3. Try as method name (across all classes)
    if function_name in self._method_index:
        results = []
        for full_name in self._method_index[function_name]:
            results.extend(self._functions[full_name])
        return results

    # 4. Fallback: case-insensitive substring search
    results = []
    fn_lower = function_name.lower()
    for full_name, funcs in self._functions.items():
        if fn_lower in full_name.lower():
            results.extend(funcs)
    return results[:50]  # Hard cap for substring matches
```

### Performance

- **Parse time**: The 218K-line file should parse in ~0.5-1s on first call
- **Memory**: ~14K function entries, each with a few locals = ~5-10MB
- **Lazy loading**: Only parse on first tool invocation, then reuse
- **No caching needed**: Dump file is static, single parse per server lifetime is fine

## Verification: ClipCollide::Collide

To verify the implementation works correctly, test against the function that motivated this tool.

### Expected Output

```
## ClipCollide::Collide
Source: C:\rockband2\system\src\char\ClipCollide.cpp
Range: 0x803A0754 -> 0x803A0BB4 (1120 bytes)

### Parameters
  this          ClipCollide*                   r28 (callee-saved)

### Local Variables (GPR)
  w             RndDrawable*                   r31 (callee-saved)
  i             int                            r29 (callee-saved)
  b             CharServoBone*                 r30 (callee-saved)
  i             int                            r29 (callee-saved)
  d             RndDrawable*                   r26 (callee-saved)
  punt          unsigned char                  r27 (callee-saved)
  mesh          RndMesh*                       r0  (volatile)

### Local Variables (FPR)
  delta         float                          f31 (callee-saved)
  f             float                          f30 (callee-saved)

### Local Variables (Stack)
  names[3]      char*                          r1+0x7C
  meshes[3]     RndTransformable*              r1+0x70
  points[3]     Vector3                        r1+0xB0
  p             Vector3                        r1+0x60
  s             Segment                        r1+0x90
  dist          float                          r1+0x10
  plane         Plane                          r1+0x50
  pos           Vector3                        r1+0x40

### References
  __RTTI__7RndMesh
  __RTTI__9ObjectDir
  __RTTI__11RndDrawable
```

### Validation Checklist

1. Lookup `"ClipCollide::Collide"` returns exactly 1 result
2. `this` parameter shows r28
3. 7 GPR locals, 2 FPR locals, 8 stack locals
4. Duplicate `i` variable name (two scopes) both listed
5. 3 references listed
6. Calling convention warning appended
7. Lookup `"ClipCollide"` returns all 25 functions in that TU
8. Lookup `"Collide"` returns ClipCollide::Collide plus any other classes with a Collide method

## Edge Cases

### Functions Not in RB2

DC3-specific classes (HamDirector, HamDriver, HamAudio, etc.) do not exist in RB2. The tool should return a clear "not found" message suggesting the function may be DC3-only:

```
No RB2 DWARF data found for: HamDirector::Poll
(Function may be DC3-only or named differently in RB2)
```

### Overloaded Functions

Multiple functions with the same `Class::Method` name but different parameter lists appear in the dump. Example from CharDriver:

```
CharClipDriver* CharDriver::Play(... CharClip* c /* r30 */, int playFlags /* r31 */, ...)
CharClipDriver* CharDriver::Play(... const DataNode& n /* r30 */, int playFlags /* r31 */, ...)
```

The parser should store *all* overloads and return them all. Each is distinguishable by its parameter list and address range. No disambiguation parameter is needed — the user selects the right overload from the output.

### Template Instantiations

Template instantiations do not appear with template syntax in the dump. Instead, they appear as concrete types:

```
class ObjPtr<CharClip, ObjectDir> { ... };
```

And template-instantiated member functions appear in the owning TU as regular function entries. The class/struct type definitions that appear inline in the dump files (like `ObjDirItr`, `_List_iterator`, etc.) are concrete instantiations, not template definitions.

Searching for `"ObjPtr"` will match class definitions but NOT function entries. Function entries always use the concrete mangled name. This is acceptable — users will search by the function they are decomping, not by template name.

### Free Functions (No Class)

Some functions have no class scope:

```
static void DebugModal(unsigned char & fail /* r29 */, char * msg /* r30 */, unsigned char wait /* r31 */) {
```

```
int main(int argc /* r0 */) {
```

These should be indexed with `class_name = None` and searchable by bare function name.

### Destructors and Operators

The dump contains destructors and special methods:

```
void * ClipCollide::~ClipCollide(class ClipCollide * const this /* r30 */) {
```

The parser must handle `~ClassName` as a valid method name. Searching for `"ClipCollide::~ClipCollide"` should work.

Operator overloads also appear:

```
void Distribution::__ls(class Distribution * const this /* r31 */, float t /* f31 */) {
```

MW EABI mangles `operator<<` as `__ls`. The parser should preserve the MW name as-is.

### Functions Without Local Variables

Many functions have no `// Local variables` section — they only use parameters and possibly references:

```
// Range: 0x803A0634 -> 0x803A068C
void ClipCollide::Demonstrate(class ClipCollide * const this /* r31 */) {}
```

These should still be returned with an empty locals list. The parameter data alone is useful.

### Inline Functions in Headers

Some dump files correspond to `.h` files (functions instantiated from header inline definitions):

```
/*
    Compile unit: C:\rockband2\system\src\char\ClipCollide.h
    Producer: MW EABI PPC C-Compiler
    Language: C++
    Code range: 0x803A3A44 -> 0x803A3D04
*/
```

These should be parsed identically. The source file path in the output reveals whether the function came from a `.cpp` or `.h` file.

### Static Locals Between Functions

The dump interleaves `static` variable declarations between function bodies:

```
static class Symbol front; // size: 0x4, address: 0x80A514C8
static class Symbol back; // size: 0x4, address: 0x80A514D0
// Range: 0x8039F5D8 -> 0x8039F904
void ClipCollide::SyncWaypoint(...) {
```

The parser should skip these when scanning for function boundaries. They are NOT local variables — they are file-scope statics that the DWARF info associates with the next function's scope.

### Mangled Symbol Input

The user may pass an MSVC-mangled symbol like `?Collide@ClipCollide@@QAEXXZ`. The tool should extract `"ClipCollide::Collide"` from the mangling before searching. Apply the same demangling heuristic used by `lookup_rb3`:

```python
if "@" in function_name and "?" in function_name:
    # MSVC mangled: ?Method@Class@@...
    parts = function_name.lstrip("?").split("@")
    if len(parts) >= 2:
        method = parts[0]
        class_name = parts[1]
        function_name = f"{class_name}::{method}"
```

## Relationship to Existing Tools

### `rb2_dwarf.py` (existing)

Parses class/struct *definitions* (member offsets, sizes, inheritance). Focuses on **type layout**, not function bodies. Used by `lookup_struct_offset`.

### `rb2_locals.py` (new)

Parses function *signatures and bodies* (parameters, local variables, references). Focuses on **function implementation details**. Complementary to rb2_dwarf.py.

### `lookup_rb3` (existing)

Grep-based search of RB3 *source code*. Returns actual C++ implementation. Higher fidelity but noisier — returns raw source lines.

### Synergy

A typical decomp session would use all three:
1. `rb2_locals("ClipCollide::Collide")` — get variable names, types, register hints
2. `lookup_rb3("ClipCollide::Collide")` — get actual RB3 source implementation
3. `lookup_struct_offset("ClipCollide", "0x48")` — resolve member offsets during diff analysis

## Testing Plan

1. **Unit tests** for the parser module:
   - Parse a small inline test string with known functions
   - Verify parameter extraction with register annotations
   - Verify local variable extraction (GPR, FPR, stack)
   - Verify reference extraction
   - Verify duplicate variable names preserved
   - Verify free functions (no class scope)
   - Verify destructors and operators

2. **Integration test** against real dump:
   - `lookup("ClipCollide::Collide")` returns expected data (see Verification section)
   - `lookup("ClipCollide")` returns 25 functions
   - `lookup("Collide")` includes ClipCollide::Collide
   - `lookup("HamDirector")` returns empty
   - `lookup("main")` returns the free function

3. **MCP end-to-end**: Invoke via Claude Code MCP client, verify output formatting
