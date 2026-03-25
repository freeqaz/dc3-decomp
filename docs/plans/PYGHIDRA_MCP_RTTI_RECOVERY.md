# pyghidra-mcp RTTI Recovery Plan

## Overview

`pyghidra-mcp` already handles:

- XEX import and Xenon language selection
- map-file-backed function lookup
- bulk function creation
- demangled function signature application
- structure seeding from `struct_db`

What it does **not** do yet is recover MSVC RTTI metadata into an explicit class model. Today we can see RTTI-flavored symbols in the map file and use them manually, but there is no automated pipeline that:

- inventories `??_R*` / vtable symbols
- parses the RTTI object graph from program memory
- reconstructs class hierarchies and base offsets
- materializes that recovered information back into Ghidra

This document is the implementation plan for that missing pipeline.

## Goals

1. Recover class names, base classes, and base offsets from MSVC RTTI.
2. Associate recovered classes with their vtables and, when present, vbtables.
3. Persist the recovered data into the Ghidra project in a way that improves decompilation and inspection.
4. Expose the recovery through MCP tools so the pipeline is scriptable and batchable.

## Non-Goals

- Byte-identical reconstruction of `.rdata`.
- Perfect recovery of every compiler-generated detail on the first pass.
- Replacing header-derived `struct_db` seeding. RTTI recovery complements it; it does not replace it.
- Solving virtual dispatch slot naming without either map-file vtable names or COFF relocation evidence.

## Current Gap

The current type-seeding pipeline is function-centric:

1. `bulk_create_functions`
2. `apply_demangled_signatures`
3. `create_structures`

That pipeline improves function signatures and struct layouts, but it does not currently recover:

- `??_R0` type descriptors
- `??_R1` base class descriptors
- `??_R2` base class arrays
- `??_R3` class hierarchy descriptors
- `??_R4` complete object locators
- the mapping from `??_7*` vtables back to the recovered class graph

## Data Sources

### 1. Linker map file

Primary symbol inventory source:

- `orig/373307D9/ham_xbox_r.map`
- `config/373307D9/symbols.txt`

Relevant symbol families:

- `??_7*` - vftables
- `??_8*` - vbtables when present
- `??_R0*` - type descriptors
- `??_R1*` - base class descriptors
- `??_R2*` - base class arrays
- `??_R3*` - class hierarchy descriptors
- `??_R4*` - complete object locators

### 2. Program memory in Ghidra

The map file gives names and addresses. The actual RTTI graph must be parsed from the loaded program's memory so we can follow pointers and validate structure contents.

### 3. Existing repo utilities

- `scripts/dump_vtable.py` for slot-level vtable validation from original `.obj` files
- `docs/sessions/STR_CLASS_HIERARCHY_ANALYSIS.md` for known-good RTTI-based inheritance examples
- `src/system/os/MapFile_Xbox.cpp` for existing project knowledge about MSVC special symbol families

## MSVC RTTI Structures To Parse

This binary uses 32-bit pointers. The recovery code should model the standard MSVC RTTI graph:

### TypeDescriptor (`??_R0`)

Fields to parse:

- `pVFTable`
- `spare`
- mangled type name string

Primary output:

- canonical class/type name
- raw mangled RTTI name
- address of the descriptor

### BaseClassDescriptor (`??_R1`)

Fields to parse:

- `pTypeDescriptor`
- `numContainedBases`
- `mdisp`
- `pdisp`
- `vdisp`
- `attributes`
- `pClassHierarchyDescriptor`

Primary output:

- base class name
- offset metadata for multiple/virtual inheritance
- per-base attributes

### BaseClassArray (`??_R2`)

Fields to parse:

- ordered pointer array of `BaseClassDescriptor`

Primary output:

- ordered list of bases participating in the hierarchy

### ClassHierarchyDescriptor (`??_R3`)

Fields to parse:

- `signature`
- `attributes`
- `numBaseClasses`
- `pBaseClassArray`

Primary output:

- hierarchy attributes
- flattened list of base descriptors

### CompleteObjectLocator (`??_R4`)

Fields to parse:

