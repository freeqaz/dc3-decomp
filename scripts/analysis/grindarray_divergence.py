#!/usr/bin/env python3
"""
GrindArray divergence analysis: find where PPC (32-bit unsigned long)
and x86_64 (64-bit unsigned long) produce different op results.

The DTA ops use `unsigned long` intermediates. On PPC (ILP32) that's 32 bits;
on x86_64 (LP64) it's 64 bits. This script implements every op twice:
  - ppc_opN: all `unsigned long` are masked to 32 bits after each operation
  - x64_opN: `unsigned long` is 64 bits (native Python int behavior, masked to 64 bits)

Then runs the full GrindArray loop with both and finds the first divergence.

Test case:
  pre_grind = df 60 0c cc 86 92 4f 59 91 ce 09 40 94 40 46 58
  seedA (mMagicA) = 602494344
  seedB (mMagicB) = 625836951
  moggVersion = 14 (encMethod = 1, uses hash6 / 64 ops)
  Expected native x64 output: 07 2f 0d 3f 15 05 37 47 cd f5 8f a7 45 5f af 97
"""

import struct
import sys

# ============================================================================
# Constants
# ============================================================================
LCG_MUL = 0x19660D
LCG_INC = 0x3C6EF35F

def m32(x):
    """Mask to 32 bits (PPC unsigned long)"""
    return x & 0xFFFFFFFF

def m64(x):
    """Mask to 64 bits (x86_64 unsigned long)"""
    return x & 0xFFFFFFFFFFFFFFFF

def u8(x):
    return x & 0xFF


# ============================================================================
# Op implementations — PPC (32-bit unsigned long) versions
# All intermediate `unsigned long` values are masked to 32 bits.
# ============================================================================

def ppc_op0(operand, w):
    operand = m32(operand); w = m32(w)
    return u8(w ^ operand)

def ppc_op1(operand, w):
    operand = m32(operand); w = m32(w)
    return u8(u8(w) + u8(operand))

def ppc_op2(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    ret = m32(ret >> u8(operand & 7))
    return u8(ret)

def ppc_op3(operand, w):
    operand = m32(operand); w = m32(w)
    b = 1 if (operand == 0) else 0
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    ret = m32(ret >> b)
    return u8(ret)

def ppc_op4(operand, w):
    operand = m32(operand); w = m32(w)
    b = 1 if (operand == 0) else 0
    a = 1 if (u8(w) == 0) else 0
    ret = m32((a << 8) | a)
    ret = m32(ret >> b)
    return u8(ret)

def ppc_op5(operand, w):
    operand = m32(operand); w = m32(w)
    ret = u8(m32(~m32(w | w)))  # NOR
    s = m32(m32(operand << 29) >> 29)
    r4 = m32(ret << 8)
    ret = m32(ret | r4)
    ret = m32(ret >> s)
    return u8(ret)

def ppc_op6(operand, w):
    operand = m32(operand); w = m32(w)
    return u8((1 if w == 0 else 0) ^ operand)

def ppc_op7(operand, w):
    operand = m32(operand); w = m32(w)
    nw = 1 if w == 0 else 0
    return u8(m32(nw + operand))

def ppc_op8(operand, w):
    operand = m32(operand); w = m32(w)
    return u8(u8(w) + u8(operand)) ^ u8(operand)

def ppc_op9(operand, w):
    b = m32(operand)
    a = u8(m32(w))
    return u8(m32(m32(a ^ b) + b))

def ppc_op10(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    not_op = 1 if operand == 0 else 0
    ret = m32(ret >> not_op)
    ret = m32(ret ^ operand)
    return u8(ret)

def ppc_op11(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    ret = m32(ret >> u8(operand & 7))
    ret = m32(ret ^ operand)
    return u8(ret)

def ppc_op12(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    ret = m32(ret >> u8(operand & 7))
    return u8(m32(ret + operand))

def ppc_op13(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(bw | m32(bw << 8))
    not_op = 1 if operand == 0 else 0
    ret = m32(ret >> not_op)
    return u8(m32(ret + operand))

def ppc_op14(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 1) | m32(bw << 7))
    return u8(m32(ret + operand))

def ppc_op15(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 2) | m32(bw << 6))
    return u8(m32(ret + operand))

def ppc_op16(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 3) | m32(bw << 5))
    return u8(m32(ret + operand))

def ppc_op17(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 4) | m32(bw << 4))
    return u8(m32(ret + operand))

def ppc_op18(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 5) | m32(bw << 3))
    return u8(m32(ret + operand))

def ppc_op19(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 6) | m32(bw << 2))
    return u8(m32(ret + operand))

def ppc_op20(operand, w):
    operand = m32(operand); w = m32(w)
    bw = u8(w)
    ret = m32(m32(bw >> 7) | m32(bw << 1))
    return u8(m32(ret + operand))

def ppc_op21(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 1) | m32(br << 7))
    return u8(m32(rot ^ l))

def ppc_op22(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 2) | m32(br << 6))
    return u8(m32(rot ^ l))

def ppc_op23(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 3) | m32(br << 5))
    return u8(m32(rot ^ l))

def ppc_op24(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 4) | m32(br << 4))
    return u8(m32(rot ^ l))

def ppc_op25(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 5) | m32(br << 3))
    return u8(m32(rot ^ l))

def ppc_op26(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 6) | m32(br << 2))
    return u8(m32(rot ^ l))

def ppc_op27(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 7) | m32(br << 1))
    return u8(m32(rot ^ l))

def ppc_op28(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 5) | m32(br << 3))
    return u8(m32(m32(rot + l) ^ l))

def ppc_op29(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 3) | m32(br << 5))
    return u8(m32(m32(rot + l) ^ l))

def ppc_op30(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 3) | m32(br << 5))
    return u8(m32(m32(rot ^ l) + l))

