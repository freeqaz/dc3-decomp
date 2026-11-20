#include "nativeui_host/ImageCache.h"

#include <algorithm>

namespace nativeui_host {

    ImageCache::ImageCache(ImageLoader &loader, Config config)
        : loader_(loader), config_(config) {
        config_.capacity = std::max(1, config_.capacity);
        config_.maxConcurrent = std::max(1, config_.maxConcurrent);
        config_.maxFinishPerPoll = std::max(1, config_.maxFinishPerPoll);
    }

    ImageCache::~ImageCache() { Clear(); }

    ImageCache::Entry *ImageCache::Find(nativeui::ImageHandle image) {
        if (image.id == 0 || image.id > entries_.size()) {
            return nullptr;
        }
        return &entries_[image.id - 1];
    }

    const ImageCache::Entry *ImageCache::Find(nativeui::ImageHandle image) const {
        if (image.id == 0 || image.id > entries_.size()) {
            return nullptr;
        }
        return &entries_[image.id - 1];
    }

    void ImageCache::Touch(Entry &entry) {
        entry.lastUse = ++useCounter_;
        entry.usedSincePoll = true;
    }

    nativeui::ImageHandle ImageCache::Request(const std::string &path) {
        if (path.empty()) {
            return {};
        }
        std::uint32_t id = 0;
        const auto found = byPath_.find(path);
        if (found != byPath_.end()) {
            id = found->second;
        } else {
            Entry entry;
            entry.path = path;
            entries_.push_back(entry);
            id = static_cast<std::uint32_t>(entries_.size());
            byPath_[path] = id;
        }
        Entry &entry = entries_[id - 1];
        Touch(entry);
        if (entry.state == kUnknown || entry.state == kEvicted) {
            entry.state = kQueued;
            entry.announced = false;
            queue_.push_back(id);
        }
        return { id };
    }

    void ImageCache::SetFallback(const std::string &path) {
        const nativeui::ImageHandle handle = Request(path);
        if (Entry *entry = Find(handle)) {
            entry->isFallback = true;
            fallback_ = handle.id;
            // The fallback goes to the head of the queue.
            const auto queued = std::find(queue_.begin(), queue_.end(), handle.id);
            if (queued != queue_.end()) {
                std::rotate(queue_.begin(), queued, queued + 1);
            }
        }
    }

    ImageCache::State ImageCache::StateOf(nativeui::ImageHandle image) const {
        const Entry *entry = Find(image);
        return entry ? entry->state : kUnknown;
    }

    bool ImageCache::HasPicture(nativeui::ImageHandle image) const {
        const Entry *entry = Find(image);
        if (!entry) {
            return false;
        }
        if (entry->state == kReady) {
            return true;
        }
        if (entry->state == kFailed && fallback_ != 0) {
            return entries_[fallback_ - 1].state == kReady;
        }
        return false;
    }

    RndTex *ImageCache::TextureFor(nativeui::ImageHandle image) {
        Entry *entry = Find(image);
        if (!entry) {
            return nullptr;
        }
        Touch(*entry);
        if (entry->state == kReady) {
            return entry->texture;
        }
        if (entry->state == kFailed && fallback_ != 0) {
            Entry &fallback = entries_[fallback_ - 1];
            if (fallback.state == kReady) {
                Touch(fallback);
                return fallback.texture;
            }
        }
        return nullptr;
    }

    void ImageCache::Announce(std::uint32_t id, Entry &entry) {
        if (entry.announced) {
            return;
        }
        entry.announced = true;
        if (onArrived) {
            onArrived({ id }, entry.path);
        }
    }

