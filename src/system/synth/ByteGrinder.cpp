#include "synth/ByteGrinder.h"

#include <string.h>
#include <stdio.h>
#include <vector>
#include "types.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "utl/Str.h"
#include "os/Debug.h"
#include "os/System.h"
#include "synth/tomcrypt/mycrypt.h"

static unsigned char gHvKeyGreen[64] = {
    0x01, 0x22, 0x00, 0x38, 0xd2, 0x01, 0x78, 0x8b, 0xdd, 0xcd, 0xd0, 0xf0, 0xfe,
    0x3e, 0x24, 0x7f, 0x51, 0x73, 0xad, 0xe5, 0xb3, 0x99, 0xb8, 0x61, 0x58, 0x1a,
    0xf9, 0xb8, 0x1e, 0xa7, 0xbe, 0xbf, 0xc6, 0x22, 0x94, 0x30, 0xd8, 0x3c, 0x84,
    0x14, 0x08, 0x73, 0x7c, 0xf2, 0x23, 0xf6, 0xeb, 0x5a, 0x02, 0x1a, 0x83, 0xf3,
    0x97, 0xe9, 0xd4, 0xb8, 0x06, 0x74, 0x14, 0x6b, 0x30, 0x4c, 0x00, 0x91
};

namespace {
    int GetEncMethod(int ver) {
        int ret = 0;
        switch (ver) {
        case 0xc:
        case 0xd:
            ret = 0;
            break;
        case 0xe:
            ret = 1;
            break;
        case 0xf:
            ret = 2;
            break;
        case 0x10:
            ret = 3;
            break;
        default:
            MILO_NOTIFY(" Wrong encryption version passed to ByteGrinder: [%d] !\n", ver);
            break;
        }
        return ret;
    }
}

void ByteGrinder::HvDecrypt(unsigned char *inBlock, unsigned char *outBlock, int moggVer) {
    symmetric_key key;
    int enc_method = GetEncMethod(moggVer);
    void *placeholder = operator new(0x20C);
    rijndael_setup(&gHvKeyGreen[enc_method * 0x10], 0x10, 0, &key);
    rijndael_ecb_decrypt(inBlock, outBlock, &key);
    delete placeholder;
}

DataNode hashTo5Bits(DataArray *da) {
    static u32 hashMapping[0x100];
    u32 seed = da->Int(1) & 0xFF;
    u32 ret = hashMapping[seed];

    bool moreThanTwo = da->Size() > 2;
    if (moreThanTwo) {
        seed = da->Int(1);
        int max = DIM(hashMapping);
        for (int idx = 0; idx < max; idx++) {
            hashMapping[idx] = (seed >> 3) & 0x1F;
            seed = (seed * 0x19660D) + 0x3C6EF35F;
        }
        return DataNode(kDataInt, 0);
    }
    return DataNode(kDataInt, ret);
}

DataNode hashTo6Bits(DataArray *da) {
    static u32 hashMapping[0x100];
    u32 seed = da->Int(1) & 0xFF;
    u32 ret = hashMapping[seed];

    bool moreThanTwo = da->Size() > 2;
    if (moreThanTwo) {
        seed = da->Int(1);
        int max = DIM(hashMapping);
        for (int idx = 0; idx < max; idx++) {
            hashMapping[idx] = (seed >> 2) & 0x3F;
            seed = (seed * 0x19660D) + 0x3C6EF35F;
        }
        return DataNode(kDataInt, 0);
    }
    return DataNode(kDataInt, ret);
}

DataNode getRandomSequence32A(DataArray *da) {
    static u32 s_seed = 0x521;
    static bool usedUp[0x20];

    bool hasArgs = da->Size() > 1;
    if (hasArgs) {
        int dataint = da->Int(1);
        memset(usedUp, 0, 0x20);
        if ((unsigned int)dataint != 0) {
            s_seed = dataint;
        }
        return DataNode(kDataInt, 0x610A660F);
    } else {
        bool loop = true;
        int idx = 0;
        while (loop) {
            s_seed = s_seed * 0x19660D + 0x3C6EF35F;
            idx = (s_seed >> 2 & 0x1F);
            if (usedUp[idx] == false) {
                loop = false;
                usedUp[idx] = true;
            }
        }
        return DataNode(kDataInt, idx);
    }
}

DataNode getRandomSequence32B(DataArray *da) {
    static u32 s_seed = 0x303F;
    static bool usedUp[0x20];

    bool hasArgs = da->Size() > 1;
    if (hasArgs) {
        int dataint = da->Int(1);
        memset(usedUp, 0, 0x20);
        if ((unsigned int)dataint != 0) {
            s_seed = dataint;
        }
        return DataNode(kDataInt, 0x610A660F);
    } else {
        bool loop = true;
        int idx = 0;
        while (loop) {
            s_seed = s_seed * 0x19660D + 0x3C6EF35F;
            idx = (s_seed >> 2 & 0x1F);
            if (usedUp[idx] == false) {
                loop = false;
                usedUp[idx] = true;
            }
        }
        return DataNode(kDataInt, idx);
    }
}
#define OP_ROT_L(byte, dist)                                                             \
    (unsigned char)((byte << (dist & 31) | byte >> (8 - dist & 31)) & 255)
