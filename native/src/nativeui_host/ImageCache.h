// ImageCache: nativeui::ImageHandle <-> textures loaded in the background.
//
// The framework never loads images: a widget holds an ImageHandle, the null
// handle meaning "not here yet". This cache gives every path one stable handle,
// loads the picture through an ImageLoader without ever waiting for it, and
// says when it has arrived so the screen can hand the widget its handle (which
// nativeui::Image then fades in over its placeholder).
//
//   ImageHandle art = cache.Request(path);            // any time, any number of times
//   image->SetImage(cache.IsReady(art) ? art : ImageHandle {});
//   cache.onArrived = [&](ImageHandle h, const std::string &) { ...SetImage(h)... };
//   // every frame, outside the canvas session (NativeUiHost::Tick does it):
//   cache.Poll();
//
// States of a handle:
//   kUnknown   never requested (or not this cache's handle)
//   kQueued    requested; waiting for a load slot
//   kLoading   the loader is reading it
//   kReady     has a texture
//   kFailed    the loader could not produce one; TextureFor() gives the
//              fallback picture if one was set and has loaded, else null
//   kEvicted   was ready, its texture was dropped to keep the bound; Request()
//              loads it again under the same handle
//
// Rules:
//  - Poll() never blocks: it starts at most `maxConcurrent` loads, asks each
//    whether it has finished, and finishes at most `maxFinishPerPoll` of them
//    (finishing creates the texture, which is the one step with a cost).
//  - Memory is bounded: at most `capacity` textures are kept. When one more
//    arrives, the least recently used is dropped -- "used" meaning requested or
//    drawn (TextureFor). A texture used since the last Poll() is never dropped,
//    so what is on screen stays; if everything is on screen the cache runs
//    over its bound until something scrolls away.
//  - A dropped texture is destroyed at the START of the next Poll(), never
//    inside the frame that may have queued it on the canvas: textures must
//    stay alive until the canvas's End().
//  - The fallback picture (SetFallback) is loaded first, is never evicted and
//    does not count against the capacity.
//  - Not thread-safe: use it from the main thread.
#pragma once

#include "nativeui/DisplayList.h"
#include "nativeui_host/CanvasRenderer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class RndTex;

namespace nativeui_host {

    // How pictures are actually loaded. GameImages.h implements it on the
    // game's loader; tests use a fake.
    class ImageLoader {
    public:
        enum Status {
            kPending,
            kDone,
            kFailed
        };

        virtual ~ImageLoader() = default;
        // Starts loading `path` in the background; returns a job id > 0, or 0
        // if it cannot even start.
        virtual int Begin(const std::string &path) = 0;
        // Has the job's file arrived? Cheap; never blocks.
        virtual Status Poll(int job) = 0;
        // Turns a kDone job into a texture (null on failure) and forgets the job.
        virtual RndTex *Finish(int job) = 0;
        // Abandons a job that has not been finished.
        virtual void Cancel(int job) = 0;
        // Destroys a texture Finish() returned.
        virtual void Destroy(RndTex *texture) = 0;
    };

    class ImageCache final : public ImageSource {
    public:
        enum State {
            kUnknown,
            kQueued,
            kLoading,
            kReady,
            kFailed,
            kEvicted
        };

        struct Config {
            int capacity = 64; // textures kept (the fallback is extra)
            int maxConcurrent = 4; // loads in flight
            int maxFinishPerPoll = 2; // textures created per Poll()
        };

        struct Stats {
            int ready = 0; // textures held now (without the fallback)
            int loading = 0;
            int queued = 0;
            int evictions = 0; // since construction
            int failures = 0;
            int pendingDestroy = 0; // dropped, waiting for the next Poll()
        };

        ImageCache(ImageLoader &loader, Config config);
        ImageCache(ImageLoader &loader) : ImageCache(loader, Config()) {}
        ~ImageCache();
        ImageCache(const ImageCache &) = delete;
        ImageCache &operator=(const ImageCache &) = delete;

        // The handle for `path` (the same one every time; never null for a
        // non-empty path). Queues a load unless the picture is already here or
        // on its way, and counts as a use.
        nativeui::ImageHandle Request(const std::string &path);
        // The picture drawn for handles whose own load failed.
        void SetFallback(const std::string &path);

        State StateOf(nativeui::ImageHandle image) const;
        bool IsReady(nativeui::ImageHandle image) const { return StateOf(image) == kReady; }
        // True once the handle has something to show: its own picture, or the
        // fallback after a failure.
        bool HasPicture(nativeui::ImageHandle image) const;

        // Called from Poll() once per handle when it becomes showable (ready,
        // or failed with a fallback to show).
        std::function<void(nativeui::ImageHandle, const std::string &path)> onArrived;

        // Advances loading; call once a frame, outside the canvas session.
        void Poll();

        // ImageSource: the texture to draw, or null. Counts as a use.
        RndTex *TextureFor(nativeui::ImageHandle image) override;

        Stats GetStats() const;
        // Drops everything (textures are destroyed now): call when no frame
        // that used them is still open.
        void Clear();

    private:
        struct Entry {
            std::string path;
            State state = kUnknown;
            int job = 0;
            RndTex *texture = nullptr;
            std::uint64_t lastUse = 0; // use counter value
            bool usedSincePoll = false;
            bool isFallback = false;
            bool announced = false;
        };

        Entry *Find(nativeui::ImageHandle image);
        const Entry *Find(nativeui::ImageHandle image) const;
        void Touch(Entry &entry);
        void EvictDownTo(int capacity);
        void Announce(std::uint32_t id, Entry &entry);

        ImageLoader &loader_;
        Config config_;
        std::vector<Entry> entries_; // handle id = index + 1
        std::unordered_map<std::string, std::uint32_t> byPath_;
        std::vector<std::uint32_t> queue_; // ids waiting for a slot, oldest first
        std::vector<RndTex *> graveyard_; // destroyed at the next Poll()
        std::uint32_t fallback_ = 0;
        std::uint64_t useCounter_ = 0;
        int evictions_ = 0;
        int failures_ = 0;
    };

}
