#!/usr/bin/env python3
"""DC3 Web Port — Development Server with Asset Streaming API

Serves WASM build artifacts + streams game assets via HTTP API on localhost:8420.
Sends required COOP/COEP headers for SharedArrayBuffer (future threading).

API endpoints:
  GET /api/manifest              — JSON list of all available assets
  GET /api/file/<path>           — bytes of an extracted asset file (br/gzip
                                   Content-Encoding negotiated for compressible
                                   assets — see the encode cache below)
  GET /api/bundle                — boot config bundle (all DTA/DTB), compressed
  GET /api/bundle/screen/<name>  — per-screen dependency bundle from
                                   native/web/screen-<name>.manifest (missing
                                   manifest → harmless empty bundle)
  GET /                          — index.html (build artifacts)
  GET /dc3-web.{js,wasm}         — WASM build output
"""

import argparse
import hashlib
import http.server
import json
import os
import random
import re
import struct
import subprocess
import sys
import urllib.parse

PORT = 8420
BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build")
# Per-screen bundle manifests live next to this script as
# screen-<name>.manifest. The <name> in /api/bundle/screen/<name> is validated
# against this regex so a request can only ever resolve to exactly
# native/web/screen-<name>.manifest (no path traversal).
SCREEN_MANIFEST_DIR = os.path.dirname(os.path.abspath(__file__))
_SCREEN_NAME_RE = re.compile(r"^[a-z0-9_]+$")
ASSETS_DIR = None  # Set via --assets-dir, DC3_ASSETS env, or auto-detect

# On-demand wire compression for /api/file (ported from rb3's R5/W5 work —
# rb3/docs/native/web-perf-roadmap/R5-wire-compression.md). The big .milo_xbox
# assets are fetched via a *synchronous* XHR that freezes the wasm main thread
# for the whole transfer, so fewer wire bytes = proportionally less freeze.
# Compressible assets are compressed once with the brotli CLI (gzip fallback),
# cached to disk, and served via standard Content-Encoding negotiation — the
# browser decompresses before the engine's XHR sees the bytes, so this is fully
# transparent to the C++ engine (server.py-only change).
#
# Auto-created at native/web/.cache/encoded/ (env DC3_ENCODE_CACHE,
# --encode-cache flag). --no-encode disables (raw path, for A/B).
ENCODE_CACHE_DIR = None
ENCODE_ENABLED = True
# Compressible asset extensions (scene-graph / DTA / object data). Deny wins
# over allow. Already-compressed payloads (.mogg/.ogg/.webm) gain ~0 and are
# explicitly denied.
COMPRESSIBLE_EXTS = {
    ".milo_xbox", ".milo", ".milo_ps3", ".milo_wii",
    ".dta", ".dtb", ".dtb_ps3",
    ".pcm", ".png_xbox", ".bmp_xbox", ".mid", ".txt",
}
INCOMPRESSIBLE_EXTS = {
    # Lossy/already-compressed media (measured br ratio ~100%): never compress.
    ".mogg", ".ogg", ".webm", ".mp4", ".jpg", ".jpeg", ".png", ".gz", ".br",
}
# brotli q5 keeps ~90% of q11's win at ~1/100th the CPU, and the cost is paid
# inline on the first (freezing) request — so q5 is the right on-demand
# default. gzip -6 is the fallback when brotli is absent.
ENCODE_LEVEL_BROTLI = 5
ENCODE_LEVEL_GZIP = 6
# Resolved encoder binaries (probed once in main()); None if absent.
BROTLI_BIN = None
GZIP_BIN = None


# ---------------------------------------------------------------------------
# Module-level encode primitives + bundle body builders (one source of truth
# for the running server; mirrors rb3's server.py so a future offline pre-warm
# can import them without fingerprint drift).
# ---------------------------------------------------------------------------


def encode_meta_text(src_size, src_mtime, enc=None, level=None):
    """Render a cache .meta line — "<size>:<mtime>[:<enc><level>]" — keyed on
    the source identity so a changed source invalidates the artifact."""
    base = f"{src_size}:{int(src_mtime)}"
    if enc and level is not None:
        return f"{base}:{enc}{level}"
    return base