def ppc_op31(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    rot = m32(m32(br >> 5) | m32(br << 3))
    return u8(m32(m32(rot ^ l) + l))

def ppc_op32(operand, w):
    operand = m32(operand)
    byteVal = u8(w)
    tmp = m32(m32(byteVal >> 3) ^ 0x1F) | m32(m32(byteVal & 7) << 5)
    return u8(m32(tmp) ^ operand)

def ppc_op33(operand, w):
    operand = m32(operand)
    w_byte = u8(w) & 0xFF
    tmp = m32(m32(w_byte >> 5) ^ 7) | m32(m32(w_byte & 0x1F) << 3)
    return u8(m32(tmp) ^ operand)

def ppc_op34(operand, w):
    operand = m32(operand)
    tmp = u8(w)
    val = m32(m32(tmp >> 2) ^ 0x3F) | m32(m32(tmp & 3) << 6)
    return u8(m32(val) ^ operand)

def ppc_op35(operand, w):
    operand = m32(operand)
    tmp = u8(w)
    val = m32(m32(tmp >> 6) ^ 3) | m32(m32(tmp & 0x3F) << 2)
    return u8(m32(val) ^ operand)

def ppc_op36(operand, w):
    l = m32(operand); r = m32(w)
    br = u8(r)
    # ~br on 32-bit unsigned long
    not_br = m32(~br)
    rot = m32(m32(br >> 2) | m32(not_br << 6))
    return u8(m32(rot ^ l))

def ppc_op37(operand, w):
    l = m32(operand)
    br = u8(m32(w))
    not_br = m32(~br)
    rot = m32(m32(br >> 5) | m32(not_br << 3))
    return u8(m32(rot ^ l))

def ppc_op38(operand, w):
    l = m32(operand)
    br = u8(m32(w))
    not_br = m32(~br)
    rot = m32(m32(br >> 6) | m32(not_br << 2))
    return u8(m32(rot ^ l))

def ppc_op39(operand, w):
    l = m32(operand)
    br = u8(m32(w))
    not_br = m32(~br)
    rot = m32(m32(br >> 3) | m32(not_br << 5))
    return u8(m32(rot ^ l))

def ppc_op40(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp = m32(m32(m32(v << 8) | m32(v ^ 0x5C)) >> 6)
    return u8(m32(tmp ^ operand))

def ppc_op41(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp = m32(u8(v >> 2) ^ 0x17) | m32(m32(v << 6) & 0xC0)
    return u8(m32(tmp ^ operand))

def ppc_op42(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp = m32(m32(v >> 3) ^ 0xB) | m32(m32(v << 5) & 0xE0)
    return u8(m32(tmp ^ operand))

def ppc_op43(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp = m32(m32(v >> 5) ^ 2) | m32(m32(v & 0x1F) << 3)
    return u8(m32(tmp ^ operand))

def ppc_op44(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp = m32(m32(v >> 2) ^ 0xD) | m32(m32(v << 6) & 0xC0)
    return u8(m32(tmp ^ operand))

def ppc_op45(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(v >> 3) ^ 6)
    lowBits = u8(m32(v & 7) << 5)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op46(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(v >> 4) ^ 3)
    lowBits = u8(m32(v << 4) & 0xF0)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op47(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(v >> 1) ^ 0x1B)
    lowBits = u8(m32(v & 1) << 7)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op48(operand, w):
    operand = m32(operand)
    a = u8(m32(w))
    working2 = m32(m32(a >> 4) ^ 0x6)
    working3 = m32(m32(m32(a << 4) & 0xF0) ^ 0x5)
    tmp = m32(working2 | working3)
    return u8(m32(tmp ^ operand))

def ppc_op49(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working3 = m32(m32(v << 8) ^ 0x5C)
    working2 = m32(v ^ 0x63)
    tmp = m32(m32(working2 | working3) >> 3)
    return u8(m32(tmp ^ operand))

def ppc_op50(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(m32(v << 3) & 0xF8) ^ 2)
    lowBits = u8(m32(v >> 5) ^ 3)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op51(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working2 = m32(v ^ 0x63)
    working3 = m32(m32(v << 8) ^ 0x5C)
    tmp = m32(m32(working2 | working3) >> 6)
    return u8(m32(tmp ^ operand))

def ppc_op52(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(v >> 1) ^ 0x2e)
    lowBits = u8(m32(v << 7) ^ 0x1b)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op53(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working3 = m32(m32(v << 8) ^ 0x36)
    working2 = m32(v ^ 0x5C)
    tmp = m32(m32(working2 | working3) >> 7)
    return u8(m32(tmp ^ operand))

def ppc_op54(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    part2 = m32(m32(v << 5) & 0xE0)
    part2 = m32(part2 ^ 0x6)
    part1 = m32(m32(v >> 3) & 0xFF)
    part1 = m32(part1 ^ 0xB)
    tmp = m32(part1 | part2)
    return u8(m32(tmp ^ operand))

def ppc_op55(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(m32(v & 0x1f) << 3) ^ 1)
    lowBits = u8(m32(v >> 5) ^ 2)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op56(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    highBits = u8(m32(m32(v & 0xF) << 4) ^ 6)
    lowBits = u8(m32(v >> 4) ^ 3)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def ppc_op57(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working2 = m32(v ^ 0x3C)
    working3 = m32(m32(v << 8) ^ 0x65)
    tmp = m32(m32(working2 | working3) >> 5)
    return u8(m32(tmp ^ operand))

def ppc_op58(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working2 = m32(v ^ 0x65)
    working3 = m32(m32(v << 8) ^ 0x3C)
    tmp = m32(m32(working2 | working3) >> 6)
    return u8(m32(tmp ^ operand))

def ppc_op59(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    working2 = m32(v ^ 0x65)
    working3 = m32(m32(v << 8) ^ 0x3C)
    tmp = m32(m32(working2 | working3) >> 2)
    return u8(m32(tmp ^ operand))

def ppc_op60(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    xorLow = m32(v ^ 0xFF)
    xorHigh = m32(m32(v << 8) ^ 0xAA)
    tmp = m32(m32(xorLow | xorHigh) >> 4)
    return u8(m32(tmp ^ operand))

def ppc_op61(operand, w):
    # DTA version SWAPS args: operand=Int(2), w=Int(1)
    # So when called as op61(bar[ix], foo), the DTA would do:
    #   operand = msg->Int(2) = foo, w = msg->Int(1) = bar[ix]
    operand_real = m32(w)    # foo
    rw = u8(m32(operand))    # bar[ix]
    a = m32(m32(rw >> 3) ^ 0x15)
    b = m32(m32(m32(rw & 7) << 5) ^ 0x1f)
    tmp = m32(a | b)
    return u8(m32(tmp ^ operand_real))

def ppc_op62(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    tmp1 = u8(m32(v >> 5) ^ 5)
    tmp2 = u8(m32(m32(v << 3) & 0xF8) ^ 7)
    combined = u8(tmp1 | tmp2)
    return u8(combined ^ operand)

def ppc_op63(operand, w):
    operand = m32(operand)
    v = u8(m32(w))
    xorLow = m32(v ^ 0xFF)
    xorHigh = m32(m32(v << 8) ^ 0xAF)
    tmp = m32(m32(xorLow | xorHigh) >> 6)
    return u8(m32(tmp ^ operand))


# ============================================================================
# Op implementations — x86_64 (64-bit unsigned long) versions
# `unsigned long` is 64 bits. We reproduce the DTA C++ source exactly,
# but with 64-bit intermediates where `unsigned long` is used.
# ============================================================================

def x64_op0(operand, w):
    # unsigned long operand, w
    return u8(w ^ operand)

def x64_op1(operand, w):
    _tmp3 = u8(w)
    _tmp2 = u8(_tmp3 + u8(operand))
    return _tmp2

def x64_op2(operand, w):
    bw = u8(w)
    ret = bw | (bw << 8)  # 64-bit: no truncation
    ret >>= u8(operand & 7)
    return u8(ret)

def x64_op3(operand, w):
    b = 1 if (operand == 0) else 0
    bw = u8(w)
    ret = bw | (bw << 8)
    ret >>= b
    return u8(ret)

def x64_op4(operand, w):
    # u32 types in source — no unsigned long difference
    b = 1 if (operand == 0) else 0
    a = 1 if (u8(w) == 0) else 0
    ret = (a << 8) | a
    ret >>= b
    return u8(ret)

def x64_op5(operand, w):
    # u32 types in source
    ret = u8(~(w | w) & 0xFFFFFFFF)  # u32 NOR
    s = ((operand & 0xFFFFFFFF) << 29) >> 29  # u32 shifts
    s &= 0xFFFFFFFF
    ret |= (ret << 8)
    ret &= 0xFFFFFFFF
    ret >>= s
    return u8(ret)

def x64_op6(operand, w):
    # u32 types
    return u8((1 if w == 0 else 0) ^ operand)

def x64_op7(operand, w):
    # unsigned long operand, w
    nw = 1 if w == 0 else 0
    return u8((nw + operand) & 0xFF)  # source: (int)((!w + operand) & 0xFF)

def x64_op8(operand, w):
    # u32 types
    return u8(u8(w) + u8(operand)) ^ u8(operand)

def x64_op9(operand, w):
    # unsigned long b, a
    b = operand
    a = u8(w)
    return u8(((a ^ b) + b) & 0xFF)

def x64_op10(operand, w):
    # unsigned long
    bw = u8(w)
    ret = bw | (bw << 8)
    not_op = 1 if operand == 0 else 0
    ret >>= not_op
    ret ^= operand
    return u8(ret & 0xFF)

def x64_op11(operand, w):
    # unsigned long
    bw = u8(w)
    ret = bw | (bw << 8)
    ret >>= u8(operand & 7)
    ret ^= operand
    return u8(ret & 0xFF)

def x64_op12(operand, w):
    # unsigned long
    bw = u8(w)
    ret = bw | (bw << 8)
    ret >>= u8(operand & 7)
    return u8(ret + operand)

def x64_op13(operand, w):
    # unsigned long
    bw = u8(w)
    ret = bw | (bw << 8)
    not_op = 1 if operand == 0 else 0
    ret >>= not_op
    return u8(ret + operand)

def x64_op14(operand, w):
    bw = u8(w)
    ret = (bw >> 1) | (bw << 7)
    return u8(ret + operand)

def x64_op15(operand, w):
    bw = u8(w)
    ret = (bw >> 2) | (bw << 6)
    return u8(ret + operand)

def x64_op16(operand, w):
    bw = u8(w)
    ret = (bw >> 3) | (bw << 5)
    return u8(ret + operand)

def x64_op17(operand, w):
    bw = u8(w)
    ret = (bw >> 4) | (bw << 4)
    return u8(ret + operand)

def x64_op18(operand, w):
    bw = u8(w)
    ret = (bw >> 5) | (bw << 3)
    return u8(ret + operand)

def x64_op19(operand, w):
    bw = u8(w)
    ret = (bw >> 6) | (bw << 2)
    return u8(ret + operand)

def x64_op20(operand, w):
    bw = u8(w)
    ret = (bw >> 7) | (bw << 1)
    return u8(ret + operand)

def x64_op21(operand, w):
    br = u8(w)
    rot = (br >> 1) | (br << 7)
    return u8((rot ^ operand) & 0xFF)

def x64_op22(operand, w):
    br = u8(w)
    rot = (br >> 2) | (br << 6)
    return u8((rot ^ operand) & 0xFF)

def x64_op23(operand, w):
    br = u8(w)
    rot = (br >> 3) | (br << 5)
    return u8((rot ^ operand) & 0xFF)

def x64_op24(operand, w):
    br = u8(w)
    rot = (br >> 4) | (br << 4)
    return u8((rot ^ operand) & 0xFF)

def x64_op25(operand, w):
    br = u8(w)
    rot = (br >> 5) | (br << 3)
    return u8((rot ^ operand) & 0xFF)

def x64_op26(operand, w):
    br = u8(w)
    rot = (br >> 6) | (br << 2)
    return u8((rot ^ operand) & 0xFF)

def x64_op27(operand, w):
    br = u8(w)
    rot = (br >> 7) | (br << 1)
    return u8((rot ^ operand) & 0xFF)

def x64_op28(operand, w):
    br = u8(w)
    rot = (br >> 5) | (br << 3)
    return u8(((rot + operand) ^ operand) & 0xFF)

def x64_op29(operand, w):
    br = u8(w)
    rot = (br >> 3) | (br << 5)
    return u8(((rot + operand) ^ operand) & 0xFF)

def x64_op30(operand, w):
    br = u8(w)
    rot = (br >> 3) | (br << 5)
    return u8(((rot ^ operand) + operand) & 0xFF)

def x64_op31(operand, w):
    br = u8(w)
    rot = (br >> 5) | (br << 3)
    return u8(((rot ^ operand) + operand) & 0xFF)

def x64_op32(operand, w):
    # u32 types
    byteVal = u8(w)
    tmp = ((byteVal >> 3) ^ 0x1F) | ((byteVal & 7) << 5)
    return u8(tmp ^ operand)

def x64_op33(operand, w):
    w_byte = u8(w) & 0xFF
    tmp = ((w_byte >> 5) ^ 7) | ((w_byte & 0x1F) << 3)
    return u8(tmp ^ operand)

def x64_op34(operand, w):
    tmp = u8(w)
    val = ((tmp >> 2) ^ 0x3F) | ((tmp & 3) << 6)
    return u8(val ^ operand)

def x64_op35(operand, w):
    tmp = u8(w)
    val = ((tmp >> 6) ^ 3) | ((tmp & 0x3F) << 2)
    return u8(val ^ operand)

def x64_op36(operand, w):
    # unsigned long — ~br is 64-bit!
    br = u8(w)
    rot = (br >> 2) | ((~br) << 6)  # ~br is huge on 64-bit
    return u8((rot ^ operand) & 0xFF)

def x64_op37(operand, w):
    br = u8(w)
    rot = (br >> 5) | ((~br) << 3)
    return u8((rot ^ operand) & 0xFF)

def x64_op38(operand, w):
    br = u8(w)
    rot = (br >> 6) | ((~br) << 2)
    return u8((rot ^ operand) & 0xFF)

def x64_op39(operand, w):
    br = u8(w)
    rot = (br >> 3) | ((~br) << 5)
    return u8((rot ^ operand) & 0xFF)

def x64_op40(operand, w):
    v = u8(w)
    tmp = (((v << 8) | (v ^ 0x5C)) >> 6)
    return u8(tmp ^ operand)

def x64_op41(operand, w):
    v = u8(w)
    tmp = (u8(v >> 2) ^ 0x17) | ((v << 6) & 0xC0)
    return u8(tmp ^ operand)

def x64_op42(operand, w):
    # unsigned long
    v = u8(w)
    tmp = ((v >> 3) ^ 0xB) | ((v << 5) & 0xE0)
    return u8((tmp ^ operand) & 0xFF)

def x64_op43(operand, w):
    # unsigned long
    v = u8(w)
    tmp = ((v >> 5) ^ 2) | ((v & 0x1F) << 3)
    return u8((tmp ^ operand) & 0xFF)

def x64_op44(operand, w):
    # unsigned long
    v = u8(w)
    tmp = ((v >> 2) ^ 0xD) | ((v << 6) & 0xC0)
    return u8((tmp ^ operand) & 0xFF)

def x64_op45(operand, w):
    v = u8(w)
    highBits = u8((v >> 3) ^ 6)
    lowBits = u8((v & 7) << 5)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op46(operand, w):
    v = u8(w)
    highBits = u8((v >> 4) ^ 3)
    lowBits = u8((v << 4) & 0xF0)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op47(operand, w):
    v = u8(w)
    highBits = u8((v >> 1) ^ 0x1B)
    lowBits = u8((v & 1) << 7)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op48(operand, w):
    a = u8(w)
    working2 = (a >> 4) ^ 0x6
    working3 = ((a << 4) & 0xF0) ^ 0x5
    tmp = working2 | working3
    return u8(tmp ^ operand)

def x64_op49(operand, w):
    # u32 types
    v = u8(w)
    working3 = (v << 8) ^ 0x5C
    working2 = (v ^ 0x63)
    tmp = (working2 | working3) >> 3
    return u8(tmp ^ operand)

def x64_op50(operand, w):
    v = u8(w)
    highBits = u8(((v << 3) & 0xF8) ^ 2)
    lowBits = u8((v >> 5) ^ 3)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op51(operand, w):
    # u32 types
    v = u8(w)
    working2 = (v ^ 0x63)
    working3 = (v << 8) ^ 0x5C
    tmp = (working2 | working3) >> 6
    return u8(tmp ^ operand)

def x64_op52(operand, w):
    v = u8(w)
    highBits = u8((v >> 1) ^ 0x2e)
    lowBits = u8((v << 7) ^ 0x1b)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op53(operand, w):
    # unsigned long
    v = u8(w)
    working3 = (v << 8) ^ 0x36
    working2 = (v ^ 0x5C)
    tmp = (working2 | working3) >> 7
    return u8((tmp ^ operand) & 0xFF)

def x64_op54(operand, w):
    v = u8(w)
    part2 = (v << 5) & 0xE0
    part2 ^= 0x6
    part1 = (v >> 3) & 0xFF
    part1 ^= 0xB
    tmp = part1 | part2
    return u8(tmp ^ operand)

def x64_op55(operand, w):
    v = u8(w)
    highBits = u8(((v & 0x1f) << 3) ^ 1)
    lowBits = u8((v >> 5) ^ 2)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op56(operand, w):
    v = u8(w)
    highBits = u8(((v & 0xF) << 4) ^ 6)
    lowBits = u8((v >> 4) ^ 3)
    rotated = u8(highBits | lowBits)
    return u8(rotated ^ operand)

def x64_op57(operand, w):
    # u32 types
    v = u8(w)
    working2 = (v ^ 0x3C)
    working3 = (v << 8) ^ 0x65
    tmp = (working2 | working3) >> 5
    return u8(tmp ^ operand)

def x64_op58(operand, w):
    v = u8(w)
    working2 = (v ^ 0x65)
    working3 = (v << 8) ^ 0x3C
    tmp = (working2 | working3) >> 6
    return u8(tmp ^ operand)

def x64_op59(operand, w):
    v = u8(w)
    working2 = (v ^ 0x65)
    working3 = (v << 8) ^ 0x3C
    tmp = (working2 | working3) >> 2
    return u8(tmp ^ operand)

def x64_op60(operand, w):
    v = u8(w)
    xorLow = (v ^ 0xFF)
    xorHigh = (v << 8) ^ 0xAA
    tmp = (xorLow | xorHigh) >> 4
    return u8(tmp ^ operand)

def x64_op61(operand, w):
    # DTA swaps: operand=Int(2)=foo, w=Int(1)=bar[ix]
    operand_real = w    # foo
    rw = u8(operand)    # bar[ix]
    a = (rw >> 3) ^ 0x15
    b = ((rw & 7) << 5) ^ 0x1f
    tmp = a | b
    return u8(tmp ^ operand_real)

def x64_op62(operand, w):
    v = u8(w)
    tmp1 = u8((v >> 5) ^ 5)
    tmp2 = u8(((v << 3) & 0xF8) ^ 7)
    combined = u8(tmp1 | tmp2)
    return u8(combined ^ operand)

def x64_op63(operand, w):
    v = u8(w)
    xorLow = (v ^ 0xFF)
    xorHigh = (v << 8) ^ 0xAF
    tmp = (xorLow | xorHigh) >> 6
    return u8(tmp ^ operand)


# ============================================================================
# All ops arrays
# ============================================================================
ppc_ops = [
    ppc_op0, ppc_op1, ppc_op2, ppc_op3, ppc_op4, ppc_op5, ppc_op6, ppc_op7,
    ppc_op8, ppc_op9, ppc_op10, ppc_op11, ppc_op12, ppc_op13, ppc_op14, ppc_op15,
    ppc_op16, ppc_op17, ppc_op18, ppc_op19, ppc_op20, ppc_op21, ppc_op22, ppc_op23,
    ppc_op24, ppc_op25, ppc_op26, ppc_op27, ppc_op28, ppc_op29, ppc_op30, ppc_op31,
    ppc_op32, ppc_op33, ppc_op34, ppc_op35, ppc_op36, ppc_op37, ppc_op38, ppc_op39,
    ppc_op40, ppc_op41, ppc_op42, ppc_op43, ppc_op44, ppc_op45, ppc_op46, ppc_op47,
    ppc_op48, ppc_op49, ppc_op50, ppc_op51, ppc_op52, ppc_op53, ppc_op54, ppc_op55,
    ppc_op56, ppc_op57, ppc_op58, ppc_op59, ppc_op60, ppc_op61, ppc_op62, ppc_op63,
]

x64_ops = [
    x64_op0, x64_op1, x64_op2, x64_op3, x64_op4, x64_op5, x64_op6, x64_op7,
    x64_op8, x64_op9, x64_op10, x64_op11, x64_op12, x64_op13, x64_op14, x64_op15,
    x64_op16, x64_op17, x64_op18, x64_op19, x64_op20, x64_op21, x64_op22, x64_op23,
    x64_op24, x64_op25, x64_op26, x64_op27, x64_op28, x64_op29, x64_op30, x64_op31,
    x64_op32, x64_op33, x64_op34, x64_op35, x64_op36, x64_op37, x64_op38, x64_op39,
    x64_op40, x64_op41, x64_op42, x64_op43, x64_op44, x64_op45, x64_op46, x64_op47,
    x64_op48, x64_op49, x64_op50, x64_op51, x64_op52, x64_op53, x64_op54, x64_op55,
    x64_op56, x64_op57, x64_op58, x64_op59, x64_op60, x64_op61, x64_op62, x64_op63,
]


# ============================================================================
# Helper: build hash/permutation tables
# ============================================================================

def build_hash5(seed):
    mapping = [0] * 256
    seed = seed & 0xFFFFFFFF
    for i in range(256):
        mapping[i] = (seed >> 3) & 0x1F
        seed = (seed * LCG_MUL + LCG_INC) & 0xFFFFFFFF
    return mapping

def build_hash6(seed):
    mapping = [0] * 256
    seed = seed & 0xFFFFFFFF
    for i in range(256):
        mapping[i] = (seed >> 2) & 0x3F
        seed = (seed * LCG_MUL + LCG_INC) & 0xFFFFFFFF
    return mapping

def generate_permutation32(seed):
    seed = seed & 0xFFFFFFFF
    used = [False] * 32
    perm = []
    for _ in range(32):
        while True:
            seed = (seed * LCG_MUL + LCG_INC) & 0xFFFFFFFF
            idx = (seed >> 2) & 0x1F
            if not used[idx]:
                used[idx] = True
                perm.append(idx)
                break
    return perm, seed


# ============================================================================
# GrindArray simulation
# ============================================================================

def grind_array(seedA, seedB, array_in, mogg_version, ops_table, label=""):
    enc_method = {12: 0, 13: 0, 14: 1, 15: 2, 16: 3}.get(mogg_version, -1)
    assert enc_method >= 0

    hashMap5 = build_hash5(seedA)
    hashMap6 = build_hash6(seedB)

    # Init permutation: ops 0-31 seeded with 0xD5, ops 32-63 with 0x23E
    initPerm0, _ = generate_permutation32(0xD5)
    initPerm1, _ = generate_permutation32(0x23E)

    oNumberToOpIdx = [-1] * 64
    for i in range(32):
        oNumberToOpIdx[initPerm0[i]] = i
        oNumberToOpIdx[initPerm1[i] + 32] = 32 + i

    # GrindArray's per-call op permutation
    grindPermB, _ = generate_permutation32(seedB)
    caseToOp = [None] * 64
    for i in range(32):
        opIdx = oNumberToOpIdx[grindPermB[i]]
        if opIdx >= 0:
            caseToOp[i] = ops_table[opIdx]

    if enc_method != 0:
        grindPermA, _ = generate_permutation32(seedA)
        for i in range(32):
            opIdx = oNumberToOpIdx[grindPermA[i] + 32]
            if opIdx >= 0:
                caseToOp[32 + i] = ops_table[opIdx]

    hashMap = hashMap6 if enc_method != 0 else hashMap5
    maxCase = 64 if enc_method != 0 else 32

    arr = list(array_in)  # work on copy

    # Also track which ops are called and with what args
    trace = []

    for i in range(len(arr)):
        foo = arr[i]
        ix = 0
        byte_trace = []
        while ix < 0x10:
            barVal = arr[ix]
            h = hashMap[barVal & 0xFF]
            if h < maxCase and caseToOp[h] is not None:
                ix += 1
                if ix < 0x10:
                    old_foo = foo
                    foo = caseToOp[h](arr[ix], foo)
                    byte_trace.append((h, arr[ix], old_foo, foo))
            ix += 1
        arr[i] = foo & 0xFF
        trace.append(byte_trace)

    return arr, trace


def grind_array_verbose(seedA, seedB, array_in, mogg_version, ppc_ops_table, x64_ops_table):
    """Run both PPC and x64 versions step by step and find first divergence."""
    enc_method = {12: 0, 13: 0, 14: 1, 15: 2, 16: 3}.get(mogg_version, -1)
    assert enc_method >= 0

    hashMap5 = build_hash5(seedA)
    hashMap6 = build_hash6(seedB)

    initPerm0, _ = generate_permutation32(0xD5)
    initPerm1, _ = generate_permutation32(0x23E)

    oNumberToOpIdx = [-1] * 64
    for i in range(32):
        oNumberToOpIdx[initPerm0[i]] = i
        oNumberToOpIdx[initPerm1[i] + 32] = 32 + i

    grindPermB, _ = generate_permutation32(seedB)
    ppc_caseToOp = [None] * 64
    x64_caseToOp = [None] * 64
    caseToOpName = [""] * 64
    for i in range(32):
        opIdx = oNumberToOpIdx[grindPermB[i]]
        if opIdx >= 0:
            ppc_caseToOp[i] = ppc_ops_table[opIdx]
            x64_caseToOp[i] = x64_ops_table[opIdx]
            caseToOpName[i] = f"op{opIdx}"

    if enc_method != 0:
        grindPermA, _ = generate_permutation32(seedA)
        for i in range(32):
            opIdx = oNumberToOpIdx[grindPermA[i] + 32]
            if opIdx >= 0:
                ppc_caseToOp[32 + i] = ppc_ops_table[opIdx]
                x64_caseToOp[32 + i] = x64_ops_table[opIdx]
                caseToOpName[32 + i] = f"op{opIdx}"

    hashMap = hashMap6 if enc_method != 0 else hashMap5
    maxCase = 64 if enc_method != 0 else 32

    ppc_arr = list(array_in)
    x64_arr = list(array_in)

    divergences = []

    for i in range(len(array_in)):
        ppc_foo = ppc_arr[i]
        x64_foo = x64_arr[i]
        ix = 0
        step = 0
        while ix < 0x10:
            barVal_ppc = ppc_arr[ix]
            barVal_x64 = x64_arr[ix]
            h_ppc = hashMap[barVal_ppc & 0xFF]
            h_x64 = hashMap[barVal_x64 & 0xFF]

            if h_ppc < maxCase and ppc_caseToOp[h_ppc] is not None:
                ix_inner = ix + 1
                if ix_inner < 0x10:
                    operand_ppc = ppc_arr[ix_inner]
                    operand_x64 = x64_arr[ix_inner]
                    old_ppc_foo = ppc_foo
                    old_x64_foo = x64_foo
                    ppc_foo = ppc_caseToOp[h_ppc](operand_ppc, ppc_foo)
                    x64_foo = x64_caseToOp[h_x64](operand_x64, x64_foo)

                    if ppc_foo != x64_foo:
                        divergences.append({
                            'byte_idx': i,
                            'step': step,
                            'ix': ix,
                            'hash': h_ppc,
                            'op_name': caseToOpName[h_ppc],
                            'operand_ppc': operand_ppc,
                            'operand_x64': operand_x64,
                            'foo_in_ppc': old_ppc_foo,
                            'foo_in_x64': old_x64_foo,
                            'foo_out_ppc': ppc_foo,
                            'foo_out_x64': x64_foo,
                        })

                ix = ix_inner
                step += 1
            ix += 1

        ppc_arr[i] = ppc_foo & 0xFF
        x64_arr[i] = x64_foo & 0xFF

    return ppc_arr, x64_arr, divergences


# ============================================================================
# First: exhaustive op-by-op comparison for all 256x256 inputs
# ============================================================================

def scan_all_ops_exhaustive():
    """For each of the 64 ops, test all 256x256 (operand, w) pairs."""
    print("=" * 70)
    print("PHASE 1: Exhaustive op-by-op scan (256x256 inputs per op)")
    print("=" * 70)

    divergent_ops = []
    for op_idx in range(64):
        ppc_fn = ppc_ops[op_idx]
        x64_fn = x64_ops[op_idx]
        count = 0
        first_example = None
        for operand in range(256):
            for w in range(256):
                ppc_result = ppc_fn(operand, w)
                x64_result = x64_fn(operand, w)
                if ppc_result != x64_result:
                    count += 1
                    if first_example is None:
                        first_example = (operand, w, ppc_result, x64_result)

        if count > 0:
            divergent_ops.append((op_idx, count, first_example))
            op, w, pr, xr = first_example
            print(f"  op{op_idx}: {count}/65536 divergent pairs! "
                  f"First: operand=0x{op:02x} w=0x{w:02x} -> ppc=0x{pr:02x} x64=0x{xr:02x}")
        else:
            pass  # op is clean

    if not divergent_ops:
        print("  All 64 ops produce IDENTICAL results for byte-range inputs (0-255).")
    else:
        print(f"\n  SUMMARY: {len(divergent_ops)} ops diverge for byte-range inputs")

    return divergent_ops


# ============================================================================
# Now test with actual FULL-WIDTH inputs (DTA Int() returns signed 32-bit)
# Some ops receive bar[ix] as operand — those are always 0-255.
# But foo (w) could be any value... wait, no:
#   - foo starts as arrayToGrind[i] (0-255)
#   - foo = op(bar[ix], foo) where op returns u8 → foo is always 0-255
# So both operand and w are always in 0-255 range for GrindArray.
# BUT: The DTA version calls msg->Int(1) / msg->Int(2) which returns signed int.
# For byte values 0-255, this is always positive, so unsigned long = same.
# ============================================================================


# ============================================================================
# PHASE 2: Full GrindArray simulation
# ============================================================================

def run_grindarray_test():
    print("\n" + "=" * 70)
    print("PHASE 2: Full GrindArray simulation with test vectors")
    print("=" * 70)

    pre_grind = [0xdf, 0x60, 0x0c, 0xcc, 0x86, 0x92, 0x4f, 0x59,
                 0x91, 0xce, 0x09, 0x40, 0x94, 0x40, 0x46, 0x58]
    seedA = 602494344
    seedB = 625836951
    mogg_version = 14

    expected_x64 = [0x07, 0x2f, 0x0d, 0x3f, 0x15, 0x05, 0x37, 0x47,
                    0xcd, 0xf5, 0x8f, 0xa7, 0x45, 0x5f, 0xaf, 0x97]

    print(f"  Input:    {' '.join(f'{b:02x}' for b in pre_grind)}")
    print(f"  seedA:    {seedA} (0x{seedA:08x})")
    print(f"  seedB:    {seedB} (0x{seedB:08x})")
    print(f"  moggVer:  {mogg_version}")

    # Run both
    ppc_result, ppc_trace = grind_array(seedA, seedB, pre_grind, mogg_version, ppc_ops, "PPC")
    x64_result, x64_trace = grind_array(seedA, seedB, pre_grind, mogg_version, x64_ops, "x64")

    print(f"\n  PPC  output: {' '.join(f'{b:02x}' for b in ppc_result)}")
    print(f"  x64  output: {' '.join(f'{b:02x}' for b in x64_result)}")
    print(f"  Expected x64:{' '.join(f'{b:02x}' for b in expected_x64)}")

    if x64_result == expected_x64:
        print("  [OK] x64 output matches expected")
    else:
        print("  [MISMATCH] x64 output does NOT match expected!")
        for i in range(16):
            if x64_result[i] != expected_x64[i]:
                print(f"    byte[{i}]: got 0x{x64_result[i]:02x}, expected 0x{expected_x64[i]:02x}")

    if ppc_result == x64_result:
        print("  [SAME] PPC and x64 produce identical output for this input")
    else:
        print(f"\n  [DIVERGENCE FOUND] PPC != x64")
        for i in range(16):
            marker = " <-- DIFF" if ppc_result[i] != x64_result[i] else ""
            print(f"    byte[{i:2d}]: ppc=0x{ppc_result[i]:02x}  x64=0x{x64_result[i]:02x}{marker}")

    return ppc_result, x64_result, x64_result == expected_x64


# ============================================================================
# PHASE 3: Step-by-step verbose trace to find first divergence
# ============================================================================

def run_verbose_trace():
    print("\n" + "=" * 70)
    print("PHASE 3: Step-by-step verbose divergence trace")
    print("=" * 70)

    pre_grind = [0xdf, 0x60, 0x0c, 0xcc, 0x86, 0x92, 0x4f, 0x59,
                 0x91, 0xce, 0x09, 0x40, 0x94, 0x40, 0x46, 0x58]
    seedA = 602494344
    seedB = 625836951
    mogg_version = 14

    ppc_arr, x64_arr, divergences = grind_array_verbose(
        seedA, seedB, pre_grind, mogg_version, ppc_ops, x64_ops
    )

    if not divergences:
        print("  No step-level divergences found.")
    else:
        print(f"  Found {len(divergences)} step-level divergence(s):")
        for d in divergences[:20]:  # show first 20
            print(f"\n    byte[{d['byte_idx']}] step {d['step']}:")
            print(f"      ix={d['ix']}, hash={d['hash']}, op={d['op_name']}")
            print(f"      operand: ppc=0x{d['operand_ppc']:02x}  x64=0x{d['operand_x64']:02x}")
            print(f"      foo_in:  ppc=0x{d['foo_in_ppc']:02x}  x64=0x{d['foo_in_x64']:02x}")
            print(f"      foo_out: ppc=0x{d['foo_out_ppc']:02x}  x64=0x{d['foo_out_x64']:02x}")


# ============================================================================
# PHASE 4: Check the native nop* implementations match PPC behavior
# ============================================================================

def check_native_vs_ppc():
    """
    The native nop* functions use u32 everywhere. They SHOULD match PPC behavior.
    Let's verify by implementing them here and comparing.
    """
    print("\n" + "=" * 70)
    print("PHASE 4: Native nop* (u32) vs DTA PPC (unsigned long=32bit)")
    print("=" * 70)

    # The native nop* are already using u32, which is what ppc_ops simulates.
    # Let's just run the native GrindArray logic and compare with ppc_ops.
    # Since ppc_ops IS the u32 simulation, they should be identical.
    # The real question is: does the ACTUAL native C++ code match?
    # We can't run C++ here, but we can check the Python model.

    pre_grind = [0xdf, 0x60, 0x0c, 0xcc, 0x86, 0x92, 0x4f, 0x59,
                 0x91, 0xce, 0x09, 0x40, 0x94, 0x40, 0x46, 0x58]
    seedA = 602494344
    seedB = 625836951
    mogg_version = 14

    ppc_result, _ = grind_array(seedA, seedB, pre_grind, mogg_version, ppc_ops, "PPC")
    expected_x64 = [0x07, 0x2f, 0x0d, 0x3f, 0x15, 0x05, 0x37, 0x47,
                    0xcd, 0xf5, 0x8f, 0xa7, 0x45, 0x5f, 0xaf, 0x97]

    print(f"  PPC (32-bit) output:  {' '.join(f'{b:02x}' for b in ppc_result)}")
    print(f"  Native x64 expected:  {' '.join(f'{b:02x}' for b in expected_x64)}")

    if ppc_result == expected_x64:
        print("  [SAME] PPC 32-bit model matches expected x64 output")
        print("  This means the native nop* functions (u32) produce correct PPC-equivalent results.")
        print("  The actual native C++ binary may have a bug in the nop* implementations.")
    else:
        print("  [DIFFERENT] PPC 32-bit model differs from expected x64 output")
        print("  This means the native nop* functions DON'T match PPC behavior,")
        print("  OR the expected x64 output came from a different code path.")

    # Also compare: what does the DTA x64 path give?
    x64_result, _ = grind_array(seedA, seedB, pre_grind, mogg_version, x64_ops, "x64-DTA")
    print(f"\n  x64 DTA model output: {' '.join(f'{b:02x}' for b in x64_result)}")
    if x64_result == expected_x64:
        print("  [MATCH] x64 DTA model matches expected output")
        print("  --> The 'native x64 output' was produced by DTA ops with 64-bit unsigned long!")
    else:
        print("  [NO MATCH] x64 DTA model does NOT match expected output")
    # Returned, not merely printed: the CONCLUSION block below is gated on it.
    return {"ppc32_matches_expected": ppc_result == expected_x64,
            "x64_dta_matches_expected": x64_result == expected_x64}


# ============================================================================
# PHASE 5: Identify which specific ops cause DTA 32-bit vs 64-bit differences
# for the specific operand/w values that occur during THIS grind
# ============================================================================

def trace_specific_ops():
    print("\n" + "=" * 70)
    print("PHASE 5: Trace which ops fire for this specific key grind")
    print("=" * 70)

    pre_grind = [0xdf, 0x60, 0x0c, 0xcc, 0x86, 0x92, 0x4f, 0x59,
                 0x91, 0xce, 0x09, 0x40, 0x94, 0x40, 0x46, 0x58]
    seedA = 602494344
    seedB = 625836951
    mogg_version = 14

    enc_method = 1  # moggVersion 14

    hashMap6 = build_hash6(seedB)

    initPerm0, _ = generate_permutation32(0xD5)
    initPerm1, _ = generate_permutation32(0x23E)

    oNumberToOpIdx = [-1] * 64
    for i in range(32):
        oNumberToOpIdx[initPerm0[i]] = i
        oNumberToOpIdx[initPerm1[i] + 32] = 32 + i

    grindPermB, _ = generate_permutation32(seedB)
    caseToOpIdx = [-1] * 64
    for i in range(32):
        opIdx = oNumberToOpIdx[grindPermB[i]]
        if opIdx >= 0:
            caseToOpIdx[i] = opIdx

    grindPermA, _ = generate_permutation32(seedA)
    for i in range(32):
        opIdx = oNumberToOpIdx[grindPermA[i] + 32]
        if opIdx >= 0:
            caseToOpIdx[32 + i] = opIdx

    hashMap = hashMap6
    maxCase = 64

    print("\n  Hash6 mapping for input bytes:")
    for i, b in enumerate(pre_grind):
        h = hashMap[b & 0xFF]
        op_idx = caseToOpIdx[h] if h < maxCase else -1
        has_op = caseToOpIdx[h] is not None and caseToOpIdx[h] >= 0 if h < maxCase else False
        print(f"    bar[{i:2d}] = 0x{b:02x} -> hash6={h:2d} -> {'op'+str(op_idx) if has_op else 'NO OP'}")

    print("\n  Case-to-op mapping (first 64 entries):")
    for i in range(64):
        if caseToOpIdx[i] >= 0:
            print(f"    case {i:2d} -> op{caseToOpIdx[i]}")

    # Now trace byte 0 in detail
    print("\n  Detailed trace for byte[0] (initial foo=0xdf):")
    # The stored conclusion turns on "op5 is NEVER called during this key's
    # grind". That is a claim about THIS run's data, so compute it here and
    # hand it back rather than letting the conclusion assert it.
    ops_that_fire = set()
    for b in pre_grind:
        h = hashMap[b & 0xFF]
        if h < maxCase and caseToOpIdx[h] >= 0:
            ops_that_fire.add(caseToOpIdx[h])
    diverged_in_trace = False
    arr = list(pre_grind)
    foo = arr[0]
    ix = 0
    step = 0
    while ix < 0x10:
        barVal = arr[ix]
        h = hashMap[barVal & 0xFF]
        has_op = h < maxCase and caseToOpIdx[h] >= 0
        if has_op:
            ix += 1
            if ix < 0x10:
                operand = arr[ix]
                old_foo = foo
                op_idx = caseToOpIdx[h]
                ppc_result = ppc_ops[op_idx](operand, foo)
                x64_result = x64_ops[op_idx](operand, foo)
                if ppc_result != x64_result:
                    diverged_in_trace = True
                marker = " *** DIVERGE ***" if ppc_result != x64_result else ""
                print(f"    step {step}: ix={ix-1}->bar[{ix-1}]=0x{barVal:02x} hash={h} -> "
                      f"op{op_idx}(operand=0x{operand:02x}, foo=0x{old_foo:02x}) "
                      f"-> ppc=0x{ppc_result:02x} x64=0x{x64_result:02x}{marker}")
                foo = ppc_result  # follow PPC path for trace
            step += 1
        else:
            print(f"    step {step}: ix={ix} bar[{ix}]=0x{barVal:02x} hash={h} -> NO OP (skip)")
            step += 1
        ix += 1
    print(f"    Final foo (PPC): 0x{foo:02x}")
    print(f"\n  Ops that actually FIRE for this key: {sorted(ops_that_fire)}")
    return {"ops_that_fire": ops_that_fire, "diverged_in_trace": diverged_in_trace}


# ============================================================================
# Main
# ============================================================================

if __name__ == "__main__":
    divergent_ops = scan_all_ops_exhaustive()
    ppc_result, x64_result, x64_matched_expected = run_grindarray_test()
    run_verbose_trace()
    native_checks = check_native_vs_ppc()
    fired = trace_specific_ops()

    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    if divergent_ops:
        print(f"  {len(divergent_ops)} ops produce different results for byte-range (0-255) inputs:")
        for op_idx, count, (op, w, pr, xr) in divergent_ops:
            print(f"    op{op_idx}: {count} divergent pairs")
    else:
        print("  All 64 ops produce identical results for byte-range inputs.")
        print("  The divergence must come from the GrindArray loop logic itself,")
        print("  or from the native nop* implementations having a bug vs the DTA ops.")

    # ------------------------------------------------------------------ #
    # The CONCLUSION block below used to be an UNCONDITIONAL print of a fixed
    # string.  Verified 2026-08-19 by sabotaging every x64_op to return
    # (ppc + 1) & 0xFF: the script duly printed `[DIVERGENCE FOUND]`,
    # `[NO MATCH]` and `*** DIVERGE ***` -- and then, sixty lines later, this
    # block printed BYTE-IDENTICAL to the clean run (1,296 B, empty diff, exit
    # 0 both times), still asserting "GrindArray is NOT the divergence source"
    # and still routing the reader to "Investigate the key derivation pipeline
    # BEFORE GrindArray".
    #
    # A hardcoded verdict that tells the next engineer where NOT to look is
    # load-bearing by definition.  It is now gated on the evidence this script
    # actually computes, and the refuting case prints the refutation instead.
    # ------------------------------------------------------------------ #
    # The premise is NOT "no op diverges" -- op5 does, and the conclusion says
    # so.  It is "no op that FIRES for this key diverges".  Compute exactly
    # that, from the two sets this run produced.
    divergent_idxs = {o[0] for o in divergent_ops}
    firing_and_divergent = sorted(divergent_idxs & fired["ops_that_fire"])
    supports_conclusion = (
        not firing_and_divergent
        and not fired["diverged_in_trace"]
        and ppc_result == x64_result
        and x64_matched_expected
        and native_checks["ppc32_matches_expected"]
        and native_checks["x64_dta_matches_expected"]
    )

    print("\n" + "=" * 70)
    if not supports_conclusion:
        print("NO CONCLUSION: this run's own evidence CONTRADICTS the "
              "GrindArray-is-innocent finding")
        print("=" * 70)
        print("  The stored conclusion below was reached on a run where all of "
              "the following held.")
        print("  This run disagrees, so it is NOT reproduced -- re-derive it "
              "before quoting it.\n")
        for label, ok in (
            ("no op that FIRES for this key diverges (op5 diverges but is "
             "never called -- that is the conclusion's own premise)",
             not firing_and_divergent),
            ("no divergence observed in the step-by-step trace",
             not fired["diverged_in_trace"]),
            ("PPC and x64 GrindArray outputs are identical", ppc_result == x64_result),
            ("x64 GrindArray output matches the expected vector", x64_matched_expected),
            ("PPC 32-bit model matches the expected vector",
             native_checks["ppc32_matches_expected"]),
            ("x64 DTA model matches the expected vector",
             native_checks["x64_dta_matches_expected"]),
        ):
            print(f"    [{'ok ' if ok else 'FAIL'}] {label}")
        if firing_and_divergent:
            print(f"\n  op(s) that BOTH fire for this key AND diverge: "
                  f"{firing_and_divergent}")
        if divergent_ops:
            print(f"  divergent op(s) overall: {sorted(divergent_idxs)} "
                  f"(ops that fire: {sorted(fired['ops_that_fire'])})")
        print("\n  GrindArray is a LIVE SUSPECT on this run. Do not skip it.")
        print("=" * 70)
        sys.exit(1)

    print("CONCLUSION: GrindArray is NOT the divergence source")
    print("=" * 70)
    print("""
  For this specific mogg (boyfriend.mogg, seeds A=602494344, B=625836951):

  1. Only op5 has 32-bit vs 64-bit divergence, but op5 is NEVER called
     during this key's grind (it maps to case 8, and no input byte hashes to 8).

  2. PPC 32-bit model output == x64 64-bit model output == expected native output
     All produce: 07 2f 0d 3f 15 05 37 47 cd f5 8f a7 45 5f af 97

  3. The session doc (2026-03-23-mogg-v0xe-decrypt-failure.md) already confirmed:
     "Even with GrindArray as NO-OP, decryption still fails"
     This means the problem is UPSTREAM of GrindArray (in getKey, header parsing,
     or the AES-CTR pipeline).

  4. The native C++ nop* functions use u32 (always 32-bit) and should produce
     identical results to PPC. The only theoretical risk is op5's
     (operand<<29)>>29 pattern on u32 — but op5 is never called here.

  Next steps: Investigate the key derivation pipeline BEFORE GrindArray:
  - Verify hiddenKeys data section is loaded correctly on native
  - Verify getKey() output matches PPC byte-for-byte
  - Run full setupCypher in Unicorn with hiddenKeys mapped
  - Check if mogg header bytes (nonce, keyMask) are read identically
""")
