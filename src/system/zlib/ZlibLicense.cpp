#include "utl\Licenses.h"

Licenses sLicense("system/src/zlib", Licenses::kRequirementNotification);

// w8-e 2026-09-15: fn_82EDDA80 (24 B, 0%) is ??__EsLicense@@YAXXZ -- the MSVC
// dynamic initializer for the `sLicense` object above.  Seven different TUs
// declare a namespace-scope `sLicense`, so ham_xbox_r.map lists that one mangled
// name SEVEN times: 82edb268 math:SHA1, 82edb280 math:Easing, 82edc020
// os:System, 82edd3c8 synth:TomCryptLicense, 82edda80 zlib:ZlibLicense (this
// one), 82edf4b8 jpeg:jcmaster, 82ee0b00 oggvorbis:VorbisMem.
// config/373307D9/symbols.txt:193695 can bind only one and binds SHA1's; dtk
// parks the other six as fn_<addr>.  Our object does emit the initializer -- it
// simply cannot be named.  Measured 0.0%, structurally unscoreable; renaming the
// variable would change six other units' rows, not fix this one.
