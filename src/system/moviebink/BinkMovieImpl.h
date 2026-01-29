#pragma once

struct BINK;

class MovieInternalBuffers {
public:
    MovieInternalBuffers();
    ~MovieInternalBuffers();

private:
    // Pointers to BINK structures (offsets 0x0-0x40)
    BINK* mBinks[17];  // Offsets 0x0, 0x4, 0x8, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40

    // Padding from 0x44 to 0xBC (0x78 bytes)
    // This is memset to 0 in constructor
    char mPadding[0x78];  // 0x44

    // Additional field at 0xBC
    void* mUnknown;  // 0xBC
};

static_assert(sizeof(MovieInternalBuffers) == 0xC0, "MovieInternalBuffers size mismatch");
