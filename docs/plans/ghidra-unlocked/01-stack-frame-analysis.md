# 01 — Stack Frame & Calling Convention Analysis

Priority: **Tier 1**  
Readiness: **Ready**  
Effort: **Low**

## Why This Is Worth Doing

This is the cleanest “high signal, low risk” feature in the set. Ghidra already tracks stack frame, parameter, local-variable, and calling-convention metadata; we just do not expose it in a machine-friendly way.

This directly helps with:

- prologue/epilogue mismatches
- stack-size mismatches
- custom-storage calling convention questions
- bad `this`/parameter propagation

## Validated Source of Truth

The live server already has the right extension pattern in `../pyghidra-mcp/src/pyghidra_mcp/tools.py`.

The Ghidra APIs we should rely on are stable and already present:

- `Function.getStackFrame()`
- `Function.getParameters()`
- `Function.getCallingConventionName()`
- `Function.getReturnType()`
- `Function.hasVarArgs()`
- `Variable.getVariableStorage()`
- `Variable.getStackOffset()`

## Scope

### V1

Expose:

- function name and entry point
- calling convention
- return type
- varargs flag
- frame size
- local size
- parameter area offset
- return-address offset
- parameters with name, datatype, ordinal, storage, register list, stack offset when applicable
- local variables with name, datatype, size, storage, stack offset when applicable

### Explicitly out of scope for V1

- “callee-saved register set” as a trusted field

Ghidra does not hand us a ready-made “saved registers” list through the same clean API surface. We can derive a heuristic later by scanning the prologue or inspecting stack/register storage, but that should not block the useful first version.

## Implementation

### MCP server

Add a thin endpoint to the live server:

- `../pyghidra-mcp/src/pyghidra_mcp/tools.py`
- `../pyghidra-mcp/src/pyghidra_mcp/server.py`

Recommended name:

- `get_function_stack_frame`

Input:

- `binary_name`
- `name_or_address`

Output shape:

```json
{
  "function_name": "CharDriver::Poll",
  "entry_point": "ram:82345678",
  "calling_convention": "__thiscall",
  "return_type": "void",
  "has_var_args": false,
  "frame": {
    "frame_size": 160,
    "local_size": 112,
    "parameter_offset": 8,
    "return_address_offset": 4
  },
  "parameters": [
    {
      "ordinal": 0,
      "name": "this",
      "type": "CharDriver *",
      "storage": "r3",
      "registers": ["r3"],
      "stack_offset": null
    }
  ],
  "locals": [
    {
      "name": "local_10",
      "type": "int",
      "size": 4,
      "storage": "Stack[-0x10]",
      "stack_offset": -16
    }
  ]
}
```

### CLI

Add:

- `tools/ghidra/stack_frame.py`

Requirements:

- symbol or address input
- human-readable table
- `--json`

### Skill

Add:

- `.claude/skills/ghidra-stack/SKILL.md`

Pattern should mirror the existing `ghidra-decompile`, `ghidra-search`, and `ghidra-struct` skills.

## Design Notes

- Reuse the server’s existing symbol/address resolution path. Do not re-implement lookup logic in the CLI.
- Return raw storage information from the server; let the CLI decide how to render register-vs-stack presentation.
- If a function does not yet exist in Ghidra but the address resolves, follow the existing server precedent and attempt function creation first.

## Acceptance Criteria

- Querying a known function by symbol works.
- Querying the same function by address works.
- Output shows at least one parameter and one local variable for a function with rich metadata.
- The endpoint is covered by an integration test in `../pyghidra-mcp/tests/integration/`.

## Follow-On

If V1 proves useful, add `saved_registers` as a clearly-labeled heuristic field, not as an inferred “ground truth” property.
