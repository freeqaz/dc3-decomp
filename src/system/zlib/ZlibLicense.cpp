#include "utl\Licenses.h"

static Licenses sLicense("system/src/zlib", Licenses::kRequirementNotification);

// fn_82EDDA80 (24 B) is ??__EsLicense@@YAXXZ, the MSVC dynamic initializer for
// `sLicense` above.  Seven TUs declare a file-static `sLicense`, so
// ham_xbox_r.map lists that one mangled name seven times (82edb268 math:SHA1,
// 82edb280 math:Easing, 82edc020 os:System, 82edd3c8 synth:TomCryptLicense,
// 82edda80 zlib:ZlibLicense, 82edf4b8 jpeg:jcmaster, 82ee0b00
// oggvorbis:VorbisMem).  dtk can bind a name to one address only, so six of
// them stay fn_<addr>, and objdiff pairs those by masked byte signature.
//
// This row read 0.0% until 2026-11-13 for a different reason than the comment
// that used to stand here claimed: this file was never COMPILED.  The split
// names the unit `system/zlib/ZlibLicense.cpp`; objects.json named it
// `system/zlib/ZlibLicense.c`, so configure.py found no source for the unit,
// objdiff.json carried no base_path, and the link used the split object.
// `static` matches the other six TUs (the map has no public `sLicense`).
