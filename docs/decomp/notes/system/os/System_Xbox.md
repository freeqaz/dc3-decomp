# Lane notes: `src/system/os/System_Xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetSystemLanguage@@YA?AVSymbol@@V1@@Z`

`GetSystemLanguage` -- moved verbatim from `src/system/os/System_Xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 41-49):

```text
// Open residual (w7-r, 2026-09-14): the image and we disagree about which of the
// two incoming pointers gets a home slot. The image spills the by-value param
// (`stw r4, 0x11c(r31)`) and keeps the hidden sret pointer in callee-saved r26,
// so every `s = <sym>` arm loads into scratch r11 and branches to one merge
// point; we spill the sret pointer (`stw r3, 0x114(r31)`) and keep `s` itself in
// callee-saved r17, so the arms load into r17 and branch to a different merge.
// Everything else in the 61 rows is a straight permutation of the callee-saved
// registers holding the 17 function-local `static Symbol`s (r17..r29), which the
// canonical ruler already forgives -- hence 98.48 with 61 raw rows.
```

Inside the body (was lines 91-107):

```text
    // w20-m (normalized 100, fuzzy 99.84): 10 diff_arg rows are a 3-way
    // callee-saved rotation {hidden return ptr, &dan, &cht} = image r26/r24/r25,
    // ours r24/r25/r26. Every row carries the same value on both sides; all
    // branch targets agree (constant +0x208 bias) and the language jump table
    // (jumptable_8207D4C0) maps each XC_LANGUAGE to the arm the source names.
    // Tried: dropping `default:` from the locale switch, `default: return s;`
    // in the language switch -- both inert.
    //
    // w26-f11 (2026-11-02): the c2-rs candidate dump names the three numbers
    // that hold the rotation. The hidden return pointer is candidate id 2
    // (sym 1/k05, arena#1) at pri -25 / tie 8, ben 58, ref 39; the two
    // local-symbol candidates that rank above it are id 53 (sym 27/k03) at
    // pri -23 / tie 218 and id 59 (sym 33/k03) at pri -25 / tie 173. Priority
    // is fed by the benefit/reference counts, so the sret can only move if a
    // spelling gives IT more references; the arms already return out of both
    // switches (which is what produced ben 58), and every other arm spelling
    // leaves those three triples identical.
```
