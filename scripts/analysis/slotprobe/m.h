extern int Cond(int);
extern void Use(void *);
// size 8, vptr, extern ctor/dtor (FilePath-like)
struct V8 { virtual ~V8(); int x; V8(const char*); };
// size 32, vptr, extern ctor/dtor
struct V32 { virtual ~V32(); int x[7]; V32(const char*); };
// size 8, no vptr, extern ctor/dtor
struct D8 { int x, y; D8(const char*); ~D8(); };
// size 32, no vptr, extern ctor/dtor
struct D32 { int x[8]; D32(const char*); ~D32(); };
// size 8, no vptr, inline empty dtor, extern ctor
struct I8 { int x, y; I8(const char*); ~I8() {} };
// size 8, vptr, inline ctor storing vptr, extern virtual dtor
struct VI8 { virtual ~VI8(); int x; VI8(const char*p) : x((int)p) {} };
// size 8, no dtor at all
struct P8 { int x, y; };
// size 8, no vptr, inline ctor storing a member, extern dtor
struct IC8 { int x, y; IC8(const char*p) : x((int)p) {} ~IC8(); };
// size 8, vptr, inline ctor that only stores the vptr
struct VE8 { virtual ~VE8(); int x; VE8(const char*) {} };
// size 8, no vptr, inline ctor storing a member, NO dtor
struct IN8 { int x, y; IN8(const char*p) : x((int)p) {} };
// vptr, inline dtor (stores vptr), extern ctor  (FilePath-like)
struct VD8 { virtual ~VD8() {} int x; VD8(const char*); };
// no vptr, no ctor; inline member that STORES
struct F8 { int x, y; void Init(int v) { x = v; } int Get() { return x; } };
// extern ctor+dtor (EH-registered), inline read
struct FD8 { int x, y; FD8(); ~FD8(); int Get() { return y; } };
struct Sub { int y; int Get() { return y; } };
// member sub-object at +4 with the inline read
struct MS8 { int x; Sub s; MS8(); ~MS8(); };
// base sub-object at +4 (polymorphic first base) with the inline read
struct Poly { virtual ~Poly(); };
struct BS8 : Poly, Sub { BS8(); ~BS8(); };
