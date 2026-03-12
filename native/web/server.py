#!/usr/bin/env python3
"""DC3 Web Port — Development Server

Serves WASM build artifacts + asset streaming API on localhost:8420.
Sends required COOP/COEP headers for SharedArrayBuffer (future threading).
"""

import http.server
import os
import sys

PORT = 8420
BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")


class DC3Handler(http.server.SimpleHTTPRequestHandler):
    """Serves static files from build/ with correct MIME types and security headers."""

    def __init__(self, *args, **kwargs):
        # Serve from build dir (contains both cmake output and copied web assets)
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
        return super().guess_type(path)

    def do_GET(self):
        # API routes (Phase 3 — asset streaming)
        if self.path.startswith("/api/"):
            self.send_response(501)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"error": "Asset API not yet implemented"}')
            return

        # Serve index.html for root
        if self.path == "/":
            self.path = "/index.html"

        super().do_GET()


def main():
    if not os.path.isdir(BUILD_DIR):
        print(f"Build directory not found: {BUILD_DIR}")
        print("Run native/web/build.sh first.")
        sys.exit(1)

    server = http.server.HTTPServer(("0.0.0.0", PORT), DC3Handler)
    print(f"DC3 Web Dev Server")
    print(f"  Serving: {BUILD_DIR}")
    print(f"  URL:     http://localhost:{PORT}")
    print(f"  COOP/COEP headers enabled")
    print()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
