#!/usr/bin/env python3
"""DC3 Web Port — Development Server with Asset Streaming API

Serves WASM build artifacts + streams game assets via HTTP API on localhost:8420.
Sends required COOP/COEP headers for SharedArrayBuffer (future threading).

API endpoints:
  GET /api/manifest         — JSON list of all available assets
  GET /api/file/<path>      — raw bytes of an extracted asset file
  GET /                     — index.html (build artifacts)
  GET /dc3-web.{js,wasm}   — WASM build output
"""

import argparse
import http.server
import json
import os
import sys
import urllib.parse

PORT = 8420
BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
ASSETS_DIR = None  # Set via --assets-dir, DC3_ASSETS env, or auto-detect


class DC3Handler(http.server.SimpleHTTPRequestHandler):
    """Serves static files from build/ with correct MIME types, security headers,
    and an asset streaming API backed by a pre-extracted game data directory."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BUILD_DIR, **kwargs)

    def end_headers(self):
        # Required for SharedArrayBuffer (future pthreads support)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Cache busting during development
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        super().end_headers()

    def guess_type(self, path):
        """Ensure correct MIME types for WASM and JS."""
        if path.endswith(".wasm"):
            return "application/wasm"
        if path.endswith(".js"):
            return "application/javascript"
        if path.endswith(".dta"):
            return "text/plain"
        if path.endswith(".webm"):
            return "video/webm"
        return super().guess_type(path)

    def do_GET(self):
        if self.path.startswith("/api/"):
            self._handle_api()
            return
        if self.path == "/":
            self.path = "/index.html"
        super().do_GET()

    def _handle_api(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/health":
            self._serve_health()
        elif path == "/api/manifest":
            self._serve_manifest()
        elif path == "/api/bundle":
            self._serve_bundle()
        elif path.startswith("/api/file/"):
            rel = path[len("/api/file/"):]
            self._serve_asset_file(rel)
        else:
            self._json_error(404, "Unknown API endpoint")

    def _serve_health(self):
        body = json.dumps({"status": "ok"}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_manifest(self):
        if not ASSETS_DIR:
            self._json_error(503, "No assets directory configured")
            return

        files = []
        for root, _dirs, filenames in os.walk(ASSETS_DIR):
            for f in filenames:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, ASSETS_DIR)
                files.append({"path": rel, "size": os.path.getsize(full)})

        files.sort(key=lambda x: x["path"])
        body = json.dumps({"files": files, "count": len(files)}, indent=1).encode()

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_bundle(self):
        """Serve all assets as a single binary bundle for efficient bulk loading.
        Format: uint32 count, then for each file:
          uint32 path_len, path (UTF-8), uint32 data_len, data (bytes)
        All integers are little-endian."""
        import struct

        if not ASSETS_DIR:
            self._json_error(503, "No assets directory configured")
            return

        # Collect config files only (DTA/DTB) for the boot bundle.
        # Binary assets (.milo_xbox etc.) are fetched on demand by the engine.
        # Ark extraction stores ".." as "(..)" in directory names — restore them
        BUNDLE_EXTS = {".dta", ".dtb"}
        entries = []
        for root, _dirs, filenames in os.walk(ASSETS_DIR):
            for f in filenames:
                _name, ext = os.path.splitext(f)
                if ext.lower() not in BUNDLE_EXTS:
                    continue
                full = os.path.join(root, f)
                rel = os.path.relpath(full, ASSETS_DIR)
                rel = rel.replace("(..)", "..")
                with open(full, "rb") as fh:
                    data = fh.read()
                entries.append((rel, data))
        entries.sort(key=lambda x: x[0])

        # Build bundle
        chunks = [struct.pack("<I", len(entries))]
        for path, data in entries:
            path_bytes = path.encode("utf-8")
            chunks.append(struct.pack("<I", len(path_bytes)))
            chunks.append(path_bytes)
            chunks.append(struct.pack("<I", len(data)))
            chunks.append(data)

        body = b"".join(chunks)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_asset_file(self, relpath):
        if not ASSETS_DIR:
            self._json_error(503, "No assets directory configured")
            return

        relpath = urllib.parse.unquote(relpath)
        safe = os.path.normpath(relpath)
        if safe.startswith("..") or os.path.isabs(safe):
            self._json_error(403, "Path traversal denied")
            return

        full_path = os.path.join(ASSETS_DIR, safe)
        if not os.path.isfile(full_path):
            self._json_error(404, f"Not found: {relpath}")
            return

        size = os.path.getsize(full_path)

        # Handle Range requests (partial content)
        range_hdr = self.headers.get("Range")
        if range_hdr:
            self._serve_range(full_path, size, range_hdr)
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        with open(full_path, "rb") as f:
            # Stream in 64KB chunks to avoid loading huge files into memory
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                self.wfile.write(chunk)

    def _serve_range(self, full_path, total_size, range_hdr):
        try:
            ranges = range_hdr.replace("bytes=", "")
            start_str, end_str = ranges.split("-")
            start = int(start_str) if start_str else 0
            end = int(end_str) if end_str else total_size - 1
        except (ValueError, IndexError):
            self._json_error(416, "Invalid range")
            return

        if start >= total_size:
            self._json_error(416, "Range not satisfiable")
            return

        end = min(end, total_size - 1)
        length = end - start + 1

        self.send_response(206)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(length))
        self.send_header("Content-Range", f"bytes {start}-{end}/{total_size}")
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        with open(full_path, "rb") as f:
            f.seek(start)
            remaining = length
            while remaining > 0:
                chunk = f.read(min(65536, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def _json_error(self, code, msg):
        body = json.dumps({"error": msg}).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        # Quieter logging: skip 200s for static assets
        if len(args) >= 2 and str(args[1]) == "200" and not str(args[0]).startswith("GET /api"):
            return
        super().log_message(format, *args)


def _find_assets_dir():
    """Auto-detect extracted assets directory."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(script_dir, "../../orig-assets/extracted"),
        os.path.join(script_dir, "../../../orig-assets/extracted"),
    ]
    env = os.environ.get("DC3_ASSETS")
    if env:
        candidates.insert(0, env)
    for c in candidates:
        if os.path.isdir(c):
            return os.path.realpath(c)
    return None


def main():
    global ASSETS_DIR

    parser = argparse.ArgumentParser(description="DC3 Web Dev Server")
    parser.add_argument(
        "--assets-dir",
        default=None,
        help="Path to extracted game assets (default: DC3_ASSETS env or auto-detect)",
    )
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    ASSETS_DIR = args.assets_dir or _find_assets_dir()

    if not os.path.isdir(BUILD_DIR):
        print(f"Build directory not found: {BUILD_DIR}")
        print("Run native/web/build.sh first.")
        sys.exit(1)

    print("DC3 Web Dev Server")
    print(f"  Build:   {BUILD_DIR}")
    if ASSETS_DIR:
        print(f"  Assets:  {ASSETS_DIR}")
    else:
        print("  Assets:  NOT CONFIGURED (set --assets-dir or DC3_ASSETS)")
    print(f"  URL:     http://0.0.0.0:{args.port} (accessible remotely)")
    print(f"  API:     http://0.0.0.0:{args.port}/api/manifest")
    print(f"  COOP/COEP headers enabled")
    print()

    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), DC3Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
