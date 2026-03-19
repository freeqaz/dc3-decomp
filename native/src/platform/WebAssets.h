// DC3 Web Port — Asset Fetcher
// Downloads game assets from the dev server's HTTP API into Emscripten MEMFS.
// Assets are stored under /data/ in MEMFS, mirroring the server's directory structure.

#pragma once

#ifdef __EMSCRIPTEN__

// Create the MEMFS directory skeleton (/data/config/, /data/ui/, etc.)
void WebAssetsInit();

// Queue an async fetch of a file from the server.
// serverPath is relative (e.g. "config/ham_keep.dta").
// The file will be written to /data/<serverPath> in MEMFS when complete.
// Returns a fetch ID for tracking.
int WebAssetsFetch(const char *serverPath);

// Check if a specific fetch is complete (success or failure).
bool WebAssetsFetchDone(int fetchId);

// Check if ALL pending fetches have completed.
bool WebAssetsAllDone();

// Download ALL assets as a single bundle from /api/bundle.
// Much faster than individual fetches for bulk loading.
// Unpacks into /data/ in MEMFS.
void WebAssetsFetchBundle();

// Fetch a single file from the server into MEMFS by its MEMFS path.
// memfsPath is the full MEMFS path (e.g. "/data/ui/gen/helpbar.milo_xbox").
// Returns a fetch ID for tracking (like WebAssetsFetch, but takes MEMFS path).
int WebAssetsFetchByPath(const char *memfsPath);

// Synchronously fetch a single file from the server into MEMFS.
// memfsPath is the full MEMFS path (e.g. "/data/ui/gen/helpbar.milo_xbox").
// Returns true if the file was fetched and written successfully.
//
// DC3_WEB_ASYNCIFY experiment: when ASYNCIFY is enabled, this uses
// emscripten_sleep() to yield to the browser event loop instead of
// blocking with synchronous XHR. This allows the loading screen to
// render while files download. Remove -sDC3_WEB_ASYNCIFY=1 from
// CMakeLists.txt to revert to blocking XHR behavior.
bool WebAssetsFetchSync(const char *memfsPath);

// Check if a specific fetch succeeded (only valid after WebAssetsFetchDone returns true).
bool WebAssetsFetchSucceeded(int fetchId);

// Counters
int WebAssetsPendingCount();
int WebAssetsCompletedCount();
int WebAssetsFailedCount();

#endif // __EMSCRIPTEN__