#define OP_ROT_R(byte, dist)                                                             \
    (unsigned char)((byte >> (dist & 31) | byte << (8 - dist & 31)) & 255)

DataNode op0(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    return DataNode(kDataInt, u8(w ^ operand));
}

DataNode op1(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    auto _tmp3 = u8(w);
    auto _tmp2 = u8(_tmp3 + u8(operand));
    auto _tmp1 = DataNode(kDataInt, _tmp2);
    return _tmp1;
}

DataNode op2(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= u8(operand & 7);
    return DataNode(kDataInt, u8(ret));
}

DataNode op3(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    bool b = (operand == 0);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= b;
    return DataNode(u8(ret));
}

DataNode op4(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = msg->Int(2);
    u32 b = (operand == 0);
    u32 a = (u8(w) == 0);
    u32 ret = (a << 8) | a;
    ret >>= b;
    return u8(ret);
}

DataNode op5(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = msg->Int(2);
    u32 ret;
    // r5 = u8(r3 NOR r3)
    // r3 = (r31 << 29) >> 29;
    // r5 |= r5 << 8
    // r5 >>= r3
    // r0 = u8(r5)
    ret = u8(~(w | w));
    u32 r3 = (operand << 29) >> 29, r4 = ret << 8;
    ret |= r4;
    ret >>= r3;
    return DataNode(kDataInt, u8(ret));
}

DataNode op6(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = msg->Int(2);
    return u8(!w ^ operand);
}

DataNode op7(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    return DataNode(kDataInt, (int)((!w + operand) & 0xFF));
}

DataNode op8(DataArray *msg) {
    u32 op = msg->Int(1);
    return u8(msg->Int(2)) + u8(op) ^ u8(op);
}

DataNode op9(DataArray *msg) {
    unsigned long b = msg->Int(1);
    unsigned long a = u8(msg->Int(2));
    return DataNode(kDataInt, (int)(((a ^ b) + b) & 0xFF));
}

DataNode op10(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= !operand;
    ret ^= operand;
    return DataNode(kDataInt, (int)(ret & 0xFF));
}

DataNode op11(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= u8(operand & 7);
    ret ^= operand;
    return DataNode(kDataInt, (int)(ret & 0xFF));
}

DataNode op12(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= u8(operand & 7);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op13(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = bw | (bw << 8);
    ret >>= !operand;
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op14(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 1) | (bw << 7);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op15(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 2) | (bw << 6);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op16(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 3) | (bw << 5);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op17(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 4) | (bw << 4);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op18(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 5) | (bw << 3);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op19(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 6) | (bw << 2);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op20(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = msg->Int(2);
    unsigned long bw = u8(w);
    unsigned long ret = (bw >> 7) | (bw << 1);
    return DataNode(kDataInt, u8(ret + operand));
}

DataNode op21(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 1) | (br << 7);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op22(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 2) | (br << 6);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op23(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 3) | (br << 5);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op24(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 4) | (br << 4);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op25(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 5) | (br << 3);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op26(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 6) | (br << 2);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op27(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 7) | (br << 1);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op28(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 5) | (br << 3);
    return DataNode(kDataInt, (int)(((rot + l) ^ l) & 0xFF));
}

DataNode op29(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 3) | (br << 5);
    return DataNode(kDataInt, (int)(((rot + l) ^ l) & 0xFF));
}

DataNode op30(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 3) | (br << 5);
    return DataNode(kDataInt, (int)(((rot ^ l) + l) & 0xFF));
}

DataNode op31(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 5) | (br << 3);
    return DataNode(kDataInt, (int)(((rot ^ l) + l) & 0xFF));
}

DataNode op32(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 byteVal = w;
    u32 tmp = ((byteVal >> 3) ^ 0x1F) | ((byteVal & 7) << 5);
    return u8(tmp ^ operand);
}

DataNode op33(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 w_byte = w & 0xFF;
    u32 tmp = ((w_byte >> 5) ^ 7) | ((w_byte & 0x1F) << 3);
    return u8(tmp ^ operand);
}

DataNode op34(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 tmp = w;
    u32 val = ((tmp >> 2) ^ 0x3F) | ((tmp & 3) << 6);
    return u8(val ^ operand);
}

DataNode op35(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 tmp = w;
    u32 val = ((tmp >> 6) ^ 3) | ((tmp & 0x3F) << 2);
    return u8(val ^ operand);
}

