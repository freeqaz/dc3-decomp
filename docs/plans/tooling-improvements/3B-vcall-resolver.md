# 3B: Virtual Call Resolver (`/resolve-vcall`)

> **Implementation**: Skill (`/resolve-vcall`) extending the existing `/vtable` skill infrastructure. Uses `scripts/dump_vtable.py` for vtable data and `tools/struct_db.py` for hierarchy. NOT a new MCP tool.

## Problem Statement

When decompiling functions that make virtual calls through sub-object pointers, resolving the target function requires manual inheritance chain tracing. For example, in `ClipCollide::Collide`, the target assembly loads a vtable from `servo+8` and calls slot `[1]`. Determining that this resolves to `CharServoBone::Poll()` required:

1. Tracing CharServoBone's inheritance chain (RndHighlightable, CharPollable, CharBonesMeshes)
2. Computing sub-object offsets under MSVC's virtual inheritance layout rules
3. Identifying which base class's vtable lives at offset +8 in the object
4. Mapping slot index 1 in that vtable to the actual function name

This process took approximately 30 minutes of manual reasoning. A tool would reduce it to seconds.

## Tool Specification

### Name and Parameters

```
resolve_vcall(
    class_name: str,           # Most-derived class (e.g., "CharServoBone")
    sub_object_offset: int,    # Byte offset from this-ptr to vtable load (e.g., 8)
    vtable_slot: int           # Slot index in the vtable (e.g., 1)
)
```

### Output Format

```json
{
    "resolved_function": "CharServoBone::Poll()",
    "declared_in": "RndPollable",
    "overridden_by": "CharServoBone",
    "vtable_symbol": "??_7CharServoBone@@6BRndPollable@@@",
    "base_at_offset": "CharPollable (via RndPollable)",
    "inheritance_path": "CharServoBone -> CharPollable -> RndPollable -> (virtual) Hmx::Object",
    "confidence": "high",
    "slot_offset_hex": "0x04",
    "all_slots": [
        {"slot": 0, "offset": "0x00", "function": "RndPollable::PollEnabled()", "note": "ICF: IRLoadConst::IsLoadConst"},
        {"slot": 1, "offset": "0x04", "function": "CharServoBone::Poll()"},
        {"slot": 2, "offset": "0x08", "function": "CharServoBone::Enter()"},
        {"slot": 3, "offset": "0x0c", "function": "RndPollable::Exit()"},
        {"slot": 4, "offset": "0x10", "function": "RndPollable::ListPollChildren()", "note": "ICF: OnlyReturns"},
        {"slot": 5, "offset": "0x14", "function": "CharServoBone::PollDeps()", "note": "ICF: CharFaceServo::PollDeps"}
    ]
}
```

Confidence levels:
- **high**: Vtable found in .obj, slot resolved unambiguously
- **medium**: Vtable found but slot points to an ICF-merged symbol (multiple possible functions at that code address; position-based resolution used)
- **low**: Sub-object offset matched heuristically (no vbtable data available), or class not found in .obj files
- **none**: Could not resolve -- returns diagnostic information about what was found

## Algorithm

### Phase 1: Identify the base class at the given sub-object offset

The sub-object offset tells us which base class's vtable pointer is being loaded. In MSVC's layout for classes with virtual inheritance:

```
CharServoBone object layout (approximate):
  +0x00: vfptr (for RndHighlightable sub-object — first non-virtual base)
  +0x04: vbptr (virtual base table pointer for RndHighlightable)
  +0x08: vfptr (for CharPollable/RndPollable sub-object — second non-virtual base)
  +0x0c: vbptr (virtual base table pointer for CharPollable)
  +0x10: vfptr (for CharBonesMeshes/CharBones sub-object — third non-virtual base)
  +0x14: vbptr (virtual base table pointer for CharBonesMeshes)
  ...members of non-virtual bases and derived class...
  +0xNN: virtual base sub-object (Hmx::Object) — at end of object
```

The algorithm to map `sub_object_offset` to a base class:

