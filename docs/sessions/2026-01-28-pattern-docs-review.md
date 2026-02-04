# Pattern Documentation Review Notes

**Date:** 2026-01-28
**Purpose:** Inventory existing pattern docs before consolidation into `docs/decomp/patterns/`

---

## 1. Complete Pattern Inventory with IDs

### Source 1: `docs/permuter/PATTERNS.md` (PRIMARY)

**Confirmed Working Patterns (13):**

| ID | Name | Impact | Success Rate |
|----|------|--------|--------------|
| U01 | Unsigned Zero Comparison (`> 0` vs `!= 0`) | +0.4-1.3% | HIGH |
| U02 | Signed/Unsigned Cast Forcing | +1-50% | 100% |
| C01 | Explicit Float Casting for `floor()` | +35% | HIGH |
| C02 | noreturn Attribute for exit() | +38.5% | 100% |
| C03 | Float/Double Separation | +80% | 95% |
| D01 | Explicit Destructor Declaration | +37-70% | 100% |
| V01 | Size to Local Variable | +35% | 95%+ |
| V02 | Comparison Operand Reordering | +2% | HIGH |
| V03 | Variable Declaration Order (Quaternions) | +88% | HIGH |
| E01 | Explicit Conditional Instead of Max() | +35% | HIGH |
| F01 | FMA Instruction Selection (fnmsubs vs fmsubs) | +75% | 98%+ |
| I01 | Inline Assignment in Function Calls | +2% | 95%+ |
| O01 | Operator Overload Selection | +1.4% | 100% |

**Harmful Patterns (13, from BurnXfm testing):**

| ID | Pattern | Effect |
|----|---------|--------|
| L01 | end iterator explicit (first) | -0.5% |
| L02 | end iterator explicit (after it) | -0.5% |
| L03 | While loop instead of for | No effect |
| L04 | Separated increment | No effect |
| V04 | child pointer inside loop | -6.5% |
| V05 | xfm declared outside loop | No effect |
| V06 | Iterator declared outside loop | No effect |
| A01 | Alias mChildren | No effect |
| A02 | Alias mLocalXfm | -6% |
| A03 | Both aliases | -6% |
| A04 | Alias mLocalXfm as pointer | -6% |
| P01 | self = this shim | No effect |
| P02 | mesh = this at function start | No effect |

**Unfixable Patterns (4):**
- LINKER_MERGED
- REGISTER_SWAP
- INSTRUCTION_SCHEDULING
- CONTROL_FLOW (Compiler-Driven)

### Source 2: `docs/meta-strategy/APPENDIX_PATTERNS.md`

**Unfixable Patterns (9):**

| Pattern | Prevalence | Gap | Detection |
|---------|------------|-----|-----------|
| LINKER_MERGED (ICF) | ~80% | 0.5-3% | `bl merged_*` |
| BOOL_MASK | ~5% | ~3% | `clrlwi r3, rX, 24` |
| ASSERT_REVS | ~10% | ~0.8-0.9% | scheduling differences |
| LTCG_POOLING | varies | 0.5-1% | extra `lis` instructions |
| FMADDS_VS_SEPARATE | float math | 1-3% | `fmadds` vs `fmuls+fadds` |
| REGISTER_ALLOCATION | ~80% | 1-3% | register swaps |
| BRANCH_OFFSETS | common | 0% | branch targets differ |
| COMMUTATIVE_REGISTER_SWAP | float ops | <1% | operand order swapped |
| 64BIT_EXTRACTION | rare | ~5% | `lhz` vs `ld+mask` |

**Fixable Patterns - High Success:**
- unsigned_zero_comparison (95%)
- sizeof_signedness (95%)
- initializer_literals (HIGH)
- empty_vs_size (95%)
- static_variable_scope (HIGH)
- loop_counter_signedness (HIGH)
- data_type_sizing (HIGH)
- argument_eval_order (MEDIUM/HIGH)

**Fixable Patterns - Medium Success:**
- ternary_vs_ifelse (MEDIUM)
- comparison_style (MEDIUM)
- variable_declaration_order (MEDIUM, 30% success for register swaps)
- loop_structure (MEDIUM)
- boolean_index (MEDIUM)

### Source 3: `docs/permuter/EXAMPLES.md`

High-impact examples with detailed before/after:
- GlitchFinder Destructor: 29.4% → 100% (D01)
- error_exit: 61.5% → 100% (C02)
- vorbis_fromdBlook: 16.8% → 97.7% (C03)
- DxRnd::DrawSafeArea: 24.98% → 100% (F01)
- ShortQuat::ToQuat: 11.47% → 99.88% (V03)
- RndFlare::SetSteps: 64.8% → 100% (E01)
- UIListLabel::ElementLabel: 64.4% → 99.3% (V01)

