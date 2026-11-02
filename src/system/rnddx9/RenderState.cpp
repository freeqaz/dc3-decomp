#include "RenderState.h"
#include "rnddx9\Rnd.h"

RndRenderState TheRenderState;

// Maps TestFunc to D3DCMPFUNC. Two banks of 8: [0-7] normal Z, [8-15] reversed Z.
D3DCMPFUNC RndRenderState::tf2cf[] = {
    // Normal Z
    D3DCMP_ALWAYS,      // 0
    D3DCMP_LESS,        // 1
    D3DCMP_EQUAL,       // 2
    D3DCMP_LESSEQUAL,   // 3
    D3DCMP_GREATER,     // 4
    D3DCMP_NOTEQUAL,    // 5
    D3DCMP_GREATEREQUAL,// 6
    D3DCMP_NEVER,       // 7
    // Reversed Z (flip less/greater)
    D3DCMP_ALWAYS,      // 8
    D3DCMP_GREATER,     // 9
    D3DCMP_EQUAL,       // 10
    D3DCMP_GREATEREQUAL,// 11
    D3DCMP_LESS,        // 12
    D3DCMP_NOTEQUAL,    // 13
    D3DCMP_LESSEQUAL,   // 14
    D3DCMP_NEVER,       // 15
};

void RndRenderState::SetBlendEnable(bool b) {
    D3DDevice_SetRenderState_AlphaBlendEnable(TheDxRnd.Device(), (u8)b);
}

void RndRenderState::SetBlendOp(BlendOp op) {
    D3DDevice_SetRenderState_BlendOp(TheDxRnd.Device(), (int)op);
}

void RndRenderState::SetBlend(
    Blend srcblend, Blend dstblend, Blend srcblenda, Blend dstblenda
) {
    D3DDevice_SetRenderState_SrcBlend(TheDxRnd.Device(), (int)srcblend);
    D3DDevice_SetRenderState_DestBlend(TheDxRnd.Device(), (int)dstblend);
    D3DDevice_SetRenderState_SrcBlendAlpha(TheDxRnd.Device(), (int)srcblenda);
    D3DDevice_SetRenderState_DestBlendAlpha(TheDxRnd.Device(), (int)dstblenda);
}

void RndRenderState::SetColorWriteMask(uint mask) {
    D3DDevice_SetRenderState_ColorWriteEnable(TheDxRnd.Device(), mask);
}

void RndRenderState::SetFillMode(FillMode mode) {
    D3DDevice_SetRenderState_FillMode(TheDxRnd.Device(), (int)mode);
}

void RndRenderState::SetCullMode(CullMode mode) {
    D3DDevice_SetRenderState_CullMode(TheDxRnd.Device(), (int)mode);
}

void RndRenderState::SetAlphaTestEnable(bool b) {
    D3DDevice_SetRenderState_AlphaTestEnable(TheDxRnd.Device(), b);
}

void RndRenderState::SetAlphaFunc(TestFunc tf, unsigned int ref) {
    D3DDevice_SetRenderState_AlphaRef(TheDxRnd.Device(), ref);
    D3DDevice_SetRenderState_AlphaFunc(TheDxRnd.Device(), tf2cf[tf]);
}

void RndRenderState::SetDepthTestEnable(bool b) {
    D3DDevice_SetRenderState_ZEnable(TheDxRnd.Device(), b);
}
void RndRenderState::SetDepthWriteEnable(bool b) {
    D3DDevice_SetRenderState_ZWriteEnable(TheDxRnd.Device(), b);
}

void RndRenderState::SetDepthFunc(TestFunc tf) {
    D3DDevice_SetRenderState_ZFunc(TheDxRnd.Device(), tf2cf[TheDxRnd.ReverseZ() * 8 + tf]);
}

void RndRenderState::SetStencilTestEnable(bool b) {
    D3DDevice_SetRenderState_StencilEnable(TheDxRnd.Device(), (u8)b);
}

void RndRenderState::SetStencilFunc(TestFunc tf, u8 ref) {
    D3DDevice_SetRenderState_StencilRef(TheDxRnd.Device(), ref);
    D3DDevice_SetRenderState_StencilFunc(
        TheDxRnd.Device(), tf2cf[TheDxRnd.ReverseZ() * 8 + tf]
    );
}

void RndRenderState::SetStencilOp(StencilOp fail, StencilOp zfail, StencilOp pass) {
    D3DDevice_SetRenderState_StencilFail(TheDxRnd.Device(), (int)fail);
    D3DDevice_SetRenderState_StencilZFail(TheDxRnd.Device(), (int)zfail);
    D3DDevice_SetRenderState_StencilPass(TheDxRnd.Device(), (int)pass);
}