DataNode op36(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long r = msg->Int(2);
    unsigned long br = u8(r);
    unsigned long rot = (br >> 2) | ((~br) << 6);
    return DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
}

DataNode op37(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long br = u8(msg->Int(2));
    unsigned long rot = (br >> 5) | ((~br) << 3);
    auto _tmp4 = DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
    return _tmp4;
}

DataNode op38(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long br = u8(msg->Int(2));
    unsigned long rot = (br >> 6) | ((~br) << 2);
    auto _tmp6 = DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
    return _tmp6;
}

DataNode op39(DataArray *msg) {
    unsigned long l = msg->Int(1);
    unsigned long br = u8(msg->Int(2));
    unsigned long rot = (br >> 3) | ((~br) << 5);
    auto _tmp5 = DataNode(kDataInt, (int)((rot ^ l) & 0xFF));
    return _tmp5;
}

DataNode op40(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = (u8)msg->Int(2);

    u32 tmp = (((w << 8) | (w ^ 0x5Cu)) >> 6);
    return u8(tmp ^ operand);
}

DataNode op41(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = (u8)msg->Int(2);

    u32 tmp = ((u8)(w >> 2) ^ 0x17) | ((w << 6) & 0xC0);
    return u8(tmp ^ operand);
}

DataNode op42(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = (u8)msg->Int(2);

    unsigned long tmp = ((w >> 3) ^ 0xB) | ((w << 5) & 0xE0);
    return DataNode(kDataInt, (int)((tmp ^ operand) & 0xFF));
}

DataNode op43(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = (u8)msg->Int(2);

    unsigned long tmp = ((w >> 5) ^ 2) | ((w & 0x1F) << 3);
    return DataNode(kDataInt, (int)((tmp ^ operand) & 0xFF));
}

DataNode op44(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = (u8)msg->Int(2);

    unsigned long tmp = ((w >> 2) ^ 0xD) | ((w << 6) & 0xC0);
    return DataNode(kDataInt, (int)((tmp ^ operand) & 0xFF));
}