def parse_meta_text(text):
    """Parse a .meta line → (src_size, src_mtime, enc|None, level|None), or
    None on a malformed line. A 2-field entry returns (size, mtime, None, None)."""
    parts = (text or "").strip().split(":")
    if len(parts) < 2:
        return None
    try:
        size = int(parts[0])
        mtime = int(parts[1])
    except ValueError:
        return None
    enc, level = None, None
    if len(parts) >= 3 and parts[2]:
        tail = parts[2]
        i = 0
        while i < len(tail) and tail[i].isalpha():
            i += 1
        enc = tail[:i] or None
        try:
            level = int(tail[i:]) if tail[i:] else None
        except ValueError:
            level = None
    return size, mtime, enc, level


def meta_is_valid_for(meta_text, src_size, src_mtime):
    """True if a .meta line describes the current source (size+mtime match)."""
    parsed = parse_meta_text(meta_text)
    if parsed is None:
        return False
    size, mtime, _enc, _level = parsed
    return size == src_size and mtime == int(src_mtime)


def _unlink_quiet(path):
    try:
        os.unlink(path)
    except OSError:
        pass


def _atomic_write(path, data):
    """Write `data` (bytes) to `path` atomically (unique temp + rename on the
    same filesystem). Returns True on success."""
    tmp = path + f".{os.getpid()}.{random.randrange(1 << 32):08x}.tmp"
    try:
        with open(tmp, "wb") as out:
            out.write(data)
        os.rename(tmp, path)
        return True
    except OSError:
        _unlink_quiet(tmp)
        return False


