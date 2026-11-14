# Lane notes: `src/system/synth_xbox/Mic.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AddData@MicXbox@@QAAXPAXH@Z`

`MicXbox::AddData` -- moved verbatim from `src/system/synth_xbox/Mic.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 386-399):

```text
// w26-f: 100 normalized, fuzzy 98.598.  The residual is two EXACT priority
// ties, both the same shape -- a formal against a code temp minted after the
// forward block walk (tie 0):
//   `this`  pri 32 tie 9 -> r29   vs  code temp (biased loop pointer)
//                                        pri 32 tie 0 -> r28
//   `bytes` pri 24 tie 7 -> r27   vs  code temp (a second copy of `bytes`,
//                                        used for `unk301c + bytes` and as the
//                                        first XMemCpy length)
//                                        pri 24 tie 0 -> r26
// The image has each code temp in the HIGHER register.  On an exact priority
// tie the tie key decides, and a tie-0 candidate always loses that comparison,
// so the flip needs +1 priority on the temp or -1 on the formal -- a different
// pre-colouring candidate set in the block, not another spelling of the same
// instruction sequence.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 98.598, 34 non-equal row(s): The 2 SYM
rows load lbl_82260F30 where we load sMaxSample: that image word is 32767.0 (.rdata,
defined in FxSendSynapse.s next to a 2700.0), the same value. The 32 register rows are
pure permutation. NOT A BUG.
