// ??_GFileStream@@UAAPAXI@Z (the scalar deleting destructor) is stuck at
// 99.583%, and the whole residue is ONE string literal: the __FILE__ that
// MEM_OVERLOAD's MemFree call passes.  The image has
// "e:\lazer_build_gmc1\system\src\utl\FileStream.h", we emit
// "...\src\utl/FileStream.h" -- a forward slash, because MSVC builds __FILE__ as
// <search-dir> + '\' + <as-written> and the spelling that wins is the FIRST
// include of the header in the TU.  This file's own line 1 is already a
// backslash; it loses because os/Debug.h is force-included through the PCH and
// pulls utl/FileStream.h in via `#include "utl/TextFileStream.h"` -> that
// header's line 2 `#include "utl/FileStream.h"`.
//
// REFUTED (w9-a): flipping TextFileStream.h line 2 to a backslash does take this
// row to 100.0% and costs SEVEN others.  Whole-binary A/B, full ninja both ways:
//   UP 1    utl/FileStream  ??_GFileStream                  99.583 -> 100.0   96 B
//   DOWN 7  synth/WavReader ??0WavReader ctor               100.0 -> 99.9505 808 B
//           rndobj/HiResScreen ?Finish@HiResScreen          100.0 -> 99.9408 676 B
//           gesture/SkeletonClip ?StopRecordingNoClear      100.0 -> 99.8667 300 B
//           rndobj/Bitmap ?LoadBmp@RndBitmap                100.0 -> 99.8387 248 B
//           os/HDCache ?OpenHeader@HDCache                  100.0 -> 99.7872 188 B
//           rndobj/Bitmap ?SaveBmp@RndBitmap                100.0 -> 99.7297 148 B
//           midi/MidiReader ??3FileStream@@SAXPAX@Z         100.0 -> 98.3333  24 B
//   matched_functions 31374 -> 31368, matched_code 5,569,568 -> 5,567,272.
// obj/Data.h's comment on the same trade says "6 want forward"; the measured
// number is 7.  The retail build carried both spellings because it had per-TU
// headers; one shared PCH cannot, so this unit cannot reach 100% without
// splitting the PCH.  Do not re-try the flip.
#include "utl\FileStream.h"
#include "os\File.h"
#include "os\Debug.h"

FileStream::FileStream(const char *file, FileType type, bool lilEndian)
    : BinStream(lilEndian), mChecksumValidator(0), mBytesChecksummed(0) {
    int fmode;
    if (type == kRead) {
        fmode = 2;
    } else if (type == kReadNoArk) {
        fmode = 0x10002;
    } else {
        fmode = type == kAppend ? 0x109 : 0x301;
    }
    mFilename = file;
    mFile = NewFile(file, fmode);
    mFail = (mFile == 0);
}

FileStream::FileStream(File *f, bool b)
    : BinStream(b), mFilename(), mChecksumValidator(0), mBytesChecksummed(0) {
    mFile = f;
    mFail = false;
}

void FileStream::ReadImpl(void *data, int bytes) {
    int got = mFile->Read(data, bytes);
#ifdef HX_NATIVE
    if (got != bytes) {
        printf("DC3 Native: FileStream::ReadImpl FAIL: wanted %d, got %d, file='%s', tell=%d, size=%d, fileFail=%d\n",
               bytes, got, mFilename.c_str(), mFile->Tell(), mFile->Size(), mFile->Fail());
    }
#endif
    if (got != bytes)
        mFail = true;
    else if (mChecksumValidator) {
        mChecksumValidator->Update((const unsigned char *)data, bytes);
        mBytesChecksummed += bytes;
    }
}

void FileStream::WriteImpl(const void *data, int bytes) {
    if (mFile->Write((char *)data, bytes) != bytes)
        mFail = true;
}

void FileStream::Flush() {
    MILO_ASSERT(!mFail, 0x4C);
    mFile->Flush();
}

void FileStream::SeekImpl(int offset, SeekType t) {
    int d[3] = { 0, 1, 2 };
    MILO_ASSERT(!mFail, 0x55);
    int res = mFile->Seek(offset, d[t]);
    if (res < 0)
        mFail = true;
}

bool FileStream::Fail() { return mFail; }

int FileStream::Tell() {
    MILO_ASSERT(!mFail, 0x5D);
    return mFile->Tell();
}

EofType FileStream::Eof() {
    MILO_ASSERT(!mFail, 0x64);
    return (EofType)(mFile->Eof() != false);
}

bool FileStream::ValidateChecksum() {
    if (!mChecksumValidator)
        return false;
    else {
        mChecksumValidator->End();
        MILO_ASSERT(mBytesChecksummed == Size(), 0x85);
        return mBytesChecksummed == Size() && mChecksumValidator->Validate();
    }
}

void FileStream::DeleteChecksum() {
    delete mChecksumValidator;
    mChecksumValidator = 0;
    mBytesChecksummed = 0;
}

void FileStream::StartChecksum() {
    DeleteChecksum();
    mChecksumValidator = new StreamChecksumValidator();
    if (!mChecksumValidator->Begin(Name(), false))
        DeleteChecksum();
}

FileStream::~FileStream() {
    if (!mFilename.empty()) {
        delete mFile;
    }
    DeleteChecksum();
}
