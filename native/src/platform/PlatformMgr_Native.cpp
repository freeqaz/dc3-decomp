// DC3 Native Port - PlatformMgr Implementation
// Replaces PlatformMgr_Xbox.cpp - minimal platform manager

#include "os/PlatformMgr.h"
#include "os/Debug.h"

// The global PlatformMgr is typically declared in the header.
// Its methods will mostly be no-ops on native.

// Platform-specific methods that were in PlatformMgr_Xbox.cpp:
// Most of these are about Xbox Live sign-in, friends, etc.
// On native, we just return sensible defaults.