- `signature`
- `offset`
- `cdOffset`
- `pTypeDescriptor`
- `pClassHierarchyDescriptor`

Primary output:

- most-derived class for a vtable
- subobject offset for the vtable
- pointer to the hierarchy descriptor

## Recovered Data Model

Add explicit RTTI models in `../pyghidra-mcp/src/pyghidra_mcp/models.py`:

- `RttiTypeDescriptor`
- `RttiBaseClass`
- `RttiHierarchy`
- `RttiLocator`
- `RecoveredClassInfo`
- `RecoveredClassListResult`

`RecoveredClassInfo` should contain at minimum:

- `class_name`
- `demangled_name`
- `type_descriptor_address`
- `complete_object_locator_addresses`
- `vtable_addresses`
- `vbtable_addresses`
- `hierarchy_attributes`
- `bases`
- `source_symbols`
- `applied_to_ghidra`

## Implementation Plan

### Phase 1: Symbol Inventory

Code touchpoints:

- `../pyghidra-mcp/src/pyghidra_mcp/symbol_lookup.py`
- tests for RTTI/vtable symbol classification

Work:

1. Add helper classifiers for `??_7`, `??_8`, `??_R0`..`??_R4`.
2. Add a parser that extracts the target class name from RTTI/vtable symbols without relying on Ghidra demangling.
3. Build address-indexed buckets:
   - RTTI symbols by address
   - RTTI symbols by class name
   - vtable/vbtable symbols by class name
4. Keep raw mangled names alongside parsed names; do not throw away the original symbol text.

Deliverable:

- a pure-Python inventory layer that can answer "what RTTI/vtable artifacts exist for class X?"

### Phase 2: Memory Parse

Code touchpoints:

- `../pyghidra-mcp/src/pyghidra_mcp/tools.py`

Work:

1. Given a `??_R4` or `??_R0` address, read the underlying structure bytes from the loaded program.
2. Decode the structures listed above using 32-bit pointers and big-endian reads where required by the program image.
3. Resolve pointer targets back to symbols when possible.
4. Detect and reject invalid candidates:
   - null descriptor pointers
   - out-of-image pointers
   - malformed type names
   - base arrays whose length is implausible

Deliverable:

- a parser that returns a structured `RecoveredClassInfo` graph from a symbol/address seed

### Phase 3: Class Graph Assembly

Code touchpoints:

- `../pyghidra-mcp/src/pyghidra_mcp/tools.py`

Work:

1. Group all locators, type descriptors, and vtables by most-derived class.
2. Distinguish:
   - primary vtable for the class
   - secondary vtables for base subobjects
   - presence of vbtable / virtual inheritance metadata
3. Normalize duplicate discoveries caused by multiple locators pointing at the same class graph.
4. Surface base offsets directly in the result model so callers do not need to decode `mdisp/pdisp/vdisp` themselves.

Deliverable:

- a stable recovered-class graph suitable for both scripting and Ghidra application

### Phase 4: Apply To Ghidra

Code touchpoints:

- `../pyghidra-mcp/src/pyghidra_mcp/tools.py`
- `../pyghidra-mcp/src/pyghidra_mcp/server.py`

Work:

1. Create canonical DTM entries for the MSVC RTTI structs themselves:
   - `_s_RTTICompleteObjectLocator`
   - `_s_RTTIClassHierarchyDescriptor`
   - `_s_RTTIBaseClassArray`
   - `_s_RTTIBaseClassDescriptor`
   - `TypeDescriptor`
2. Apply those types at the recovered RTTI addresses.
3. Rename RTTI and vtable symbols where Ghidra still has generic names.
4. Add repeatable comments at vtable / locator addresses summarizing:
   - recovered class name
   - primary base list
   - subobject offset
5. If a class structure already exists in the DTM, use RTTI recovery to annotate inheritance metadata; do not overwrite user-curated field layouts.
6. If a class structure does not exist yet, optionally create a placeholder structure named for the class and record recovered base offsets as comments/metadata.

Deliverable:

- recovered RTTI visible inside the saved Ghidra project, not just in MCP responses

### Phase 5: Function-Level Propagation

