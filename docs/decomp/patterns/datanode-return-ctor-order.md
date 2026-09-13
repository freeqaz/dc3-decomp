# `return 0;` vs `return DataNode(kDataInt, 0);` — the sret store order names the ctor

**Established 2026-09-13** (lane w3-j4) on four functions, each crossing to
`report.json` `match_percent_normalized == 100.0` with zero mismatch rows:
`HamDirector::OnFileLoaded` 99.99234, `HamDirector::OnClipSafeToAdd` 99.10526,
`HamDirector::OnPracticeSafeToAdd` 99.10526, `XboxPurchaser::OnMsg` and
`XboxMultipleItemsPurchaser::OnMsg` 99.95349.

## The observable

A `DataNode`-returning function that is otherwise perfect ends with **two `stw` of the
same zero register into the sret pointer, in the opposite order from ours**, usually with
the `mr r3, <sret>` scheduled between them:

```
  target                       ours
  stw r21, 0x4(r20)            stw r21, 0x0(r20)
  mr  r3, r20                  mr  r3, r20
  stw r21, 0x0(r20)            stw r21, 0x4(r20)
```

`run_objdiff` renders it as `OFFSET_SWAP (0x0,0x4)`, and `run_diff_inspect mode=offsets`
resolves 0x0/0x4 to `DataNode::mValue` / `DataNode::mType`.

## The rule

`DataNode`'s constructors in `src/system/obj/Data.h` do **not** all assign in the same
order, and MSVC emits the two stores in exactly the source order of the ctor body:

| ctor | assigns | store order |
|---|---|---|
| `DataNode()` | `mValue.integer = 0; mType = kDataInt;` | `0x0` then `0x4` |
| `DataNode(int)` | `mValue.integer = i; mType = kDataInt;` | `0x0` then `0x4` |
| `DataNode(DataType, int)` | `mType = ty; mValue.integer = i;` | **`0x4` then `0x0`** |

`return 0;` and `return DataNode();` both select a value-first ctor. When the target
stores `0x4` first with **both** stores taking the same zero register, the original wrote
the two-argument ctor: `return DataNode(kDataInt, 0);`. It is the same two zero stores and
the same observable value — `kDataInt` is 0 — so this is a spelling change, not a
behaviour change.

## Scope, and how to tell it apart from scheduling

This is a *source* difference, not the scheduler reordering two independent stores. In
`HamDirector.cpp` alone the target shows **both** orders: `OnToggleDebugInterests` and
`OnPostProcs` store `0x0` first (with the same `mr r3` interleaved, so the interleave is
not the discriminator), while `OnFileLoaded`, `OnClipSafeToAdd` and `OnPracticeSafeToAdd`
store `0x4` first. One TU, one compiler invocation, two orders — only the source can
differ.

To sweep for the class, grep the split listings for the pair with a one-instruction gap:

```
grep -n 'stw r[0-9]*, 0x4(r[0-9]*)' build/373307D9/asm/**/*.s
```

and keep the hits whose next-but-one line is the same register into `0x0` of the same
base. On a function already near 100 it is a two-minute fix worth the function's whole size.

## Related

The register permutation that usually accompanies it (`r28`↔`r29`↔`r30` on the purchasers,
`r21`↔`r22`/`r23`↔`r25` on the SafeToAdd pair) is a **symptom**: it cleared on its own
when the ctor was corrected. Do not chase it first — see
[fixable-declarations.md](fixable-declarations.md#pre-compute-references-before-clobbering-calls).
