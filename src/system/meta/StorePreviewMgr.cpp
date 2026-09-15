#include "meta\StorePreviewMgr.h"

#include "meta\StreamPlayer.h"
#include "movie\TexMovie.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj\MetaMaterial.h"
#include "synth\MoggClip.h"
#include "utl\NetCacheLoader.h"
#include "utl\NetCacheMgr.h"
#include "utl\Str.h"
#include "utl\Symbol.h"

StorePreviewMgr::StorePreviewMgr()
    : mAttenuation(0.0f), mLoopForever(1), mStreamPlayer(nullptr), mNetCacheLoader(0), mHasFailure(0) {
    mStreamPlayer = new StreamPlayer();
    MILO_ASSERT(mStreamPlayer, 0x1d);
    DataArray *d = SystemConfig("sound", "song_select");
    d->FindData("loop_forever", mLoopForever);
    d->FindData("attenuation", mAttenuation);
    SetName("store_preview_mgr", ObjectDir::Main());
}

StorePreviewMgr::~StorePreviewMgr() {
    RELEASE(mStreamPlayer);
    if (mNetCacheLoader) {
        TheNetCacheMgr->DeleteNetCacheLoader(mNetCacheLoader);
        mNetCacheLoader = 0;
    }
}

bool StorePreviewMgr::GetLastFailure(NetCacheMgrFailType &t) {
    if (mHasFailure) {
        t = mLastFailType;
        mHasFailure = false;
        return true;
    }
    return false;
}

bool StorePreviewMgr::IsPlaying() const {
    return (!mCurrentPreviewFile.empty() && TheNetCacheMgr->IsLocalFile(mCurrentPreviewFile.c_str()));
}

void StorePreviewMgr::ClearCurrentPreview() {
    if (!mCurrentPreviewFile.empty()) {
        mCurrentPreviewFile = gNullStr;
        PlayCurrentPreview();
    }
}

void StorePreviewMgr::SetCurrentPreviewFile(String const &str, TexMovie *tex) {
    if (mCurrentPreviewFile == str && mTexMovie == tex)
        return;
    mTexMovie = tex;
    mCurrentPreviewFile = str;
    PlayCurrentPreview();
}

bool StorePreviewMgr::IsDownloadingFile(String const &str) {
    if (mNetCacheLoader) {
        if (str == mNetCacheLoader->GetRemotePath()) {
            return true;
        }
    }
    return mDownloadQueue.end() != std::find(mDownloadQueue.begin(), mDownloadQueue.end(), str);
}

bool StorePreviewMgr::AllowPreviewDownload(String const &str) {
    if (mNetCacheLoader) {
        if (str == mNetCacheLoader->GetRemotePath())
            return false;
    }
    if (TheNetCacheMgr->IsLocalFile(str.c_str()))
        return false;
    else
        return std::find(mDownloadQueue.begin(), mDownloadQueue.end(), str) == mDownloadQueue.end();
}

void StorePreviewMgr::PlayCurrentPreview() {
    MILO_ASSERT(mStreamPlayer, 0xd8);
    if (mCurrentPreviewFile.empty() || !TheNetCacheMgr->IsLocalFile(mCurrentPreviewFile.c_str())) {
        mStreamPlayer->StopPlaying();
        if (mTexMovie) {
            FilePath fp(gNullStr);
            mTexMovie->SetFile(fp);
        }
    } else {
        String str(mCurrentPreviewFile.c_str());
        if (mTexMovie) {
            mStreamPlayer->StopPlaying();
            {
                FilePath fp(mCurrentPreviewFile.c_str());
                mTexMovie->SetFile(fp);
            }
            mTexMovie->SetVolume(-mAttenuation);
        } else {
            int len = str.length();
            if (str.find(".mogg", len - 5) != String::npos) {
                str.erase(len - 5);
            }
            mStreamPlayer->PlayFile(str.c_str(), -mAttenuation, 0.0f, mLoopForever);
        }
    }
}

void StorePreviewMgr::AddToDownloadQueue(String const &str) {
    if (mNetCacheLoader) {
        if (str == mNetCacheLoader->GetRemotePath()) {
            return;
        }
    }
    if (!TheNetCacheMgr->IsLocalFile(str.c_str())) {
        if (std::find(mDownloadQueue.begin(), mDownloadQueue.end(), str) == mDownloadQueue.end())
            mDownloadQueue.push_back(str);
    }
}

