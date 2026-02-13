# Unicorn Function Runner: Value Demonstration

How the unicorn runner adds value alongside objdiff and Ghidra/m2c.

## The Core Question

**objdiff** tells you WHAT's different (instruction diffs). **Unicorn** tells you IF it matters (behavioral equivalence under zeroed inputs). These answer different questions.

## Scenario A: False Positive Filtering (Primary Value)

objdiff flags functions as LIKELY_FIXABLE or NEEDS_INVESTIGATION based on instruction diffs. But many of those diffs are cosmetic — linker symbol names, register allocation, static variable guards. Unicorn proves behavioral equivalence, saving time.

### UITransitionHandler: 11 functions saved from investigation

```
$ python3 -m scripts.unicorn_runner.diagnose --unit system/ui/UITransitionHandler --batch

=== system/ui/UITransitionHandler (13 functions) ===
  SKIP   99.7%  UITransitionHandler::~UITransitionHandler       objdiff=LIKELY_FIXABLE        unicorn=EQUIVALENT
  SKIP   99.5%  CopyHandlerData                                 objdiff=AT_LIMIT              unicorn=EQUIVALENT
  SKIP   99.4%  SaveHandlerData                                 objdiff=AT_LIMIT              unicorn=EQUIVALENT
  SKIP   99.3%  UpdateHandler                                   objdiff=NEEDS_INVESTIGATION   unicorn=EQUIVALENT
  SKIP   97.5%  SetInAnim                                       objdiff=LIKELY_FIXABLE        unicorn=EQUIVALENT
  SKIP   97.5%  SetOutAnim                                      objdiff=LIKELY_FIXABLE        unicorn=EQUIVALENT
  ... (5 more SKIPs)
  DONE  100.0%  HasTransitions                                  objdiff=COMPLETE              unicorn=EQUIVALENT
  DONE  100.0%  LoadHandlerData                                 objdiff=COMPLETE              unicorn=EQUIVALENT

  Summary: 2 DONE, 11 SKIP, 0 FIX
  Without unicorn: objdiff flagged 9 as needing work → 9 are actually equivalent
```

**Result**: This unit is done. All 11 non-100% functions are behaviorally equivalent. No further work needed.

### Profile: 6 of 7 flagged functions are equivalent

```
$ python3 -m scripts.unicorn_runner.diagnose --unit system/meta/Profile --batch

=== system/meta/Profile (12 functions) ===
  FIX    99.5%  Profile::Profile(int)                           objdiff=NEEDS_INVESTIGATION   unicorn=DIVERGENT
  SKIP   99.8%  scalar deleting destructor                      objdiff=LIKELY_FIXABLE        unicorn=EQUIVALENT
  SKIP   99.4%  ~Profile                                        objdiff=NEEDS_INVESTIGATION   unicorn=EQUIVALENT
  SKIP   99.0%  Handle                                          objdiff=NEEDS_INVESTIGATION   unicorn=EQUIVALENT
  SKIP   98.7%  SetSaveState                                    objdiff=NEEDS_INVESTIGATION   unicorn=EQUIVALENT
  SKIP   97.5%  GetName                                         objdiff=NEEDS_INVESTIGATION   unicorn=EQUIVALENT
  ... (5 DONE)

  Summary: 5 DONE, 6 SKIP, 1 FIX
  Without unicorn: objdiff flagged 7 as needing work → 6 are actually equivalent
```

**Result**: Only the constructor needs attention. 6 functions that objdiff flagged are false positives.

### ContentMgr: 10 of 16 flagged functions are equivalent

```
$ python3 -m scripts.unicorn_runner.diagnose --unit system/os/ContentMgr --batch

  Summary: 14 DONE, 10 SKIP, 7 FIX
  Without unicorn: objdiff flagged 16 as needing work → 10 are actually equivalent
```

**Result**: Focuses work on the 7 actual divergent functions instead of 16.

## Scenario B: Batch Triage

Run batch mode on a unit to get an instant overview of what needs attention.

### keygen_xbox: 9 of 16 flagged are equivalent

