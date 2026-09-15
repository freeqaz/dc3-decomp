#include "meta\StoreEnumeration.h"
#include "os\Debug.h"
#include "utl\MakeString.h"
#include "xdk\XAPILIB.h"
#include <cstring>


XboxEnumeration::XboxEnumeration(int i, std::vector<unsigned long long> *offerIDs)
    : mUserIndex(i), mOfferIDCount(0), mOfferIDsBegin(0), mOfferIDsCur(0), mEnumerating(false), mHandle(0), mBufferSize(0), mCurOffers(0) {
    if (offerIDs != 0) {
        mOfferIDCount = (offerIDs->end() - offerIDs->begin());
        MILO_ASSERT(mOfferIDCount, 0x197);
        u32 allocSize = mOfferIDCount << 3;
        if (mOfferIDCount > 0x1FFFFFFFU) {
            allocSize = 0xFFFFFFFF;
        }
        mOfferIDsBegin = (unsigned long long *)new char[allocSize];
        memcpy(mOfferIDsBegin, &(*offerIDs)[0], mOfferIDCount << 3);
        mOfferIDsCur = mOfferIDsBegin;
    }
}


XboxEnumeration::~XboxEnumeration() {
    delete[] mOfferIDsBegin;
    mOfferIDsBegin = 0;

    if (mHandle != 0 && mOverlapped.InternalLow == 0x3E5U) {
        u32 result = XCancelOverlapped(&mOverlapped);
        if (result != 0) {
            MILO_FAIL("Error cancelling enum %d", result);
        }
    }

    if (mHandle != 0) {
        CloseHandle(mHandle);
        mHandle = 0;
    }

    delete mCurOffers;
    mCurOffers = 0;
}

bool XboxEnumeration::IsSuccess() const {
    MILO_ASSERT(!mHandle, 0x208);
    return mEnumerating;
}

void XboxEnumeration::Start() {
    mEnumerating = true;
    if (mHandle == 0) {
        unsigned int error;
        mBufferSize = 0;
        if (mOfferIDsCur == mOfferIDsBegin) {
            mContentList.clear();
        }
        if (mOfferIDsBegin == 0) {
            error = XMarketplaceCreateOfferEnumerator(mUserIndex, 0x100002, 0xFFFFFFFFFFFFFFFFULL, 99, &mBufferSize, &mHandle);
        } else {
            int remaining = (int)(mOfferIDCount - (u32)(mOfferIDsCur - mOfferIDsBegin));
            if (remaining >= 99) remaining = 99;
            error = XMarketplaceCreateOfferEnumeratorByOffering(mUserIndex, remaining, mOfferIDsCur, (WORD)remaining, &mBufferSize, &mHandle);
            mOfferIDsCur += remaining;
        }
        // Resolved 2026-08-20: the 0x44 member IS the original's `mCurOffers`
        // (the assert literal names it and the guarded instruction loads 0x44),
        // so the header was renamed rather than this line.  The old cursor name
        // moved to mOfferIDsCur.
        MILO_ASSERT(!mCurOffers, 0x1EA);
        mCurOffers = new char[mBufferSize];
        if (error != 0) {
            goto error_path;
        }
    }
    memset(mCurOffers, 0, mBufferSize);
    memset(&mOverlapped, 0, 0x1c);
    {
        DWORD result = XEnumerate(mHandle, mCurOffers, mBufferSize, 0, &mOverlapped);
        if (result == 0x3e5) {
            return;
        }
    }
error_path:
    if (mHandle != 0) {
        CloseHandle(mHandle);
        mHandle = 0;
    }
    delete[] (char*)mCurOffers;
    mEnumerating = false;
    mCurOffers = 0;
}

bool XboxEnumeration::IsEnumerating() const {
    // 0x824B0BE8 (ICF survivor HamMove::Mirrored) reads the WORD at 0x3c and
    // returns `!= 0`; at 0x3c XboxEnumeration has mHandle, not mEnumerating
    // (which is a byte at 0x1c and would have been an lbz).
    return mHandle != 0;
}