    void ImageCache::Poll() {
        // 1. Textures dropped during the last frame: that frame's canvas has
        //    ended, so they can go now.
        for (RndTex *texture : graveyard_) {
            loader_.Destroy(texture);
        }
        graveyard_.clear();

        // 2. Finish what has arrived (bounded), note what failed.
        int finished = 0;
        int loading = 0;
        for (std::size_t i = 0; i < entries_.size(); i++) {
            Entry &entry = entries_[i];
            if (entry.state != kLoading) {
                continue;
            }
            const ImageLoader::Status status = loader_.Poll(entry.job);
            if (status == ImageLoader::kPending) {
                loading++;
                continue;
            }
            if (status == ImageLoader::kDone) {
                if (finished >= config_.maxFinishPerPoll) {
                    loading++; // stays in its slot until a later Poll()
                    continue;
                }
                finished++;
                entry.texture = loader_.Finish(entry.job);
            } else {
                loader_.Cancel(entry.job);
                entry.texture = nullptr;
            }
            entry.job = 0;
            const std::uint32_t id = static_cast<std::uint32_t>(i + 1);
            if (entry.texture) {
                entry.state = kReady;
                entry.lastUse = ++useCounter_;
                entry.usedSincePoll = true; // not dropped by the pass that brought it
                Announce(id, entry);
                if (entry.isFallback) {
                    // Failures that were waiting for the fallback can show now.
                    for (std::size_t k = 0; k < entries_.size(); k++) {
                        if (entries_[k].state == kFailed) {
                            Announce(static_cast<std::uint32_t>(k + 1), entries_[k]);
                        }
                    }
                }
            } else {
                entry.state = kFailed;
                failures_++;
                if (fallback_ != 0 && entries_[fallback_ - 1].state == kReady) {
                    Announce(id, entry);
                }
            }
        }

        // 3. Keep the bound.
        EvictDownTo(config_.capacity);

        // 4. Start queued loads into the free slots.
        std::size_t next = 0;
        while (next < queue_.size() && loading < config_.maxConcurrent) {
            const std::uint32_t id = queue_[next++];
            Entry &entry = entries_[id - 1];
            if (entry.state != kQueued) {
                continue;
            }
            entry.job = loader_.Begin(entry.path);
            if (entry.job > 0) {
                entry.state = kLoading;
                loading++;
            } else {
                entry.state = kFailed;
                failures_++;
                if (fallback_ != 0 && entries_[fallback_ - 1].state == kReady) {
                    Announce(id, entry);
                }
            }
        }
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(next));

        // 5. A new "since the last Poll" window opens.
        for (Entry &entry : entries_) {
            entry.usedSincePoll = false;
        }
    }

    void ImageCache::EvictDownTo(int capacity) {
        for (;;) {
            int ready = 0;
            Entry *oldest = nullptr;
            for (Entry &entry : entries_) {
                if (entry.state != kReady || entry.isFallback) {
                    continue;
                }
                ready++;
                if (entry.usedSincePoll) {
                    continue; // on screen (or just arrived): never dropped
                }
                if (!oldest || entry.lastUse < oldest->lastUse) {
                    oldest = &entry;
                }
            }
            if (ready <= capacity || !oldest) {
                return;
            }
            graveyard_.push_back(oldest->texture);
            oldest->texture = nullptr;
            oldest->state = kEvicted;
            oldest->announced = false;
            evictions_++;
        }
    }

    ImageCache::Stats ImageCache::GetStats() const {
        Stats stats;
        for (const Entry &entry : entries_) {
            if (entry.state == kReady && !entry.isFallback) {
                stats.ready++;
            } else if (entry.state == kLoading) {
                stats.loading++;
            } else if (entry.state == kQueued) {
                stats.queued++;
            }
        }
        stats.evictions = evictions_;
        stats.failures = failures_;
        stats.pendingDestroy = static_cast<int>(graveyard_.size());
        return stats;
    }

    void ImageCache::Clear() {
        for (RndTex *texture : graveyard_) {
            loader_.Destroy(texture);
        }
        graveyard_.clear();
        for (Entry &entry : entries_) {
            if (entry.state == kLoading && entry.job > 0) {
                loader_.Cancel(entry.job);
            }
            if (entry.texture) {
                loader_.Destroy(entry.texture);
            }
        }
        entries_.clear();
        byPath_.clear();
        queue_.clear();
        fallback_ = 0;
    }

}
