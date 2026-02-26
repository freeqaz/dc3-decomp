# Linked Binary Verification (Re-Split Comparison)

**Status:** Idea — not yet implemented

## Concept

Split the decomp's linked XEX with jeff (the same tool that splits the original), then compare the two sets of split objects in objdiff. This gives ground-truth match percentages that account for link-time effects invisible to `.obj`-level comparison.

```
Original binary ──jeff split──→ split .obj ──┐
                                             ├── objdiff ──→ true match%
Decomp XEX ─────jeff split──→ re-split .obj ─┘
```

## What This Fixes

**ICF-merged functions.** Currently, ICF-merged functions show 0% in objdiff because the split object at the merged address contains the "winner" function's body. objdiff compares your Function B against Function A's code. A re-split comparison sidesteps this — both sides have ICF applied, so merged functions compare correctly.

**COMDAT resolution.** The linked binary has one copy of each COMDAT symbol (the linker picked a winner). No ambiguity about which definition is being compared.

**String/data references.** Resolved to actual addresses in both binaries, not dangling unresolved zeros on one side.

## Why Not Use It As the Primary Workflow

| Concern | Detail |
|---------|--------|
| **Speed** | Adds a full link + re-split to the feedback loop. Current `.obj` comparison is instant after compile. |
| **Granularity** | Linked binary hides `.obj`-level problems (wrong COMDAT selection, duplicate resolution, section layout). |
| **Address delta** | The 18.8 KB `.text` size delta shifts every address. Jeff's relocation reconstruction (via MAP file) should handle this, but needs the decomp MAP as input. |
| **Build complexity** | Requires a working link (currently needs `/FORCE` flags) and XEX packaging before comparison can happen. |

## Proposed Usage

Use as a **second-pass verification tool** alongside the existing workflow:

```
Day-to-day:    decomp .obj  vs  split .obj      (fast, per-function)
Verification:  re-split XEX vs  split original   (slow, ground truth)
```

Run the verification pass at milestones — "are we actually at N% when accounting for ICF and link-time effects?" This gives the real number, free of ICF false negatives.

## Implementation Sketch

1. Build the decomp XEX normally (link + `build_xex.py`)
2. Run jeff in split mode against the decomp XEX, using the decomp MAP for function boundaries
3. Point objdiff at both split directories (original splits vs decomp re-splits)
4. Compare — functions that match at the linked level are truly matching

**Open question:** does jeff need any changes to split a decomp XEX cleanly, or does it already handle arbitrary XEX inputs? The decomp XEX has the same format as the original, so it should work — but the different section layout (`.text$x` subsections, 18.8 KB delta) may need attention.

## Relationship to Clean Link Project

This tool becomes more useful as the clean link project progresses:
- After M1 (drop `/FORCE:UNRESOLVED`): re-split comparison is meaningful — no address-zero artifacts
- After M3 (1:1 XEX): re-split comparison should show 100% on all functions

See [CLEAN_LINK_PROJECT.md](CLEAN_LINK_PROJECT.md) for the link error elimination roadmap.