DataNode op45(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8((byteVal >> 3) ^ 6);
    u8 lowBits = u8(((byteVal & 7) << 5));
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op46(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8((byteVal >> 4) ^ 3);
    u8 lowBits = u8((byteVal << 4) & 0xF0);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op47(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8((byteVal >> 1) ^ 0x1B);
    u8 lowBits = u8((byteVal & 1) << 7);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op48(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 a = w;
    u32 working2 = ((a >> 4) ^ 0x6u);
    u32 working3 = (((a << 4) & 0xF0u) ^ 0x5u);
    u32 tmp = (working2 | working3);
    return u8(tmp ^ operand);
}

DataNode op49(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 working3 = (w << 8) ^ 0x5Cu;
    u32 working2 = (w ^ 0x63u);
    u32 tmp = ((working2 | working3) >> 3);
    return u8(tmp ^ operand);
}

DataNode op50(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8(((byteVal << 3) & 0xF8) ^ 2);
    u8 lowBits = u8((byteVal >> 5) ^ 3);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op51(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = (u8)msg->Int(2);

    u32 working2 = (w ^ 0x63);
    u32 working3 = (w << 8) ^ 0x5C;
    u32 tmp = ((working2 | working3) >> 6);
    return u8(tmp ^ operand);
}

DataNode op52(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8((byteVal >> 1) ^ 0x2e);
    u8 lowBits = u8((byteVal << 7) ^ 0x1b);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op53(DataArray *msg) {
    unsigned long operand = msg->Int(1);
    unsigned long w = (u8)msg->Int(2);

    unsigned long working3 = (w << 8) ^ 0x36u;
    unsigned long working2 = (w ^ 0x5Cu);
    unsigned long tmp = ((working2 | working3) >> 7);
    return DataNode(kDataInt, (int)((tmp ^ operand) & 0xFF));
}

DataNode op54(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 w32 = (u32)w;
    u32 part2 = (w32 << 5) & 0xE0;
    part2 ^= 0x6;
    u32 part1 = (w32 >> 3) & 0xFF;
    part1 ^= 0xB;
    u32 tmp = part1 | part2;
    return u8(tmp ^ operand);
}

DataNode op55(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8(((byteVal & 0x1f) << 3) ^ 1);
    u8 lowBits = u8((byteVal >> 5) ^ 2);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op56(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 byteVal = w;

    u8 highBits = u8(((byteVal & 0xF) << 4) ^ 6);
    u8 lowBits = u8((byteVal >> 4) ^ 3);
    u8 rotated = u8(highBits | lowBits);
    return u8(rotated ^ operand);
}

DataNode op57(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 w_extended = w;
    u32 working2 = (w_extended ^ 0x3Cu);
    u32 working3 = (w_extended << 8) ^ 0x65u;
    u32 tmp = ((working2 | working3) >> 5);
    return DataNode(kDataInt, u8(tmp ^ operand));
}

DataNode op58(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 working2 = (w ^ 0x65u);
    u32 working3 = (w << 8) ^ 0x3Cu;
    u32 tmp = ((working2 | working3) >> 6);
    return u8(tmp ^ operand);
}

DataNode op59(DataArray *msg) {
    u32 operand = msg->Int(1);
    u32 w = (u8)msg->Int(2);

    u32 working2 = (w ^ 0x65u);
    u32 working3 = (w << 8) ^ 0x3Cu;
    u32 tmp = ((working2 | working3) >> 2);
    return u8(tmp ^ operand);
}

DataNode op60(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 byteVal = w;
    u32 xorLow = (byteVal ^ 0xFFu);
    u32 xorHigh = (byteVal << 8) ^ 0xAAu;
    u32 tmp = ((xorLow | xorHigh) >> 4);
    return u8(tmp ^ operand);
}

DataNode op61(DataArray *msg) {
    u32 operand = msg->Int(2);
    u8 w = msg->Int(1);

    u32 a = (w >> 3) ^ 0x15;
    u32 b = ((w & 7) << 5) ^ 0x1f;
    u32 tmp = a | b;
    return u8(tmp ^ operand);
}

DataNode op62(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);
    u32 w32 = w;

    u8 tmp1 = u8((w32 >> 5) ^ 5);
    u8 tmp2 = u8(((w32 << 3) & 0xF8) ^ 7);
    u8 combined = u8(tmp1 | tmp2);
    return u8(combined ^ operand);
}

DataNode op63(DataArray *msg) {
    u32 operand = msg->Int(1);
    u8 w = msg->Int(2);

    u32 byteVal = w;
    u32 xorLow = (byteVal ^ 0xFFu);
    u32 xorHigh = (byteVal << 8) ^ 0xAFu;
    u32 tmp = ((xorLow | xorHigh) >> 6);
    return u8(tmp ^ operand);
}

extern DataArray *DataReadString(const char *);

unsigned long ByteGrinder::pickOneOf32A(bool b, long l) {
    DataArray *a;
    char script[256];
    if (b) {
        sprintf(script, "{xa %d}", l);
        a = DataReadString(script);
    } else {
        a = DataReadString("{xa}");
    }
    unsigned long result = a->Evaluate(0).Int();
    a->Release();
    return result;
}

unsigned long ByteGrinder::pickOneOf32B(bool b, long l) {
    DataArray *a;
    char script[256];
    if (b) {
        sprintf(script, "{ya %d}", l);
        a = DataReadString(script);
    } else {
        a = DataReadString("{ya}");
    }
    unsigned long result = a->Evaluate(0).Int();
    a->Release();
    return result;
}

DataNode getRandomLong(DataArray *da) {
    static u32 s_seed = 0x521;
    bool hasOne = da->Size() > 1;
    if (hasOne) {
        s_seed = s_seed * 0x19660D + 0x3C6EF35F;
    }
    return (s32)s_seed;
}

DataNode magicNumberGenerator(DataArray *da) {
    int magic = 0x5c5c5c5c;
    if (da->Int(2) == 2) {
        magic = 0x36363636;
    }
    int idx = da->Int(1);
    int v = ((idx ^ magic) * 0x19660d + 0x3c6ef35f);
    if (da->Int(2) == 1) {
        v = (v * 0x19660d + 0x3c6ef35f);
    }
    return DataNode(kDataInt, v);
}

#ifdef HX_NATIVE
// ============================================================================
// Native (non-DTA) implementation of GrindArray
// Bypasses DTA scripting — directly implements the hash+op pipeline in C++.
// The DTA version fails on x86_64 due to integer width/evaluation differences.
// ============================================================================

namespace {

static const u32 kLcgMul = 0x19660D;
static const u32 kLcgInc = 0x3C6EF35F;

// --- Native op functions (u32 operand = bar[ix], u32 w = foo accumulator) ---

static u8 nop0(u32 op, u32 w) { return u8(w ^ op); }
static u8 nop1(u32 op, u32 w) { return u8(u8(w) + u8(op)); }
static u8 nop2(u32 op, u32 w) { u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=u8(op&7); return u8(r); }
static u8 nop3(u32 op, u32 w) { u32 b=(op==0); u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=b; return u8(r); }
static u8 nop4(u32 op, u32 w) { u32 b=(op==0); u32 a=(u8(w)==0); u32 r=(a<<8)|a; r>>=b; return u8(r); }
static u8 nop5(u32 op, u32 w) { u32 r=u8(~(w|w)); u32 s=(op<<29)>>29; r|=r<<8; r>>=s; return u8(r); }
static u8 nop6(u32 op, u32 w) { return u8(!w ^ op); }
static u8 nop7(u32 op, u32 w) { return u8((!w + op) & 0xFF); }
static u8 nop8(u32 op, u32 w) { return u8(u8(w) + u8(op)) ^ u8(op); }
static u8 nop9(u32 op, u32 w) { return u8(((u8(w) ^ op) + op) & 0xFF); }
static u8 nop10(u32 op, u32 w) { u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=!op; r^=op; return u8(r&0xFF); }
static u8 nop11(u32 op, u32 w) { u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=u8(op&7); r^=op; return u8(r&0xFF); }
static u8 nop12(u32 op, u32 w) { u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=u8(op&7); return u8(r+op); }
static u8 nop13(u32 op, u32 w) { u32 bw=u8(w); u32 r=bw|(bw<<8); r>>=!op; return u8(r+op); }
static u8 nop14(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>1)|(bw<<7))+op); }
static u8 nop15(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>2)|(bw<<6))+op); }
static u8 nop16(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>3)|(bw<<5))+op); }
static u8 nop17(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>4)|(bw<<4))+op); }
static u8 nop18(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>5)|(bw<<3))+op); }
static u8 nop19(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>6)|(bw<<2))+op); }
static u8 nop20(u32 op, u32 w) { u32 bw=u8(w); return u8(((bw>>7)|(bw<<1))+op); }
static u8 nop21(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>1)|(b<<7))^op)&0xFF); }
static u8 nop22(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>2)|(b<<6))^op)&0xFF); }
static u8 nop23(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>3)|(b<<5))^op)&0xFF); }
static u8 nop24(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>4)|(b<<4))^op)&0xFF); }
static u8 nop25(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>5)|(b<<3))^op)&0xFF); }
static u8 nop26(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>6)|(b<<2))^op)&0xFF); }
static u8 nop27(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>7)|(b<<1))^op)&0xFF); }
static u8 nop28(u32 op, u32 w) { u32 b=u8(w); u32 r=(b>>5)|(b<<3); return u8(((r+op)^op)&0xFF); }
static u8 nop29(u32 op, u32 w) { u32 b=u8(w); u32 r=(b>>3)|(b<<5); return u8(((r+op)^op)&0xFF); }
static u8 nop30(u32 op, u32 w) { u32 b=u8(w); u32 r=(b>>3)|(b<<5); return u8(((r^op)+op)&0xFF); }
static u8 nop31(u32 op, u32 w) { u32 b=u8(w); u32 r=(b>>5)|(b<<3); return u8(((r^op)+op)&0xFF); }
static u8 nop32(u32 op, u32 w) { u32 v=u8(w); return u8(((v>>3)^0x1F|((v&7)<<5))^op); }
static u8 nop33(u32 op, u32 w) { u32 v=u8(w)&0xFF; return u8(((v>>5)^7|((v&0x1F)<<3))^op); }
static u8 nop34(u32 op, u32 w) { u32 v=u8(w); return u8(((v>>2)^0x3F|((v&3)<<6))^op); }
static u8 nop35(u32 op, u32 w) { u32 v=u8(w); return u8(((v>>6)^3|((v&0x3F)<<2))^op); }
static u8 nop36(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>2)|((~b)<<6))^op)&0xFF); }
static u8 nop37(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>5)|((~b)<<3))^op)&0xFF); }
static u8 nop38(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>6)|((~b)<<2))^op)&0xFF); }
static u8 nop39(u32 op, u32 w) { u32 b=u8(w); return u8((((b>>3)|((~b)<<5))^op)&0xFF); }
static u8 nop40(u32 op, u32 w) { u32 v=u8(w); return u8((((v<<8)|(v^0x5Cu))>>6)^op); }
static u8 nop41(u32 op, u32 w) { u32 v=u8(w); return u8(((u8(v>>2)^0x17)|((v<<6)&0xC0))^op); }
static u8 nop42(u32 op, u32 w) { u32 v=u8(w); return u8((((v>>3)^0xB)|((v<<5)&0xE0))^op)&0xFF; }
static u8 nop43(u32 op, u32 w) { u32 v=u8(w); return u8((((v>>5)^2)|((v&0x1F)<<3))^op)&0xFF; }
static u8 nop44(u32 op, u32 w) { u32 v=u8(w); return u8((((v>>2)^0xD)|((v<<6)&0xC0))^op)&0xFF; }
static u8 nop45(u32 op, u32 w) { u32 v=u8(w); return u8((u8((v>>3)^6)|u8((v&7)<<5))^op); }
static u8 nop46(u32 op, u32 w) { u32 v=u8(w); return u8((u8((v>>4)^3)|u8((v<<4)&0xF0))^op); }
static u8 nop47(u32 op, u32 w) { u32 v=u8(w); return u8((u8((v>>1)^0x1B)|u8((v&1)<<7))^op); }
static u8 nop48(u32 op, u32 w) { u32 a=u8(w); return u8((((a>>4)^0x6u)|(((a<<4)&0xF0u)^0x5u))^op); }
static u8 nop49(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x63u)|((v<<8)^0x5Cu))>>3))^op); }
static u8 nop50(u32 op, u32 w) { u32 v=u8(w); return u8((u8(((v<<3)&0xF8)^2)|u8((v>>5)^3))^op); }
static u8 nop51(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x63)|((v<<8)^0x5C))>>6))^op); }
static u8 nop52(u32 op, u32 w) { u32 v=u8(w); return u8((u8((v>>1)^0x2e)|u8((v<<7)^0x1b))^op); }
static u8 nop53(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x5Cu)|((v<<8)^0x36u))>>7)^op)&0xFF); }
static u8 nop54(u32 op, u32 w) { u32 v=u8(w); u32 p2=(v<<5)&0xE0; p2^=0x6; u32 p1=(v>>3)&0xFF; p1^=0xB; return u8((p1|p2)^op); }
static u8 nop55(u32 op, u32 w) { u32 v=u8(w); return u8((u8(((v&0x1f)<<3)^1)|u8((v>>5)^2))^op); }
static u8 nop56(u32 op, u32 w) { u32 v=u8(w); return u8((u8(((v&0xF)<<4)^6)|u8((v>>4)^3))^op); }
static u8 nop57(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x3Cu)|((v<<8)^0x65u))>>5))^op); }
static u8 nop58(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x65u)|((v<<8)^0x3Cu))>>6))^op); }
static u8 nop59(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0x65u)|((v<<8)^0x3Cu))>>2))^op); }
static u8 nop60(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0xFFu)|((v<<8)^0xAAu))>>4))^op); }
// op61: DTA version swaps args (operand=Int(2), w=Int(1)). Our caller passes (bar[ix], foo).
static u8 nop61(u32 op, u32 w) { u8 rw=(u8)op; u32 ro=w; u32 a=(rw>>3)^0x15; u32 b=((rw&7)<<5)^0x1f; return u8((a|b)^ro); }
static u8 nop62(u32 op, u32 w) { u32 v=u8(w); return u8((u8((v>>5)^5)|u8(((v<<3)&0xF8)^7))^op); }
static u8 nop63(u32 op, u32 w) { u32 v=u8(w); return u8(((((v^0xFFu)|((v<<8)^0xAFu))>>6))^op); }

