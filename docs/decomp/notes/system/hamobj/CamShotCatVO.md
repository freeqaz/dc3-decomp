# Lane notes: `src/system/hamobj/CamShotCatVO.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CamShotVOData@@YAXVSymbol@@AAV1@111@Z`

`CamShotVOData` -- moved verbatim from `src/system/hamobj/CamShotCatVO.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 110-127):

```text
                        // String(Symbol), NOT String(const char *) -- REFUTED
                        // 2026-09-14 (w7-q).  objdiff's "Function Call Diff"
                        // reports `??0String@@QAA@VSymbol@@@Z` as base-only and
                        // `??0String@@QAA@PBD@Z` as target-2/base-1, which reads
                        // exactly like a wrong-callee bug.  It is not: the two
                        // constructors are ICF-FOLDED.
                        //   build/373307D9/icf_aliases.map:1875-1876
                        //     ??0String@@QAA@PBD@Z        827CE9E8
                        //     ??0String@@QAA@VSymbol@@@Z  827CE9E8
                        // dtk names the single body after the PBD symbol, so the
                        // listing can never distinguish the two spellings here.
                        // Measured anyway, full ninja, report.json canonical:
                        //   String s2Str(charSym)        97.42986 (fuzzy 96.77376)
                        //   String s2Str(charSym.Str())  97.12820 (fuzzy 96.41177)
                        //   Symbol c = ...; String s2Str(c.Str())  97.0 -- the
                        //     named local takes a frame word and shifts every
                        //     stack slot in the function by 8.
                        // Keep the Symbol overload.
```

Inside the body (was lines 152-178):

```text
                // BUG FIX (w7-bv): the win-level / character block below is
                // INSIDE this final else, not after the chain.  Every earlier
                // arm exits straight to the vector destructor at 0x82519524 in
                // the image -- BATTLE `b 0x825194FC` (at 0x82519118, via the
                // shared `lwz r11, 0(r3); stw r11, 0(r10)` tail), WIN
                // `b 0x82519500` (0x82519174), LOSE `b 0x82519524` (0x8251920C)
                // and HYPE (0x825192F0) -- and only the DLG/MOV arm and the
                // no-match case (`ble cr6, 0x8251937C` at 0x82519310) reach the
                // `subStrings.size() > 1` test at 0x82519378.  We used to fall
                // into it from every arm, which notified "Couldn't find win
                // level" / "Couldn't find character" for every BATTLE/CAMP/HYPE
                // category and overwrote charSym with subStrings[3] on any
                // four-part BATTLE name.  97.43 -> 100.0 canonical (99.95 raw);
                // the whole r23/r25 and TheDebug-in-r14 cascade that w7-q
                // recorded as a register-allocator floor was downstream of
                // this control flow.  Residual 6 rows: the low/med/high statics
                // mangle as scope `?FG@` in the image and `?FI@` here (MSVC's
                // per-function scope ordinal -- two more numbered scopes reach
                // this block in our spelling), a class the canonical ruler
                // folds.  w20-g: the image-side ?FG@ names are not image
                // data -- symbols.txt took them from a build of the earlier
                // spelling (58bfcfcfb), where this block sat OUTSIDE the
                // else chain (two fewer enclosing scopes).  Not a value or
                // control-flow difference; all operands/branches re-checked.
                // w25-ha: symbols.txt now names them ?FI@, this spelling's
                // ordinal (every other static's name already agreed), since
                // the image carries no static names to contradict it.
```
