struct S { virtual ~S(); virtual void P(const char*); int a,b,c; };
struct T { virtual void F(); virtual bool Fail(); int x; };
struct D : S { T t; D(const char*, bool); T& File(); };
inline T& D::File() { return t; }
void f3() { D *log = new D("x",true); T &fs = log->File(); if(!fs.Fail()){ fs.F(); } delete log; }
void f4() { D *log = new D("x",true); T &fs = log->File(); if(!fs.Fail()){ fs.F(); } if(log) delete log; }
void f5() { D *log = new D("x",true); T &fs = log->File(); if(fs.Fail()){ delete log; return; } fs.F(); delete log; }
void f6() { D *log = new D("x",true); T &fs = log->File(); if(!fs.Fail()){ fs.F(); } log->~D(); }
void f7() { D &log = *new D("x",true); T &fs = log.File(); if(!fs.Fail()){ fs.F(); } delete (S*)&log; }