typedef u8 (*NativeOp)(u32, u32);
static const NativeOp kAllOps[64] = {
    nop0,  nop1,  nop2,  nop3,  nop4,  nop5,  nop6,  nop7,
    nop8,  nop9,  nop10, nop11, nop12, nop13, nop14, nop15,
    nop16, nop17, nop18, nop19, nop20, nop21, nop22, nop23,
    nop24, nop25, nop26, nop27, nop28, nop29, nop30, nop31,
    nop32, nop33, nop34, nop35, nop36, nop37, nop38, nop39,
    nop40, nop41, nop42, nop43, nop44, nop45, nop46, nop47,
    nop48, nop49, nop50, nop51, nop52, nop53, nop54, nop55,
    nop56, nop57, nop58, nop59, nop60, nop61, nop62, nop63,
};

static void GeneratePermutation32(u32 seed, u32 outPerm[32]) {
    bool usedUp[32];
    memset(usedUp, 0, sizeof(usedUp));
    for (int i = 0; i < 32; i++) {
        u32 idx;
        for (;;) {
            seed = seed * kLcgMul + kLcgInc;
            idx = (seed >> 2) & 0x1F;
            if (!usedUp[idx]) { usedUp[idx] = true; break; }
        }
        outPerm[i] = idx;
    }
}

