typedef unsigned long long u64;

struct Opts {
    u64 perPixel : 1; // 0
    u64 specularMap : 1; // 1
    u64 specular : 1; // 2
    u64 environMap : 1; // 3
    u64 diffuse : 1; // 4
    u64 normalMap : 1; // 5
    u64 pad6 : 1; // 6
    u64 glowMap : 1; // 7
    u64 prelit : 1; // 8
    u64 pad9 : 1;
    u64 texGen : 2; // 10
    u64 skinned : 1; // 12
    u64 screenAligned : 1; // 13
    u64 rimUnder : 1; // 14
    u64 rimMap : 1; // 15
    u64 realLights : 1; // 16
    u64 approxLights : 1; // 17
    u64 fog : 1; // 18
    u64 shadowBuffer : 1; // 19
    u64 aniso : 1; // 20
    u64 colorXfm : 1; // 21
    u64 pseudoHDR : 1; // 22
    u64 pad23 : 1;
    u64 normDetail : 1; // 24
    u64 billboard : 1; // 25
    u64 fadeOut : 2; // 26
    u64 numProj : 2; // 28
    u64 variation : 2; // 30
    u64 colorMod : 2; // 32
    u64 pad34 : 1;
    u64 pad35 : 1;
    u64 pad36 : 1;
    u64 rim : 1; // 37
    u64 ao : 1; // 38
    u64 toneMapping : 1; // 39
    u64 numPoint : 2; // 40
    u64 pad42 : 1;
    u64 environFalloff : 1; // 43
    u64 projMultiply : 1; // 44
    u64 pad45 : 1;
    u64 refractWorld : 1; // 46
    u64 pad47 : 1;
    u64 pad48 : 1;
    u64 environSpecMask : 1; // 49
    u64 showCost : 1; // 50
    u64 spotlight : 1; // 51
    u64 hiRes : 1; // 52
    u64 intensify : 1; // 53
    u64 flipNormal : 1; // 54
    u64 rest : 9;
};

extern int Diffuse();
extern int Prelit();
extern int UseEnviron();

u64 Test1(u64 skinned) {
    Opts o;
    *(u64 *)&o = skinned;
    o.diffuse = Diffuse() != 0;
    o.prelit = Prelit() != 0;
    return *(u64 *)&o;
}

// alternative: plain shifts, as the current source spells it
u64 Test2(u64 skinned) {
    return (((u64)(Prelit() != 0) << 4 | (u64)(Diffuse() != 0)) << 4) | skinned;
}
