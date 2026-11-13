#include "platform/NativeNotice.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

namespace NativeNotice {
namespace {

struct Entry {
    std::string key;
    std::string text;
    std::string shortText;
    float bannerSeconds;
    // The banner's clock starts on the first Current() -- the first frame that
    // can draw it -- not at Post(): a camera failure is posted during boot,
    // seconds before anything is on screen.
    bool bannerStarted;
    std::chrono::steady_clock::time_point bannerUntil;
};

std::mutex sMutex;
std::vector<Entry> sEntries; // oldest first

} // namespace

void Post(const char *key, const std::string &text, const std::string &shortText,
          float bannerSeconds) {
    fprintf(stderr, "DC3 NOTICE [%s]: %s\n", key, text.c_str());
    std::lock_guard<std::mutex> lock(sMutex);
    for (size_t i = 0; i < sEntries.size(); i++) {
        if (sEntries[i].key == key) {
            sEntries.erase(sEntries.begin() + i);
            break;
        }
    }
    Entry e;
    e.key = key;
    e.text = text;
    e.shortText = shortText;
    e.bannerSeconds = bannerSeconds;
    e.bannerStarted = false;
    sEntries.push_back(e);
}

void Clear(const char *key) {
    std::lock_guard<std::mutex> lock(sMutex);
    for (size_t i = 0; i < sEntries.size(); i++) {
        if (sEntries[i].key == key) {
            sEntries.erase(sEntries.begin() + i);
            return;
        }
    }
}

bool Current(View &out) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sEntries.empty())
        return false;
    Entry &e = sEntries.back();
    if (!e.bannerStarted) {
        e.bannerStarted = true;
        e.bannerUntil = std::chrono::steady_clock::now()
            + std::chrono::milliseconds((long long)(e.bannerSeconds * 1000.0f));
    }
    float left = std::chrono::duration<float>(
        e.bannerUntil - std::chrono::steady_clock::now()).count();
    out.text = e.text;
    out.shortText = e.shortText;
    out.banner = left > 0.0f;
    out.bannerAlpha = left >= 1.0f ? 1.0f : (left > 0.0f ? left : 0.0f);
    out.corner = !out.banner && !e.shortText.empty();
    return out.banner || out.corner;
}

std::string Text(const char *key) {
    std::lock_guard<std::mutex> lock(sMutex);
    for (const Entry &e : sEntries) {
        if (e.key == key)
            return e.text;
    }
    return std::string();
}

} // namespace NativeNotice