static void BuildHashMapping5(u32 seed, u32 mapping[256]) {
    for (int i = 0; i < 256; i++) {
        mapping[i] = (seed >> 3) & 0x1F;
        seed = seed * kLcgMul + kLcgInc;
    }
}

static void BuildHashMapping6(u32 seed, u32 mapping[256]) {
    for (int i = 0; i < 256; i++) {
        mapping[i] = (seed >> 2) & 0x3F;
        seed = seed * kLcgMul + kLcgInc;
    }
}

} // anonymous namespace

void ByteGrinder::GrindArray(
    long seedA, long seedB, unsigned char *arrayToGrind, int arrayLen, int moggVersion
) {
    int encMethod = GetEncMethod(moggVersion);

    // Build hash mapping tables (ma seeded with seedA, za seeded with seedB)
    u32 hashMap5[256], hashMap6[256];
    BuildHashMapping5((u32)seedA, hashMap5);
    BuildHashMapping6((u32)seedB, hashMap6);

    // Reproduce Init's fixed permutation: O-number → op function index
    // Init uses getRandomSequence32A seeded 0xD5 (ops 0-31) and 0x23E (ops 32-63)
    u32 initPerm0[32], initPerm1[32];
    GeneratePermutation32(0xD5, initPerm0);
    GeneratePermutation32(0x23E, initPerm1);

    int oNumberToOpIdx[64];
    memset(oNumberToOpIdx, -1, sizeof(oNumberToOpIdx));
    for (int i = 0; i < 32; i++) {
        oNumberToOpIdx[initPerm0[i]] = i;
        oNumberToOpIdx[initPerm1[i] + 32] = 32 + i;
    }

    // Generate GrindArray's per-call op permutation (ya seeded with seedB, then seedA)
    u32 grindPerm[32];
    NativeOp caseToOp[64];
    memset(caseToOp, 0, sizeof(caseToOp));

    GeneratePermutation32((u32)seedB, grindPerm);
    for (int i = 0; i < 32; i++) {
        int opIdx = oNumberToOpIdx[grindPerm[i]];
        if (opIdx >= 0) caseToOp[i] = kAllOps[opIdx];
    }

    if (encMethod != 0) {
        GeneratePermutation32((u32)seedA, grindPerm);
        for (int i = 0; i < 32; i++) {
            int opIdx = oNumberToOpIdx[grindPerm[i] + 32];
            if (opIdx >= 0) caseToOp[32 + i] = kAllOps[opIdx];
        }
    }

    u32 *hashMap = (encMethod != 0) ? hashMap6 : hashMap5;
    u32 maxCase = (encMethod != 0) ? 64u : 32u;

    // Grind: for each key byte, run the DTA while/switch loop.
    // The DTA script is: {while (size($bar) > $ix) {switch hash (N {++ $ix}{set $foo ...})...} {++ $ix}}
    // When a switch case matches: ix increments TWICE (once in case body BEFORE op, once trailing).
    // The op reads bar[ix] AFTER the first increment, so it uses bar[ix+1] relative to the hash byte.
    // When no case matches: ix increments ONCE (trailing only).
    for (int i = 0; i < arrayLen; i++) {
        u32 foo = arrayToGrind[i];
        int ix = 0;
        while (ix < 0x10) {
            u32 barVal = arrayToGrind[ix];
            u32 hash = hashMap[barVal & 0xFF];
            if (hash < maxCase && caseToOp[hash]) {
                // Case matched: {++ $ix} then {set $foo {Op {elem $bar $ix} $foo}}
                ix++;  // first increment (inside case body, before op)
                if (ix < 0x10)
                    foo = caseToOp[hash](arrayToGrind[ix], foo);
            }
            ix++;  // trailing increment (always, end of while body)
        }
        arrayToGrind[i] = (unsigned char)(foo & 0xFF);
    }
}

