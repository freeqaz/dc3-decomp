// DC3 Web Port — Asset Fetcher Implementation
// Uses emscripten_fetch() to download files from the dev server's HTTP API
// into Emscripten's in-memory filesystem (MEMFS).
// All fetches are async — poll WebAssetsAllDone() from the main loop.
//
// DC3_WEB_ASYNCIFY experiment (opt-in via CMakeLists.txt):
//   When enabled, WebAssetsFetchSync() yields to the browser event loop
//   via emscripten_sleep() instead of blocking with synchronous XHR.
//   This lets the loading screen render while files download.
//   To revert: remove -sASYNCIFY and DC3_WEB_ASYNCIFY from CMakeLists.txt.

#ifdef __EMSCRIPTEN__

#include "platform/WebAssets.h"

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/em_asm.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

struct FetchRequest {
    int id;
    std::string serverPath;  // e.g. "config/ham_keep.dta"
    std::string memfsPath;   // e.g. "/data/config/ham_keep.dta"
    bool done;
    bool success;
};

static int sNextFetchId = 1;
static int sPending = 0;
static int sCompleted = 0;
static int sFailed = 0;
static std::vector<FetchRequest *> sFetchRequests;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Recursively create directory path in MEMFS
static void mkdirRecursive(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);  // Ignore EEXIST
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

// ---------------------------------------------------------------------------
// Fetch callbacks
// ---------------------------------------------------------------------------

static void onFetchSuccess(emscripten_fetch_t *fetch) {
    FetchRequest *req = static_cast<FetchRequest *>(fetch->userData);

    // Create parent directories
    std::string dir = req->memfsPath;
    size_t slash = dir.rfind('/');
    if (slash != std::string::npos) {
        dir.resize(slash);
        mkdirRecursive(dir.c_str());
    }

    // Write fetched data to MEMFS
    FILE *f = fopen(req->memfsPath.c_str(), "wb");
    if (f) {
        fwrite(fetch->data, 1, fetch->numBytes, f);
        fclose(f);
        printf("WebAssets: %s (%llu bytes)\n",
               req->serverPath.c_str(), (unsigned long long)fetch->numBytes);
        req->success = true;
        sCompleted++;
    } else {
        printf("WebAssets: MEMFS write failed %s (errno %d)\n",
               req->memfsPath.c_str(), errno);
        req->success = false;
        sFailed++;
    }

    req->done = true;
    sPending--;
    emscripten_fetch_close(fetch);
}

