typedef unsigned long long u64;

// MSVC Xenon allocates bitfields MSB-first: the FIRST declared field is LSB bit 63.
// So declare from bit 63 downwards.
struct Opts {
    u64 pad63 : 1;
    u64 pad62 : 1;
    u64 b61 : 1; // LSB bit 61
    u64 pad60_55 : 6;
    u64 b54 : 1;
    u64 pad53_50 : 4;
    u64 pad49_44 : 6;
    u64 pad43_40 : 4;
    u64 pad39_33 : 7;
    u64 pad32_25 : 8;
    u64 pad24_18 : 7;
    u64 b17 : 1;
    u64 b16 : 1;
    u64 pad15_13 : 3;
    u64 b12 : 1;
    u64 pad11_9 : 3;
    u64 b8 : 1; // prelit
    u64 pad7_5 : 3;
    u64 b4 : 1; // diffuse
    u64 b3 : 1;
    u64 b2 : 1;
    u64 b1 : 1;
    u64 b0 : 1;
};

struct Mat {
    char pad[0x3c];
    bool useEnviron; // 0x3c
    bool prelit; // 0x3d
    char pad2[0x4c - 0x3e];
    void *diffuse; // 0x4c
    bool Prelit() const { return prelit; }
    bool UseEnviron() const { return useEnviron; }
    void *GetDiffuseTex() const { return diffuse; }
};

u64 Test1(u64 init, Mat *mat) {
    Opts o;
    *(u64 *)&o = init;
    o.b4 = mat->GetDiffuseTex() != 0;
    o.b8 = mat->Prelit();
    if (mat->UseEnviron()) {
        o.b61 = 1;
    }
    return *(u64 *)&o;
}

u64 Test2(u64 init, Mat *mat) {
    Opts o;
    *(u64 *)&o = init;
    o.b4 = (bool)mat->GetDiffuseTex();
    o.b8 = mat->Prelit();
    if (mat->UseEnviron()) {
        o.b61 = 1;
    }
    return *(u64 *)&o;
}

u64 Test3(u64 init, Mat *mat) {
    Opts o;
    *(u64 *)&o = init;
    bool hasDiffuse = mat->GetDiffuseTex() != 0;
    o.b4 = hasDiffuse;
    o.b8 = mat->Prelit();
    if (mat->UseEnviron()) {
        o.b61 = 1;
    }
    return *(u64 *)&o;
}
