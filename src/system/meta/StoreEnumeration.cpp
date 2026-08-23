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

void XboxEnumeration::Poll() {
    if (0 == mHandle || mOverlapped.InternalLow == 0x3E5U) {
        return;
    }

    DWORD bytesReceived = 0;
    DWORD overlappedResult = XGetOverlappedResult(&mOverlapped, &bytesReceived, 0);

    DWORD productCount = 0;
    if (bytesReceived > 0) {
        std::list<EnumProduct>::iterator it = mContentList.end();
        u32 offset = 0;
        while (productCount < bytesReceived) {
            // One String, not two: the image constructs EnumProduct FIRST
            // (??0String@@QAA@XZ into slot 0x60, which is prod.mName -- the
            // EnumProduct temp occupies 0x60..0x78 with mOfferID at 0x68,
            // mPurchased at 0x70 and mPrice at 0x74), then assigns the char
            // buffer straight in with String::operator=(char const*).  The
            // separate `String str` cost an extra ctor/dtor pair and turned the
            // assignment into operator=(String const&).
            char buf[256];
            EnumProduct prod;
            u8 *entryPtr = (u8 *)mCurOffers + offset;
            WideCharToMultiByte(0, 0, *(LPCWSTR *)(entryPtr + 0x14), *(int *)(entryPtr + 0x10), buf, 0xFF, 0, 0);
            prod.mName = buf;

            prod.mOfferID = *(u64 *)entryPtr;
            prod.mPurchased = *(int *)(entryPtr + 0x48);
            // mPrice is written BEFORE the insert (0x82E1D3D8 stores to 0x74,
            // then bl insert).  Setting it afterwards wrote to the dead local
            // and every product in mContentList kept price 0.
            prod.mPrice = *(int *)(entryPtr + 0x64);
            mContentList.insert(it, prod);

            offset += 0x68;
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

    if (overlappedResult == 0) {
        goto done;
    }

    // THE THREE ERROR ARMS WERE ROTATED.  0x82E1D448 sends overlappedResult
    // == 0x12 (ERROR_NO_MORE_FILES) to .L_82E1D518, the "error no more files"
    // block -- which in our source was `error_no_more`, and NOTHING BRANCHED TO
    // IT.  0x82E1D454 sends 0x65b to .L_82E1D488, the extended-error / winsock
    // block.  And the FALLTHROUGH at 0x82E1D458 is the "overlapped failed
    // with ... extended ..." message, where our source called
    // XGetOverlappedExtendedError and threw the result away.
    if (overlappedResult == 0x12) {
        goto error_no_more;
    }

    if (overlappedResult == 0x65b) {
        goto handle_65b;
    }

    {
        DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
        // The middle argument is the 16-BIT-TRUNCATED error: 0x82E1D45C is
        // `clrlwi r9, r3, 16`, and 0x54(r31) (arg 2) holds r9 while 0x50(r31)
        // (arg 3) holds the full value.
        TheDebug << MakeString(" store enum: overlapped failed with: %d, extended: %d (0x%X)\n", (unsigned long)overlappedResult, (unsigned long)(WORD)extError, (unsigned long)extError);
    }
    goto check_more_offers;

handle_65b:
    {
        DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
        if ((WORD)extError == 0x12) {
            goto done;
        }
        // Same shape at 0x82E1D4A4/0x82E1D4AC: arg 1 is 0x50(r31), the
        // truncated value, and arg 2 is 0x54(r31), the full one.
        TheDebug << MakeString(" store enum: funciton failed with: %d (0x%X)\n", (unsigned long)(WORD)extError, (unsigned long)extError);
        if ((WORD)extError >= 0x2710 && (WORD)extError < 0x2EE0) {
            TheDebug << MakeString(" which is a winsock error, so fail.\n");
        }
    }

check_more_offers:
    if (mOfferIDsBegin != 0) {
        if (mOfferIDsCur < mOfferIDsBegin + mOfferIDCount) {
            goto continue_enum;
        }
    }
    goto done;

error_no_more:
    if (mOfferIDsBegin != 0) {
        // MakeString<unsigned int>, not <unsigned long>:
        // ??$MakeString@I@@YAPBDPBDABI@Z at 0x82E1D530.
        TheDebug << MakeString(" store enum: error no more files (%d)\n", (unsigned int)overlappedResult);
        mEnumerating = false;
        return;
    }
    goto done;

continue_enum:
    if (mOfferIDsBegin != 0) {
        if (mOfferIDsCur < mOfferIDsBegin + mOfferIDCount) {
            Start();
            return;
        }
    } else {
        if (bytesReceived >= 99) {
            Start();
            return;
        }
    }

done:
    mEnumerating = false;
}