Code touchpoints:

- `../pyghidra-mcp/src/pyghidra_mcp/tools.py`

Work:

1. Identify virtual methods associated with recovered vtables.
2. When safe, apply `this*` types for recovered classes to virtual functions whose signatures are still weak.
3. Do not override stronger signatures already established by `apply_demangled_signatures`.
4. Expose per-function "source of truth" in results:
   - demangled signature
   - RTTI-derived class association
   - map-file-only fallback

Deliverable:

- RTTI recovery starts improving decompilation, not just metadata browsing

## Proposed MCP Tools

### `recover_rtti`

Purpose:

- inventory, parse, and optionally apply RTTI recovery in one tool

Suggested signature:

```python
recover_rtti(
    binary_name: str,
    mode: Literal["inventory", "parse", "apply"] = "parse",
    query: str = ".*",
    limit: int = 100,
    apply_to_ghidra: bool = False,
) -> dict
```

Expected behavior:

- `inventory`: list candidate RTTI/vtable symbols
- `parse`: return `RecoveredClassInfo` objects without mutating the project
- `apply`: parse + write types/comments/labels + save project

### `list_class_hierarchies`

Purpose:

- cheap read-only query over recovered classes

Suggested signature:

```python
list_class_hierarchies(
    binary_name: str,
    query: str = ".*",
    offset: int = 0,
    limit: int = 100,
) -> RecoveredClassListResult
```

Expected behavior:

- return recovered classes and their direct bases without re-running the full parser if cached results exist

## Persistence And Idempotence

Requirements:

1. Re-running recovery must not duplicate DTM entries or spam comments.
2. Applied RTTI types/comments should be deterministic across runs.
3. Cached recovered models should be invalidated when:
   - the imported binary changes
   - the map file changes
   - the implementation version changes

Recommended approach:

- store a small recovery-version marker in the project metadata or tool-side cache
- use "create or update" behavior for labels/comments/types

## Validation Plan

### Known-good spot checks

Use these before claiming the pipeline works:

1. `String`
   - should recover `TextStream` as first base and `FixedString` as secondary base
   - should agree with `docs/sessions/STR_CLASS_HIERARCHY_ANALYSIS.md`
2. `ObjRef` / `ObjRefConcrete<...>`
   - validates template / object wrapper handling
3. one single-inheritance class with a simple vtable
4. one multiple-inheritance class with non-zero base offsets
5. one class with virtual inheritance if a real specimen exists in DC3

### Cross-validation sources

For each sample class:

1. Compare recovered vtable association against `??_7*` map symbols.
2. Compare slot counts / slot symbol order against `scripts/dump_vtable.py`.
3. Compare recovered base offsets against constructor code and existing header knowledge.

### Failure reporting

The tool should report explicit counters:

- candidate RTTI symbols found
- classes recovered
- classes applied
- malformed descriptors skipped
- classes with unresolved bases
- classes with multiple locators

## File-Level Work Breakdown

### `../pyghidra-mcp/src/pyghidra_mcp/symbol_lookup.py`

- add RTTI/vtable symbol classifiers
- add class-name extraction helpers
- expose RTTI-aware inventory results

### `../pyghidra-mcp/src/pyghidra_mcp/models.py`

- add RTTI recovery result models

### `../pyghidra-mcp/src/pyghidra_mcp/tools.py`

- add memory parsers
- add recovery pipeline
- add Ghidra materialization helpers

### `../pyghidra-mcp/src/pyghidra_mcp/server.py`

- register new MCP tools
- save the project after mutating apply-mode operations

### Tests

- parser tests for representative `??_R*` and `??_7*` names
- unit tests for structure decoding from synthetic byte blobs
- integration test for at least one recovered class graph

## Suggested Execution Order

1. Land pure symbol classification and unit tests.
2. Land read-only RTTI parsing returning JSON results.
3. Land Ghidra application of RTTI struct types and comments.
4. Land class hierarchy query tools.
5. Land optional function-level propagation from recovered classes.

This ordering keeps the first milestone low-risk and testable before the Ghidra-mutation layer is introduced.
