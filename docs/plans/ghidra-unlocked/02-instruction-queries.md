# 02 — Instruction-Level Queries

Priority: **Tier 1**  
Readiness: **Ready**  
Effort: **Low**

## Why This Is Worth Doing

We already know a lot about target-side PPC idioms. What is missing is a fast way to ask:

- which mnemonics appear in this function
- where a specific opcode pattern occurs
- whether one of our known decomp patterns appears in the target

This is a straightforward read-only listing query.

## Validated Source of Truth

Relevant APIs in `../ghidra`:

- `Program.getListing()`
- `Listing.getInstructions(AddressSetView, boolean)`
- `Instruction.getMnemonicString()`
- `Instruction.getNumOperands()`
- `Instruction.getDefaultOperandRepresentation(i)`
- `Instruction.getOperandObjects(i)`
- `Instruction.getFlowType()`
- `Instruction.getPcode()`

## Scope

### V1

Expose raw per-instruction data:

- address
- mnemonic
- operand strings
- flow type
- optional raw p-code op mnemonics

Do pattern detection in the CLI, not in the server.

### Initial pattern set

Keep the first release tight and well-tested:

- `addic/subfe`
- `neg/andc/srwi`
- `cntlzw >> 5`
- `subf.` record-form loop conditions
- `nor`-based byte inversion

Do not start with every pattern in `docs/decomp/patterns/`. Add only patterns we can test and explain.

## Implementation

### MCP server

Add:

- `get_function_instructions(binary_name, name_or_address, include_pcode=false)`

Use the live server in `../pyghidra-mcp`.

Return normalized instruction records; do not pre-format joined strings beyond operand text.

Suggested JSON:

```json
{
  "function_name": "ShowIfPossible",
  "entry_point": "ram:82345690",
  "instruction_count": 87,
  "instructions": [
    {
      "address": "ram:82345690",
      "mnemonic": "addic",
      "operands": ["r9", "r3", "-0x1"],
      "flow_type": "FALL_THROUGH",
      "pcode_ops": ["INT_ADD", "INT_CARRY"]
    }
  ]
}
```

### CLI

Add:

- `tools/ghidra/insn_query.py`
- `tools/ghidra/insn_patterns.py`

Capabilities:

- full dump
- regex mnemonic filter
- histogram
- `--patterns`
- `--json`

### Skill

Add:

- `.claude/skills/ghidra-insn/SKILL.md`

## Design Notes

- Use `listing.getInstructions(func.getBody(), True)` directly. It is simpler than iterating address ranges manually.
- Keep pattern explanations in Python so they can point back to our decomp guidance without changing server code.
- Make the CLI resilient to pattern drift. Unknown instruction forms should not crash detection.

## Acceptance Criteria

- The endpoint returns the full instruction list for a known function.
- Histogram output matches the instruction count total.
- At least one known function exercises one pattern detector in a stable test fixture.
- Human-readable output is useful without requiring objdiff.