int magicNumberGeneratorNative(int idx, int mode) {
    int magic = (mode == 2) ? 0x36363636 : 0x5c5c5c5c;
    int v = (idx ^ magic) * 0x19660d + 0x3c6ef35f;
    if (mode == 1) v = v * 0x19660d + 0x3c6ef35f;
    return v;
}

#else // !HX_NATIVE
void ByteGrinder::GrindArray(
    long seedA, long seedB, unsigned char *arrayToGrind, int arrayLen, int moggVersion
) {
    char script[256];
    DataArray *mainScriptArray;

    sprintf(script, "{ma %d 2}", seedA);
    mainScriptArray = DataReadString(script);
    mainScriptArray->Evaluate(0).Int();
    mainScriptArray->Release();

    sprintf(script, "{za %d 2}", seedB);
    mainScriptArray = DataReadString(script);
    mainScriptArray->Evaluate(0).Int();
    mainScriptArray->Release();

    String mainScript;
    int encMethod = GetEncMethod(moggVersion);
    mainScript = "($foo $bar){O68($ix 0){O64{>{O65 $bar}$ix}{O66{ma{O67 $bar $ix}}";
    if (encMethod != 0) {
        mainScript = "($foo $bar){O68($ix 0){O64{>{O65 $bar}$ix}{O66{za{O67 $bar $ix}}";
    }

    pickOneOf32B(true, seedB);
    for (int i = 0; i < 0x20; i++) {
        char block[256];
        char callName[16];
        sprintf(callName, "O%d", pickOneOf32B(false, 0));
        sprintf(block, "(%d{O70 $ix}{O69 $foo{%s{O67 $bar $ix}$foo}})", i, callName);
        mainScript += block;
    }

    if (encMethod != 0) {
        pickOneOf32B(true, seedA);
        for (int i = 0x20; i < 0x40; i++) {
            char block[256];
            char callName[16];
            sprintf(callName, "O%d", pickOneOf32B(false, 0) + 0x20);
            sprintf(block, "(%d{O70 $ix}{O69 $foo{%s{O67 $bar $ix}$foo}})", i, callName);
            mainScript += block;
        }
    }

    mainScript += "}{O70 $ix}}}$foo";
    mainScriptArray = DataReadString(mainScript.c_str());
    for (int i = 0; i < arrayLen; i++) {
        char itoaBuffer[32];
        unsigned char w = arrayToGrind[i];
        String stringArgs("");
        Hx_snprintf(itoaBuffer, sizeof(itoaBuffer), "%d", w);
        stringArgs += itoaBuffer;
        stringArgs += " (";
        for (int j = 0; j < 0x10; j++) {
            Hx_snprintf(itoaBuffer, sizeof(itoaBuffer), "%d", arrayToGrind[j]);
            stringArgs += itoaBuffer;
            stringArgs += " ";
        }
        stringArgs += ")";
        DataArray *args = DataReadString(stringArgs.c_str());
        arrayToGrind[i] = mainScriptArray->ExecuteScript(0, nullptr, args, 0).Int();
        args->Release();
    }
    mainScriptArray->Release();
}
#endif // HX_NATIVE

