# Lane notes: `src/system/rndobj/TexRenderer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawToTexture@RndTexRenderer@@QAAXXZ`

`RndTexRenderer::DrawToTexture` -- moved verbatim from `src/system/rndobj/TexRenderer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 240-248):

```text
// w32-a: the ~60 r23/r24 rows are ONE colour decision, not a per-site value
// exchange.  The instruction count (1078/1078), the opcode multiset and the
// relocation-symbol multiset all match the image; only the register number of
// `this` differs, on every one of its uses.  The c2 colour worklist names the
// pair exactly: `this` is id 2, pri -82, tie 6 -> r24, while id 217
// (sym 199/k03, arena#224, pri -86, tie 168) -> r23, and the image holds that
// value in r24.  Closing it needs that candidate to outrank `this`, i.e. pri
// above -82 or a tie-key win, neither of which any spelling of this body
// reaches -- it is a priority ordering, not a source-visible operand order.
```

Inside the body (was lines 299-334):

```text
            // Measured, one full ninja each (canonical, report.json ruler):
            //   shared Multiply() inline                       98.82838
            //   accumulators seeded off a `Vector3(0,-f34,0)`  98.68646
            //   accumulators with the zeros written literally  99.42579
            // The Vector3 temp is what costs: naming it perturbs the scheduling
            // of the Subtract/Length above and of an unrelated Transform copy
            // 300 instructions later, for +88 score points net, even though the
            // multiply block itself comes out identical either way.  The
            // component order (y, x, z) is measured too -- x, y, z scores 1112
            // against this order's 1104.
            //
            // w26-o: the residual rows here are scheduler order, not content --
            // the two insert/delete pairs at ~552..577 are the same three
            // Vector3 copies (vertVectors[3] and the verts[3] pointer stores)
            // emitted in the other order, and the pair at ~730 is one fmuls
            // scheduled five slots early among independent stack loads; the
            // last `fadds` (outZ + halfHeight) is a commutative order row.
            // A `const float &halfHeight = mImpostorHeight / 2.0f` binding is
            // byte-inert at the top of the block and costs 0.26 canonical at
            // first use, so the CSE temp keeps its key over the named local
            // (C2RS-BRIDGE 8.7: a temp leaf's sid<<6 beats a variable's sid<<5
            // for the same sid).
            // w26-n3: the 3 insert / 3 delete pairs are reorderings, verified
            // instruction by instruction -- each insert has its matching delete
            // elsewhere (the two Vector3 copy blocks at ~552..577 and the single
            // `fmuls f26, f17, f29` at ~730..735), and the three
            // fmuls/fmadds rows at 812/821/823 are commutative operand swaps of
            // the SAME values.  No value, store address, branch or callee
            // differs, so this row is behaviour-equivalent.
            // Three spellings of the vertVectors copy, measured on the
            // per-object path (which reads 99.40723 here against report.json's
            // 99.42579 -- this TU holds function-local statics, so rank only and
            // confirm with a full ninja): element-wise assignment 98.85807,
            // three named `const Vector3 &` bindings 99.40723 (bit-identical),
            // per-element scope-bound references 99.21985.  The copy order is
            // c2's, not the source's.
```

p2-b99 (2026-11-14, name_check, 99-100 band audit): NOT A BUG. The impostor-distance Length() sums the same three squared components (v.x, v.y, v.z); the image accumulates them as y,x,z and we accumulate them as x,z,y. This is association only, with the same values.
