#pragma once

class FftIpp {
public:
    void FftRealCcs(unsigned int *, volatile float &, unsigned int *, float &);
    void
    FftReal(unsigned int *, volatile float &, unsigned int *, float &, volatile float &);
    ~FftIpp();
    FftIpp();
    void SetMode(int);

    int unk0;
    int unk4;
    unsigned int unk8;
    unsigned int unkc;
    unsigned int unk10;
    unsigned int unk14;
    unsigned int unk18;
    unsigned int unk1c;
    unsigned int unk20;
    unsigned int unk24;
    unsigned int unk28;
    unsigned int unk2c;
    unsigned int unk30;
    unsigned int unk34;
    unsigned int unk38;
    unsigned int unk3c;
    int unk40;
};
