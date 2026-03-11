// rlwinm-sensitive byte shift/mask fixture: type-dependent fusion behavior.
// Tests: u8 vs u32 shifts, rotation decomposition, mask placement.

typedef unsigned char u8;
typedef unsigned int u32;

extern u8 get_u8();
extern u32 get_u32();
extern void sink_u8(u8);
extern void sink_u32(u32);

// u8 shift — should produce fused rlwinm
u32 u8_shift_right(u32 w) {
    u8 b = (u8)w;
    return b >> 4;
}

// u32 with mask — should produce separate shift + mask
u32 u32_mask_shift_right(u32 w) {
    u32 v = w & 0xFF;
    return v >> 4;
}

// u8 rotation — should produce fused extrwi + clrlslwi
u8 u8_rotate(u8 b) {
    return (b >> 2) | (b << 6);
}

// u32 rotation with mask — should produce separate srwi + slwi
u32 u32_mask_rotate(u32 w) {
    u32 v = w & 0xFF;
    u32 r = (v >> 2) | (v << 6);
    return r & 0xFF;
}

// Left shift with mask
u32 u8_shift_left(u32 w) {
    u8 b = (u8)w;
    return b << 3;
}

// Combined shift+mask in expression
u32 extract_nibble(u32 w) {
    return (w >> 4) & 0x0F;
}

// Signed shift (known to not fuse)
u32 signed_shift(u32 w) {
    int v = w & 0xFF;
    return v >> 2;
}
