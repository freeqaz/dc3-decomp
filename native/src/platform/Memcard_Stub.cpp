// DC3 Native Port - Memcard Stub
// Replaces Memcard_Xbox.cpp - filesystem-based save (later)

#include "os/Memcard.h"
#include "os/Memcard_Xbox.h"
#include <cstring>
#include <cwchar>

MemcardXbox TheMC;

void MemcardXbox::Poll() {}

// Init moved out-of-line in Memcard_Xbox.h (68973a46), making the Xbox TU the
// vtable key-function home. Native replaces that TU with this stub, so the
// definition (and thus the vtable emission) must live here too.
void MemcardXbox::Init() { Memcard::Init(); }

// Same story for Terminate, moved out-of-line so the SystemTerminate call site
// emits ?Terminate@MemcardXbox@@UAAXXZ (see src/system/os/Memcard_Xbox.cpp).
// Retail's body is a bare blr, and Memcard::Terminate is empty anyway.
void MemcardXbox::Terminate() {}

void MemcardXbox::SetContainerName(const char *name) {
    strncpy(mFileName, name, XCONTENT_MAX_FILENAME_LENGTH - 1);
    mFileName[XCONTENT_MAX_FILENAME_LENGTH - 1] = '\0';
}

void MemcardXbox::SetContainerDisplayName(const wchar_t *name) {
    wcsncpy(mDisplayName, name, XCONTENT_MAX_DISPLAYNAME_LENGTH - 1);
    mDisplayName[XCONTENT_MAX_DISPLAYNAME_LENGTH - 1] = L'\0';
}

void MemcardXbox::ShowDeviceSelector(const ContainerId &, Hmx::Object *, int, bool) {}
bool MemcardXbox::IsDeviceValid(const ContainerId &) { return false; }
MCResult MemcardXbox::DeleteContainer(const ContainerId &) { return (MCResult)0; }
MCContainer *MemcardXbox::CreateContainer(const ContainerId &id) {
    return new MCContainerXbox(id);
}

// MCContainerXbox stubs
MCContainerXbox::MCContainerXbox(const ContainerId &id) : MCContainer(id) {}
MCResult MCContainerXbox::Mount(CreateType) { return (MCResult)0; }
MCResult MCContainerXbox::Unmount() { return (MCResult)0; }
MCResult MCContainerXbox::GetPathFreeSpace(const char *, u64 *) { return (MCResult)0; }
MCResult MCContainerXbox::GetDeviceFreeSpace(u64 *) { return (MCResult)0; }
MCResult MCContainerXbox::Delete(const char *) { return (MCResult)0; }
MCResult MCContainerXbox::RemoveDir(const char *) { return (MCResult)0; }
MCResult MCContainerXbox::MakeDir(const char *) { return (MCResult)0; }
MCResult MCContainerXbox::GetSize(const char *, int *) { return (MCResult)0; }
MCFile *MCContainerXbox::CreateMCFile() { return new MCFileXbox(this); }
String MCContainerXbox::BuildPath(const char *f) { return String(f); }
MCResult MCContainerXbox::PrintDir(const char *, bool) { return (MCResult)0; }

// MCFileXbox stubs
MCResult MCFileXbox::Open(const char *, AccessType, CreateType) { return (MCResult)0; }
MCResult MCFileXbox::Read(void *, int) { return (MCResult)0; }
MCResult MCFileXbox::Write(const void *, int) { return (MCResult)0; }
MCResult MCFileXbox::Seek(int, SeekType) { return (MCResult)0; }
MCResult MCFileXbox::Close() { return (MCResult)0; }
bool MCFileXbox::IsOpen() { return false; }
MCResult MCFileXbox::GetSize(int *out) { if (out) *out = 0; return (MCResult)0; }
