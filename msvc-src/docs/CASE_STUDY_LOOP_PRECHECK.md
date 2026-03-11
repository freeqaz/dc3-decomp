# Case Study: Loop Pre-Check Duplication in MSVC PPC

Date: 2026-03-11
Functions: `ObjPtrVec::unique`, `ObjPtrVec::find`

## Summary

The MSVC PPC optimizer uses **different loop transformation strategies depending
on the cost of the loop condition**. This was discovered by analyzing why
ObjPtrVec::unique couldn't match the target despite trying all loop structures
(while, for(;;), do-while, goto).

## Key Finding: Condition Cost Threshold

| Condition Type | while/for(;;) | do-while with guard |
|---------------|---------------|---------------------|
| **Cheap** (ptr compare, 2-3 insns) | Duplicated (while→do-while transform) | Duplicated |
| **Expensive** (divw chain, 13+ insns) | **NOT duplicated** (condition at top) | Duplicated |

### Cheap condition: while→do-while transform
```
Source:  while (jt != v->_M_last) { body; }

PPC:    [setup]
        cmplw cr6, r31, r11     ← PRE-CHECK (duplicated)
        beq cr6, exit
loop:   [body]
        lwz r11, 4(r29)         ← CONDITION (at bottom)
        cmplw cr6, r31, r11
        bne cr6, loop
```
The optimizer converts `while` to `if (cond) do { body } while (cond)`,
duplicating the condition (3 extra instructions).

### Expensive condition: condition stays at top
```
Source:  while (jt != v->end()) { body; }
         where end() = begin() + size() → involves divwu + multiply

PPC:    [setup]
loop:   lwz r9, 0(r30)          ← CONDITION (at top, 13 insns)
        lwz r8, 4(r30)
        subf, divwu, subfic, slwi, subfe, add, and, slwi, add
        cmplw cr6, r31, r9
        beq cr6, exit
        [body]
        b loop                   ← back-edge goes to condition
```
The optimizer keeps the condition at the loop entry point. Both the initial
flow and the back-edge go to the same condition code. **No duplication.**

## Proof: All Loop Forms Normalize

### With cheap conditions (pointer compare)

All four source structures produce **byte-identical** PPC:

| Source Form | PPC Size | Structure |
|-------------|----------|-----------|
| `while (jt != _M_last)` | 104B | pre-check + bottom condition |
| `for (;;) { if (jt == _M_last) break; ... }` | 104B | identical |
| `if (jt != _M_last) { do { ... } while (jt != _M_last); }` | 104B | identical |
| `goto check; loop: ...; check: if (jt != _M_last) goto loop;` | 104B | identical |

The c1xx front-end generates different IL for each:
- `while`: IL has NE condition before body, GOTO back-edge
- `for(;;)`: IL has EQ condition at body start, GOTO to start
- `do-while`: IL has NE pre-check, NE at body bottom
- `goto`: IL has ASSIGN check label, body, NE at check label, COND_BRANCH

But c2.dll's optimizer normalizes all to the same canonical form:
`pre-check → do-while loop` (condition at bottom with back-edge).

### With expensive conditions (end() involving divwu)

| Source Form | PPC Size | Structure |
|-------------|----------|-----------|
| `while (jt != end())` | 148B | condition at **top**, no duplication |
| `for (;;) { if (jt == end()) break; ... }` | 148B | **byte-identical** to while |
| `if (jt != end()) { do { ... } while (jt != end()); }` | 176B | **DIFFERENT** — condition duplicated |

The while and for(;;) versions are byte-identical (condition at top).
The do-while version is 28 bytes larger (7 extra instructions for pre-check).

## Target Analysis

The target ObjPtrVec::unique (152 bytes) uses:
- `__savegprlr_28` (4 callee-saved: r28=this, r29=sizeof, r30=&mNodes, r31=jt)
- Cheap empty check at top: `cmplw _M_first, _M_last; beq exit`
- **Condition at loop top** (the expensive end() computation)
- Back-edges from both paths branch to loop condition
- Calls `erase<RndTex>` (ICF merged with `erase<Hmx::Object>`)