static void onFetchError(emscripten_fetch_t *fetch) {
    FetchRequest *req = static_cast<FetchRequest *>(fetch->userData);
    printf("WebAssets: FAILED %s (HTTP %d)\n", fetch->url, fetch->status);
    req->done = true;
    req->success = false;
    sPending--;
    sFailed++;
    emscripten_fetch_close(fetch);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WebAssetsInit() {
    mkdir("/data", 0755);
    mkdir("/data/config", 0755);
    mkdir("/data/ui", 0755);
    mkdir("/data/world", 0755);
    mkdir("/data/char", 0755);
    mkdir("/data/songs", 0755);
    mkdir("/data/gen", 0755);
    mkdir("/data/videos", 0755);
    printf("WebAssets: MEMFS initialized\n");
}

int WebAssetsFetch(const char *serverPath) {
    FetchRequest *req = new FetchRequest();
    req->id = sNextFetchId++;
    req->serverPath = serverPath;
    req->memfsPath = std::string("/data/") + serverPath;
    req->done = false;
    req->success = false;
    sFetchRequests.push_back(req);

    // Build server URL
    char url[512];
    snprintf(url, sizeof(url), "/api/file/%s", serverPath);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onFetchSuccess;
    attr.onerror = onFetchError;
    attr.userData = req;

    emscripten_fetch(&attr, url);
    sPending++;

    return req->id;
}

bool WebAssetsFetchDone(int fetchId) {
    for (const auto *req : sFetchRequests) {
        if (req->id == fetchId) return req->done;
    }
    return true;  // Unknown ID treated as done
}

// ---------------------------------------------------------------------------
// Bundle download — single HTTP request for ALL assets
// ---------------------------------------------------------------------------

static void onBundleSuccess(emscripten_fetch_t *fetch) {
    printf("WebAssets: bundle received (%llu bytes), unpacking...\n",
           (unsigned long long)fetch->numBytes);

    const uint8_t *ptr = (const uint8_t *)fetch->data;
    const uint8_t *end = ptr + fetch->numBytes;

    if (end - ptr < 4) {
        printf("WebAssets: bundle too small\n");
        sPending--;
        sFailed++;
        emscripten_fetch_close(fetch);
        return;
    }

    // Read file count (little-endian uint32)
    uint32_t count = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
    ptr += 4;

    int unpacked = 0;
    for (uint32_t i = 0; i < count && ptr + 4 <= end; i++) {
        // Read path
        uint32_t pathLen = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        ptr += 4;
        if (ptr + pathLen > end) break;
        std::string relPath((const char *)ptr, pathLen);
        ptr += pathLen;

        // Read data
        if (ptr + 4 > end) break;
        uint32_t dataLen = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
        ptr += 4;
        if (ptr + dataLen > end) break;
        const uint8_t *data = ptr;
        ptr += dataLen;

        // Write to MEMFS — resolve ".." in path
        std::string memfsPath = std::string("/data/") + relPath;

        // Resolve ".." components to get a clean absolute path
        // e.g. "/data/../../system/run/config/macros.dta" → "/system/run/config/macros.dta"
        {
            std::vector<std::string> parts;
            size_t pos = 0;
            while (pos < memfsPath.size()) {
                size_t next = memfsPath.find('/', pos + 1);
                if (next == std::string::npos) next = memfsPath.size();
                std::string part = memfsPath.substr(pos, next - pos);
                if (part == "/..") {
                    if (!parts.empty()) parts.pop_back();
                } else if (part != "/.") {
                    parts.push_back(part);
                }
                pos = next;
            }
            memfsPath.clear();
            for (const auto &p : parts) memfsPath += p;
            if (memfsPath.empty()) memfsPath = "/";
        }

        // Create parent directories
        std::string dir = memfsPath;
        size_t slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir.resize(slash);
            mkdirRecursive(dir.c_str());
        }

        FILE *f = fopen(memfsPath.c_str(), "wb");
        if (f) {
            fwrite(data, 1, dataLen, f);
            fclose(f);
            unpacked++;
        }
    }

    printf("WebAssets: unpacked %d/%u files into MEMFS\n", unpacked, count);
    sCompleted += unpacked;
    sPending--;
    emscripten_fetch_close(fetch);
}

static void onBundleError(emscripten_fetch_t *fetch) {
    printf("WebAssets: bundle download FAILED (HTTP %d)\n", fetch->status);
    sPending--;
    sFailed++;
    emscripten_fetch_close(fetch);
}

void WebAssetsFetchBundle() {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onBundleSuccess;
    attr.onerror = onBundleError;

    emscripten_fetch(&attr, "/api/bundle");
    sPending++;
}

// ---------------------------------------------------------------------------
// Fetch by MEMFS path (async) — like WebAssetsFetch but takes a MEMFS path
// ---------------------------------------------------------------------------

// Normalize a MEMFS path to a server-relative path.
// Paths come in several forms:
//   /data/ui/gen/foo.milo_xbox          -> ui/gen/foo.milo_xbox
//   /system/run/ham/gen/skeleton.milo   -> system/run/ham/gen/skeleton.milo
//   /../system/run/config/gen/meta.milo -> system/run/config/gen/meta.milo
static const char *normalizeMemfsPath(const char *memfsPath) {
    const char *rel = memfsPath;
    if (strncmp(rel, "/data/", 6) == 0) {
        rel += 6;
    } else if (strncmp(rel, "/../", 4) == 0) {
        rel += 4;
    } else if (rel[0] == '/') {
        rel += 1;
    }
    return rel;
}