Medium-impact examples:
- HamNavList::SetHighButtonMode: 17% → 100% (U02)
- ScrollSelect::CatchNavAction: 11.47% → 100% (Full Implementation)
- ClipDistMap::CalcHeight: 64.4% → ~99% (C01)
- TypeProps::InsertArrayValue: 99.09% → 100% (V01)
- TransformArea::Load: 98.6% → 100% (O01)
- RndCam::SetViewProj: 98.04% → 100% (I01)

Fine-tuning examples:
- CharFaceServo::Load: 98.8% → 99.5% (U01)
- OSCMessenger::Connect: 98.57% → 100% (U02)
- ObjectDir::RemoveSubDir: 98.125% → 100% (Direct Offset Access)

At-limit examples:
- FastInvert: 99.45% (REGISTER_SWAP - f30/f31)
- UIList::StartScroll: 99.83% (LINKER_MERGED)
- CharBonesMeshes::PoseMeshes: 99.24% (REGISTER_SWAP)
- UIFontImporter::GetASCIIMinusChars: 80.91% (plateau)

### Source 4: `docs/decomp/patterns.json` (OUTDATED)

Machine-readable format with 36 patterns:
- Uses snake_case IDs (e.g., `unsigned_zero_comparison`, `variable_declaration_order`)
- Does NOT use U01/V01 prefix system
- Contains detection signatures for objdiff
- Has decision tree logic encoded
- Last updated: 2026-01-27

### Source 5: `scripts/master_agent_prompt.md`

~54 lines of pattern content, mirrors PATTERNS.md with additions:
- Decision tree by match percentage
- Phase guidance for agents
- References to pre-computed context

### Source 6: `docs/decomp/TECHNICAL_NOTES.md` (CONTEXT)

31 lessons learned, including patterns not in other docs:
- Lesson 17: Static const for float comparisons
- Lesson 30: VMX128 XMVECTOR parameters
- Lesson 31: 64-bit to 16-bit extraction
- Detailed compiler behavior notes
- Class layout references

---

## 2. Example Mappings

| Pattern ID | Examples (from EXAMPLES.md) | Impact |
|------------|----------------------------|--------|
| D01 | GlitchFinder, ClipDistMap | +37-70% |
| C02 | error_exit | +38.5% |
| C03 | vorbis_fromdBlook | +80% |
| F01 | DxRnd::DrawSafeArea | +75% |
| V03 | ShortQuat::ToQuat | +88% |
| E01 | RndFlare::SetSteps | +35% |
| V01 | UIListLabel::ElementLabel, TypeProps::InsertArrayValue | +35% |
| U02 | HamNavList::SetHighButtonMode, OSCMessenger::Connect | +1-83% |
| C01 | ClipDistMap::CalcHeight | +35% |
| O01 | TransformArea::Load | +1.4% |
| I01 | RndCam::SetViewProj | +2% |
| U01 | CharFaceServo::Load | +0.7% |

---

## 3. Conflicts and Inconsistencies Found

### 3.1 ID System Conflict

Three different ID schemes exist:
1. **PATTERNS.md**: Alphanumeric prefixes (U01, V01, C01, D01, etc.)
2. **patterns.json**: snake_case (unsigned_zero_comparison, variable_declaration_order)
3. **APPENDIX_PATTERNS.md**: ALL_CAPS for unfixable (LINKER_MERGED, BOOL_MASK)

**Resolution needed:** Standardize on one ID system.

### 3.2 Pattern Count Discrepancy

| Source | Fixable | Unfixable | Total |
|--------|---------|-----------|-------|
| PATTERNS.md | 13 working + 13 harmful | 4 | 30 |
| APPENDIX_PATTERNS.md | ~13 | 9 | ~22 |
| patterns.json | 26 | 10 | 36 |
| TECHNICAL_NOTES.md | 31 lessons | - | 31 |

**Resolution needed:** Reconcile counts, deduplicate.

### 3.3 Success Rate Differences

| Pattern | PATTERNS.md | APPENDIX_PATTERNS.md |
|---------|-------------|---------------------|
| Control flow | Not listed as fixable | 70% success |
| Variable reorder | Listed as maybe fixable | 30% success |
| Comparison fixes | - | 50% success |

**Resolution needed:** Standardize success rates with empirical data.

### 3.4 Categorization Conflicts

- **CONTROL_FLOW**: Listed as "Unfixable (Compiler-Driven)" in PATTERNS.md, but APPENDIX_PATTERNS.md says 70% success rate
- **REGISTER_SWAP**: PATTERNS.md says unfixable, APPENDIX_PATTERNS.md says 30% fixable via declaration reorder

**Resolution needed:** Clear definitions for when a pattern is "unfixable" vs "rarely fixable".

---

## 4. Gaps Identified

### 4.1 Patterns Missing from Primary Docs

These patterns appear in patterns.json or TECHNICAL_NOTES.md but lack entries in PATTERNS.md:

| Pattern | Source | Should Have ID |
|---------|--------|----------------|
| loop_counter_signedness | APPENDIX, patterns.json | L05? |
| string_iteration_signedness | APPENDIX, patterns.json | S01? |
| static_symbol_order | patterns.json | S02? |
| static_init_mismatch | patterns.json | S03? |
| obj_mem_line_number | patterns.json | M01? |
| wrapper_struct_padding | patterns.json | W01? |
| thread_function_pointer | patterns.json | T01? |
| freelist_pointer_chain | patterns.json | P03? |
| sequential_if_return | patterns.json | C04? |
| dot_product_order | patterns.json | D02? |
| bitwise_alignment | patterns.json | B01? |
| control_flow_structure | patterns.json | C05? |

### 4.2 Missing from All Docs

From TECHNICAL_NOTES.md lessons not covered elsewhere:
- Lesson 17: Static const for float comparisons
- Lesson 30: VMX128 XMVECTOR parameter handling
- Intentional bugs in original code (must preserve)

### 4.3 Missing Statistics

- Per-pattern attempt counts from decomp.db
- Time-to-fix estimates
- Module-specific pattern prevalence

---

## 5. Recommendations for Consolidation

### Proposed Structure: `docs/decomp/patterns/`

```
docs/decomp/patterns/
├── INDEX.md              # Quick reference table, links to all
├── fixable/
│   ├── comparison.md     # U01, U02, comparison_style
│   ├── casting.md        # C01, C02, C03
│   ├── declarations.md   # V01, V02, V03, D01
│   ├── control-flow.md   # E01, ternary_vs_ifelse, loop_structure
│   ├── operators.md      # O01, I01, F01
│   └── misc.md           # Other patterns
├── unfixable/
│   ├── linker.md         # LINKER_MERGED, LTCG_POOLING
│   ├── compiler.md       # BOOL_MASK, ASSERT_REVS, FMADDS
│   └── register.md       # REGISTER_SWAP, COMMUTATIVE_SWAP
└── harmful/
    └── avoid.md          # L01-L04, V04-V06, A01-A04, P01-P02
```

### Proposed ID Convention

- **Fixable patterns**: Two-letter category + two-digit number
  - `CP` = Comparison (CP01, CP02...)
  - `CS` = Casting (CS01, CS02...)
  - `VD` = Variable/Declaration (VD01, VD02...)
  - `CF` = Control Flow (CF01, CF02...)
  - `OP` = Operators (OP01, OP02...)

- **Unfixable patterns**: `UF_` prefix + descriptive name
  - `UF_LINKER_MERGED`
  - `UF_BOOL_MASK`

- **Harmful patterns**: `AVOID_` prefix + descriptive name
  - `AVOID_MEMBER_ALIAS`
  - `AVOID_CHILD_POINTER_IN_LOOP`

### Content Migration Plan

1. **Keep separate (link from):**
   - `docs/decomp/TECHNICAL_NOTES.md` - context and lessons learned
   - `scripts/master_agent_prompt.md` - agent context

2. **Migrate and deprecate:**
   - `docs/permuter/PATTERNS.md` → `docs/decomp/patterns/fixable/`
   - `docs/permuter/EXAMPLES.md` → inline in pattern files
   - `docs/meta-strategy/APPENDIX_PATTERNS.md` → `docs/decomp/patterns/unfixable/`

3. **Delete:**
   - `docs/decomp/patterns.json` - outdated, replace with structured markdown
   - `docs/decomp/PATTERN_DATABASE.md` - integration docs for deleted file

---

## 6. Statistics Summary

### From PATTERNS.md Database (2026-01-28)

47,213 functions analyzed:
- REGISTER_SWAP: 607 functions, avg 92.1% match
- LINKER_MERGED: 400 functions, avg 96.2% match
- CONTROL_FLOW: 134 functions, avg 91.8% match
- BOOL_MASK: 9 functions, avg 92.8% match
- COMPARISON_STYLE: 7 functions, avg 93.2% match

### From EXAMPLES.md (2026-01-28)

Overall project stats:
- Total functions: 47,213
- Perfect matches (100%): 22,055 (46.7%)
- Near-perfect (99%+): 23,015 (48.8%)
- Average match: 98.2%
- Generated patches: 840
- Patches at 100%: 476 (56.7%)

### ROI Rankings (from PATTERNS.md)

| Rank | Pattern | Time | Success | Impact |
|------|---------|------|---------|--------|
| 1 | D01 Destructor | 2 min | 100% | +37-70% |
| 2 | Full Implementation | 30 min | 90%+ | +50-85% |
| 3 | U02 Cast Forcing | 5 min | 100% | +1-50% |
| 4 | F01 FMA Order | 5 min | 98%+ | +1-75% |
| 5 | V01 Variable Extraction | 3 min | 95%+ | +1-35% |
| 6 | O01 Operator Overload | 2 min | 100% | +1-2% |
| 7 | I01 Inline Assignment | 2 min | 95%+ | +1-2% |
| 8 | Ternary to if/else | 10 min | 75%+ | +5-10% |