This matches the `while (jt != end())` / `for(;;)` pattern with expensive condition.

### Target's end() computation (per iteration)
```
lwz  r11, 4(r30)       ; _M_finish
li   r10, 0x14         ; sizeof(Node)
lwz  r9, 0(r30)        ; _M_start
subf r8, r9, r11       ; byte distance
cmplw cr6, r9, r11     ; empty check
divw r11, r8, r10      ; count = dist / sizeof(Node)
li   r10, 0            ; default begin = 0
beq  cr6, <skip>        ; if empty, keep 0
clrrwi r10, r9, 0      ; begin = _M_start
mulli r11, r11, 0x14   ; offset = count * sizeof(Node)
add  r11, r11, r10     ; end = begin + offset
cmplw cr6, r31, r11    ; jt vs end
beq  cr6, exit
```
13 instructions for the condition, executed once per loop iteration.

## Compiler Pipeline Analysis

The loop normalization happens in c2.dll's optimization passes (Groups 1-4),
not in c1xx's front-end. Evidence:

1. **c1xx produces different IL** for each loop form (confirmed by IL capture)
2. **c2.dll produces identical PPC** (confirmed by /FAcs listing)
3. **Passes.md documents** loop rejection strings: `WHILE_REJECTED_CONTAINS_CALL`,
   `DO_REJECTED_OVERFLOW`, etc. — proving a dedicated loop optimizer exists

The loop optimizer:
1. Recognizes loop patterns (while, do-while, for)
2. Attempts optimization (counted loops → bdnz, etc.)
3. **Rejects** loops containing function calls (can't use bdnz)
4. But still applies the **while→do-while transform** for cheap conditions
5. **Preserves condition-at-top** for expensive conditions

### Simple counted loops
When the loop has no calls and a simple count, the optimizer goes further:
```
while (i < count) { arr[i]++; i++; }
→ cmpwi + blelr + mtctr + bdnz    (hardware counted loop)
```
Both `while` and `for(;;)` produce identical bdnz loops.

## Implications for Decomp

### For ObjPtrVec::unique (AT_LIMIT at 63%)

The previous implementation attempted a single-pass algorithm matching the
target. The 37% gap was attributed to "loop pre-check duplication," but this
analysis shows:

1. With expensive `end()`, `while` and `for(;;)` **do NOT duplicate** the condition
2. The target uses condition-at-top (no duplication)
3. Our compiler should also produce condition-at-top with `while (jt != end())`

The remaining gap likely comes from:
- Algorithm correctness issues (the source was reverted to O(n²))
- Different `end()` computation details (signed divw vs unsigned divwu)
- Different ternary lowering (branch-based vs boolean mask)
- sizeof(Node) caching in callee-saved register

### For other loop-heavy functions

When writing loops with expensive conditions:
- **Use `while` or `for(;;)`** — both produce the same efficient code
- **Avoid `do-while` with explicit pre-check** — causes condition duplication
- The compiler is smart enough to NOT duplicate expensive conditions

When writing loops with cheap conditions:
- **All forms produce identical code** — the optimizer normalizes everything
- Pre-check duplication is unavoidable but cheap (2-3 extra instructions)

## Test Files

- `/tmp/claude-1000/il_loop_precheck.cpp` — cheap condition, 4 loop variants
- `/tmp/claude-1000/il_loop_cost.cpp` — expensive vs cheap, 4 variants
- IL captured at `/tmp/claude-1000/_CL_2bd3e6e6` (cheap) and
  `/tmp/claude-1000/_CL_cb7fe71e` (expensive)

## Relationship to Known Patterns

This extends the understanding from `msvc-src/docs/PASSES.md`:
- The loop rejection strings (`WHILE_REJECTED_CONTAINS_CALL`) now have
  a concrete behavioral model: loops with calls skip counted-loop transform
  but still undergo normalization and condition-cost-based duplication decisions
- The `CODE_MOTION` pass (index 13) is likely responsible for the condition
  placement optimization (hoisting or sinking based on cost)