```
$ python3 -m scripts.unicorn_runner.diagnose --unit keygen_xbox --batch

=== keygen_xbox (20 functions) ===
  FIX    96.7%  shuffle1                objdiff=MAYBE_FIXABLE     unicorn=DIVERGENT
  FIX    96.2%  shuffle3                objdiff=MAYBE_FIXABLE     unicorn=DIVERGENT
  FIX    95.6%  shuffle2                objdiff=MAYBE_FIXABLE     unicorn=DIVERGENT
  ... (5 more FIX)
  SKIP   98.8%  getKey                  objdiff=NEEDS_INVESTIGATION  unicorn=EQUIVALENT
  SKIP   95.6%  asciiDigitToHex         objdiff=MAYBE_FIXABLE     unicorn=EQUIVALENT
  SKIP   94.6%  roll                    objdiff=MAYBE_FIXABLE     unicorn=EQUIVALENT
  SKIP   94.2%  mash                    objdiff=MAYBE_FIXABLE     unicorn=EQUIVALENT
  SKIP   77.3%  random                  objdiff=MAYBE_FIXABLE     unicorn=EQUIVALENT
  SKIP   69.8%  opaquePredicate         objdiff=NEEDS_INVESTIGATION  unicorn=EQUIVALENT
  ... (3 DONE)

  Summary: 3 DONE, 9 SKIP, 8 FIX
  Without unicorn: objdiff flagged 16 → 9 are equivalent
```

## Scenario C: Bug Localization (Limited by Zeroed Inputs)

When unicorn reports DIVERGENT, it gives the specific call index and offset where behavior diverges. However, in practice, most divergences fall into two categories:

1. **Call count mismatches from zeroed memory** — loops iterate differently when linked list pointers are NULL, producing different call counts. This doesn't represent a real bug.

2. **Register allocation artifacts** — the decomp computes the same values but through different registers. When those values are passed as function arguments, they can differ slightly due to how register allocation interacts with address computations.

Both patterns mean the DIVERGENT verdict isn't directly actionable for code fixes. The tool's value is primarily in **confirming equivalence** (SKIP), not in localizing bugs (FIX).

### When DIVERGENT IS actionable

DIVERGENT results are most useful when:
- A function has a real logic error (wrong branch, missing call, wrong constant)
- The divergence shows a specific call argument mismatch that corresponds to a known instruction diff in objdiff
- The function doesn't loop over dynamic data structures (avoiding zeroed-memory artifacts)

## Usage

### Single function diagnosis
```bash
python3 -m scripts.unicorn_runner.diagnose --unit system/meta/Profile \
    --symbol "??0Profile@@QAA@H@Z"
```

### Batch unit triage
```bash
python3 -m scripts.unicorn_runner.diagnose --unit system/ui/UITransitionHandler --batch
```

### When to use unicorn vs objdiff alone

| Situation | Use |
|-----------|-----|
| Quick check on a single function | objdiff alone |
| Deciding which functions to work on | unicorn batch (diagnose --batch) |
| Unit is "done" but has non-100% functions | unicorn confirms behavioral equivalence |
| Function has LINKER_MERGED diffs | unicorn — merged calls are cosmetic |
| Function has register swaps | unicorn — confirms same behavior despite different registers |
| Need to understand specific instructions | objdiff with --include-instructions |

## Limitations

1. **Zeroed inputs**: All memory starts at zero. Functions that branch on member values, iterate linked lists, or dereference pointers may follow different paths than real execution. DIVERGENT doesn't always mean "broken."

2. **Trampoline abstraction**: External function calls go to stubs that log arguments and return 0. Side effects (memory writes from called functions) are not simulated.

3. **One execution path**: With zeroed inputs, only one path through the function is tested. A function could be EQUIVALENT on the zeroed path but divergent with other inputs.

4. **Symbol resolution**: When decomp and original use different symbol names for the same entity (common with linker-generated names), the trampoline addresses differ, which can cause false DIVERGENT results.

## Integration with Decomp Workflow

The recommended workflow for unit triage:

1. `diagnose.py --batch` on the unit
2. Mark SKIP functions as "at limit" or "cosmetic diffs only"
3. Focus decomp effort on FIX functions
4. For each FIX: use objdiff `--include-instructions` to understand the actual diff
5. After fixing: re-run `diagnose.py` to confirm EQUIVALENT