void ByteGrinder::Init() {
    char functionName[0x100];
    // This *must* be written out in reverse to match
    functionName[1] = 'a';
    functionName[0] = 'N';
    functionName[2] = '\0';
    DataRegisterFunc(functionName, getRandomLong);
    functionName[0] = 'h';
    DataRegisterFunc(functionName, magicNumberGenerator);
    functionName[0] = 'm';
    DataRegisterFunc(functionName, hashTo5Bits);
    functionName[0] = 'z';
    DataRegisterFunc(functionName, hashTo6Bits);
    functionName[0] = 'x';
    DataRegisterFunc(functionName, getRandomSequence32A);
    functionName[0] = 'y';
    DataRegisterFunc(functionName, getRandomSequence32B);
    std::vector<DataFunc *> funPtrs;
    funPtrs.push_back(op0);
    funPtrs.push_back(op1);
    funPtrs.push_back(op2);
    funPtrs.push_back(op3);
    funPtrs.push_back(op4);
    funPtrs.push_back(op5);
    funPtrs.push_back(op6);
    funPtrs.push_back(op7);
    funPtrs.push_back(op8);
    funPtrs.push_back(op9);
    funPtrs.push_back(op10);
    funPtrs.push_back(op11);
    funPtrs.push_back(op12);
    funPtrs.push_back(op13);
    funPtrs.push_back(op14);
    funPtrs.push_back(op15);
    funPtrs.push_back(op16);
    funPtrs.push_back(op17);
    funPtrs.push_back(op18);
    funPtrs.push_back(op19);
    funPtrs.push_back(op20);
    funPtrs.push_back(op21);
    funPtrs.push_back(op22);
    funPtrs.push_back(op23);
    funPtrs.push_back(op24);
    funPtrs.push_back(op25);
    funPtrs.push_back(op26);
    funPtrs.push_back(op27);
    funPtrs.push_back(op28);
    funPtrs.push_back(op29);
    funPtrs.push_back(op30);
    funPtrs.push_back(op31);
    pickOneOf32A(true, 0xD5);
    for (int i = 0; i < funPtrs.size(); i++) {
        int oNum = pickOneOf32A(false, 0);
        sprintf(functionName, "O%d", oNum);
        DataRegisterFunc(functionName, funPtrs[i]);
    }
    funPtrs.clear();
    funPtrs.push_back(op32);
    funPtrs.push_back(op33);
    funPtrs.push_back(op34);
    funPtrs.push_back(op35);
    funPtrs.push_back(op36);
    funPtrs.push_back(op37);
    funPtrs.push_back(op38);
    funPtrs.push_back(op39);
    funPtrs.push_back(op40);
    funPtrs.push_back(op41);
    funPtrs.push_back(op42);
    funPtrs.push_back(op43);
    funPtrs.push_back(op44);
    funPtrs.push_back(op45);
    funPtrs.push_back(op46);
    funPtrs.push_back(op47);
    funPtrs.push_back(op48);
    funPtrs.push_back(op49);
    funPtrs.push_back(op50);
    funPtrs.push_back(op51);
    funPtrs.push_back(op52);
    funPtrs.push_back(op53);
    funPtrs.push_back(op54);
    funPtrs.push_back(op55);
    funPtrs.push_back(op56);
    funPtrs.push_back(op57);
    funPtrs.push_back(op58);
    funPtrs.push_back(op59);
    funPtrs.push_back(op60);
    funPtrs.push_back(op61);
    funPtrs.push_back(op62);
    funPtrs.push_back(op63);
    pickOneOf32A(true, 0x23E);
    for (int i = 0; i < funPtrs.size(); i++) {
        int oNum = pickOneOf32A(false, 0) + 32;
        sprintf(functionName, "O%d", oNum);
        DataRegisterFunc(functionName, funPtrs[i]);
    }
}
