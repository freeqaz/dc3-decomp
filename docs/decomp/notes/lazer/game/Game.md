# Lane notes: `src/lazer/game/Game.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?IsReady@Game@@QAA_NXZ`

`Game::IsReady` -- moved verbatim from `src/lazer/game/Game.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 288-295):

```text
// w21-k: written as an if/return pair (RB3's Game::IsReady shape, minus its
// per-player loop), NOT `return IsLoaded() != false;`.  Out of line both
// spellings emit the same 11 instructions (82867F80), but inlined into a
// caller they differ (measured; the mechanism is inferred): presumably the
// if/return keeps a branch inside the inlined
// condition until late, so MSVC neither re-folds the 0/1 normalisation
// (`clrlwi / subic / subfe / clrlwi.`) nor rotates the while loop around it.
// That is the image's shape at both inlined sites, OnSetShuttle and Poll.
```


## `?Poll@Game@@QAAXXZ`

`Game::Poll` -- moved verbatim from `src/lazer/game/Game.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 398-402):

```text
    // w21-k: the image's `clrlwi / subic / subfe / clrlwi.` test at 8286827C
    // is the INLINED Game::IsReady() (see the comment above IsReady), not a
    // normalised IsLoaded(); calling IsReady() here closed the three rows
    // that w7-ag certified as a floor (98.77 -> 100 modulo one fadds
    // register-permutation row at idx 90).
```


## `?OnSetShuttle@Game@@AAA?AVDataNode@@PAVDataArray@@@Z`

`Game::OnSetShuttle` -- moved verbatim from `src/lazer/game/Game.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 911-917):

```text
        // Top-tested, NOT do/while: 0x828685D4 is the loop head, reached by
        // fall-through from the Jump() block, and the image runs the IsLoaded()
        // test BEFORE the first Poll (`bne .L_8286859C` straight out to the
        // SetActive tail).  A do/while polls the synth once even when the song
        // is already loaded.
        // The 4-instruction 0/1 test at the loop head is the inlined
        // IsReady() (w21-k); with it MSVC no longer peels the first test.
```


## `?OnCycleTestDancer@@YA?AVDataNode@@PAVDataArray@@@Z`

`OnCycleTestDancer` -- moved verbatim from `src/lazer/game/Game.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1086-1107):

```text
// w17-e: still 98.9.  `while (i < size) { if (...) break; i = i + 1; }` is
// byte-identical to the for loop (the image's `addi r10,r29,1` / `mr r29,r10`
// increment-through-a-temp is not a spelling of the increment).
// w21-bn (98.936, same 14 rows: the `mr r29, r10` delete plus the r8/r9/r10
// rename it causes).  Standalone cl.exe probe with the real STLport vector
// reproduces our loop exactly; 20 loop spellings in it all keep the in-place
// `addi r29, r29, 1`: int/unsigned/`(int)` compare, `++i`, `i = i + 1`,
// while/for/do-while (guarded and `while (++i < size)`), `i = -1;
// while (++i < size)`, a goto exit, a `const String &` element alias, an
// iterator loop with a side counter, an empty-body for with the test in the
// condition, a bool `found` flag, `int i;` hoisted to function scope, `next`
// computed before `size`, and Mod()/a size-first Mod in the tail (those move
// `i + 1` into the divide arm -- worse).  Only a SECOND counter
// (`i = j + 1` at the bottom of a `j` loop) produces an increment-through-a-
// temp, and it emits two copies (r27 and r28), not the image's one.
// w24-c2 (98.936, unchanged, nothing built): re-read the listing -- 1 delete
// (`mr r29, r10` at row 53) + 13 register-only rows (r8/r9/r10 rename the
// extra copy causes); every value, address and branch target agrees, so no
// behaviour question. The image's in-loop `i + 1` is NOT CSE'd with the
// post-loop `addi r10, r29, 1` (row 59), and r29 must survive the operator==
// call, so the scratch is a genuine second value -- consistent with w21-bn's
// probe. Floor stands.
```

Inside the body (was lines 1117-1134):

```text
        // RESIDUAL (w39-f): we are one instruction SHORT of the image, 93
        // against 94.  The missing one is 82865D84 `mr r29, r10`: the image
        // computes `i + 1` into a scratch, compares THAT against the size and
        // only then copies it back into the callee-saved `i`, where we
        // increment r29 in place.  Every other row is a register permutation
        // that the canonical ruler forgives, and the mnemonic multiset differs
        // by exactly that one `mr`.  Because the body is short, no score gain
        // here is admissible until the `mr` exists.
        // 20 spellings measured and none produces it: `i = i + 1`, `++i`,
        // `i += 1`, `i++` moved into the body, a hoisted `size`, a hoisted
        // `n`, an `int nn = i; nn += 1; i = nn;` temp, a `const int &` binding
        // of `i + 1`, and `(unsigned)(i + 1)` in the test (95.9, and it is a
        // different bound).  The c2 colour tap shows `i` is already TWO
        // candidates here -- sym 6/k04 id 5 (pri 17 tie 65, nonvolatile-only)
        // and the same symbol id 6 (pri 17 tie 86) -- so the two versions the
        // image separates both exist; what differs is which one carries the
        // increment.  That is a version merge after colouring, not a priority
        // or tie disagreement.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
and call args equal under symbolic sim; residual is register/scheduling only.
