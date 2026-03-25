# 08 — Version Tracking / Program Diff

Priority: **Tier 2**  
Readiness: **Spike required**  
Effort: **Medium**

## Why This Matters

Version Tracking could give us a structural view of “before vs after” changes that objdiff does not.

## What The Review Found

The capability is real, but the current plan simplified the execution model too much.

Relevant shipped scripts:

- `AutoVersionTrackingScript.java`
- `OpenVersionTrackingSessionScript.java`
- `SetAutoVersionTrackingOptionsScript.java`

### Important constraints from the stock script

`AutoVersionTrackingScript.java` expects:

- a destination program already opened/processed in the project
- a source program already present in the project
- a VT session created in a project folder

This is a **project/session workflow**, not a cheap ad hoc diff of two random `.obj` files.

## Recommended Scope

### Phase 0 — Feasibility spike

Prove a headless session can be created for:

- one source program
- one destination program
- one scratch project

Output should be a saved VT session plus a short textual summary.

### Phase 1 — Wrapper

If the spike works, create:

- `tools/ghidra/version_diff.py`

Responsibilities:

- import or locate both programs in a scratch project
- run `AutoVersionTrackingScript.java` headlessly
- summarize match counts and correlator output

## Non-Goals For V1

- running this on every commit
- treating VT as a drop-in replacement for `report.json`
- promising useful results for every individual `.obj`

## When It Is Most Useful

- large systemic changes
- before/after linked binaries
- cases where instruction-level diffs are noisy but structure may still be close

## Acceptance Criteria

- One reproducible headless VT session can be created and reopened.
- The wrapper can emit a machine-readable summary of the session.
- The workflow documents project layout and artifact locations clearly.
