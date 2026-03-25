# 03 — RTTI Class Recovery

Priority: **Tier 1**  
Readiness: **Spike required**  
Effort: **Medium-High**

## Why This Matters

Correct class hierarchy and vtable shape is one of the few changes that can fix or break dozens of functions at once. If we can recover RTTI reliably, it becomes a high-leverage input to header repair and vtable validation.

## What The Review Found

The attractive stock path exists:

- `Ghidra/Features/Decompiler/ghidra_scripts/RecoverClassesFromRTTIScript.java`
- `Ghidra/Features/Decompiler/ghidra_scripts/classrecovery/RTTIWindowsClassRecoverer.java`

But the stock script is **not implementation-ready for Xenon as-is**.

### Blocking detail

`RecoverClassesFromRTTIScript.java` takes the MS/Windows path only when:

- `!isGcc() && isWindows()`

And `isWindows()` is implemented by checking whether the current compiler spec ID contains `windows`.

Our Xenon language definition in `../ghidra/Ghidra/Processors/PowerPC/data/languages/ppc.ldefs` defines:

- language ID `PowerPC:BE:64:Xenon`
- compiler ID `default`

It does **not** expose a `windows` compiler entry for Xenon. That means the stock script will not naturally take the Windows RTTI recovery path for the current DC3 import configuration.

## Conclusion

This is not a “30 minute one-time run.” It is a feasibility spike first.

## Recommended Plan

### Phase 0 — Feasibility spike

Goal:

- prove whether the stock Windows recoverer can be used at all on Xenon with a small compatibility shim

Tasks:

- run the stock script on the imported DC3 program and capture the exact failure/exit path
- verify whether the blocker is only the compiler-spec gate or whether big-endian/Xenon layout assumptions also break deeper in recovery
- inspect whether RTTI analyzer output is present and usable on the target import

Deliverable:

- a short spike note with one of:
  - “patch script gate only”
  - “patch recoverer + datatype parsing”
  - “stock path unusable; build bespoke Xenon RTTI parser”

### Phase 1 — Custom Xenon recovery path

If Phase 0 succeeds, implement a DC3-specific recovery script rather than patching the stock one in-place.

Recommended location:

- `tools/ghidra/rtti_recover.py` as the operator entry point
- custom Java/PyGhidra script under repo-owned tooling, not under upstream Ghidra script directories

Responsibilities:

- invoke or adapt `RTTIWindowsClassRecoverer`
- export recovered classes, bases, offsets, vftable addresses, and virtual functions
- avoid assuming x86 little-endian data layout

### Phase 2 — Export and validation

Produce a cacheable artifact:

- `ghidra_rtti.json` or SQLite

Then build:

- `tools/ghidra/rtti_check.py`

Use it to compare recovered data against headers and known vtable expectations.

## Implementation Guidance

- Treat upstream recoverers as source material, not a drop-in product.
- Keep the recovery/export step separate from header comparison.
- Start with a small class sample set that we already understand manually.

## Go / No-Go Criteria

Proceed only if the spike can recover a small sample of known classes with:

- correct vftable labeling
- believable base offsets
- stable class naming

If not, stop and replace this plan with a narrower bespoke RTTI parser for just the structures we care about:

- Complete Object Locator
- Class Hierarchy Descriptor
- Base Class Array / Descriptor
- vftable discovery

## Acceptance Criteria

- A spike note exists with explicit feasibility outcome.
- At least 3-5 known classes can be exported and inspected from a scriptable artifact.
- The plan is updated after the spike before wider implementation begins.
