// GameImageLoader: nativeui_host::ImageLoader on the game's own loader -- the
// pattern of DynamicTex in src/lazer/meta_ham/TexLoadPanel.cpp: a FileLoader
// added to TheLoadMgr reads the file in the background (the load manager
// advances it inside its per-frame time slice), and once IsLoaded() the bytes
// become an RndTex with RndTex::SetBitmap(FileLoader *).
//
//   Begin   TheLoadMgr.AddLoader(path, kLoadFront)  (no read happens here)
//   Poll    loader->IsLoaded()
//   Finish  RndTex::SetBitmap(loader) -- decodes the bitmap; the one step with
//           a cost, which is why ImageCache finishes only a few per frame
//
// Paths are the game's: a song's cover is "<song dir>/<song>_keep.png" (the
// song catalog gives each song's, SongEntry::albumArtPath: a downloaded
// song's goes through its DLC root's drive, see SongCatalogSource_Native.cpp),
// kFallbackArtPath is the logo shown when a song has none.
#pragma once

#include "nativeui_host/ImageCache.h"

#include <unordered_map>

class FileLoader;

namespace nativeui_host {

    inline constexpr const char *kFallbackArtPath = "ui/image/dc_logo_keep.png";

    class GameImageLoader final : public ImageLoader {
    public:
        GameImageLoader();
        ~GameImageLoader() override;

        int Begin(const std::string &path) override;
        Status Poll(int job) override;
        RndTex *Finish(int job) override;
        void Cancel(int job) override;
        void Destroy(RndTex *texture) override;

    private:
        std::unordered_map<int, FileLoader *> jobs_;
        int nextJob_ = 1;
    };

}
