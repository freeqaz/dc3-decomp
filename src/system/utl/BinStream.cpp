#include "utl/BinStream.h"
#include "math/Rand.h"
#include "math/Rand2.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os/Endian.h"
#include "os\Timer.h"
#include <vector>

#define BUF_SIZE 512

const char *BinStream::Name() const { return "<unnamed>"; }

BinStream::BinStream(bool b) : mLittleEndian(b), mCrypto(nullptr), mRevStack(nullptr) {}

void SwapData(const void *in, void *out, int size) {
    switch (size) {
    case 2: {
        unsigned short *s1 = (unsigned short *)in;
        unsigned short *s2 = (unsigned short *)out;
        *s2 = EndianSwap(*s1);
        break;
    }
    case 4: {
        unsigned int *i1 = (unsigned int *)in;
        unsigned int *i2 = (unsigned int *)out;
        *i2 = EndianSwap(*i1);
        break;
    }
    case 8: {
        unsigned long long *l1 = (unsigned long long *)in;
        unsigned long long *l2 = (unsigned long long *)out;
        *l2 = EndianSwap(*l1);
        break;
    }
    default:
        MILO_ASSERT(0, 0xAC);
        break;
    }
}

void BinStream::DisableEncryption() {
    MILO_ASSERT(mCrypto, 0xDC);
    RELEASE(mCrypto);
}

void BinStream::Write(const void *void_data, int bytes) {
    if (Fail()) {
        MILO_PRINT_ONCE("Stream error: Can't write to %s\n", Name());
    } else {
        const unsigned char *data = (u8 *)void_data;
        if (!mCrypto) {
            WriteImpl(void_data, bytes);
        } else {
            char crypt[512];
            while (bytes > 0) {
                int x = Min(512, bytes);
                for (int i = 0; i < x; i++) {
                    u8 bastard = mCrypto->Int();
                    crypt[i] = data[i] ^ bastard;
                }
                WriteImpl(crypt, x);
                bytes -= 512;
                data += 512;
            }
        }
    }
}

void BinStream::Seek(int offset, SeekType type) {
    MILO_ASSERT(!Fail(), 0x11F);
    MILO_ASSERT(!mCrypto, 0x122);
    SeekImpl(offset, type);
}

