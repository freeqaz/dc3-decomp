# Lane notes: `src/system/hamobj/HamScrollBehavior.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Update@HamScrollBehavior@@QAAXM@Z`

`HamScrollBehavior::Update` -- moved verbatim from `src/system/hamobj/HamScrollBehavior.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 291-308):

```text
    // Scroll speed anim
    // w7-by (96.291664 -> 100.0): the w7-ai note below was right about the
    // shape and stopped one step short.  The named `anim` local gives retail's
    // unsigned test on the callee-saved r29 (cmplwi cr6, r29, 0x0 at
    // 0x8248B738; the w7-ai note cited 0x8248B928, a unit-relative offset), and
    // the r28<->r29 swap it seemed to cost only existed while
    // a `float shift` local carried the result to ONE SetFrame call after the
    // switch: with `anim->SetFrame(expr, 1.0f)` written in every arm, MSVC
    // materialises f1/f2 per arm and cross-jumps into the merged tail at
    // three different points (0x8248B7F8 / 0x8248B7FC / 0x8248B804), the
    // swell arms load anim's vtable BEFORE their CalculateSwell call
    // (`lwz r28, 0x0(r29)` at 0x8248B7A8 and 0x8248B7D8), and both registers land where
    // retail has them.
    // NOTE (w7-ai, superseded): Retail loads mScrollSpeedAnim into the
    // callee-saved r29 and tests it UNSIGNED where the ObjPtr expression gave
    // a volatile r11 and a SIGNED cmpwi.  A named `RndAnimatable *anim` alone
    // produced the cmplwi but swapped r28<->r29 against a single call after a
    // `float shift` local: 95.8% with the local vs 96.3% without.
```