int WebAssetsFetchByPath(const char *memfsPath) {
    const char *rel = normalizeMemfsPath(memfsPath);

    FetchRequest *req = new FetchRequest();
    req->id = sNextFetchId++;
    req->serverPath = rel;
    req->memfsPath = memfsPath;  // use the original MEMFS path
    req->done = false;
    req->success = false;
    sFetchRequests.push_back(req);

    char url[512];
    snprintf(url, sizeof(url), "/api/file/%s", rel);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onFetchSuccess;
    attr.onerror = onFetchError;
    attr.userData = req;

    emscripten_fetch(&attr, url);
    sPending++;

    return req->id;
}

bool WebAssetsFetchSucceeded(int fetchId) {
    for (const auto *req : sFetchRequests) {
        if (req->id == fetchId) return req->success;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Synchronous single-file fetch
// ---------------------------------------------------------------------------
//
// DC3_WEB_ASYNCIFY path: uses async emscripten_fetch + emscripten_sleep()
// to yield to the browser event loop. Loading screen can render during fetch.
//
// Fallback path: uses blocking synchronous XHR. Simple but freezes the UI.

#ifdef DC3_WEB_ASYNCIFY

bool WebAssetsFetchSync(const char *memfsPath) {
    const char *rel = normalizeMemfsPath(memfsPath);
    printf("WebAssets: async-yield fetch %s\n", rel);

    int fetchId = WebAssetsFetchByPath(memfsPath);

    // Yield to the browser event loop while waiting for the fetch to complete.
    // emscripten_sleep() requires -sASYNCIFY in the link flags.
    // During sleep, the browser can process fetch callbacks and render frames.
    while (!WebAssetsFetchDone(fetchId)) {
        emscripten_sleep(16);  // ~60fps yield
    }

    bool ok = WebAssetsFetchSucceeded(fetchId);
    if (ok) {
        printf("WebAssets: fetched %s (%s)\n", rel, memfsPath);
    } else {
        printf("WebAssets: FAILED %s\n", rel);
    }
    return ok;
}

#else // !DC3_WEB_ASYNCIFY — original blocking XHR path

bool WebAssetsFetchSync(const char *memfsPath) {
    const char *rel = normalizeMemfsPath(memfsPath);

    char url[512];
    snprintf(url, sizeof(url), "/api/file/%s", rel);

    printf("WebAssets: on-demand fetch %s -> %s\n", url, memfsPath);

    // Synchronous XHR — blocks main thread until complete.
    // Note: synchronous XHR cannot set responseType="arraybuffer" in browsers,
    // so we use overrideMimeType to force binary and manually convert the response.
    int result = EM_ASM_INT({
        try {
            var url = UTF8ToString($0);
            var memfsPath = UTF8ToString($1);
            var xhr = new XMLHttpRequest();
            xhr.open("GET", url, false);  // synchronous
            xhr.overrideMimeType("text/plain; charset=x-user-defined");
            xhr.send();
            if (xhr.status !== 200) {
                console.log("WebAssets: XHR failed " + url + " status=" + xhr.status);
                return 0;
            }

            var text = xhr.responseText;
            var data = new Uint8Array(text.length);
            for (var i = 0; i < text.length; i++) {
                data[i] = text.charCodeAt(i) & 0xFF;
            }

            var parts = memfsPath.split("/");
            var dir = "";
            for (var i = 0; i < parts.length - 1; i++) {
                if (parts[i] === "") continue;
                dir += "/" + parts[i];
                try { FS.mkdir(dir); } catch(e) {}
            }

            FS.writeFile(memfsPath, data);
            return 1;
        } catch(e) {
            console.log("WebAssets: XHR exception: " + e);
            return 0;
        }
    }, url, memfsPath);

    if (result) {
        printf("WebAssets: fetched on-demand %s (%s)\n", rel, memfsPath);
    } else {
        printf("WebAssets: FAILED on-demand fetch %s\n", rel);
    }
    return result != 0;
}

#endif // DC3_WEB_ASYNCIFY

bool WebAssetsAllDone() { return sPending == 0; }
int WebAssetsPendingCount() { return sPending; }
int WebAssetsCompletedCount() { return sCompleted; }
int WebAssetsFailedCount() { return sFailed; }

#endif // __EMSCRIPTEN__
