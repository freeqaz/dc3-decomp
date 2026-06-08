#!/usr/bin/env bash
# DEPRECATED entry point — kept for back-compat. The canonical web build script
# is scripts/web/build.sh (one cohesive script, mirrors rb3's scripts/web/build.sh).
# Delegate so existing references / muscle memory keep working.
exec "$(cd "$(dirname "$0")/../../scripts/web" && pwd)/build.sh" "$@"
