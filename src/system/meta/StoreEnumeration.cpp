#include "meta/StoreEnumeration.h"
#include "os/Debug.h"
#include "utl/MakeString.h"
#include "xdk/XAPILIB.h"
#include <cstring>

XboxEnumeration::XboxEnumeration(int i, std::vector<unsigned long long> *offerIDs)
    : unk18(i), mOfferIDCount(0), unk10(0), mCurOffers(0), unk1c(false), mHandle(0), unk40(0), mEnumBuffer(0) {
    if (offerIDs != 0) {
        mOfferIDCount = (offerIDs->end() - offerIDs->begin());
        MILO_ASSERT(mOfferIDCount, 0x197);
        u32 allocSize = mOfferIDCount << 3;
        if (mOfferIDCount > 0x1FFFFFFFU) {
            allocSize = 0xFFFFFFFF;
        }
        unk10 = (unsigned long long *)new char[allocSize];
        memcpy(unk10, &(*offerIDs)[0], mOfferIDCount << 3);
        mCurOffers = unk10;
    }
}

XboxEnumeration::~XboxEnumeration() {
    delete[] unk10;
    unk10 = 0;

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

    delete mEnumBuffer;
    mEnumBuffer = 0;
}

bool XboxEnumeration::IsSuccess() const {
    if (*((u32*)((u8*)this + 0x3c)) != 0) {
        MILO_ASSERT(false, 0x208);
    }
    return *((bool*)((u8*)this + 0x24));
}

void XboxEnumeration::Start() {
    unk1c = true;
    if (mHandle == 0) {
        int error;
        unk40 = 0;
        if (mCurOffers == unk10) {
            mContentList.clear();
        }
        if (unk10 == 0) {
            error = XMarketplaceCreateOfferEnumerator(unk18, 0x100002, 0xFFFFFFFFFFFFFFFFULL, 99, &unk40, &mHandle);
        } else {
            int remaining = (int)(mOfferIDCount - (u32)(mCurOffers - unk10));
            if (remaining >= 99) remaining = 99;
            error = XMarketplaceCreateOfferEnumeratorByOffering(unk18, remaining, mCurOffers, (WORD)remaining, &unk40, &mHandle);
            mCurOffers += remaining;
        }
        MILO_ASSERT(!mEnumBuffer, 0x1EA);
        mEnumBuffer = new char[unk40];
        if (error != 0) {
            goto error_path;
        }
    }
    memset(mEnumBuffer, 0, unk40);
    memset(&mOverlapped, 0, 0x1c);
    {
        DWORD result = XEnumerate(mHandle, mEnumBuffer, unk40, 0, &mOverlapped);
        if (result == 0x3e5) {
            return;
        }
    }
error_path:
    if (mHandle != 0) {
        CloseHandle(mHandle);
        mHandle = 0;
    }
    delete[] (char*)mEnumBuffer;
    unk1c = false;
    mEnumBuffer = 0;
}

bool XboxEnumeration::IsEnumerating() const {
    return unk1c;
}

void XboxEnumeration::Poll() {
    if (mHandle == 0 || mOverlapped.InternalLow == 0x3E5U) {
        return;
    }

    DWORD bytesReceived = 0;
    DWORD overlappedResult = XGetOverlappedResult(&mOverlapped, &bytesReceived, 0);

    DWORD productCount = 0;
    if (bytesReceived > 0) {
        u8 *entryPtr = (u8 *)mEnumBuffer;
        std::list<EnumProduct>::iterator it = mContentList.end();
        while (productCount < bytesReceived) {
            String str;
            char buf[256];
            DWORD nameLen = *(DWORD *)(entryPtr + 0x10);
            LPCWSTR wideName = *(LPCWSTR *)(entryPtr + 0x14);
            WideCharToMultiByte(0, 0, wideName, nameLen, buf, 0xFF, 0, 0);
            str = buf;

            EnumProduct prod;
            u64 offerID = *(u64 *)entryPtr;
            prod.unk0 = 0;
            prod.unk4 = 0;
            prod.unk8 = (u32)(offerID >> 32);
            prod.unkc = (u32)(offerID);
            prod.unk10 = *(int *)(entryPtr + 0x2A20);
            prod.unk14 = *(int *)(entryPtr + 0x2A24);
            mContentList.insert(it, prod);

            entryPtr += 0x2A40;
            productCount++;
        }
    }

    if (overlappedResult == 0 && bytesReceived >= 99) {
        if (unk1c) {
            u32 remaining = mOfferIDCount - (u32)(mCurOffers - unk10);
            if (remaining > 0) {
                Start();
            }
        }
        return;
    }

    if (mHandle != 0) {
        CloseHandle(mHandle);
        mHandle = 0;
    }

    delete (char *)mEnumBuffer;
    mEnumBuffer = 0;

    if (overlappedResult == 0x12) {
        // ERROR_NO_MORE_FILES - enumeration complete
    } else if (overlappedResult == 0x65B) {
        // ERROR_FUNCTION_FAILED
        DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
        TheDebug << MakeString(" store enum: funciton failed with: %d (0x%X)", overlappedResult, overlappedResult);
        if (extError >= 0x2710 && extError < 0x2EE0) {
            TheDebug << " which is a winsock error, so fail.";
        }
    } else if (overlappedResult != 0) {
        DWORD extError = XGetOverlappedExtendedError(&mOverlapped);
        FormatString fs(" store enum: overlapped failed with: %d, extended: %d (0x%X)");
        fs << overlappedResult << extError << extError;
        TheDebug << fs.Str();
    }

    unk1c = false;
}
