# OBJ_CLASSNAME .rdata Mismatch Detection

**Date**: 2026-03-18
**Context**: Root cause analysis of SuperEasyRemixer choreography init failure led to discovery that `OBJ_CLASSNAME(OriginalChoreoRemixer)` was wrong — should be `OBJ_CLASSNAME(SuperEasyRemixer)`. objdiff reported 100% match on all affected functions.

---

## The Problem

`OBJ_CLASSNAME(X)` generates `StaticClassName()` and `ClassName()` which return a string literal. When `X` is wrong, the string is wrong, but the **machine code is identical** — just loading a different address from `.rdata`. objdiff normalizes relocations and reports 100% match.

This class of bug is invisible to instruction-level matching:
- Wrong string constants in `OBJ_CLASSNAME`
- Wrong enum values of the same size
- Wrong default parameter values that inline identically

### Why objdiff misses it

The report generator (`objdiff-cli/src/cmd/report.rs:364`) uses:
```rust
function_reloc_diffs: diff::FunctionRelocDiffs::None,
```

This **ignores all relocation differences** when computing match percentages. The interactive diff viewer (`objdiff diff`) uses `DataValue` by default and WOULD show the mismatch, but nobody manually inspects macro-generated boilerplate functions.

---

## Detection Script

`scripts/analysis/check_obj_classname.py` cross-references three sources:

1. **C++ headers**: Parses `class X { OBJ_CLASSNAME(Y); }` where X != Y
2. **dc_symbols.txt**: Checks if `X::StaticClassName` and `Y::StaticClassName` exist at different addresses
3. **DTA config**: Checks if `(X (types ...))` exists in ham_objects.dta

### Detection logic

| X::StaticClassName | Y::StaticClassName | Same addr? | Verdict |
|-|-|-|-|
| exists | exists | NO (different) | **BUG** — target returns different strings |
| exists | exists | YES (ICF-merged) | Intentional — same string, OBJ_CLASSNAME(Y) correct |
| exists | not found | — | Unknown — Y is likely a base class without own macro |
| not found | exists | — | Likely ICF-merged, probably correct |

### Results (2026-03-18)

```
6 bugs detected:
  SuperEasyRemixer         → OBJ_CLASSNAME(OriginalChoreoRemixer)  ← CONFIRMED BUG (root cause of choreo init)
  AppLabel                 → OBJ_CLASSNAME(HamLabel)
  AppNavProvider           → OBJ_CLASSNAME(HamNavProvider)
  HamStarsDisplay          → OBJ_CLASSNAME(StarsDisplay)
  AppMiniLeaderboardDisplay → OBJ_CLASSNAME(MiniLeaderboardDisplay)
  NgDOFProc                → OBJ_CLASSNAME(DOFProc)

50 intentional (ICF-merged inner classes)
71 unknown (Rnd* → short name pattern, base class without own macro)
```

### Usage

```bash
python3 scripts/analysis/check_obj_classname.py
```

No dependencies beyond Python 3 stdlib.

---

## Impact of Each Bug

### SuperEasyRemixer (CONFIRMED — full cascade documented)

`OBJ_CLASSNAME(OriginalChoreoRemixer)` → `SetType("easeup_remixer")` crashes → entire DTA-driven choreography init disabled → C++ hack needed → 13 downstream defensive guards. See `2026-03-18-choreo-remixer-init-lifecycle.md`.

### AppLabel, AppNavProvider, HamStarsDisplay, AppMiniLeaderboardDisplay

These are `lazer/meta_ham/` classes (DC3-specific wrappers around engine classes). If their DTA configs reference the wrapper class name (e.g., `(AppLabel (types ...))`), `SetType` would fail silently on Xbox too. These may have vestigial type configs, or the configs may use the parent class name. Needs investigation.

### NgDOFProc

`OBJ_CLASSNAME(DOFProc)` — the NG (next-gen) implementation reports as the base DOFProc. If there's a `(NgDOFProc (types ...))` DTA config, this is a bug. If not, it may be intentional (NG class acts as drop-in replacement).

---

## Medium-Term: objdiff report.json with DataValue mode

The report generator hardcodes `FunctionRelocDiffs::None`. Changing to `FunctionRelocDiffs::DataValue` would:

1. **Catch this class of bug automatically** — functions with wrong string literals would show <100%
2. **Reduce some match percentages** where relocations point to different-named symbols (could be noisy for address layout differences)
3. **Require testing** to understand false positive rate

