# Lane notes: `src/system/os/Joypad_Xinput.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/os/Joypad_Xinput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

File-level (was lines 11-19; nearest following definition: `JoypadInitXboxPCDeadzone`):

```text
// w10-e: the old FLOOR note here was a config defect, not a compiler one.
// symbols.txt spelled gXboxDeadzone `?gXboxDeadzone@?A@@3MA`, but that name was
// never retail ground truth: it entered in 391d1b080 ("name 407 addresses the
// splitter had only numbered"), which named target addresses after OUR symbols
// of the day.  Every other anon-namespace symbol of this TU (gCaps, gCapsValid,
// gCritSection's ??__E/??__F) carries `?A0xf503845b`, and 0x83099C78 lies inside
// this unit's .data split (0x83099C28..0x83099CA4).  Renamed to the TU's hash;
// JoypadInitXboxPCDeadzone 99.29 -> 100 and TranslateStick 99.767 -> 100 with
// zero instruction changes (both were already instruction-identical).
```


## `?ReadSingleXinputJoypad@@YA?AW4JoypadType@@HHPAIPAD11111QAM2QAE@Z`

`ReadSingleXinputJoypad` -- moved verbatim from `src/system/os/Joypad_Xinput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 104-188):

```text
// NEGATIVE RESULT (w7-ap, 2026-09-14, 92.5 canonical): writing the scale as
// `/ 95.0f * -26539.0f` does NOT split the multiply.  The image keeps two
// (0x825FCF78 `fmuls f0, f0, f9` then 0x825FCF7C `fmuls f0, f0, f8`); our
// /fp:fast build rewrites the division to a reciprocal and then folds it into
// the following constant, giving one `fmuls` against -279.3578.  Two rows per
// clamp block remain on that account.
//
// NEGATIVE RESULT (w7-bl, 94.8 canonical): spelling the reciprocal as the
// image's own literal -- `* 0.010526316f * -26539.0f`, i.e. exactly
// __real@3c2c7692 followed by __real@c6cf5600 -- folds the same way, to the
// same single `fmuls` against __real@c38badcf.  Identical 212-row
// 33/3/5/4 split.  So it is not the DIVISION that MSVC is folding; it
// reassociates two adjacent float literal multiplies under /fp:fast whatever
// the first one is spelled as.  This is the whole of the missing 4th literal
// (`lis r6, __real@41d80000` / `lfs f8, __real@c6cf5600`) and the r6/r9/r10
// renumbering of the other three pool bases -- 5 rows, plus 1 per clamp block.
//
// LEVER (w7-br, 94.7 -> 96.7 canonical): the fold is a FRONT-END fold of one
// expression tree, not a back-end reassociation.  Naming the quotient --
// `float scaled = (f3 - 27.0f) / 95.0f;` then `scaled * -26539.0f` in the
// call -- keeps both multiplies (0x825FCF78 / 0x825FCF7C) and restores all
// four pool literals with the image's r11/r10/r9/r6 bases (0x825FCF04..1C).
// A function-local `static const float kPedalScale = -26539.0f` used in the
// single-expression form is NOT a lever here: MSVC constant-propagates it and
// folds identically (same 48 rows, 94.7).
//
// RESIDUAL (w7-bl, 94.8 canonical, 45 of 212 rows) -- the rest, all measured:
//  * the two `-0x8000 - (unsigned short)(int)` arguments (6 rows: 108-110 and
//    140-142).  The image narrows the INPUT (`lhz r11, 0x56(r1)`) and passes
//    the subtraction straight to r4; we load the whole fctiwz word
//    (`lwz r11, 0x54(r1)`) and narrow the OUTPUT with a trailing `extsh`.
//    Both are correct mod 2^16 and TranslateStick sign-extends its own
//    argument at 0x825FCBE0, so the caller-side narrowing is optional and
//    MSVC picks the other end.  Three spellings measured inert at 94.8 with
//    an identical row split: a named `unsigned short` local for the fctiwz
//    result; casting the whole argument `(unsigned short)(-0x8000 - u)`; and
//    (w7-ap, earlier) a named `short` local, which is worse.
//    CLOSED (w7-br, 96.7 -> 100.0 canonical): the conversion is
//    `(unsigned short)(short)(float)`.  With `(int)` in the middle the front
//    end emits a separate zext16 on the int word, and the narrowing pass
//    trades it for an `extsh` on the OUTPUT; with `(short)` the float->short
//    conversion lowers to fctiwz + stfd + a halfword load of 0x56(r1), the
//    zext folds into that load (lha -> lhz), and there is nothing left to
//    narrow at the output.  Two more spellings measured on the way:
//    `(unsigned short)(float)` with no signed step gives the same lhz and no
//    extsh but converts with `fctidz` (MSVC's unsigned path), 2 rows, 99.4;
//    `(short)(0x8000 - (unsigned short)(int)x)` is canonicalised back to the
//    subfic/extsh form, 98.5.
//  * deadzone_apply2 (rows 153-161, closed w7-br): `bool`, not
//    `unsigned char` -- the `unsigned char` costs the `clrlwi` / `subic` /
//    `subfe` re-normalisation before the fourth TranslateStick at
//    0x825FD04C, exactly as the first note says of deadzone_apply.
//  * The remaining 26 rows at 100.0 canonical (99.3 raw) are three register
//    permutations, listed below, plus `lis r11, lbl_83099C7C@h` vs our
//    absolute `lis r11, 0x830a`: the threshold byte at 0x83099C7C has no
//    symbol in config/373307D9/symbols.txt, so the target carries a placeholder
//    relocation and we carry an immediate.  Naming it is a config change, not
//    a source one.
//  * r24 <-> r25 (9 rows): the image copies `buttons` to r24 and `stick_ry`
//    to r25, we do the reverse.  Both are plain `mr` saves of incoming
//    argument registers in the prologue; nothing in the source orders them.
//  * r9 <-> r10 (11 rows): the image colours `rt` r9 and `lt` r10 in the
//    trigger tail, we do the reverse.  Swapping the two declarations in BOTH
//    blocks (the kJoypadAnalog one and the tail one) is byte-inert.
//  * r31 <-> r7 (5 rows): `rx` is held in the callee-saved r31 for us and in
//    the volatile r7 in the image, which is the same one liveness decision
//    seen twice.
//  w20-d: the r24/r25 and r9/r10 permutations and the lbl_83099C7C row were
//  ONE cause -- the threshold was read through a hardcoded image address;
//  naming it (gTriggerThreshold) cleared all three.  At 100 normalized only
//  the 7 r31/r7 rows remain; values checked (image: setup_flag r31, rx r7;
//  ours the reverse; TranslateStick is same-TU, so r7 survives its calls on
//  both sides).  Inert: hoisting `rx` beside `lx`.
//  w25-gq (c2rs tap): a COLOR pop-order question, not a value difference.
//  The narrowed setup_flag (the clrlwi CSE temp, k03 id32, pri -8 tie 106)
//  pops before `rx` (k04 id24, pri -9 tie 92) and takes r7, the only volatile
//  that survives the same-TU TranslateStick calls; rx then falls to r31.  The
//  image needs rx popped first: pri(rx) >= pri(temp) + 1, i.e. +2 on the
//  difference.  Open: the source form that adds one candidate of pressure in
//  the rx-test block (rx referenced, the temp only spanning) or removes one
//  from the post-switch block (temp referenced twice, rx once).  ~37
//  spellings measured inert or code-changing (else-arm param named/ternary/
//  split, deadzone_apply/apply2 respellings, rx decl placement, compare
//  spellings, named drum-arm temps); `int setup_flag` and a split param_a
//  flip the pop order but change the code.
```

Inside the body (was lines 266-291):

```text
    // bool, not unsigned char: 0x825FCEF0 `cntlzw r11, r31` /
    // 0x825FCEF8 `extrwi r8, r11, 1, 26` feeds TranslateStick's bool argument
    // DIRECTLY.  An unsigned char makes MSVC re-normalise it at every call
    // site with the `subic`/`subfe` pair.
    // STILL OPEN (w26-f8, fuzzy 99.78261, canonical 100.0): one two-way
    // exchange between this flag and `rx`.  Image: `clrlwi r31, r31, 24`
    // narrows setup_flag IN PLACE, the flag keeps r31 to the end and `rx`
    // takes the volatile r7.  Ours: the zero-extended value is its own temp
    // (`clrlwi r7, r31, 24`) that takes r7, and `rx` takes r31.  Tap for this
    // TU (c2rs stage cands --regasg --fn ReadSingleXinputJoypad): id32, the
    // narrow temp, is pri -8 tie 106 with an allowed set spanning the volatiles
    // (it takes r7); id24, the `rx` local, is pri -9 tie 92 and nonvolatile-only
    // (it takes r31).  Since the image narrows in place, the image plausibly has
    // ONE candidate here where we have two -- a candidate-count difference, not
    // a key one.  Spellings measured inert at 99.78261 (canonical 100.0):
    // `!setup_flag`, `setup_flag ? false : true`; worse: `unsigned char`
    // deadzone_apply 98.74, `setup_flag = (setup_flag == 0)` 98.33,
    // `(0 == setup_flag)` 98.29.
    //
    // MEASUREMENT NOTE: this TU does NOT reproduce through the per-object path.
    // `ninja build/.../Joypad_Xinput.obj` + `obj_patch_chain.py --apply` reads
    // fuzzy 99.73430 / canonical 99.95169, and `verify_objs_patched.py
    // --verify-manifest` then reports the object as drifted while
    // obj_patch_chain reports "anon_ns=0, dynamic_init=0, guard=0,
    // bool_mangle=0, atexit_scope=0, build_metadata=0".  A full `ninja`
    // restores 99.78261 / 100.0.  Judge this row from a full build only.
