#include "utl\EncryptXTEA.h"
#include <cstring>

XTEABlockEncrypter::XTEABlockEncrypter() {
    mNonce[0] = 0;
    mNonce[1] = 0;
}

void XTEABlockEncrypter::SetKey(const unsigned char *uc) { memcpy(mKey, uc, 0x10); }

void XTEABlockEncrypter::SetNonce(const unsigned long long *nonce, unsigned int shift) {
    mNonce[0] = nonce[0] + shift;
    mNonce[1] = nonce[1] + shift;
}

unsigned long long XTEABlockEncrypter::Encipher(unsigned long long nonce, unsigned int *key) {
    unsigned long v1 = nonce & 0xFFFFFFFF;
    unsigned long v2 = nonce >> 32;
    unsigned int sum = 0;
    for (int i = 0; i < 4; i++) {
        v1 += (v2 + (v2 << 4 ^ v2 >> 5)) ^ sum + key[sum & 3];
        sum += 0x9E3779B9;
        v2 += (v1 + (v1 << 4 ^ v1 >> 5)) ^ sum + key[(sum >> 11) & 3];
    }
    return (static_cast<unsigned long long>(v2) << 32)
         | (static_cast<unsigned long long>(v1) & 0xFFFFFFFF);
}

void XTEABlockEncrypter::Encrypt(const XTEABlock *in, XTEABlock *out) {
    unsigned int *key = mKey;
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
    unsigned long long *nonce = mNonce;
    unsigned long offset = (char *)out - (char *)in;
    for (int i = 0; i < 2; i++) {
        *(unsigned long long *)(offset + (char *)in) =
            *(unsigned long long *)in ^ Encipher(*nonce, key);
        *nonce += 1;
        nonce++;
        in = (const XTEABlock *)((char *)in + 8);
    }
}
