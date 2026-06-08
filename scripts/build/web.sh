#!/usr/bin/env bash
# DEPRECATED entry point — kept for back-compat. The canonical web build script
# is scripts/web/build.sh (one cohesive script, mirrors rb3's scripts/web/build.sh).
# This used to do a FLAT deploy to native/web/build/dc3-web.{js,wasm}, but the
# dual-deploy index.html/server.py only load the release/+debug/ subdirs, so the
# flat output was never served. Delegate so the documented command does the right
# thing regardless of which path you typed.
exec "$(cd "$(dirname "$0")/../web" && pwd)/build.sh" "$@"