// XDK XMARKETPLACE_CONTENTOFFER_INFO (sizeof 0x68): one entry of the buffer
// XEnumerate fills for a marketplace offer enumerator.  Field offsets are
// proven by Poll (0x82E1D3A0-0x82E1D3DC read 0x10, 0x14, 0x00, 0x48 and 0x64
// off each entry and step by 0x68); the names are the XDK's.  It belongs in
// xdk/xapilibi/xbox.h next to XMarketplaceCreateOfferEnumerator, but that
// header is PCH-reached and adding the struct there reorders small COMDAT
// .text sections in 18 unrelated objects (Dir, MatAnim, Char, PanelDir, ...;
// measured 2026-09-15 against a same-source control rebuild that moved 0),
// so it stays TU-local until someone owning the xdk headers adjudicates that.
struct XMARKETPLACE_CONTENTOFFER_INFO {
    ULONGLONG qwOfferID;        // 0x00
    ULONGLONG qwPreviewOfferID; // 0x08
    DWORD dwOfferNameLength;    // 0x10
    WCHAR *wszOfferName;        // 0x14
    DWORD dwOfferType;          // 0x18
    unsigned char contentId[20];// 0x1c
    BOOL fIsUnrestrictedLicense;// 0x30
    DWORD dwLicenseMask;        // 0x34
    DWORD dwTitleID;            // 0x38
    DWORD dwContentCategory;    // 0x3c
    DWORD dwTitleNameLength;    // 0x40
    WCHAR *wszTitleName;        // 0x44
    BOOL fUserHasPurchased;     // 0x48
    DWORD dwPackageSize;        // 0x4c
    DWORD dwInstallSize;        // 0x50
    DWORD dwSellTextLength;     // 0x54
    WCHAR *wszSellText;         // 0x58
    DWORD dwAssetID;            // 0x5c
    DWORD dwPurchaseQuantity;   // 0x60
    DWORD dwPointsPrice;        // 0x64
};

