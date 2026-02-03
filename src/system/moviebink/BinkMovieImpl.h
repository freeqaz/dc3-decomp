#pragma once

struct BINK;

class MovieInternalBuffers {
public:
    MovieInternalBuffers();
    ~MovieInternalBuffers();

private:
    // Pointers to BINK structures (offsets 0x0-0x40)
    BINK* mBinks[17];

    // Padding from 0x44 to 0xBC (0x78 bytes)
    // This is memset to 0 in constructor
    char mPadding[0x78];  // 0x44

    // Additional field at 0xBC
    void* mUnknown;  // 0xBC
};

static_assert(sizeof(MovieInternalBuffers) == 0xC0, "MovieInternalBuffers size mismatch");
