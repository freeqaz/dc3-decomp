#include "nativeui_host/GameImages.h"

#include "meta_ham/HamSongMgr.h"
#include "rndobj/Tex.h"
#include "utl/FilePath.h"
#include "utl/Loader.h"

namespace nativeui_host {

    GameImageLoader::GameImageLoader() {}

    GameImageLoader::~GameImageLoader() {
        for (auto &job : jobs_) {
            delete job.second;
        }
    }

    int GameImageLoader::Begin(const std::string &path) {
        if (path.empty()) {
            return 0;
        }
        FileLoader *loader =
            dynamic_cast<FileLoader *>(TheLoadMgr.AddLoader(FilePath(path.c_str()), kLoadFront));
        if (!loader) {
            return 0;
        }
        const int job = nextJob_++;
        jobs_[job] = loader;
        return job;
    }

    ImageLoader::Status GameImageLoader::Poll(int job) {
        const auto found = jobs_.find(job);
        if (found == jobs_.end()) {
            return kFailed;
        }
        return found->second->IsLoaded() ? kDone : kPending;
    }

    RndTex *GameImageLoader::Finish(int job) {
        const auto found = jobs_.find(job);
        if (found == jobs_.end()) {
            return nullptr;
        }
        FileLoader *loader = found->second;
        jobs_.erase(found);
        if (!loader->IsLoaded() || loader->GetSize() <= 0) {
            delete loader; // a missing or empty file: no picture
            return nullptr;
        }
        RndTex *tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(loader); // takes the loader
        if (tex->Width() <= 0 || tex->Height() <= 0) {
            delete tex;
            return nullptr;
        }
        return tex;
    }

    void GameImageLoader::Cancel(int job) {
        const auto found = jobs_.find(job);
        if (found != jobs_.end()) {
            delete found->second;
            jobs_.erase(found);
        }
    }

    void GameImageLoader::Destroy(RndTex *texture) { delete texture; }

    std::string AlbumArtPath(const char *songShortName) {
        if (!songShortName || !*songShortName) {
            return std::string();
        }
        const char *path = TheHamSongMgr.GetAlbumArtPath(Symbol(songShortName));
        return path ? std::string(path) : std::string();
    }

}
