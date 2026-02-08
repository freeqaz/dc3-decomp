# RhythmBattle::OnBeat Stack-Shape Campaign (2026-02-08)

## Scope

- Function: `RhythmBattle::OnBeat` (`?OnBeat@RhythmBattle@@AAAXXZ`)
- File: `src/system/hamobj/RhythmBattle.cpp`
- Goal: Reduce stack-layout mismatch first (frame `0x760 -> 0x750`, dominant `+16/+8` offset groups), without logic changes.

## Locked Baseline

- Match: `93.3%` (`AT_LIMIT`)
- Frame size: target `0x750`, source `0x760` (`+16`)
- Offset histogram (top):
  - `+16`: `228`
  - `+8`: `165`
  - `+4`: `22`
- Early stack-map anchors (idx ~0-350) confirming `0x74/0x7c` region:
  - `idx 328`: `addi r3, r31, 0x74` vs `0x7c` (`+8`)
  - `idx 717`: same `0x74/0x7c` pattern in `finish_intro` init path

## Experiments Run

All edits were done one-at-a-time with immediate `run_objdiff` + `run_diff_inspect offsets`, and reverted if regressive.

1. Split long-lived `DataNode handled` into two block locals
- Intent: shorten lifetime and free/reuse an 8-byte slot.
- Result: major regression to `92.8%`; offset profile destabilized.
- Decision: reverted.

2. Move `bool b22 = false;` into early local cluster
- Intent: improve byte-local packing around the early locals.
- Result: `93.1%`, no frame improvement, `+16/+8` unchanged.
- Decision: reverted (regression > 0.15% and no stack gain).

3. Add inner scopes around `countInMsg` and `finish_intro` static-Message init/use
- Intent: narrow scratch Symbol/temp lifetime in the `0x74/0x7c` zone.
- Result: no measurable change (`93.3%`, `+16=228`, `+8=165`).
- Decision: reverted.

4. Reorder early scalar/local declaration cluster
- Reordered cluster: `focusPanel`, `goofy`, `inMindControl`, `remainingValue`, `beat`, `i6cc`, `leader` (declaration/assignment split only).
- Intent: align reverse declaration order with target stack map.
- Result: match nominally flat (`93.3%`) but `+8` worsened (`165 -> 171`), frame unchanged.
- Decision: reverted.

## What Was Learned

1. Local lifetime/declaration shaping in current source is not sufficient to collapse the dominant stack mismatch.
2. `DataNode` lifetime surgery is highly codegen-sensitive here and can trigger broad RA/scheduling fallout.
3. The `0x74/0x7c` local scratch mismatch is an indicator, not the root cause: nudging those blocks does not move the global frame.
4. Current residual mismatch remains dominated by compiler-level behavior:
- frame delta `+16` cascade (`228` instructions),
- secondary `+8` cascade (`165` instructions),
- plus known unfixable/near-unfixable noise (ICF merged calls, bool mask patterns, relocation noise).

## Path Forward (Realistic)

### Short answer
There is no reliable source-only micro-shaping path currently demonstrated for fixing this stack misalignment.

### Practical options

1. Accept `OnBeat` as practical `AT_LIMIT` at `93.3%` for now.
2. If revisiting, use a different strategy class (not more declaration/lifetime shuffling):
- Investigate compile-environment/codegen parity levers that affect frame formation globally.
- Use RB3 pairing/reference to identify larger structural patterns the compiler may be responding to (not cosmetic local order tweaks).
- Target non-stack structural mismatches (small real `replace`/control-flow islands) for incremental gains independent of frame-size parity.

### Revisit trigger

Only revisit stack-shape on this function if one of the following changes:
- toolchain/build settings that can affect frame layout,
- new evidence from RB3/source-pairing showing a materially different high-level structure,
- project-wide convention change that alters static local/message construction shape.

## Final State

- Source restored to baseline (no retained code edits in `RhythmBattle.cpp` from this campaign).
- Final validated metrics unchanged from locked baseline:
  - Match `93.3%`
  - Frame `0x760` vs target `0x750`
  - `+16=228`, `+8=165`