def encode_bytes(data, enc, level):
    """Compress `data` (bytes) via the brotli/gzip CLI at `level`. Returns the
    compressed bytes, or None on failure. enc ∈ {'br','gzip'}."""
    if enc == "br":
        if not BROTLI_BIN:
            return None
        cmd = [BROTLI_BIN, "-q", str(level), "-c"]
    elif enc == "gzip":
        if not GZIP_BIN:
            return None
        cmd = [GZIP_BIN, f"-{level}", "-c"]
    else:
        return None
    try:
        proc = subprocess.run(cmd, input=data, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout


def encode_file_to_cache(src_path, cache_path, enc, level):
    """Compress the file `src_path` into `cache_path` at `enc`/`level`, writing
    an atomic body + a `.meta` recording the source identity and encode level.
    Streams the CLI src->stdout (never holds a big milo body in RAM). Returns
    True on success, False on any failure (caller falls through to raw)."""
    cache_dir = os.path.dirname(cache_path)
    try:
        os.makedirs(cache_dir, exist_ok=True)
    except OSError:
        return False

    if enc == "br":
        if not BROTLI_BIN:
            return False
        cmd = [BROTLI_BIN, "-q", str(level), "-c", src_path]
    elif enc == "gzip":
        if not GZIP_BIN:
            return False
        cmd = [GZIP_BIN, f"-{level}", "-c", src_path]
    else:
        return False

    tmp_path = cache_path + f".{os.getpid()}.{random.randrange(1 << 32):08x}.tmp"
    try:
        with open(tmp_path, "wb") as out:
            proc = subprocess.run(cmd, stdout=out, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            _unlink_quiet(tmp_path)
            return False
        # Capture source identity BEFORE the rename so .meta describes the
        # exact bytes compressed (not a racing rewrite of the source).
        st = os.stat(src_path)
        os.rename(tmp_path, cache_path)
    except OSError:
        _unlink_quiet(tmp_path)
        return False

    _atomic_write(cache_path + ".meta",
                  encode_meta_text(st.st_size, st.st_mtime, enc, level).encode())
    return True


def serialize_bundle(entries):
    """Serialize bundle `entries` ([(rel, bytes), ...]) into the binary bundle
    body the engine's onBundleSuccess parses:
        uint32 count, then per file: uint32 path_len, path, uint32 data_len, data
    Integers little-endian. Entries sorted by path for a stable body."""
    entries = sorted(entries, key=lambda x: x[0])
    chunks = [struct.pack("<I", len(entries))]
    for path, data in entries:
        path_bytes = path.encode("utf-8")
        chunks.append(struct.pack("<I", len(path_bytes)))
        chunks.append(path_bytes)
        chunks.append(struct.pack("<I", len(data)))
        chunks.append(data)
    return b"".join(chunks)


def bundle_fingerprint(entries):
    """16-hex fingerprint of a bundle's entry set+sizes (cheap — no payload
    hash). A changed asset set / size rebuilds the cached compressed artifact."""
    ents = sorted(entries, key=lambda x: x[0])
    return hashlib.sha1(
        (f"{len(ents)}|" + "|".join(
            f"{r}:{len(d)}" for r, d in ents)).encode()
    ).hexdigest()[:16]


def resolve_asset_path(rel):
    """Resolve a server-relative asset path to an on-disk file: ASSETS_DIR →
    the "(..)/(..)" system/run layout. Returns the absolute path or None.
    Shared by /api/file and the bundle routes so entry sets never drift.
    `rel` must already be traversal-checked by callers that accept client
    input; manifest files are trusted repo files."""
    full_path = os.path.join(ASSETS_DIR, rel)
    if not os.path.isfile(full_path) and rel.startswith("system/"):
        alt = os.path.join(ASSETS_DIR, "(..)", "(..)", rel)
        if os.path.isfile(alt):
            full_path = alt
    return full_path if os.path.isfile(full_path) else None


def read_manifest(manifest_path):
    """Newline-delimited, comment-tolerant manifest → [server-relative paths]."""
    paths = []
    try:
        with open(manifest_path, "r") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                paths.append(line)
    except OSError:
        pass
    return paths


def build_config_bundle_entries():
    """Build the /api/bundle (config DTA/DTB) entry set — walks ASSETS_DIR for
    .dta/.dtb, restores "(..)"→"..". Returns [(rel, bytes), ...]."""
    BUNDLE_EXTS = {".dta", ".dtb"}
    entries = []
    for root, _dirs, filenames in os.walk(ASSETS_DIR):
        for f in filenames:
            _name, ext = os.path.splitext(f)
            if ext.lower() not in BUNDLE_EXTS:
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, ASSETS_DIR).replace("(..)", "..")
            with open(full, "rb") as fh:
                entries.append((rel, fh.read()))
    return entries


class DC3Handler(http.server.SimpleHTTPRequestHandler):
    """Serves static files from build/ with correct MIME types, security headers,
    and an asset streaming API backed by a pre-extracted game data directory."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BUILD_DIR, **kwargs)

    def end_headers(self):
        # Required for SharedArrayBuffer (future pthreads support)
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Allow CORS for video pixel readback (getImageData on <video> frames)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        self.send_header("Cache-Control", self._cache_control_for_path())
        super().end_headers()

    def _cache_control_for_path(self):
        """Per-path HTTP cache policy. build.sh deploys two variants into distinct
        subdirs so caching is decided purely from the request path (index.html
        picks the subdir via ?debug=true):

          /release/*  → long-lived immutable cache. index.html version-busts the
                        URL with ?v=<asset+wasm mtime> from /api/version, so a
                        rebuilt release wasm is a new URL and the browser reuses
                        the cached + already-compiled wasm on every reload between
                        builds. This is what makes reloads fast.
          /debug/*    → never cached (fast-iteration build, always fresh).
          /api/version→ never cached, or the cache-bust token goes stale.
          everything else (index.html, audio-worklet.js, /api/bundle, manifest,
          /api/file assets, the splash video) → revalidate each load.
        """
        path = urllib.parse.urlparse(self.path).path
        if path.startswith("/release/"):
            return "public, max-age=31536000, immutable"
        if path.startswith("/debug/") or path == "/api/version":
            return "no-cache, no-store, must-revalidate"
        return "no-cache"

    def _maybe_serve_precompressed(self, head_only=False):
        """Serve a pre-compressed .br or .gz next to the requested .wasm / .js
        when the client advertises support. Returns True if a response was sent.
        The .br/.gz artifacts are generated by build.sh; if missing we fall
        through to the raw file (no on-the-fly compression). Strip the ?query
        FIRST — release URLs carry a ?v=<version> cache-bust token, so the suffix
        check must run on the path without it (else br/gz is skipped)."""
        url_path = urllib.parse.urlparse(self.path).path
        if not (url_path.endswith(".wasm") or url_path.endswith(".js")):
            return False
        rel = url_path.lstrip("/")
        base_path = os.path.join(BUILD_DIR, rel)
        accept = (self.headers.get("Accept-Encoding") or "").lower()
        candidates = []
        if "br" in accept:
            candidates.append((base_path + ".br", "br"))
        if "gzip" in accept:
            candidates.append((base_path + ".gz", "gzip"))
        for disk_path, enc in candidates:
            if os.path.isfile(disk_path):
                self._serve_encoded(disk_path, base_path, enc, head_only)
                return True
        return False

    def _serve_encoded(self, disk_path, base_path, encoding, head_only):
        """Serve a pre-compressed file with the matching Content-Encoding.
        Content-Type follows the *base* (.wasm / .js) so the browser parses it
        correctly after decompression."""
        size = os.path.getsize(disk_path)
        ctype = self.guess_type(base_path)
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Encoding", encoding)
        self.send_header("Content-Length", str(size))
        self.send_header("Vary", "Accept-Encoding")
        self.end_headers()
        if head_only:
            return
        with open(disk_path, "rb") as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                self.wfile.write(chunk)

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
        if self._maybe_serve_precompressed():
            return
        super().do_GET()

    def do_HEAD(self):
        if self.path.startswith("/api/"):
            self._handle_api()
            return
        if self.path == "/":
            self.path = "/index.html"
        if self._maybe_serve_precompressed(head_only=True):
            return
        super().do_HEAD()

    def _handle_api(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/health":
            self._serve_health()
        elif path == "/api/version":
            self._serve_version()
        elif path == "/api/manifest":
            self._serve_manifest()
        elif path == "/api/bundle":
            self._serve_bundle()
        elif path.startswith("/api/bundle/screen/"):
            self._serve_screen_bundle(path[len("/api/bundle/screen/"):])
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
        if self.command != "HEAD":
            self.wfile.write(body)

    def _serve_version(self):
        """Opaque asset-version tag used by index.html to cache-bust the immutable
        release artifacts. Combines the assets-dir mtime and the release wasm
        mtime, so rebuilding either yields a new ?v= and busts the cache. Clients
        only compare for equality."""
        parts = []
        if ASSETS_DIR and os.path.isdir(ASSETS_DIR):
            try:
                parts.append(str(int(os.path.getmtime(ASSETS_DIR))))
            except OSError:
                parts.append("0")
        else:
            parts.append("noassets")
        wasm_path = None
        for cand in ("release/dc3-web.wasm", "debug/dc3-web.wasm", "dc3-web.wasm"):
            p = os.path.join(BUILD_DIR, cand)
            if os.path.isfile(p):
                wasm_path = p
                break
        if wasm_path:
            try:
                parts.append(str(int(os.path.getmtime(wasm_path))))
            except OSError:
                parts.append("0")
        else:
            parts.append("nowasm")
        body = json.dumps({"version": "-".join(parts)}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
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
        if self.command != "HEAD":
            self.wfile.write(body)

    def _serve_bundle(self):
        """All boot-path DTA/DTB config files as a single binary bundle (see
        _emit_bundle for the format). Binary assets (.milo_xbox etc.) are
        fetched on demand by the engine (or pre-fetched via screen bundles)."""
        if not ASSETS_DIR:
            self._json_error(503, "No assets directory configured")
            return
        self._emit_bundle(build_config_bundle_entries(), cache_name="config")

    def _serve_screen_bundle(self, name):
        """A single named screen's dependency working set as one binary bundle
        (the format _emit_bundle produces, identical to the config bundle).

        `name` comes from /api/bundle/screen/<name>; it is validated against
        _SCREEN_NAME_RE so it can only ever resolve to
        native/web/screen-<name>.manifest (no path traversal). A missing or
        empty manifest emits an EMPTY bundle (count=0) — the client unpacks
        nothing and the engine's per-file sync path remains the backstop, so an
        unknown/absent screen never errors the transition."""
        if not ASSETS_DIR:
            self._json_error(503, "No assets directory configured")
            return
        if not _SCREEN_NAME_RE.match(name or ""):
            self._json_error(400, "Invalid screen name")
            return

        manifest_path = os.path.join(SCREEN_MANIFEST_DIR, f"screen-{name}.manifest")
        entries = []
        missing = 0
        for rel in read_manifest(manifest_path):
            full = resolve_asset_path(rel)
            if not full:
                missing += 1
                self.log_message("screen-bundle[%s]: missing %s (skipped)", name, rel)
                continue
            with open(full, "rb") as fh:
                entries.append((rel, fh.read()))

        total = sum(len(d) for _r, d in entries)
        self.log_message(
            "screen-bundle[%s]: %d files, %.1f MB%s",
            name, len(entries), total / 1e6,
            (f", {missing} missing" if missing else ""),
        )
        # cache_name is per-screen so each screen's compressed artifact is
        # distinct; the fingerprint inside _emit_bundle rebuilds on drift.
        self._emit_bundle(entries, cache_name=f"screen-{name}")

    def _emit_bundle(self, entries, cache_name=None):
        """Write `entries` ([(rel, bytes), ...]) as the binary bundle the
        engine's onBundleSuccess (milo-native-engine/src/platform/WebAssets.cpp)
        parses:
          uint32 count, then per file: uint32 path_len, path, uint32 data_len, data
        Integers little-endian. Entries emitted sorted by path (stable body).

        When `cache_name` is given and the client accepts br/gzip, the bundle
        body is compressed on the wire (Content-Encoding) with a persistent
        disk cache keyed by the entry-set fingerprint — the browser decodes
        before the engine's fetch sees the bytes, so onBundleSuccess still
        parses the raw bundle (transparent, same as /api/file)."""
        body = serialize_bundle(entries)

        enc, cache_ext = self._pick_encoding()
        if enc and cache_name and self.command != "HEAD":
            fp = bundle_fingerprint(entries)
            comp = self._bundle_encoded(cache_name, fp, body, enc, cache_ext)
            if comp is not None:
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Encoding", enc)
                self.send_header("Content-Length", str(len(comp)))
                self.send_header("Vary", "Accept-Encoding")
                self.end_headers()
                self.wfile.write(comp)
                return

        # Raw fallback (HEAD, no encoder advertised, or encode failed).
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Vary", "Accept-Encoding")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _pick_encoding(self):
        """('br'|'gzip', ext) per Accept-Encoding + available encoders + the
        ENCODE_ENABLED toggle, else (None, None). Prefers brotli."""
        if not (ENCODE_ENABLED and ENCODE_CACHE_DIR):
            return None, None
        accept = (self.headers.get("Accept-Encoding") or "").lower()
        if "br" in accept and BROTLI_BIN:
            return "br", ".br"
        if "gzip" in accept and GZIP_BIN:
            return "gzip", ".gz"
        return None, None

    def _bundle_encoded(self, cache_name, fp, body, enc, cache_ext):
        """Get-or-build the compressed bundle artifact, cached under
        ENCODE_CACHE_DIR/_bundles/<name>.<fp><ext> (the fingerprint is in the
        filename, so a changed asset set rebuilds). Returns the compressed
        bytes, or None on failure (caller serves raw — no regression)."""
        try:
            cache_dir = os.path.join(ENCODE_CACHE_DIR, "_bundles")
            os.makedirs(cache_dir, exist_ok=True)
        except OSError:
            return None
        cache_path = os.path.join(cache_dir, f"{cache_name}.{fp}{cache_ext}")
        if os.path.isfile(cache_path):
            try:
                with open(cache_path, "rb") as fh:
                    return fh.read()
            except OSError:
                pass  # fall through and rebuild
        level = ENCODE_LEVEL_BROTLI if enc == "br" else ENCODE_LEVEL_GZIP
        comp = encode_bytes(body, enc, level)
        if comp is None:
            return None
        _atomic_write(cache_path, comp)  # atomic; duplicate builders race harmlessly
        return comp

    def _maybe_compressed_asset(self, full_path, safe_rel, head_only):
        """On-demand wire compression for /api/file. If `full_path` is a
        compressible asset and the client advertises br/gzip, compress it once
        into ENCODE_CACHE_DIR (atomic temp+rename) and serve the cached
        artifact with the matching Content-Encoding. Returns True if a response
        was sent (caller bails), False to fall through to the raw path.

        Bails (→ raw path) for: compression disabled, a Range request (Range ⊕
        Content-Encoding is invalid), no encoder available, an ext not in the
        allowlist or in the deny set. The raw fallback guarantees no
        regression."""
        if not (ENCODE_ENABLED and ENCODE_CACHE_DIR):
            return False
        if self.headers.get("Range"):
            return False
        _root, ext = os.path.splitext(safe_rel)
        ext = ext.lower()
        if ext in INCOMPRESSIBLE_EXTS or ext not in COMPRESSIBLE_EXTS:
            return False

        accept = (self.headers.get("Accept-Encoding") or "").lower()
        if "br" in accept and BROTLI_BIN:
            enc, cache_ext = "br", ".br"
        elif "gzip" in accept and GZIP_BIN:
            enc, cache_ext = "gzip", ".gz"
        else:
            return False

        cache_path = os.path.join(ENCODE_CACHE_DIR, safe_rel + cache_ext)
        if not self._encoded_cache_valid(cache_path, full_path):
            level = ENCODE_LEVEL_BROTLI if enc == "br" else ENCODE_LEVEL_GZIP
            if not encode_file_to_cache(full_path, cache_path, enc, level):
                return False  # encode failed → fall through to raw
        if not os.path.isfile(cache_path):
            return False

        self._serve_encoded(cache_path, full_path, enc, head_only)
        return True

    @staticmethod
    def _encoded_cache_valid(cache_path, src_path):
        """True if `cache_path` exists and was built from the current
        `src_path` (size + mtime match the sidecar .meta)."""
        if not os.path.isfile(cache_path):
            return False
        try:
            st = os.stat(src_path)
            with open(cache_path + ".meta", "r") as fh:
                meta = fh.read().strip()
        except OSError:
            return False
        return meta_is_valid_for(meta, st.st_size, st.st_mtime)

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

        # Prefer AI-upscaled _high variant for video files when available.
        # e.g., videos/intro.webm -> videos/intro_high.webm
        name, ext = os.path.splitext(full_path)
        if ext.lower() in (".webm", ".mp4") and not name.endswith("_high"):
            high_path = name + "_high" + ext
            if os.path.isfile(high_path):
                full_path = high_path

        # Ark extraction stores ".." as "(..)" in directory names.
        # Files under system/run/ live at (..)/(..)/system/run/ on disk.
        # (resolve_asset_path is the shared source of truth with the bundle
        # routes; the _high video preference above stays a /api/file special.)
        if not os.path.isfile(full_path):
            resolved = resolve_asset_path(safe)
            if resolved:
                full_path = resolved

        if not os.path.isfile(full_path):
            self._json_error(404, f"Not found: {relpath}")
            return

        size = os.path.getsize(full_path)
        content_type = self.guess_type(full_path)
        head_only = self.command == "HEAD"

        # On-demand wire compression (transparent Content-Encoding). Returns
        # True if the compressed artifact was served; falls through to raw
        # (incl. all Range requests) otherwise.
        if self._maybe_compressed_asset(full_path, safe, head_only):
            return

        # Handle Range requests (partial content)
        range_hdr = self.headers.get("Range")
        if range_hdr and not head_only:
            self._serve_range(full_path, size, range_hdr, content_type)
            return

        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(size))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        if not head_only:
            with open(full_path, "rb") as f:
                # Stream in 64KB chunks to avoid loading huge files into memory
                while True:
                    chunk = f.read(65536)
                    if not chunk:
                        break
                    self.wfile.write(chunk)

    def _serve_range(self, full_path, total_size, range_hdr, content_type="application/octet-stream"):
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
        self.send_header("Content-Type", content_type)
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


def _default_encode_cache_dir():
    """Default on-disk encode cache location: native/web/.cache/encoded/
    (next to this script, gitignored). Overridable via DC3_ENCODE_CACHE."""
    env = os.environ.get("DC3_ENCODE_CACHE")
    if env:
        return env
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        ".cache", "encoded")


def main():
    global ASSETS_DIR, ENCODE_CACHE_DIR, ENCODE_ENABLED, BROTLI_BIN, GZIP_BIN

    parser = argparse.ArgumentParser(description="DC3 Web Dev Server")
    parser.add_argument(
        "--assets-dir",
        default=None,
        help="Path to extracted game assets (default: DC3_ASSETS env or auto-detect)",
    )
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument(
        "--encode-cache", default=None,
        help="Wire-compression cache dir (default: DC3_ENCODE_CACHE env or "
             "native/web/.cache/encoded)")
    parser.add_argument(
        "--no-encode", action="store_true",
        help="Disable on-demand wire compression (serve raw, for A/B)")
    args = parser.parse_args()

    ASSETS_DIR = args.assets_dir or _find_assets_dir()

    # Wire-compression setup: probe encoder CLIs once; a missing binary just
    # narrows the negotiation (brotli → gzip → raw), never errors.
    import shutil as _shutil
    ENCODE_ENABLED = not args.no_encode
    ENCODE_CACHE_DIR = args.encode_cache or _default_encode_cache_dir()
    BROTLI_BIN = _shutil.which("brotli")
    GZIP_BIN = _shutil.which("gzip")
    if ENCODE_ENABLED:
        try:
            os.makedirs(ENCODE_CACHE_DIR, exist_ok=True)
        except OSError:
            ENCODE_CACHE_DIR = None

    if not os.path.isdir(BUILD_DIR):
        print(f"Build directory not found: {BUILD_DIR}")
        print("Run scripts/web/build.sh first.")
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
    if ENCODE_ENABLED and ENCODE_CACHE_DIR:
        enc_names = [n for n, b in (("brotli", BROTLI_BIN), ("gzip", GZIP_BIN)) if b]
        print(f"  Encode:  {'+'.join(enc_names) or 'NO ENCODERS FOUND (raw)'} → {ENCODE_CACHE_DIR}")
    else:
        print("  Encode:  disabled (raw assets)")
    print()

    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), DC3Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
