# XboxEnumeration::Poll - Deep Dive

**Date**: 2026-02-19
**Match**: 26.1% -> 65.7% (+39.6%)
**Status**: AT_LIMIT

## What This Function Does

`XboxEnumeration::Poll` is the Xbox 360 marketplace content enumeration state machine. It checks if an asynchronous `XEnumerate` call has completed, parses the returned offer data into `EnumProduct` structs, handles errors, and decides whether to continue enumerating more offers.

The store system works like this:
1. `StorePanel::EnumerateOffers` creates a `StoreEnumJob` which creates an `XboxEnumeration`
2. `XboxEnumeration::Start()` calls Xbox SDK APIs to begin async offer enumeration
3. `XboxEnumeration::Poll()` is called each frame to check if results are ready
4. When complete, `StorePanel::FinishEnum` processes the `EnumProduct` list

## The Challenge

This function has extremely complex control flow with gotos, error handling branches, and interleaved Xbox SDK calls. The target binary's control flow graph has ~15 basic blocks with non-trivial goto-based transitions that don't map cleanly to structured C++.

### Key Complications

**1. Xbox Marketplace API**
The function uses Xbox 360 SDK APIs that don't exist in any public documentation:
- `XGetOverlappedResult` - check async operation status
- `XGetOverlappedExtendedError` - get detailed error info
- `XOVERLAPPED` struct at offset 0x20 with `InternalLow` field for status
- `XMARKETPLACE_CONTENTOFFER_INFO` entries at 0x68 bytes each

**2. Goto-Based Error Dispatch**
The target binary has a 3-way error dispatch after the main enumeration loop:
```
overlappedResult == 0     -> done (success)
overlappedResult == 0x12  -> check winsock error range
overlappedResult == 0x65b -> log "overlapped failed"
default                   -> just get extended error
```
All error paths converge at `check_more_offers` which decides whether to call `Start()` again for the next batch.

**3. EnumProduct Parsing**
Each marketplace offer entry is 0x68 bytes:
- `+0x00`: 8-byte offer ID (u64, stored via `*(u64*)&prod.unk8`)
- `+0x10`: title string length (int)
- `+0x14`: title string (wide char, converted via `WideCharToMultiByte`)
- `+0x48`: purchased flag (int)
- `+0x64`: cost (int)

The Milo `String` class is used for the title conversion, matching the target's `String::String()`/`String::operator=()`/`String::~String()` call sequence.

**4. Batched Enumeration**
The Xbox API returns at most 99 offers per call. If there are more:
- For general enumeration (`unk10 == 0`): continue if `bytesReceived >= 99`
- For specific offer IDs (`unk10 != 0`): advance `mCurOffers` pointer and check if more remain

## Implementation Journey

### Attempt 1: std::string (WRONG)
The original implementation used `std::string` for the title conversion. The target uses Milo `String` class which generates completely different codegen:
- `String::String()` on stack (constructor)
- `String::operator=(const char*)` for assignment
- `String::~String()` in destructor
vs `std::string` which uses small string optimization and different ABI.

### Attempt 2: Structured if/else (PARTIAL)
Tried to use structured C++ control flow instead of gotos. This produced different basic block ordering and branch directions because the MSVC PowerPC compiler lays out blocks in declaration order.

### Attempt 3: Goto-Based (CURRENT - 65.7%)
Matched the target's control flow with explicit gotos and labels:
```
handle_65b, handle_12, check_more_offers, error_no_more, continue_enum, done
```
This gets the basic block ordering correct but still has:
- +16 byte stack frame difference (target 0x11F0 vs ours 0x1200)
- 2 LINKER_MERGED calls (operator delete, MakeString template)
- Register swap r23/r25 across 13 instructions
- 5 condition inversions (beq/bne swaps)

## Remaining Mismatches (34.3%)

| Pattern | Instructions | Fixable? |
|---------|-------------|----------|
| LINKER_MERGED | 2 calls | No (ICF) |
| REGISTER_SWAP (r23/r25) | 13 | No (compiler quirk) |
| Stack frame +16 | 2 | Unknown source |
| Control flow inversions | 5 | Maybe but risky |
| Symbol relocations | ~30 | No (linker addresses) |
| Insert/delete blocks | 21/30 | Structural difference |

## Struct Layout

```cpp
class XboxEnumeration : public StoreEnumeration {
    // StoreEnumeration base: vtable + mContentList (std::list<EnumProduct>)
    u32 mOfferIDCount;          // 0x0C
    unsigned long long *unk10;  // 0x10 - allocated offer ID array
    unsigned long long *mCurOffers; // 0x14 - current position in offer array
    int unk18;                  // 0x18 - player index
    bool unk1c;                 // 0x1c - is enumerating flag
    XOVERLAPPED mOverlapped;   // 0x20 - async operation state (28 bytes)
    HANDLE mHandle;             // 0x3C - enumeration handle
    u32 unk40;                  // 0x40 - buffer size from XMarketplaceCreate*
    void *mEnumBuffer;          // 0x44 - buffer for XEnumerate results
};

struct EnumProduct {
    u32 unk0;   // unused?
    u32 unk4;   // unused?
    u64 unk8;   // offer ID
    int unk10;  // purchased flag
    int unk14;  // cost
};
```

## Lessons Learned

1. **Milo String vs std::string**: The Milo engine has its own `String` class. Using `std::string` produces completely different codegen. Always check what the target actually calls.

2. **Goto-based control flow**: When the target has complex goto-based flow (common in error handling), structured C++ won't match. Use explicit gotos with labels.

3. **Xbox SDK APIs are opaque**: No public docs for `XMarketplaceCreate*`, `XGetOverlappedExtendedError`, etc. Must reverse-engineer from Ghidra decompilation and cross-reference with other games.

4. **Batched async enumeration**: The pattern of "enumerate up to N, check if more, call Start() again" is common in Xbox marketplace APIs. The 99-offer batch size appears to be an API limit.
