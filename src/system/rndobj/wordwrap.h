#pragma once

void WordWrap_SetOption(unsigned int);

extern unsigned int g_uOption;
extern unsigned char IsEastAsianChar(unsigned short);
extern unsigned short g_LineBreakTable[];

inline bool WordWrap_CanBreakLineAt(const wchar_t *arg0, const wchar_t *arg1)
{
    unsigned short temp_r31;
    int temp_r29;
    unsigned short temp_r10;
    int var_r11;
    int var_r7;
    int temp_r9;
    int temp_r10_2;
    unsigned short temp_r8;
    unsigned char var_r11_2;
    unsigned short temp_r11;
    unsigned short var_r4;
    unsigned short temp_r11_2;
    unsigned char temp_ret;
    unsigned char temp_ret_2;
    unsigned char temp_r3;
    unsigned char temp_r3_2;
    unsigned int temp_r5;
    int var_r11_3;
    int var_r7_2;
    int temp_r9_2;
    int temp_r10_3;
    unsigned short temp_r8_2;
    unsigned char var_r11_4;
    int var_r11_5;
    int var_r7_3;
    int temp_r9_3;
    int temp_r10_4;
    unsigned short temp_r8_3;
    unsigned char var_r11_6;
    unsigned char var_r11_7;
    unsigned char var_r3;

    if (arg0 == arg1) {
        goto block_1;
    }
    temp_r31 = *arg0;
    temp_r29 = g_uOption;
    if ((temp_r31 == 9) || (temp_r31 == 0xD) || (temp_r31 == 0x20) || (temp_r31 == 0x3000)) {
        temp_r10 = arg0[1];
        if (temp_r29 & 1) {
            var_r11 = 0;
            var_r7 = 0x91;
loop_8:
            temp_r9 = ((var_r7 - var_r11) / 2) + var_r11;
            temp_r10_2 = temp_r9 * 4;
            temp_r8 = g_LineBreakTable[temp_r9 * 2];
            if (temp_r10 != temp_r8) {
                if (temp_r10 < temp_r8) {
                    var_r7 = temp_r9 - 1;
                } else {
                    var_r11 = temp_r9 + 1;
                }
                if (var_r11 > var_r7) {
                    goto block_13;
                }
                goto loop_8;
            }
            var_r11_2 = g_LineBreakTable[temp_r9 * 2 + 1];
        } else {
block_13:
            var_r11_2 = 0;
        }
        if (var_r11_2 == 0) {
            goto block_15;
        }
        goto block_1;
    }
block_15:
    if ((((int)((arg0 - arg1) & 0xFFFFFFFE) <= 2) || ((temp_r11 = arg0[-2], ((temp_r11 == 9) == 0)) && (temp_r11 != 0xD) && (temp_r11 != 0x20) && (temp_r11 != 0x3000)) || ((unsigned short)arg0[-1] != 0x22) || (temp_r31 == 9) || (temp_r31 == 0xD) || (temp_r31 == 0x20) || (temp_r31 == 0x3000)) && ((var_r4 = arg0[-1], ((var_r4 == 9) != 0)) || (var_r4 == 0xD) || (var_r4 == 0x20) || (var_r4 == 0x3000) || (temp_r31 != 0x22) || ((temp_r11_2 = arg0[1], ((temp_r11_2 == 9) == 0)) && (temp_r11_2 != 0xD) && (temp_r11_2 != 0x20) && (temp_r11_2 != 0x3000)))) {
        if ((temp_r31 == 9) || (temp_r31 == 0xD) || (temp_r31 == 0x20) || (temp_r31 == 0x3000) || (temp_ret = IsEastAsianChar(temp_r31), temp_r3 = temp_ret, var_r4 = (unsigned short)(unsigned int)(unsigned long long)temp_ret, ((temp_r3 == 0) == 0)) || (temp_ret_2 = IsEastAsianChar(var_r4), temp_r3_2 = temp_ret_2, var_r4 = (unsigned short)(unsigned int)(unsigned long long)temp_ret_2, ((temp_r3_2 == 0) == 0)) || ((unsigned int)0x2D == 0x2DU)) {
            temp_r5 = temp_r29 & 1;
            if (temp_r5 != 0) {
                var_r11_3 = 0;
                var_r7_2 = 0x91;
loop_43:
                temp_r9_2 = ((var_r7_2 - var_r11_3) / 2) + var_r11_3;
                temp_r10_3 = temp_r9_2 * 4;
                temp_r8_2 = g_LineBreakTable[temp_r9_2 * 2];
                if (temp_r31 != temp_r8_2) {
                    if (temp_r31 < temp_r8_2) {
                        var_r7_2 = temp_r9_2 - 1;
                    } else {
                        var_r11_3 = temp_r9_2 + 1;
                    }
                    if (var_r11_3 > var_r7_2) {
                        goto block_49;
                    }
                    goto loop_43;
                }
                var_r11_4 = g_LineBreakTable[temp_r9_2 * 2 + 1];
            } else {
block_49:
                var_r11_4 = 0;
            }
            if (var_r11_4 == 0) {
                if (temp_r5 != 0) {
                    var_r11_5 = 0;
                    var_r7_3 = 0x91;
loop_53:
                    temp_r9_3 = ((var_r7_3 - var_r11_5) / 2) + var_r11_5;
                    temp_r10_4 = temp_r9_3 * 4;
                    temp_r8_3 = g_LineBreakTable[temp_r9_3 * 2];
                    if (var_r4 != temp_r8_3) {
                        if (var_r4 < temp_r8_3) {
                            var_r7_3 = temp_r9_3 - 1;
                        } else {
                            var_r11_5 = temp_r9_3 + 1;
                        }
                        if (var_r11_5 > var_r7_3) {
                            goto block_59;
                        }
                        goto loop_53;
                    }
                    var_r11_6 = g_LineBreakTable[temp_r9_3 * 2 + 1];
                } else {
block_59:
                    var_r11_6 = 0;
                }
                var_r11_7 = 1;
                if (var_r11_6 != 0) {
                    goto block_61;
                }
            } else {
                goto block_61;
            }
        } else {
block_61:
            var_r11_7 = 0;
        }
        var_r3 = var_r11_7;
    } else {
block_1:
        var_r3 = 0;
    }
    return var_r3;
}