1. **Parse the class hierarchy** from `struct_db.sqlite` using `resolve_inheritance_chain()`.
2. **Enumerate non-virtual bases** in declaration order. Each non-virtual base that introduces new virtual functions gets a vtable pointer (vfptr) in the object layout, typically followed by a virtual base pointer (vbptr) if the base has virtual bases.
3. **Compute accumulated offsets** by walking the base list:
   - Each non-virtual base with virtual functions contributes a vfptr (4 bytes) and potentially a vbptr (4 bytes).
   - Non-virtual bases without their own new virtual functions may be embedded at zero additional cost (their vtable is merged into the derived class's).
4. **Match the offset** to identify which base's vfptr is at that position.

**Fallback strategy**: If the struct_db layout data is insufficient, scan all `??_7<class_name>@@6B<base>@@@` vtable symbols in the .obj file. The mangled name encodes which base class the vtable belongs to. Cross-reference with the `??_R4` (RTTI Complete Object Locator) at the end of each vtable, which contains the sub-object offset as a field.

### Phase 2: Look up the vtable for that base class

Once we know the base class, construct the MSVC mangled vtable symbol name:

```
??_7<most_derived_class>@@6B<base_class>@@@
```

For example: `??_7CharServoBone@@6BRndPollable@@@`

Use the existing `dump_vtable.py` infrastructure to:
1. Find the .obj file containing the class (via `find_obj_file()`)
2. Parse the COFF symbol table to locate the vtable symbol
3. Read relocations for the vtable's section to get the ordered list of function pointers

### Phase 3: Map the slot index to a function

Index into the vtable relocation entries by the `vtable_slot` parameter. Each relocation entry gives us the target symbol name.

For the sub-object vtables (non-primary bases), the slot layout is:
- Slots contain only the virtual functions **introduced or overridden in that base's hierarchy**, NOT the full Object virtuals (those are in the primary vtable at offset 0).
- The RTTI Complete Object Locator (`??_R4`) entry is always the last slot.

Demangle the symbol and handle ICF-merged entries:
- If the symbol is `OnlyReturns` or another known ICF merge target, annotate it but still identify the **positional** function (e.g., slot 4 in RndPollable = `ListPollChildren`, even though it ICF-merged to `OnlyReturns`).
- Use the base class header to determine which virtual function corresponds to each slot position.

### Phase 4: Determine overrides

Once the slot's declared function is known (e.g., `RndPollable::Poll`), check if the most-derived class overrides it:
- If the relocation target matches the most-derived class (e.g., `CharServoBone::Poll`), report the override.
- If it matches an intermediate class, report the intermediate override.
- If it matches the declaring class, it's not overridden.

## Data Sources

### Primary: COFF .obj files

Location: `build/373307D9/obj/`

The .obj files contain:
- **Vtable symbols** (`??_7`): One per base class that has virtual functions. The relocation entries in the vtable's section give the ordered function pointer list.
- **Vbtable symbols** (`??_8`): Virtual base displacement tables. Each entry is a signed 32-bit offset (big-endian on PPC) from the vbptr to the virtual base sub-object.
- **Adjustor thunks** (`$4` in mangled name): Functions that adjust the `this` pointer before forwarding to the real implementation. The adjustment value is encoded in the mangled name (e.g., `$4PPPPPPPM@A@` = -4 adjustment, where `PPPPPPPM` is the twos-complement hex encoding).
- **RTTI descriptors** (`??_R0` through `??_R4`): `??_R1` (Base Class Descriptor) contains the sub-object offset (`mdisp`), vbptr offset (`pdisp`), and vbtable index (`vdisp`). `??_R4` (Complete Object Locator) at the end of each vtable contains the offset of this vtable within the complete object.

### Secondary: struct_db.sqlite

The struct database provides:
- Class names and header file locations
- Inheritance relationships (parent names, virtual inheritance flags, declaration order)
- Member offsets (for validating sub-object sizes)

Used for: building the inheritance chain, determining which bases are virtual vs non-virtual, computing approximate sub-object layout.

### Tertiary: Header files

Direct header parsing (via `struct_db.py`'s `parse_header()`) provides:
- Virtual function declarations (order matters for vtable slot assignment)
- Class hierarchy with virtual inheritance markers

Used when struct_db is stale or missing entries.

### Quaternary: /vtable skill output

The existing `/vtable` skill can dump a single named vtable. The new tool wraps this capability with automatic base class identification.

## MSVC PPC ABI Details

### Object Layout with Virtual Inheritance

MSVC lays out objects with virtual inheritance as follows:

```
[non-virtual base sub-objects, in declaration order]
  Each non-virtual base that has virtual functions gets:
    - vfptr (4 bytes): pointer to vtable for this base's virtual functions
    - vbptr (4 bytes): pointer to vbtable for reaching virtual bases
  Each non-virtual base that has no virtual functions but has data members:
    - Data members only (no vfptr/vbptr if no virtuals in its hierarchy)
[derived class's own data members]
[virtual base sub-objects, in DFS order]
  The virtual base appears once, shared by all bases that virtually inherit it
```

### Vtable Symbol Naming

MSVC mangled vtable symbols follow this pattern:

| Pattern | Meaning |
|---------|---------|
| `??_7Class@@6BBase@@@` | vftable for `Class`, sub-object vtable for `Base` |
| `??_7Class@@6B@` | vftable for `Class`, vtable with no named base (direct virtual base interface) |
| `??_7Class@@6B0@@` | vftable for `Class`, self-referencing (class is its own base context) |
| `??_8Class@@7BBase@@@` | vbtable for `Class`, relative to `Base` sub-object |

The `6B` encoding means "const ptr to const data" (i.e., vtable). The `7B` encoding is used for vbtables.

### Adjustor Thunks

When a virtual function is called through a non-primary base's vtable, MSVC generates adjustor thunks that subtract the sub-object offset from `this` before calling the real function. These appear as:

```
?Method@Class@@$4<adjustment>@<flags>@...
```

Where `<adjustment>` is a hex-encoded signed displacement. Common patterns:
- `$4PPPPPPPM@A@` = adjustment of -4 (i.e., `this -= 4` before calling)
- `$4PPPPPPPM@LI@` = different calling convention flags

### Sub-object Vtable Slot Layout

For non-primary base vtables:
- Slot 0+: Virtual functions declared in this base's hierarchy (but NOT inherited from the virtual base)
- Last slot: `??_R4` RTTI Complete Object Locator

For the primary (offset 0) vtable:
- Slots follow the virtual base's vtable layout (e.g., Hmx::Object's ~22 standard virtuals)
- Additional slots for derived class's new virtual functions
- Last slot: `??_R4` RTTI Complete Object Locator

### RTTI Complete Object Locator (`??_R4`)

The `??_R4` entry at the end of each vtable contains (in the COFF section data):
- `signature` (4 bytes): Always 0
- `offset` (4 bytes): Offset of this vtable within the complete object
- `cdOffset` (4 bytes): Offset of the constructor displacement
- `pTypeDescriptor` (reloc): Pointer to `??_R0` type descriptor
- `pClassHierarchyDescriptor` (reloc): Pointer to `??_R3`

The `offset` field is the key datum: it tells us exactly at what byte offset from the start of the complete object this particular vtable pointer resides. This is the ground truth for mapping `sub_object_offset` to the correct vtable.

## Implementation Plan

### Option A: Extend `scripts/dump_vtable.py` (Recommended)

Add a new function alongside the existing `get_vtable_layout()` and `lookup_vtable_offset()`:

```python
def resolve_vcall(class_name: str, sub_object_offset: int, vtable_slot: int,
                  obj_path: str = None, project_root: str = None) -> dict:
    """Resolve a virtual call through a sub-object vtable.

    Given (class, sub_object_offset, vtable_slot), returns the resolved
    function name, inheritance path, and full vtable dump.
    """
```

Implementation steps:

1. **Find the .obj file** using `find_obj_file(class_name)`.

2. **Enumerate all vtable symbols** matching `??_7<class_name>@@6B*`. Parse the base class name from each mangled symbol.

3. **Read the `??_R4` RTTI entry** at the end of each vtable to extract the `offset` field. This gives the ground-truth sub-object offset for each vtable.

4. **Match `sub_object_offset`** against the RTTI offsets. If an exact match is found, we know which vtable to look in.

5. **Index into the matched vtable** at `vtable_slot`. Read the relocation entry to get the target symbol.

6. **Demangle and annotate**: Resolve ICF-merged symbols by cross-referencing slot position against the base class's virtual function declaration order (parsed from headers).

7. **Build inheritance path**: Use struct_db or header parsing to trace the inheritance chain from the resolved base back to the virtual base.

### Option B: MCP Server Extension

Add `resolve_vcall` as a new MCP tool in `scripts/orchestrator/mcp_server.py`, following the same pattern as `lookup_struct_offset`:

```python
Tool(
    name="resolve_vcall",
    description="Resolve a virtual function call through a sub-object vtable. "
                "When target assembly loads a vtable from (this+offset) and calls "
                "slot N, this tool identifies the actual function being called.",
    inputSchema={
        "type": "object",
        "properties": {
            "class_name": {
                "type": "string",
                "description": "Most-derived class name (e.g., 'CharServoBone')",
            },
            "sub_object_offset": {
                "type": "integer",
                "description": "Byte offset from this-ptr to vtable load (e.g., 8)",
            },
            "vtable_slot": {
                "type": "integer",
                "description": "Slot index in the vtable (0-based), or byte offset if > 100",
            },
        },
        "required": ["class_name", "sub_object_offset", "vtable_slot"],
    },
),
```

The handler calls the `resolve_vcall()` function from `dump_vtable.py` and formats the result.

### Recommended: Both

Implement the core logic in `scripts/dump_vtable.py` (Option A) with a CLI entry point for direct use. Then wire it into the MCP server (Option B) for agent access. This mirrors the existing pattern where `dump_vtable.py` has both library functions and a CLI, and the `/vtable` skill calls the library functions.

### Implementation Sequence

1. **Add `??_R4` parsing to `dump_vtable.py`**: Read the RTTI Complete Object Locator from each vtable section to extract sub-object offsets. This is the critical missing capability.

2. **Add `enumerate_all_vtables()` function**: Return all vtables for a class with their base names and sub-object offsets.

3. **Add `resolve_vcall()` function**: Core resolution logic.

4. **Add CLI subcommand**: `python3 scripts/dump_vtable.py resolve CharServoBone 8 1`

5. **Add MCP tool**: Wire into `mcp_server.py`.

6. **Add slot annotation**: Cross-reference vtable slot positions against virtual function declarations in headers to provide human-readable names even for ICF-merged entries.

## Parsing `??_R4` Sub-Object Offsets

The `??_R4` relocation at the end of each vtable points to a COFF section containing the Complete Object Locator structure. The layout (big-endian on PPC):

```
Offset  Size  Field
0x00    4     signature (always 0)
0x04    4     offset (sub-object offset within complete object) <-- THIS IS WHAT WE NEED
0x08    4     cdOffset (constructor displacement offset)
0x0c    reloc pTypeDescriptor (-> ??_R0)
0x10    reloc pClassHierarchyDescriptor (-> ??_R3)
```

To read it:
1. Find the `??_R4` symbol in the COFF symbol table.
2. Its `section` field points to the section containing the COL data.
3. Read 4 bytes at offset 0x04 (big-endian) from the section's raw data.

This gives us the authoritative sub-object offset for each vtable, eliminating the need to compute sub-object layout from the class hierarchy (which is fragile and depends on exact layout rules).

## Verification: ClipCollide `servo+8, vtable[1]`

Expected resolution flow:
1. Input: `resolve_vcall("CharServoBone", 8, 1)`
2. Find `build/373307D9/obj/system/char/CharServoBone.obj`
3. Enumerate vtable symbols:
   - `??_7CharServoBone@@6BObject@Hmx@@@` (primary, for virtual base Hmx::Object)
   - `??_7CharServoBone@@6BCharBones@@@` (for CharBonesMeshes/CharBones sub-object)
   - `??_7CharServoBone@@6BRndPollable@@@` (for CharPollable/RndPollable sub-object)
   - `??_7CharServoBone@@6BRndHighlightable@@@` (for RndHighlightable sub-object)
4. Read `??_R4` from each vtable to get sub-object offsets:
   - Object@Hmx: offset 0x00 (primary vtable)
   - RndHighlightable: offset 0x00 or a small offset
   - RndPollable: offset 0x08 (matches our query!)
   - CharBones: some larger offset
5. Match: `sub_object_offset=8` matches `??_7CharServoBone@@6BRndPollable@@@`
6. Read slot 1 from RndPollable vtable:
   - `[0]` IRLoadConst::IsLoadConst (ICF for PollEnabled)
   - **`[1]` CharServoBone::Poll()** <-- answer
7. Output: `CharServoBone::Poll()`, declared in `RndPollable`, overridden by `CharServoBone`

## Edge Cases

### Multiple Inheritance Without Virtual Bases

Classes like `class Foo : public Bar, public Baz` (no virtual keyword). Each base gets its own vtable pointer at successive offsets. The `??_R4` approach handles this identically.

### Virtual Bases

The virtual base sub-object (e.g., `Hmx::Object`) is placed at the END of the object and is shared. Its vtable is the primary vtable (offset 0 in the most-derived class). The vbtable (`??_8`) is used at runtime to locate the virtual base, but for our tool, we only need the `??_R4` offset field.

### ICF-Merged Vtable Entries

When multiple virtual functions compile to identical machine code (e.g., empty destructors, simple return-this), the linker merges them to a single address. The vtable relocation then points to one arbitrary representative. For example:
- `OnlyReturns` = any function that just returns (void or this)
- `IRLoadConst::IsLoadConst` = a bool-returning function that returns a constant

The tool should:
1. Report the ICF-merged symbol name
2. Annotate with the positional function name (from header virtual function declaration order)
3. Set confidence to "medium"

### Adjustor Thunks in Primary Vtable

When the primary vtable (for the virtual base) includes overrides from the derived class, those entries may be adjustor thunks rather than direct function pointers. The thunk adjusts `this` from the virtual base sub-object pointer to the complete object pointer before calling the real function. The tool should demangle the thunk to reveal the actual target function.

### Classes Not in .obj Files

Some classes may not have their .obj file available (e.g., third-party libraries, or classes defined across multiple TUs). Fallback:
1. Search ALL .obj files for vtable symbols matching the class name
2. If not found, fall back to header-only analysis (parse virtual functions, compute approximate slot layout)

### Diamond Inheritance

Example: `RndText : virtual RndDrawable, virtual RndTransformable` where both virtually inherit `RndHighlightable` which virtually inherits `Hmx::Object`.

The `??_R4` approach handles this correctly because each vtable's RTTI records its actual offset. The shared virtual base appears once, and its vtable is the primary vtable at offset 0 in the most-derived class.

### Byte Offset vs Slot Index

Agents may specify the vtable offset as either a slot index (small number like 1) or a byte offset (like 0x04). The tool should auto-detect:
- If `vtable_slot > 100`, assume byte offset and divide by 4 to get slot index
- Otherwise, treat as slot index
- Alternatively, accept both `vtable_slot` and `vtable_byte_offset` parameters

### Multiple .obj Files for Same Class

The same vtable may appear in multiple .obj files (COMDAT sections). The tool should:
1. Prefer the .obj file matching the class name (e.g., `CharServoBone.obj` for `CharServoBone`)
2. Fall back to any .obj file containing the vtable symbol

## Testing Strategy

### Unit Tests

1. **CharServoBone at offset 8, slot 1** -> `CharServoBone::Poll()` (the motivating case)
2. **CharServoBone at offset 0, slot 5** -> `CharServoBone::Handle()` (primary vtable, Object base)
3. **RndText at offset for RndDrawable, slot 0** -> `RndText::~RndText()` or `Draw()` (diamond inheritance)
4. **RndTransformable at offset 0, slot 0** -> dtor (single virtual base)
5. **Any class, invalid offset** -> graceful error with available offsets listed

### Integration Tests

Run against the full .obj directory to validate:
- Every `??_R4` entry can be parsed
- Every vtable can be enumerated
- No assertion failures on the full class set

### Regression Test

After implementation, re-run the ClipCollide analysis and verify the tool produces the correct answer in under 1 second.
