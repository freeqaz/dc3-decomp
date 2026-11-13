# Lane notes: `src/system/rnddx9/RenderState.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetTextureClamp@RndRenderState@@QAAXIW4ClampMode@1@@Z`

`RndRenderState::SetTextureClamp` -- moved verbatim from `src/system/rnddx9/RenderState.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 107-141):

```text
// OPEN (w26-f7): 100 normalized, fuzzy 99.706 -- ONE diff_arg row of 34, the
// `or` that stores m_Mask[3] in the THIRD clamp group.  Our three groups are
// the same source statement three times and emit `or r11, r7, r11` (mask
// first) in all three; the image emits that in groups 1 and 2 and
// `or r11, r11, r7` (the `ld` result first) in group 3.  Dest and both operands
// are the same registers -- only the commutative operand order differs.
//
// Tap (`c2rs stage cands --regasg`, fn 18) says the three `or` tuples are
// keyed identically, which is why we cannot distinguish them.  `CQT cqlo3`
// print slots r5 / r15 / r25 are the three `or`s; each is `2cb` with two
// folded single-use temps whose definers are one `2ca` and one `2c9`:
//   r5  : T135 <2ca slot r7 > T131 <2c9 slot r6 >
//   r15 : T157 <2ca slot r17> T91  <2c9 slot r16>
//   r25 : T86  <2ca slot r27> T88  <2c9 slot r26>
// and the three `2ca` parents differ only in the constant they fold
// (`k07[1c00,0x10,ffe3ff]`, `[e000,0x10,ffff1fff]`, `[7,0x10,fff8ffff]`); the
// memory leaf is `M6{0001c008}` and the named `mask` is `V6{000100a0}` in all
// three.  So the image's group 3 was a different IL shape, not a different
// sort of the same shape.
//
// 17 spellings attempted this pass (12 measured, 5 dropped because the target
// text occurs 3 times and a uniform substitution moves all three groups): all
// inert (99.706) or worse.  Nothing that keeps the code changes one of the
// three groups relative to the others:
//   `m_Mask[3] = m_Mask[3] | mask;` for all three (skipped, 3 sites) /
//   the third only: `= ... | mask` (inert), `|= (UINT64)mask` (inert),
//   `|= mask >> 0` (inert), `UINT64 m3 = mask; |= m3` (inert),
//   `|= (mask & 0xffffffffffffffffull)` (inert); `unsigned long long mask`
//   (inert); `UINT64 mask = 1ull << (63 - sampler)` 75.29; mask store moved
//   before the fetch-constant store 61.00 (third group only: 81.76);
//   `UINT64 &maskWord = dev->m_Pending.m_Mask[3]` 81.76 (third only: 81.76);
//   `DxRnd &rnd = TheDxRnd; dev = rnd.Device();` (inert -- the binding folds).
// Open question: which source difference makes ONE of three textually
// identical statements a different IL shape while emitting the same
// instruction, or whether the image used a helper that was inlined twice.
```