void RndRenderState::SetTextureFilter(uint sampler, FilterMode filter, bool) {
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), sampler, filter);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), sampler, filter);
    // One device read for the fetch-constant write AND the dirty-mask write: the
    // image loads 0x224(r29) once and copies it (`mr r7, r10`) so the pointer
    // survives the `stw`, where two TheDxRnd.Device() expressions make MSVC
    // reload it (the store may alias TheDxRnd's own member).
    D3DDevice *dev = TheDxRnd.Device();
    DWORD *pWord = &dev->m_Constants.TextureFetch[sampler].dword[3];
    *pWord = (*pWord & ~0x01800000) | ((filter & 3) << 23);
    dev->m_Pending.m_Mask[3] |= 0x8000000000000000ull >> (sampler + 0x20);
}

// OPEN (w26-f7): 100 normalized, fuzzy 99.706 -- ONE diff_arg row of 34, the
// `or` that stores m_Mask[3] in the THIRD clamp group.  Our three groups are
// the same source statement three times and emit `or r11, r7, r11` (mask
// first) in all three; the image emits that in groups 1 and 2 and
// `or r11, r11, r7` (the `ld` result first) in group 3.  Dest and both operands
// are the same registers -- only the commutative operand order differs.
//
// Tap (`c2rs stage cands --regasg`, fn 18) says the three `or` tuples are
// keyed identically, which is why we cannot distinguish them.  `CQT cqlo3`
// print slots r5 / r15 / r25 are the three `or`s; each is `2cb` with two
// folded single-use temps whose definers are one `2ca` and one `2c9`:
//   r5  : T135 <2ca slot r7 > T131 <2c9 slot r6 >
//   r15 : T157 <2ca slot r17> T91  <2c9 slot r16>
//   r25 : T86  <2ca slot r27> T88  <2c9 slot r26>
// and the three `2ca` parents differ only in the constant they fold
// (`k07[1c00,0x10,ffe3ff]`, `[e000,0x10,ffff1fff]`, `[7,0x10,fff8ffff]`); the
// memory leaf is `M6{0001c008}` and the named `mask` is `V6{000100a0}` in all
// three.  So the image's group 3 was a different IL shape, not a different
// sort of the same shape.
//
// 17 spellings attempted this pass (12 measured, 5 dropped because the target
// text occurs 3 times and a uniform substitution moves all three groups): all
// inert (99.706) or worse.  Nothing that keeps the code changes one of the
// three groups relative to the others:
//   `m_Mask[3] = m_Mask[3] | mask;` for all three (skipped, 3 sites) /
//   the third only: `= ... | mask` (inert), `|= (UINT64)mask` (inert),
//   `|= mask >> 0` (inert), `UINT64 m3 = mask; |= m3` (inert),
//   `|= (mask & 0xffffffffffffffffull)` (inert); `unsigned long long mask`
//   (inert); `UINT64 mask = 1ull << (63 - sampler)` 75.29; mask store moved
//   before the fetch-constant store 61.00 (third group only: 81.76);
//   `UINT64 &maskWord = dev->m_Pending.m_Mask[3]` 81.76 (third only: 81.76);
//   `DxRnd &rnd = TheDxRnd; dev = rnd.Device();` (inert -- the binding folds).
// Open question: which source difference makes ONE of three textually
// identical statements a different IL shape while emitting the same
// instruction, or whether the image used a helper that was inlined twice.
void RndRenderState::SetTextureClamp(uint sampler, ClampMode clamp) {
    UINT64 mask = 0x8000000000000000ull >> (sampler + 0x20);
    // Three groups, one device read each (the image has three `lwz 0x224(r9)` and
    // three `mr` copies, not six loads): the fetch-constant store and the mask
    // store in a group go through the SAME pointer.
    D3DDevice *dev = TheDxRnd.Device();
    DWORD *pWord = &dev->m_Constants.TextureFetch[sampler].dword[0];
    *pWord = (*pWord & ~0x00001C00) | ((clamp & 7) << 10);
    dev->m_Pending.m_Mask[3] |= mask;
    dev = TheDxRnd.Device();
    pWord = &dev->m_Constants.TextureFetch[sampler].dword[0];
    *pWord = (*pWord & ~0x0000E000) | ((clamp & 7) << 13);
    dev->m_Pending.m_Mask[3] |= mask;
    dev = TheDxRnd.Device();
    pWord = &dev->m_Constants.TextureFetch[sampler].dword[0];
    *pWord = (*pWord & ~0x00070000) | ((clamp & 7) << 16);
    dev->m_Pending.m_Mask[3] |= mask;
}

// dword[5] bits 0-1 of the fetch constant select the border-colour source
// (transparent black vs opaque black); the rest of the dword is untouched.
void RndRenderState::SetBorderColor(uint sampler, bool border) {
    D3DDevice *dev = TheDxRnd.Device();
    DWORD *pWord = &dev->m_Constants.TextureFetch[sampler].dword[5];
    *pWord = (*pWord & ~0x00000003) | ((border != 0) & 3);
    dev->m_Pending.m_Mask[3] |= 0x8000000000000000ull >> (sampler + 0x20);
}

void RndRenderState::Init(void) {
    SetTextureClamp(4, (ClampMode)2);
    SetTextureClamp(5, (ClampMode)2);
    SetTextureFilter(5, (FilterMode)1, false);
}
