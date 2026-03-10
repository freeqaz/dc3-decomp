# Synthesis Engine

The synthesis engine is the long-term vision for closing the gap on DC3's 4,403 AT_LIMIT
functions (12.9%). Rather than treating the compiler as a black box and brute-forcing source
variations, we reverse-engineer the compiler itself to understand *why* it makes specific
codegen decisions, then use that knowledge to either:

1. **Fix source** — knowing the exact heuristic, write source that triggers the desired path
2. **Patch binaries** — for truly unfixable patterns, generate targeted .obj patches
3. **Synthesize from spec** — given a target instruction sequence, work backwards through
   the compiler's decision tree to find source that produces it

## Architecture

```
                    ┌─────────────────────────┐
                    │   Synthesis Engine       │
                    │                         │
                    │  ┌───────────────────┐  │
                    │  │ Compiler Model    │  │ ← RE'd from c2.dll
                    │  │  • Regalloc rules │  │
                    │  │  • Inline thresh  │  │
                    │  │  • Peephole table │  │
                    │  │  • Pass ordering  │  │
                    │  └────────┬──────────┘  │
                    │           │              │
                    │  ┌────────▼──────────┐  │
                    │  │ Decision Oracle   │  │ ← predicts codegen choices
                    │  │  • Will this inline?│ │
                    │  │  • Which register? │  │
                    │  │  • Which peephole? │  │
                    │  └────────┬──────────┘  │
                    │           │              │
                    │  ┌────────▼──────────┐  │
                    │  │ Source Synthesizer │  │ ← generates candidate source
                    │  │  • Guided permuter│  │
                    │  │  • Pattern library │  │
                    │  │  • Constraint SAT │  │
                    │  └───────────────────┘  │
                    └─────────────────────────┘
```

## Components

| Component | Status | Doc |
|-----------|--------|-----|
| MSVC Compiler RE | Exploration | [MSVC_ROADMAP.md](MSVC_ROADMAP.md) |
| Differential Testing | Not started | [DIFFERENTIAL_TESTING.md](DIFFERENTIAL_TESTING.md) |
| Decision Oracle | Design phase | — |
| Guided Permuter | Existing (scripts/permuter/) | — |
| Binary Patching | Existing (obj_*_patcher.py) | — |

## How It Connects

The synthesis engine sits between the existing permuter and the existing objdiff pipeline:

```
Current workflow:
  Source → permuter (brute force) → compile → objdiff → score

Synthesis engine workflow:
  Target asm → compiler model (predict decisions) → oracle (constrain search) →
  guided permuter (targeted mutations) → compile → objdiff → score
```

The compiler model dramatically prunes the search space. Instead of trying all possible
source mutations, we only try mutations that the model predicts will change the specific
codegen decision causing the mismatch.

## Roadmap

See individual component docs for detailed plans:
- [MSVC_ROADMAP.md](MSVC_ROADMAP.md) — Compiler RE plan and timeline
- [DIFFERENTIAL_TESTING.md](DIFFERENTIAL_TESTING.md) — Empirical codegen mapping