void XboxEnumeration::Poll() {
    if (0 == mHandle || mOverlapped.InternalLow == 0x3E5U) {
        return;
    }

    unsigned long long *offersEnd;

    DWORD bytesReceived = 0;
    DWORD overlappedResult = XGetOverlappedResult(&mOverlapped, &bytesReceived, 0);
    // FRAME SLOTS (w7-bn, 2026-09-15): the image homes the four 4-byte locals
    // as insert-pos copy 0x50 / insert result 0x54 / bytesReceived 0x58 /
    // overlappedResult 0x5c.  Every spelling that gives the ERROR_NO_MORE_FILES
    // arm below a conversion TEMPORARY for its MakeString argument adds a
    // fifth 4-byte object and the packer reverses all four (97.5 canonical,
    // 22 offset rows); see the note at that arm.  Declaration order and scope
    // of `it` / the DWORDs were inert (function scope, before bytesReceived,
    // push_back instead of insert(end()): all 97.5 with identical rows).

    DWORD productCount = 0;
    if (bytesReceived > 0) {
        std::list<EnumProduct>::iterator it = mContentList.end();
        // Indexed as an array of XMARKETPLACE_CONTENTOFFER_INFO off the
        // MEMBER, not a cached local: 0x82E1D380/0x82E1D388 reload mCurOffers
        // every iteration and add the strength-reduced index (`add r30, r28,
        // r11`, induction variable first).  A cached `const ...INFO *offers`
        // local lets MSVC turn the whole thing into a pointer induction
        // variable (`lwzu r11, 0x68(r30)`, __savegprlr_23: 90.9), and an
        // explicit byte offset `(u8 *)mCurOffers + offset` keeps both
        // counters but emits the add pointer-first (97.5, 1 row).
        while (productCount < bytesReceived) {
            // One String, not two: the image constructs EnumProduct FIRST
            // (??0String@@QAA@XZ into slot 0x60, which is prod.mName -- the
            // EnumProduct temp occupies 0x60..0x78 with mOfferID at 0x68,
            // mPurchased at 0x70 and mPrice at 0x74), then assigns the char
            // buffer straight in with String::operator=(char const*).  The
            // separate `String str` cost an extra ctor/dtor pair and turned the
            // assignment into operator=(String const&).
            char buf[256];
            // The EnumProduct gets its OWN scope: retail runs ~String
            // (0x82E1D3E8) BEFORE the two induction increments at
            // 0x82E1D3F0/0x82E1D3F4.  With prod at while-body scope MSVC hoists
            // both increments above the dtor call.
            {
                EnumProduct prod;
                const XMARKETPLACE_CONTENTOFFER_INFO &offer = ((const XMARKETPLACE_CONTENTOFFER_INFO *)mCurOffers)[productCount];
                WideCharToMultiByte(0, 0, offer.wszOfferName, offer.dwOfferNameLength, buf, 0xFF, 0, 0);
                prod.mName = buf;

                prod.mOfferID = offer.qwOfferID;
                prod.mPurchased = offer.fUserHasPurchased;
                // mPrice is written BEFORE the insert (0x82E1D3D8 stores to
                // 0x74, then bl insert).  Setting it afterwards wrote to the
                // dead local and every product in mContentList kept price 0.
                prod.mPrice = offer.dwPointsPrice;
                mContentList.insert(it, prod);
            }

            productCount++;
        }
    }

    if (mOfferIDsBegin == 0 && overlappedResult == 0 && bytesReceived >= 99) {
        goto continue_enum;
    }

    if (mHandle != 0) {
        CloseHandle(mHandle);
        mHandle = 0;
    }

    delete mCurOffers;
    mCurOffers = 0;

    // 0x82E1D440 sends overlappedResult == 0 to .L_82E1D54C, which reloads
    // bytesReceived and falls straight into the continue_enum tail at
    // .L_82E1D550 -- i.e. a SUCCESSFUL poll still asks whether another batch is
    // outstanding and calls Start() again.  Our source sent it to `done`, which
    // stopped every batched enumeration after its first 99 offers.  Spelling it
    // as an `if (overlappedResult != 0) { ... }` wrapper rather than a
    // `goto continue_enum` is what keeps the three dispatch tests as FORWARD
    // beq's with the "overlapped failed" arm as the fallthrough, the way
    // 0x82E1D440-0x82E1D458 lays them out.
    if (overlappedResult != 0) {
        // THE THREE ERROR ARMS WERE ROTATED.  0x82E1D448 sends overlappedResult
        // == 0x12 (ERROR_NO_MORE_FILES) to .L_82E1D518, the "error no more
        // files" block -- which in our source was `error_no_more`, and NOTHING
        // BRANCHED TO IT.  0x82E1D454 sends 0x65b to .L_82E1D488, the
        // extended-error / winsock block.  And the FALLTHROUGH at 0x82E1D458 is
        // the "overlapped failed with ... extended ..." message, where our
        // source called XGetOverlappedExtendedError and threw the result away.
        // RESOLVED (w7-bn, 2026-09-15, 88.7 -> 100.0 canonical): the image's
        // dispatch is two FORWARD beq's with the bodies laid out default /
        // 0x65b / check_more_offers / error_no_more (0x82E1D444-0x82E1D518).
        // It is a `switch` written in CASE ORDER 0x12 / 0x65b / default.  MSVC
        // lays the compare chain out with the `default:` body as the
        // fall-through no matter where `default:` is written, and the three
        // arms share the `TheDebug << str; mEnumerating = false; return` tail
        // at .L_82E1D534: the cross-jumped copy that is KEPT is the earliest
        // one in source order, so the 0x12 arm must be written first for its
        // copy to be the one at .L_82E1D534.  w7-bi measured the same switch
        // with `default:` written first at 45.0 (210 instructions) and read
        // it as "MSVC duplicates the continue_enum tail"; re-measured (S1),
        // the 45.0 is a pure block-order difference plus two constant-
        // propagation rows, not tail duplication.  Still refuted: nested
        // inverted ifs (bi, 45.0, byte-identical to default-first) and a
        // forward `if (r == 0x12) ... else if (r == 0x65b) ... else` chain in
        // image order (37.0: bne inversions and 216 instructions).
        switch (overlappedResult) {
        case 0x12:
            if (mOfferIDsBegin == 0) {
                return;
            }
            // MakeString<unsigned int>, not <unsigned long>
            // (??$MakeString@I@@YAPBDPBDABI@Z at 0x82E1D530), and its argument
            // is overlappedResult's OWN home slot: 0x82E1D528 is `addi r4, r31,
            // 0x5c`, the same slot the default arm hands to
            // MakeString<unsigned long> at 0x82E1D47C, with no store in either
            // arm.  A value cast in either direction makes MSVC materialise a
            // temporary -- `(unsigned int)overlappedResult` folds the case
            // constant into it (`li r11, 0x12; stw r11, ...`, 2 rows), typing
            // the variable `unsigned int` and casting the default arm
            // `(DWORD)` stores r25 into one instead (idx 88), and a C-style
            // `(const unsigned int &)` cast is a static_cast here, so it is
            // the same temporary -- and that fifth 4-byte object is what
            // reverses the frame-slot order above.  Only an lvalue
            // reinterpretation reproduces the image (99.9 raw, 4 rows, all
            // frame slots equal).  DWORD is 4 bytes on every target this
            // builds for, so the pun is exact.
            TheDebug << MakeString(" store enum: error no more files (%d)\n", *(const unsigned int *)&overlappedResult);
            mEnumerating = false;
            return;
        case 0x65b: {
            DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
            // The 16-bit code is a NAMED DWORD local (HRESULT_CODE shape,
            // MakeString<unsigned long> at 0x82E1D4B0): its home store
            // `stw r30, 0x50(r31)` sits at 0x82E1D494, BEFORE the `== 0x12`
            // test.  As a `(unsigned long)(WORD)extError` temporary the store
            // sinks below the test (1 insert / 1 delete).
            DWORD extCode = extError & 0xFFFF;
            if (extCode == 0x12) {
                return;
            }
            // Same shape at 0x82E1D4A4/0x82E1D4AC: arg 1 is 0x50(r31), the
            // truncated value, and arg 2 is 0x54(r31), the full one.
            TheDebug << MakeString(" store enum: funciton failed with: %d (0x%X)\n", extCode, extError);
            if (extCode >= 0x2710 && extCode < 0x2EE0) {
                TheDebug << MakeString(" which is a winsock error, so fail.\n");
                mEnumerating = false;
                return;
            }
            // .L_82E1D4FC computes `mOfferIDsBegin + mOfferIDCount` and then
            // jumps INTO the continue_enum block at .L_82E1D570 -- the
            // `mOfferIDsCur >= end` test and the Start() call are SHARED
            // between the two paths, not duplicated.
            if (mOfferIDsBegin == 0) {
                return;
            }
            offersEnd = mOfferIDsBegin + mOfferIDCount;
            goto test_cur;
        }
        default: {
            DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
            DWORD extCode = extError & 0xFFFF;
            // The middle argument is the 16-BIT-TRUNCATED error: 0x82E1D45C is
            // `clrlwi r9, r3, 16`, and 0x54(r31) (arg 2) holds r9 while
            // 0x50(r31) (arg 3) holds the full value.
            TheDebug << MakeString(" store enum: overlapped failed with: %d, extended: %d (0x%X)\n", overlappedResult, extCode, extError);
            // Only the MESSAGE arms clear mEnumerating.  The shared tail at
            // .L_82E1D534/.L_82E1D540 is `bl TextStream::operator<<` /
            // `stb r25, 0x1c(r29)` / `b .L_82E1D590`, and the epilogue label
            // .L_82E1D590 itself carries NO store to 0x1c -- so a successful
            // poll must leave mEnumerating set, which is what IsSuccess() reads.
            mEnumerating = false;
            return;
        }
        }
    }

continue_enum:
    if (mOfferIDsBegin != 0) {
        goto compute_end;
    }
    if (bytesReceived >= 99) {
        goto call_start;
    }
compute_end:
    offersEnd = mOfferIDsBegin + mOfferIDCount;
test_cur:
    if (mOfferIDsCur >= offersEnd) {
        return;
    }
call_start:
    Start();
}

