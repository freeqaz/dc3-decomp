# Lane notes: `src/system/obj/DirLoader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ClassIndex@ClassAndNameSort@DirLoader@@IAAHPAVObject@Hmx@@@Z`

`DirLoader::ClassAndNameSort::ClassIndex` -- moved verbatim from `src/system/obj/DirLoader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 514-532):

```text
        // w14-d (94.92 -> 100): the key is taken as a `const FixedString *`.
        // String has TextStream at +0 and FixedString (mStr, c_str()) at +4, so
        // an explicit String* -> FixedString* POINTER conversion is what makes
        // MSVC emit the image's null-guarded base adjustment --
        //   825A4500  addic. r11, r31, 0x10    <- &it->first, tested
        //   825A4504  addi   r11, r11, 0x4     <- String* -> FixedString*
        //   825A4508  bne    .L_825A4510
        //   825A450C  li     r11, 0x0
        //   825A4518  lwz    r4, 0x0(r11)      <- mStr
        // A member call through `it->first`, `(*it).first` or a `const String *`
        // local adjusts `this` without a test (one `lwz r4, 0x14(r31)`).
        //
        // BEHAVIOURAL: there is no `if (file)` here.  The image goes straight
        // from that address computation into the virtual Print call with no
        // test of r30 anywhere in the loop.  The guard was ours, and it was
        // already inconsistent -- the very next statement dereferences `file`
        // unconditionally, and MILO_ASSERT(file) at the top of the function
        // says the argument is required -- so removing it changes no reachable
        // behaviour.
```

Inside the body (was lines 580-592):

```text
// w14-d (99.87, frame 0x180 vs our 0x190): the image gives the find() String
// temp and the insert's String temp ONE slot (0x58); we give the find temp its
// own (0x60) and shift every later slot by 0x10.  WORSE: `it = insert(...).first`
// with a single `it->second += memDelta` (94.46); a reference bound to a
// ?: of the two arms (80.75, adds a cleanup-flag word).
// w17-d (99.866): `it` declared then assigned (inert); the find folded into the
// if condition `(it = find(...)) != end()` (96.1, materialises a bool).
// w24-slot (99.866, unchanged): bracing the find() so its String temp dies
// inside a nested block (`it; { it = sMemPointMap.find(String(name)); }`) is
// byte-inert, with the same 15 rows.  A standalone cl.exe probe of this shape
// DOES share the find temp with the else-arm temp, so whatever keeps them
// apart here is outside the temp's lexical scope (EH state of the insert
// path, or the pair temp's size).
```

Inside the body (was lines 650-654):

```text
    // The comparator must be a NAMED local, not a temporary: the image reads it
    // with `lbz r4, 0x50(r31)` and never constructs one. `sort(ClassAndNameSort())`
    // adds a real `stb r24, 0x0(r11)` zero-init the image does not have
    // (98.53 -> 97.8, w7-r 2026-09-14). This spelling is byte-identical to the
    // older `auto _tmp2 = ClassAndNameSort();` it replaces.
```
