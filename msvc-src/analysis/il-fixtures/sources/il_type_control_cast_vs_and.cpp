typedef unsigned char u8;
typedef unsigned int u32;

u32 cast_shift(u32 w) {
    u8 byte = (u8)w;
    u8 hi = byte >> 2;
    return (u32)hi;
}

u32 and_shift(u32 w) {
    unsigned long val = w & 0xFF;
    unsigned long hi = val >> 2;
    return (u32)(hi & 0xFF);
}

u32 cast_xor(u32 rot, u32 l) {
    return (u32)((u8)(rot ^ l));
}

u32 and_xor(u32 rot, u32 l) {
    return (u32)((rot ^ l) & 0xFF);
}
