typedef unsigned long long u64;
struct Opts {
    u64 pad63_56 : 8;
    u64 pad55_48 : 8;
    u64 pad47_42 : 6;
    u64 pad41_40 : 2;
    u64 pad39_34 : 6;
    u64 pad33_32 : 2;
    u64 pad31_30 : 2;
    u64 pad29_28 : 2;
    u64 pad27_26 : 2;
    u64 b25 : 1;
    u64 b24 : 1;
    u64 b23 : 1;
    u64 b22 : 1;
    u64 b21 : 1;
    u64 b20 : 1;
    u64 b19 : 1;
    u64 b18 : 1;
    u64 b17 : 1;
    u64 b16 : 1;
    u64 b15 : 1;
    u64 b14 : 1;
    u64 b13 : 1;
    u64 b12 : 1;
    u64 b11_10 : 2;
    u64 b9 : 1;
    u64 b8 : 1;
    u64 b7 : 1;
    u64 b6 : 1;
    u64 b5 : 1;
    u64 b4 : 1;
    u64 b3 : 1;
    u64 b2 : 1;
    u64 b1 : 1;
    u64 b0 : 1;
};
struct Mat {
    char pad[0x3c];
    bool useEnviron;
    bool prelit;
    char pad2[0x4c - 0x3e];
    void *diffuse; // 0x4c
    char pad3[0x104 - 0x50];
    void *normalMap; // 0x104
    char pad4[0x1ec - 0x108];
    int cull; // 0x1ec
    bool Prelit() const { return prelit; }
    bool UseEnviron() const { return useEnviron; }
    void *GetDiffuseTex() const { return diffuse; }
    void *NormalMap() const { return normalMap; }
    int GetCull() const { return cull; }
};
extern int NumReal();

// A: direct, opts starts at zero
u64 TestA(Mat *mat) {
    Opts o;
    *(u64 *)&o = 0;
    o.b4 = mat->GetDiffuseTex() != 0;
    o.b8 = mat->Prelit();
    o.b16 = mat->UseEnviron() && NumReal() > 0;
    return *(u64 *)&o;
}
// B: through a ctor-like init value
u64 TestB(Mat *mat, u64 init) {
    Opts o;
    *(u64 *)&o = init;
    o.b5 = mat->NormalMap() != 0;
    o.b0 = 1;
    o.b24 = mat->GetDiffuseTex() != 0;
    o.b23 = mat->GetCull() == 2;
    return *(u64 *)&o;
}
