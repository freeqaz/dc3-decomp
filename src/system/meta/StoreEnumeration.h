#pragma once
#include "stl/_vector.h"
#include "types.h"
#include "utl/Str.h"
#include "xdk/xapilibi/xbase.h"
#include <list>

enum StoreError {
    kStoreErrorSuccess = 0,
    kStoreErrorNoContent = 1,
    kStoreErrorCacheNoSpace = 2,
    kStoreErrorCacheRemoved = 3,
    kStoreErrorLiveServer = 4,
    kStoreErrorStoreServer = 5,
    kStoreErrorSignedOut = 6,
    kStoreErrorNoMetadata = 7,
    kStoreErrorEcommerce = 8,
    kStoreErrorNoEula = 9
};

struct EnumProduct {
    u32 unk0;
    u32 unk4;
    u32 unk8;
    u32 unkc;
    int unk10;
    int unk14;
};

class StoreEnumeration {
public:
    enum State {
        kEnumWaiting = 0,
        kEnumProcessing = 1,
        kPreSuccess = 2,
        kPreFail = 3,
        kSuccess = 4,
        kFail = 5,
    };
    StoreEnumeration() {}
    virtual ~StoreEnumeration() {}
    virtual void Start() = 0;
    virtual bool IsEnumerating() const = 0;
    virtual bool IsSuccess() const = 0;
    virtual void Poll() = 0;

    std::list<EnumProduct> mContentList;
};

class XboxEnumeration : public StoreEnumeration {
public:
    // StoreEnumeration
    virtual ~XboxEnumeration();
    virtual void Start();
    virtual bool IsEnumerating() const;
    virtual bool IsSuccess() const;
    virtual void Poll();

    XboxEnumeration(int, std::vector<unsigned long long> *);

protected:
    void *unk10;                            // 0x10 - pointer deleted in destructor
    std::vector<unsigned long long> *unkc;  // 0x14 - pointer to vector (not owned)
    int unk18;                              // 0x18
    bool unk1c;                             // 0x1c
    XOVERLAPPED mOverlapped;                // 0x20 - Xbox overlapped I/O structure (28 bytes)
    HANDLE mEnumHandle;                     // 0x3C - enumeration handle
    u32 unk40;                              // 0x40
    void *mEnumBuffer;                      // 0x44 - buffer for enumeration results
};
