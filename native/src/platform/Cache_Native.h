#pragma once

// DC3 Native Port - Cache backend entry points.
// The implementation lives in Cache_Stub.cpp (CacheMgrNative / CacheNative /
// CacheIDNative). src/system/utl/CacheMgr.cpp's HX_NATIVE branch reaches the
// factory through an extern declaration, because that decomp TU cannot include
// a native-only header.

class CacheMgr;

// The cache backend used by CacheMgr::CreateCacheMgr() on native.
CacheMgr *CacheMgrCreateNativeBackend();

// Constructs TheCacheMgr if it is still null. Not called from System.cpp yet:
// wiring the cache into the boot path is a separate phase.
void CacheMgrNativeInit();

// Destroys TheCacheMgr.
void CacheMgrNativeTerminate();
