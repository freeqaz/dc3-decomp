#include "meta/StoreEnumeration.h"
#include "os/Debug.h"
#include "xdk/win_types.h"
#include "xdk/xapilibi/handleapi.h"
#include "xdk/xapilibi/xbase.h"
#include "xdk/xapilibi/xbox.h"
#include "xdk/xapilibi/stringapiset.h"

XboxEnumeration::XboxEnumeration(int i, std::vector<unsigned long long> *mOfferIDCount)
    : unk18(i), unk1c(false) {}

XboxEnumeration::~XboxEnumeration() {}

bool XboxEnumeration::IsSuccess() const {
    if (*((u32*)((u8*)this + 0x3c)) == 0) {
        return *((bool*)((u8*)this + 0x24));
    }
    MILO_ASSERT(false, "mHandle");
    return *((bool*)((u8*)this + 0x24));
}

void XboxEnumeration::Start() {}

void XboxEnumeration::Poll() {
    u32 handle = *(u32*)(void*)((u32)this + 0x3C);
    u32 rev = *(u32*)(void*)((u32)this + 0x20);

    if (handle == 0 || rev == 0x3E5U) {
        return;
    }

    u32 bytesReceived = 0;
    u32 overlappedResult = XGetOverlappedResult((XOVERLAPPED*)(void*)((u32)this + 0x20), &bytesReceived, 0);

    u32 productCount = 0;
    if (bytesReceived > 0) {
        auto it = mContentList.begin();
        u32 index = 0;
        while (productCount < bytesReceived) {
            std::string tempStr;
            char buffer[0x100];
            void* dataPtr = (void*)((u32)this + 0x44 + (index * 0x68));
            LPCWSTR wideStr = (LPCWSTR)((u32)dataPtr + 0x10);
            int wideStrLen = *(s32*)(void*)((u32)dataPtr + 0x14);
            WideCharToMultiByte(0, 0, wideStr, wideStrLen, buffer, 0xFF, 0, 0);
            tempStr = buffer;

            EnumProduct prod;
            prod.unk0 = *(u32*)dataPtr;
            prod.unk4 = *(u32*)(void*)((u32)dataPtr + 0x48);
            prod.unk8 = *(u32*)(void*)((u32)dataPtr + 0x64);
            mContentList.insert(it, prod);

            productCount++;
            index += 0x68;
        }
    }

    if (unk1c == 0 && overlappedResult == 0 && bytesReceived >= 0x63) {
        // Success path - just return
        return;
    }

    // Error handling path
    if ((*(u32*)(void*)((u32)this + 0x3C)) != 0) {
        CloseHandle(*(HANDLE*)(void*)((u32)this + 0x3C));
        *(u32*)(void*)((u32)this + 0x3C) = 0;
    }

    void* bufPtr = *(void**)(void*)((u32)this + 0x44);
    if (bufPtr != 0) {
        delete bufPtr;
    }
    *(u32*)(void*)((u32)this + 0x44) = 0;

    switch (overlappedResult) {
    case 0x65B: {
        u16 extError = XGetOverlappedExtendedError((XOVERLAPPED*)(void*)((u32)this + 0x20));
        if (extError != 0x12) {
            if (extError >= 0x2710 && extError < 0x2EE0) {
                // Winsock error handling
            }
            if (unk1c != 0) {
                u32 calcVal = (unkc.size() * 8) + unk1c;
                if (unk18 < calcVal) {
                    typedef void (*VirtFunc)(void*);
                    VirtFunc vf = *(VirtFunc*)(void*)((*(u32*)this) + 4);
                    vf(this);
                }
            }
        }
        break;
    }
    case 0x12: {
        if (unk1c != 0) {
            u16 extError = XGetOverlappedExtendedError((XOVERLAPPED*)(void*)((u32)this + 0x20));
        }
        break;
    }
    case 0x0:
    default: {
        if (overlappedResult != 0) {
            u16 extError = XGetOverlappedExtendedError((XOVERLAPPED*)(void*)((u32)this + 0x20));
        }
        break;
    }
    }

    unk1c = 0;
}