void BinStream::WriteEndian(const void *in, int size) {
#ifdef HX_NATIVE
    // On LE host: swap when file is BE (mLittleEndian=false)
    if (!mLittleEndian) {
#else
    // On BE host: swap when file is LE (mLittleEndian=true)
    if (mLittleEndian) {
#endif
        u64 output[2]; // 128 bits of buffer to swap
        SwapData(in, output, size);
        Write(output, size);
    } else
        Write(in, size);
}

bool BinStream::AddSharedInlined(const class FilePath &) {
    MILO_FAIL("BinStream::AddSharedInlined is a PC dev tool only !!");
    return false;
}

BinStream &BinStream::operator<<(const char *str) {
    MILO_ASSERT(str, 0x60);
    int size = strlen(str);
    *this << size;
    Write(str, size);
    return *this;
}

BinStream &BinStream::operator<<(const Symbol &sym) {
    const char *str = sym.Str();
    unsigned int len = strlen(str);
    MILO_ASSERT(len < BUF_SIZE, 0x6C);
    *this << len;
    Write(str, len);
    return *this;
}

BinStream &BinStream::operator<<(const class String &str) {
    int size = str.length();
    *this << size;
    Write(str.c_str(), size);
    return *this;
}

void BinStream::EnableWriteEncryption() {
    MILO_ASSERT(!mCrypto, 0xC8);
    int i = RandomInt();
    *this << i;
    mCrypto = new Rand2(i);
}

int BinStream::PopRev(Hmx::Object *o) {
    MILO_ASSERT(mRevStack, 0x34);
#ifdef HX_NATIVE
    if (mRevStack->empty()) {
        fprintf(stderr, "PopRev ABORT: empty stack for %s '%s' (stream=%p)\n", o->ClassName(), o->Name(), (void*)this);
        abort();
    }
#endif
    ObjVersion *back = &mRevStack->back();
    while (back->obj == nullptr) {
        MILO_NOTIFY("hey object got deleted!");
        mRevStack->pop_back();
        back = &mRevStack->back();
    }
    int revs = back->revs;
    if (o != back->obj) {
        MILO_LOG("rev stack $this mismatch (%08x != %08x\n", o, back->obj);
        MILO_LOG("curr obj: %s %s\n", o->ClassName(), PathName(o));
        MILO_LOG("stack obj: %s %s\n", back->obj->ClassName(), PathName(back->obj));
        MILO_FAIL(
            "rev stack (%08x %s %s != %08x %s %s)\n",
            o,
            o->ClassName(),
            PathName(o),
            back->obj,
            back->obj->ClassName(),
            PathName(back->obj)
        );
    }
    mRevStack->pop_back();
    return revs;
}

void BinStream::Read(void *data, int bytes) {
    if (Fail()) {
        MILO_NOTIFY_ONCE("Stream error: Can't read from %s", Name());
        memset(data, 0, bytes);
    } else {
        AutoGlitchReport report(50.0f, __FUNCTION__);
        ReadImpl(data, bytes);
        if (mCrypto) {
            for (unsigned char *ptr = (unsigned char *)data;
                 ptr < (unsigned char *)data + bytes;
                 ptr++) {
                unsigned char cryptoInt = mCrypto->Int();
                *ptr ^= cryptoInt;
            }
        }
    }
}

int BinStream::ReadAsync(void *v, int i) {
    Read(v, i);
    return Fail() ? 0 : i;
}

void BinStream::ReadEndian(void *out, int size) {
    Read(out, size);
#ifdef HX_NATIVE
    // On x86_64 (LE host), mLittleEndian=true means file is LE — no swap needed.
    // On Xbox 360 (BE host), mLittleEndian=true means swap LE file data to BE host.
    if (!mLittleEndian) {
        SwapData(out, out, size);
    }
#else
    if (mLittleEndian) {
        SwapData(out, out, size);
    }
#endif
}

void BinStream::ReadString(char *c, int i) {
    unsigned int a;
    *this >> a;
    if (a >= i)
        MILO_FAIL("String chars %d > %d", a + 1, i);
    Read(c, a);
    c[a] = 0;
}

BinStream &BinStream::operator>>(Symbol &sym) {
    char buf[BUF_SIZE];
    ReadString(buf, BUF_SIZE);
    sym = buf;
    return *this;
}
BinStream &BinStream::operator>>(String &str) {
    int siz;
    *this >> siz;
#ifdef HX_NATIVE
    if (siz > 10000 || siz < 0) {
        fprintf(stderr, "BinStream::operator>>(String) ABORT: bad size=%d at stream pos=%d\n", siz, Tell());
        abort();
    }
#endif
    str.resize(siz);
    Read((void *)str.c_str(), siz);
    return *this;
}

void BinStream::EnableReadEncryption() {
    MILO_ASSERT(!mCrypto, 0xC0);
    int i;
    *this >> i;
    mCrypto = new Rand2(i);
}

BinStream::~BinStream() {
    delete mCrypto;
    delete mRevStack;
}

#ifdef HX_NATIVE
bool BinStream::WaitUntilReady(int sleepMs) {
    for (int polls = 0; ; polls++) {
        EofType eof = Eof();
        if (eof == NotEof)
            return true;
        if (eof == RealEof) {
            MILO_WARN("BinStream::WaitUntilReady: unexpected end of file "
                       "(stream: %s)", Name());
            return false;
        }
#ifdef __EMSCRIPTEN__
        // Safe to bail: WebAssetsFetchSync() guarantees all file data is in
        // MEMFS before AsyncFile returns, so Eof() returns NotEof on first
        // check (line 242). This early exit prevents deadlock — can't spin-wait
        // on single-threaded browser event loop. All 11 call sites are also
        // #ifdef HX_NATIVE, so this path is never reached on web builds.
        MILO_WARN("BinStream::WaitUntilReady: data not ready, cannot block "
                   "on web (stream: %s)", Name());
        return false;
#else
        if (polls > 100000) {
            MILO_WARN("BinStream::WaitUntilReady: timed out after 100k polls "
                       "(stream: %s)", Name());
            return false;
        }
        Timer::Sleep(sleepMs);
#endif
    }
}
#endif

// RESIDUAL (w7-az, 94.74, 3 rows).  Our build emits one extra instruction,
// `stw r30, 0x50(r31)` -- a dead zero-store to a compiler temp -- ahead of the
// `if`, and that forces `li r30, 0x0` up with it.  The image has no such store:
// slot 0x50 is written exactly once, by `stw r11, 0x50(r31)` at 0x827DE9CC
// (r11 = r3+8, the homed `this` of the inlined _STLP_alloc_proxy ctor), inside
// the new-succeeded arm, and its `li r30, 0x0` sits at 0x827DE9A0 immediately
// before `cmplwi cr6, r11, 0x0` at 0x827DE9A4.  A /FAs listing names our extra
// slot `$T38365 = 80 ; size = 4`, a temp distinct from `$T38376` (the proxy
// this, same offset), written once and never read, attributed to the `if` line.
// NEGATIVES, all three byte-inert (94.74 unchanged, same 3 rows):
//   * `new std::vector<ObjVersion>` without the `()`.
//   * hoisting the allocation into a named local
//     (`std::vector<ObjVersion> *revStack = new ...; mRevStack = revStack;`).
//   * deleting `~ObjVersion() {}` from obj/Object.h:1861 (RB3's ObjVersion has
//     no user destructor, so this was worth testing as a lineage question --
//     it changes nothing here, and the header is PCH-reached, so it was
//     reverted rather than landed unverified).
// The temp is manufactured inside STLport's `_VECTOR_IMPL(const allocator_type&
// __a = allocator_type())` default-argument expansion, not at this call site.
// w9-b re-measured two of the three negatives above independently (both still
// exactly inert at 94.73684): `new std::vector<ObjVersion>` without the `()`,
// and the allocation through a named local.  One NEW negative, also inert:
// naming the ObjVersion temporary (`ObjVersion rev(revs, obj);
// mRevStack->push_back(rev);`).  Confirms the diagnosis: the dead slot is not
// produced by anything at this call site, so the remaining lead is STLport's
// own ctor shape in src/system/stlport/stl/_vector.h:226 -- and that header is
// PCH-reached by 574 TUs, so it needs a whole-binary A/B in both directions
// before anyone touches it, not a local edit.
// w9-b ATTEMPTED that STLport experiment and ABANDONED it -- it is UNTESTED, not
// refuted, and produced NO measurement.  The edit tried was splitting the
// default argument into two overloads at src/system/stlport/stl/_vector.h:226:
//     _VECTOR_IMPL() : _Vector_base<_Tp, _Alloc>(allocator_type()) {}
//     explicit _VECTOR_IMPL(const allocator_type& __a) : _Vector_base<...>(__a) {}
// in place of
//     explicit _VECTOR_IMPL(const allocator_type& __a = allocator_type())
// The theory is that the dead zero-store is the default-argument temporary, so a
// no-arg overload would never materialise it.  The full `ninja` this needs was
// killed at 606/849 because the box was at load ~240 with six lanes building, so
// no number exists in either direction.  Whoever resumes it: `_vector.h` IS in
// the decomp_pch.h closure (confirmed via `ninja -t deps
// build/373307D9/pch/decomp_pch.obj`), so it needs a full `ninja` and a
// whole-binary per-function row diff in BOTH directions -- the change touches
// every default-constructed std::vector in the binary, so it can pay or cost far
// beyond these 3 rows, and a per-target build would read LOW and silent.
void BinStream::PushRev(int revs, Hmx::Object *obj) {
    if (!mRevStack) {
        mRevStack = new std::vector<ObjVersion>();
    }
    mRevStack->push_back(ObjVersion(revs, obj));
}