### Proposed change

In `objdiff-cli/src/cmd/report.rs:364`:
```rust
// Before:
function_reloc_diffs: diff::FunctionRelocDiffs::None,

// After (option A — strictest):
function_reloc_diffs: diff::FunctionRelocDiffs::DataValue,

// After (option B — name-only, less noisy):
function_reloc_diffs: diff::FunctionRelocDiffs::NameAddress,
```

`NameAddress` checks that relocation target symbol names match — this would catch `StaticClassName` returning the wrong string (the static local has the correct mangled name but different data, so... actually no, `NameAddress` checks the symbol name of the relocation target, which is the static local variable. The static local's mangled name includes the enclosing function name but NOT the string content.)

Actually, **`DataValue` is required** for this specific bug. `NameAddress` wouldn't catch it because the relocation target symbol name (`?name@?1??StaticClassName@SuperEasyRemixer@@...`) is the same in both builds — it encodes the function name, not the string value. Only `DataValue` mode follows the relocation and compares the actual bytes.

### Changes made (2026-03-18)

1. `objdiff-cli/src/cmd/report.rs:364` — changed default from `None` to `DataValue`
2. `tools/project.py:1521` — changed `report_raw` rule from `name_address` to `data_value`
3. Cleared report cache to force regeneration

### Results (2026-03-18)

The `DataValue` mode affects **fuzzy/raw match percentages only**, not normalized percentages (which is what progress tracking uses). The normalized match already strips all relocation info.

Comparison:
- **Normalized changes: 0** — progress tracking completely unaffected
- **Fuzzy/raw changes: 11,338** — mostly small functions (static initializers, operator delete, global accessors) dropping from 100% to 95-96%

The raw drops are primarily in functions that reference global/static data whose addresses differ between our build layout and the target. This is expected noise — different linker layout produces different addresses for the same data.

### Conclusion

The `DataValue` mode change is **safe for the report** — it doesn't affect any normalized match percentages that drive progress tracking. The raw/fuzzy percentages now reflect data-level differences, which provides additional diagnostic signal without disrupting the workflow.

### Why DataValue mode can't distinguish bugs from noise

All 366 `StaticClassName` functions show ~98.4% fuzzy — whether the name is correct or wrong. This is because the `Symbol` static local contains a hash field computed from the string's address (not content), plus a `const char*` pointer. Both fields differ between any two builds due to address layout. The percentage drop is uniform regardless of correctness.

A relocation-aware approach (masking pointer bytes, comparing only non-relocation bytes like hash/length) would still fail because the hash is address-dependent.

### The right tool for each job

| Detection target | Tool | Why |
|---|---|---|
| `OBJ_CLASSNAME` bugs | `check_obj_classname.py` | Cross-references headers vs target symbol table — semantic check |
| Wrong string constants | Future: COFF .rdata string extractor | Compare actual string bytes, not instruction match% |
| Wrong enum/flag values | Future: per-symbol data diff | Compare non-relocation bytes within matched data symbols |
| Address layout noise | Ignore | Structural difference between builds, not a bug |

`check_obj_classname.py` remains the primary tool. The `DataValue` objdiff mode is safe to keep (zero normalized impact) but doesn't provide actionable signal for this class of bug.

### What DataValue catches that None misses

- Wrong `OBJ_CLASSNAME` string (this bug)
- Wrong vtable entries (if a virtual function resolves to a different symbol)
- Wrong static data initializers (constructors that embed wrong constants)
- Wrong `SystemConfig` keys (any `static DataArray*` initialized from wrong path)

### What it does NOT catch

- Data differences in sections not referenced by code relocations
- String pool layout differences (different ordering, same content)
- Differences in unreferenced data

---

## Appendix: How Symbol Encoding Works

`OBJ_CLASSNAME(X)` expands to:
```cpp
static Symbol StaticClassName() {
    static Symbol name("X");    // ← string "X" stored in .rdata
    return name;
}
virtual Symbol ClassName() const { return StaticClassName(); }
```

The compiled function:
1. Loads address of static local `name` (relocation to `.data` / `.bss` symbol)
2. Checks static init guard
3. If not initialized: calls `Symbol::Symbol(const char*)` with string literal address (relocation to `.rdata`)
4. Returns `name`

The machine code is identical regardless of string content. The only difference is in `.rdata` — which string literal the relocation points to. This is a pure data-section difference invisible to code-only comparison.
