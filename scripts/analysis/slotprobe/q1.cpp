#include "char/FileMerger.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"
extern int Cond(int);
extern std::vector<FileMerger::Merger> &List();
void F(Hmx::Object *o, Symbol a, Symbol b, Symbol c) {
    if (Cond(0)) {
        FilePath fp(MakeString("x/%s.milo", a));
        FileMerger::Merger merger(o);
        merger.mPreClear = true;
        merger.mSelected = fp;
        List().push_back(merger);
    }
    if (Cond(1)) {
        FilePath fp(MakeString("y/%s.milo", b));
        FileMerger::Merger merger(o);
        merger.mPreClear = true;
        merger.mSelected = fp;
        List().push_back(merger);
    }
    if (Cond(2)) {
        FilePath fp(MakeString("z/%s.milo", c));
        FileMerger::Merger merger(o);
        merger.mPreClear = true;
        merger.mSelected = fp;
        List().push_back(merger);
    }
}