```

Inside the body (was lines 309-320):

```text
        // A DIVISION by 95.0f, not a multiply by 0.010526316f, and the
        // quotient is a NAMED LOCAL: in one expression the front end folds the
        // reciprocal into the -26539.0f that follows (w7-ap/w7-bl, see the
        // note above the function), giving one `fmuls`; the image keeps both
        // (0x825FCF78 `fmuls f0, f0, f9` then 0x825FCF7C `fmuls f0, f0, f8`).
        // 95 is exactly 122 - 27, the width of the clamp window.
        //
        // The float is converted to SHORT, then zero-extended: 0x825FD000
        // `lhz r11, 0x56(r1)` takes only the low halfword of the fctiwz word
        // at 0x54(r1), and 0x825FD004 `subfic r4, r11, -0x8000` is the
        // argument, with no `extsh` behind it.  `(unsigned short)(int)` costs
        // that extsh (w7-br, note above).
```

Inside the body (was lines 343-356):

```text
        // param_a is NOT a constant 0 here (w7-bl: the earlier note calling
        // that edge "not a sane source expression" is refuted).  Three edges
        // reach this call and the image distinguishes them:
        //   0x825FCFAC `bne cr6, .L_825FD014` -- not drums: r5 = 0;
        //   0x825FCFB8 `ble .L_825FD00C` / 0x825FCFBC `bge cr6, .L_825FD00C`
        //     -- drums but rx out of (0, 0x100): fall into
        //   0x825FD00C `li r5, 0x1` / 0x825FD010 `bgt cr6, .L_825FD018`,
        //     where cr6 still holds `rx vs 0x100` from 0x825FCFB4, so r5
        //     survives as 1 only when rx > 0x100.
        // i.e. param_a == (drums && rx > 0x100).  Written that way, as ONE
        // call, MSVC reproduces the whole shape including the hoisted
        // `cmpwi cr6, r11, 0x100` above the `ble` (92.46 -> 94.8 canonical).
        // Splitting it into a nested if/else with two calls instead is much
        // worse -- MSVC duplicates the tail rather than merging it (88.9).
```
