# Lane notes: `src/system/char/CharCollide.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetRadius@CharCollide@@QBAMABVVector3@@AAV2@@Z`

`CharCollide::GetRadius` -- moved verbatim from `src/system/char/CharCollide.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 49-72):

```text
    // w14-b: 99.954%, 15 register-only rows in the cigar arm -- the image sums
    // Dot(out, unk1fc) as y,z,x, we emit y,x,z. Tried: swapped Dot args, all
    // six explicit term orders (break the plane arm, 96.1), Dot temp, axis ref,
    // Axis() accessor, explicit ScaleAdd, Dot*unk1f8: none better.
    // w21-as: 99.954 -> 99.969, 15 -> 4 rows.  Three changes, found with a
    // standalone cl.exe probe that reproduced the old listing exactly:
    //  - the target's dead `addi r11, r3, 0x1fc` is a reference bound to the
    //    axis (`const Vector3 &axis = unk1fc;`);
    //  - the cigar dot is written with the image's association (y + z) + x
    //    (fmuls 0x200, fmadds 0x204, fmadds 0x1fc); any non-Dot spelling here
    //    otherwise reshuffles the PLANE arm too;
    //  - the subtraction is spelled as three locals declared z, y, x (any order
    //    with z before y keeps the image's z-first subtract and puts the x term
    //    last with the image's operand order; Subtract() does not).
    // w26-c: the 4-row y/z residual is CLOSED by binding the axis a second
    // time for the dot (`dotAxis` below).  Both products are subexpressions,
    // and c2's post-lowering commutative sort (C2RS-BRIDGE 8.7) keys them off
    // their operands; with the single `axis` binding the two tie and the z
    // product is emitted first, while the image emits y first.  A second bound
    // reference moves the memory-operand base the products are keyed on and
    // the pair comes out y, z -- the image's order -- with no other
    // instruction changed (65/65).  Source operand order inside the products
    // really is inert (canonicalised by the sort), as is every spelling the
    // w14-b/w21-as lists above tried.
```