BEGIN_HANDLERS(StorePreviewMgr)
HANDLE_ACTION(clear_current_preview, ClearCurrentPreview())
HANDLE_ACTION(set_current_preview_file, SetCurrentPreviewFile(_msg->Str(2), nullptr))
HANDLE_ACTION(set_current_preview_movie, SetCurrentPreviewFile(_msg->Str(2), _msg->Obj<TexMovie>(3)))
HANDLE_ACTION(download_preview_file, AddToDownloadQueue(_msg->Str(2)))
HANDLE_EXPR(is_downloading_file, IsDownloadingFile(_msg->Str(2)))
HANDLE_EXPR(allow_preview_download, AllowPreviewDownload(_msg->Str(2)))
HANDLE_EXPR(is_playing, IsPlaying())
HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void StorePreviewMgr::Poll() {
    MILO_ASSERT(mStreamPlayer, 0x6f);
    mStreamPlayer->Poll();
    if (mNetCacheLoader) {
        bool isCurrentFile = mCurrentPreviewFile == mNetCacheLoader->GetRemotePath();
        if (mNetCacheLoader->IsLoaded()) {
            TheNetCacheMgr->IsLocalFile(mNetCacheLoader->GetRemotePath());
            TheNetCacheMgr->DeleteNetCacheLoader(mNetCacheLoader);
            mNetCacheLoader = nullptr;
            if (isCurrentFile) {
                PlayCurrentPreview();
            }
            static PreviewDownloadCompleteMsg msg(true, false);
            msg[1] = isCurrentFile;
            Hmx::Object::Handle(msg, false);
        } else if (mNetCacheLoader->HasFailed()) {
            mHasFailure = true;
            mLastFailType = mNetCacheLoader->GetFailType();
            TheNetCacheMgr->DeleteNetCacheLoader(mNetCacheLoader);
            mNetCacheLoader = nullptr;
            static PreviewDownloadCompleteMsg msg(false, false);
            msg[1] = isCurrentFile;
            Hmx::Object::Handle(msg, false);
        }
    }

    for (auto it = mDownloadQueue.begin(); it != mDownloadQueue.end()
         && TheNetCacheMgr->IsLocalFile(mDownloadQueue.front().c_str());
         it = ++mDownloadQueue.end()) {
        mDownloadQueue.pop_front();
    }

    if (!mNetCacheLoader && !mDownloadQueue.empty()) {
        MILO_ASSERT(!TheNetCacheMgr->IsLocalFile(mDownloadQueue.front().c_str()), 0xa5);
        mNetCacheLoader = TheNetCacheMgr->AddNetCacheLoader(
            mDownloadQueue.front().c_str(), (NetLoaderPos)1
        );
        mDownloadQueue.pop_front();
    }
}
// w8-j 2026-09-15 -- FLOOR for three rows in this TU, all ONE phenomenon:
// MSVC coalesces an EH-tracked object onto a stack slot the image kept separate.
// Frame sizes match exactly on both functions, no instruction is inserted or
// deleted, and every mismatched row is an `addi rN, r31, <slot>`.
//
//   ?Handle@StorePreviewMgr@@UAA?AVDataNode@@PAVDataArray@@_N@Z  99.9897%
//     1164 B, 288 of 291 equal.  Three rows, all the temporary String built for
//     HANDLE_ACTION(download_preview_file, AddToDownloadQueue(_msg->Str(2))):
//     image 0x68, ours 0x58.  The image puts the OTHER THREE String temps of
//     this dispatch (set_current_preview_file, set_current_preview_movie,
//     is_downloading_file, allow_preview_download) at 0x58 exactly as we do --
//     verified directly in build/373307D9/asm/system/meta/StorePreviewMgr.s at
//     82E1E868, 82E1E8F0, 82E1EA08 and 82E1EA90 -- so this is not a base-offset
//     shift, it is one temp of four that the image declined to coalesce.
//
//   fn_82E1ED0C  99.9%, 40 B -- the EH unwind funclet for exactly that temp
//     (`addi r3, r31, 0x68` / `bl ??1String@@UAA@XZ`).  It moves if and only if
//     the Handle row above moves; it is listed in the state-unwind table
//     lbl_8225AFD0 and carries UNVERIFIABLE_PAIRING (objdiff paired it by masked
//     byte signature, not by name), so it cannot be adjudicated on its own.
//
//   ?PlayCurrentPreview@StorePreviewMgr@@IAAXXZ  99.883%, 412 B, 91 of 103
//     equal.  Every one of the 12 rows is a uniform -0x8: the image's first
//     EH-tracked object (`String str`) starts at 0x58 and ours at 0x50, so
//     `str`, both `FilePath fp` temps and the `int len` spill all sit 8 bytes
//     low.  The image reserves 0x50..0x57 for the MILO_ASSERT(mStreamPlayer,
//     0xd8) scratch -- the line number is passed to MakeString BY REFERENCE
//     (`ABH` in the mangled name), so it needs a real slot -- and then starts
//     objects at 0x58.  We reuse that same slot for `str`, which is legal
//     because the assert temp is dead by then, and MSVC took the reuse.
//     Instructions 0..32 are identical on both sides, including both stores to
//     0x50, so there is no missing local to add: the difference is purely
//     whether the allocator coalesced.
// No source lever found for any of the three; this is MSVC temp-area shaping.
// Measured percentages above are match_percent_normalized from a full ninja.
