# 09 — FunctionID for Library Detection

Priority: **Tier 2**  
Readiness: **Spike required**  
Effort: **Medium-High**

## Why This Matters

Library/source classification would help prioritization, especially for:

- Xbox/XDK code
- STL or support libraries
- imported engine code that is not worth bespoke decomp effort

## What The Review Found

The FunctionID subsystem exists and the relevant pieces are present:

- `CreateEmptyFidDatabase.java`
- `FunctionIDHeadlessPrescript.java`
- `FunctionIDHeadlessPostscript.java`
- `ImportMSLibs.java`
- `CreateMultipleLibraries.java`

But the current plan understated the ingestion problem.

### Important corrections

- `ImportMSLibs.java` is interactive and oriented around Visual Studio COFF library import.
- It is not the right half-day headless ingestion path for our Xenon use case.
- Function ID is language/compiler sensitive. A useful DB depends on having a compatible PowerPC/Xenon library corpus.

## Recommended Plan

### Phase 0 — Corpus feasibility

Answer two questions before committing engineering time:

1. Do we have importable Xbox/XDK or other library objects in a form Ghidra can ingest usefully?
2. Do those imports line up with the Xenon language/compiler combination closely enough for FID matching to be meaningful?

### Phase 1 — FID DB creation and ingestion

Only if Phase 0 passes:

- create a dedicated FID database
- ingest one small library corpus
- run FID against DC3 and inspect hit quality

This may use:

- `CreateEmptyFidDatabase.java`
- `CreateMultipleLibraries.java`
- pre/post scripts for headless analysis

## Non-Goals For V1

- full XDK coverage
- automatic game/engine/SDK classification for the entire binary
- replacing BSim

## Positioning

FID is best treated as a **precision tool for highly compatible libraries**, not as the first-line cross-binary similarity system. BSim should come first.

## Acceptance Criteria

- A small compatible library corpus is imported and ingested successfully.
- FID produces at least some believable matches on DC3.
- We decide, based on actual hit quality, whether to continue or drop the feature.
