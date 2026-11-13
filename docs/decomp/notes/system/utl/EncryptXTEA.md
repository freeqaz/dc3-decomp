# Lane notes: `src/system/utl/EncryptXTEA.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Encipher@XTEABlockEncrypter@@AAA_K_KPAI@Z`

`XTEABlockEncrypter::Encipher` -- moved verbatim from `src/system/utl/EncryptXTEA.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 17-21):

```text
    // w22-a12: the halves are 32-bit words (the image does every round in 32-bit
    // `slwi`/`srwi`/`add` arithmetic).  `unsigned long` is 64 bits on an LP64
    // host, so natively v1/v2 carried past bit 31 and `v >> 5` shifted those
    // carries back in -- a different cipher.  `unsigned int` is the same type
    // width as the image's `unsigned long` on the 360.
```


## `?Encrypt@XTEABlockEncrypter@@QAAXPBUXTEABlock@@PAU2@@Z`

`XTEABlockEncrypter::Encrypt` -- moved verbatim from `src/system/utl/EncryptXTEA.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Inside the body (was lines 36-95):

```text
    // w8-g: 89.17 -> 91.67 (normalized, full ninja).  The image sets up the biased nonce
    // pointer (`subi r30, r3, 0x8`) BEFORE the out-in delta (`subf r26, r4, r5`);
    // with `mNonce[i]` indexing, MSVC creates that induction pointer only after
    // every named local is initialised, so it lands after the delta.  An
    // explicit walking `nonce` declared between `key` and `offset` puts it in the
    // image's slot.  Semantics are unchanged: mNonce[0] then mNonce[1].
    // w9-b: the two rows are ONE cause, not two.  MSVC picks the `stdx` base
    // register by definition order in the preheader: the image defines r26
    // (`subf r26, r4, r5`) last and writes `stdx r11, r26, r31`; we define
    // r31 last and write `stdx r11, r31, r26`.  So fixing the `mr r31, r4`
    // position would carry the operand order with it -- the store spelling is
    // not the lever.  Measured, all inert or worse:
    //   * `(char *)in + offset` instead of `offset + (char *)in`: exactly inert.
    //   * `offset + (unsigned long)in` (neither operand a pointer): exactly inert.
    //   * advancing `in` in the for-increment clause: exactly inert.
    //   * two independent cursors (`const unsigned long long *src`, `*dst`,
    //     no `offset`): 65.708.
    //   * `const unsigned long long *src = in->mData;` as the first statement,
    //     offset taken from `src`: 65.708.
    //   * `const XTEABlock *cur = in;` ahead of `key`: 65.708.
    // w31-b (2026-11-03): ADJUDICATED VALUE FOR VALUE against the image -- no
    // behaviour difference anywhere in the function.  Both sides are 24
    // instructions / 96 bytes and both do: r26 = out-in (`subf`), r30 = this-8
    // with `ld r4,0x8(r30)` + `stdu r11,0x8(r30)` walking mNonce[0] then
    // mNonce[1], r27 = this+0x10 = mKey, `li r29,0x2`, `ld r11,0x0(r31)` =
    // *(u64*)in, `xor r11,r3,r11` = Encipher()'s 64-bit result (the 360 returns
    // an unsigned long long wholly in r3), `stdx` to out+i*8, `addi r31,r31,8`.
    // The two residual rows are placement only: the image homes `in` into r31
    // before `subf r26` (ours sinks it below) and it writes `stdx r11,r26,r31`
    // where we write `stdx r11,r31,r26` (base/index order, C2RS-BRIDGE 8.7's
    // unread lwzx/stdx sub-question).
    // THE ONE SPELLING THAT FIXES THE PLACEMENT IS A REAL BUG -- do not take it.
    // `in++;` (a typed increment instead of `(char *)in + 8`) puts `mr r31,r4`
    // in the image's slot AND drops the stdx row, scoring 99.95834 canon /
    // 99.95834 fuzzy with an equal 24-instruction body. It is wrong:
    // sizeof(XTEABlock) is 16 (mData[2] of unsigned long long) while the loop
    // must advance 8 bytes, so the emitted increment becomes
    // `addi r31,r31,0x10` against the image's 0x8 and the second half of the
    // block is never transformed. objdiff charges that wrong immediate as a
    // single cheap diff_arg row, which is why the score RISES. The placement
    // really is coupled to the induction-variable stride, and no stride-8
    // spelling reaches it: `(unsigned long long *)in + 1`, `(unsigned int *)in
    // + 2`, `(const char *)in + 8`, `+ sizeof(unsigned long long)`, `+ 8u`,
    // `(unsigned char *)in + 8` and `(long)((char *)in + 8)` are ALL exactly
    // 91.25/91.667, as are index forms and named u64 cursors.
    //   * subscript form, no offset local (`out->mData[i] = in->mData[i] ^ ...`):
    //     90.750.
    // The three 65.708 collapses are all the SAME failure and it is not the
    // "extra register" the note above guessed: an explicit cursor local makes
    // MSVC choose `nonce` as the PRIMARY induction variable instead of `in`.
    // The diff then shows `ld r4, 0x0(r31)` reading the nonce off r31 with
    // `addi r31, r31, 0x8`, plus a manufactured `subf r28, r3, r4` delta and
    // `ldx r11, r28, r31` for the read -- the image's shape (in primary in r31,
    // nonce as the biased `0x8(r30)` + `stdu` pointer) is what the CURRENT
    // spelling already buys, and any named cursor throws it away.
    // RESIDUAL (2 rows): the image homes `in` into r31 FIRST (`mr r31, r4` at
    // index 4, right after `mr r28, r3`); with the walking pointer we sink it
    // below `subf r26`.  Refuted for that last pair: an explicit `const XTEABlock
    // *cur = in;` declared ahead of `key` (25 instrs, __savegprlr_25 instead of
    // _26) and a `const char *p` version (same) both cost more than they buy.
```
