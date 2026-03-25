---
name: rb2-locals
description: Get local variable names, types, and register/stack assignments from RB2 DWARF debug info. Shows parameters, locals (GPR/FPR/stack), and references for functions in the shared Milo engine. Ground truth for variable names/types when starting decomp.
argument-hint: "[function-name]"
allowed-tools: Bash(python3 *), Read, Grep, Glob
---

# RB2 Locals Skill

Query the RB2 DWARF dump for function-level local variable information.

## Arguments

`$ARGUMENTS` - Function name. Accepts:
- `Class::Method` — exact match (e.g., `ClipCollide::Collide`)
- `ClassName` — all methods of that class (e.g., `ClipCollide`)
- `MethodName` — all classes with that method (e.g., `Collide`)
- MSVC mangled symbols are auto-demangled

## Steps

1. **Run the lookup**:
   ```bash
   python3 scripts/orchestrator/rb2_locals.py "$ARGUMENTS"
   ```

2. **Present the results** to the user. Key sections:
   - **Parameters**: function arguments with register annotations
   - **Local Variables (GPR)**: variables in general-purpose registers
   - **Local Variables (FPR)**: variables in floating-point registers
   - **Local Variables (Stack)**: variables on the stack frame
   - **References**: globals/statics/RTTIs accessed by the function

3. **Interpret for DC3 decomp**:
   - Variable **names** and **types** transfer directly (shared Milo engine codebase)
   - Variable **count** and stack-vs-register decisions are suggestive
   - Specific register numbers do NOT transfer (MW EABI vs MSVC PPC — different allocators)
   - Stack frame offsets do NOT transfer (different frame layouts)

## Tips

- Use alongside `/rb3-pair` (RB3 source code) and `/rb2-class` (struct layouts) for full context
- Callee-saved registers (r13-r31, f14-f31) indicate variables live across function calls
- Volatile registers (r0-r12, f0-f13) indicate short-lived or single-use variables
- Stack variables are usually structs, arrays, or spilled values — expect them on stack in DC3 too
