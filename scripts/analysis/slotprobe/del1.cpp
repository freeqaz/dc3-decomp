struct S { virtual ~S(); virtual void P(const char*); int a,b,c; void Use(); };
struct T { virtual void F(); int x; };
struct D : S { T t; D(const char*, bool); T& File(); };
inline T& D::File() { return t; }
void sink(int);
void f1() {           // plain delete of a new'd pointer
    D *log = new D("x", true);
    T &fs = log->File();
    fs.F();
    delete log;
}
void f2() {           // delete of a reference's address
    D &log = *new D("x", true);
    T &fs = log.File();
    fs.F();
    delete &log;
}
