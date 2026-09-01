#pragma once
#include "win_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct __declspec(uuid("00000000-0000-0000-c000-000000000046")) IUnknown { /* Size=0x4 */
    virtual HRESULT QueryInterface(const _GUID &riid, void **ppvObject);
    virtual ULONG AddRef();
    virtual ULONG Release();

    //   public: IUnknown(const IUnknown&);
    //   public: IUnknown();
    //   public: IUnknown& operator=(const IUnknown&);
};

#ifdef __cplusplus
}
#endif
