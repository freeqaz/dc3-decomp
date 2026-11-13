# Lane notes: `src/system/flow/FlowSetProperty.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/flow/FlowSetProperty.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 61-77; nearest following definition: `FlowSetProperty::~FlowSetProperty`):

```text
// w21-ap: ?EaseStairstep@@YAMMMM@Z (body in math/Easing.h, which this lane
// may not edit) stops at 96.15 normalized, 3 rows: the image computes
// `fdivs f31, f28, f31` (1/steps into a callee-saved FPR) right after
// floor() and before the EasePolyInOut call; ours sinks it to `fdivs f13`
// after the call.  Standalone cl.exe probe (/fp:fast, /O1): the ONLY one of
// 13 spellings that reproduces the image byte for byte is TWO divisions,
//     return fl / steps + EasePolyInOut(x - fl, power, 0.0f) / steps;
// which MSVC folds into one reciprocal and (p + fl) * inv.  Not shipped:
// natively (clang, no fast-math) that is two rounded divisions plus an add,
// while the image -- and the current code -- compute (p + fl) * (1/steps), so
// it would move native rounding AWAY from the image.  Inert: named reciprocal
// before/after floor(), `/ steps` once (plain fdivs), stepSize first with
// x = t / stepSize, distributed fl*inv + p*inv (refolded or fmadds), an extra
// local for the call result.  The MakeString row is an ICF-folded template
// name (char[19]/char[5] vs our char[45]/char[17], same body), not a bug.
// w24-c2: SHIPPED the two-division spelling in math/Easing.h -> 100.000
// (whole-binary 1 UP / 0 DOWN). Native rounding note kept next to the body.
```
