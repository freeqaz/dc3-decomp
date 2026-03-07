#pragma once

// template <>
inline void EndianSwapEq(unsigned int &i) {
    i = i >> 0x18 | i << 0x18 | i >> 8 & 0xFF00 | (i & 0xFF00) << 8;
}

// template <>
// inline void EndianSwapEq(unsigned short &s) {
//     s = (s << 8 | s >> 8);
// }

// template <>
// inline void EndianSwapEq(short &s) {
//     s = (s << 8 | s >> 8);
// }

inline unsigned short EndianSwap(unsigned short s) {
    unsigned short us = s;
    return us << 8 | us >> 8;
}

inline unsigned int EndianSwap(unsigned int i) {
    unsigned int ui = i;
    return ui >> 0x18 | ui << 0x18 | ui >> 8 & 0xFF00 | (ui & 0xFF00) << 8;
}

inline unsigned short SwapBytes(unsigned short bytes) { return EndianSwap(bytes); }

// the asm for this is inlined, it's in BinStream::ReadEndian and WriteEndian
// could also find the standalone function asm in RB3 retail

// example input:   0x12345678DEADBEEF
// should yield:    0xEFBEADDE78563412
inline unsigned long long EndianSwap(unsigned long long ull) {
    unsigned int hi = (ull >> 32) & 0xFFFFFFFF;
    unsigned int lo = ull & 0xFFFFFFFF;
    unsigned int hi_swapped = EndianSwap(hi);
    unsigned long long lo_swapped = EndianSwap(lo);
    return (lo_swapped << 32) | hi_swapped;
}
