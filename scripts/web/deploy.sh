#!/usr/bin/env bash
# Rebuild the web build, restart the server unit when its code changed, and
# smoke-test what is being served (PLAN W-13, docs/native/WEB.md "Deployment").
#
# scripts/web/build.sh writes native/web/build/, which server.py reads off disk
# per request, so a rebuild alone is picked up live. server.py's own code is
# not: dc3-web.service ran a server.py from before W-7's sidecar routes for
# hours after the change, and every SFX sidecar 404'd. So after building, this
# asks the running server which source it runs (/api/health server_sha256) and
# restarts the unit when that is not this tree's native/web/server.py.
#
# Usage (from the checkout the unit serves):
#   scripts/web/deploy.sh                       # build both variants, restart if needed, smoke test
#   scripts/web/deploy.sh --no-build            # only the restart check + smoke test
#   scripts/web/deploy.sh --browser             # also boot the page in Chromium (scripts/web/smoke-test.mjs; GPU)
#   scripts/web/deploy.sh --unit U --port P     # another user unit / port (default dc3-web.service, 8420)
#   scripts/web/deploy.sh --restart             # restart even when the code matches
#   scripts/web/deploy.sh -- --release          # anything after -- goes to build.sh
#
# Refuses (exit 3) when the unit's WorkingDirectory is not this checkout:
# deploying from a worktree must not restart the server of another tree.
#
# Exit: 0 deployed and smoke test passed; 1 build failed (nothing restarted);
# 2 restart failed or the server never came back; 3 refused; 4 smoke test failed.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UNIT=dc3-web.service
PORT=8420
DO_BUILD=1
FORCE_RESTART=0
BROWSER=0
BUILD_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --unit)      shift; UNIT="$1" ;;
        --port)      shift; PORT="$1" ;;
        --no-build)  DO_BUILD=0 ;;
        --restart)   FORCE_RESTART=1 ;;
        --browser)   BROWSER=1 ;;
        --)          shift; BUILD_ARGS=("$@"); break ;;
        -h|--help)   sed -n '/^# Usage/,/^# Exit/p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (build.sh options go after --)" >&2; exit 3 ;;
    esac
    shift
done
BASE="http://127.0.0.1:$PORT"
say() { echo "==> deploy: $*"; }

# --- the unit must serve this checkout --------------------------------------
UNIT_DIR="$(systemctl --user show "$UNIT" -p WorkingDirectory --value 2>/dev/null || true)"
if [ -z "$UNIT_DIR" ]; then
    echo "deploy: REFUSED -- user unit $UNIT not found (systemctl --user show)" >&2
    exit 3
fi
if [ "$(cd "$UNIT_DIR" 2>/dev/null && pwd -P)" != "$(cd "$REPO_ROOT" && pwd -P)" ]; then
    echo "deploy: REFUSED -- $UNIT serves $UNIT_DIR, not this checkout ($REPO_ROOT)" >&2
    exit 3
fi
say "$UNIT serves $UNIT_DIR on port $PORT"

# --- build -------------------------------------------------------------------
if [ "$DO_BUILD" = 1 ]; then
    say "building: scripts/web/build.sh ${BUILD_ARGS[*]:-}"
    if ! "$REPO_ROOT/scripts/web/build.sh" "${BUILD_ARGS[@]}"; then
        echo "deploy: BUILD FAILED -- nothing restarted (the server keeps serving what is on disk)" >&2
        exit 1
    fi
fi

# --- restart when the running server is not this tree's server.py -------------
health() { curl -fsS --max-time 5 "$BASE/api/health" 2>/dev/null || true; }
field() { python3 -c 'import json,sys
try: print(json.loads(sys.argv[1]).get(sys.argv[2], ""))
except Exception: print("")' "$1" "$2"; }
WANT_SHA="$(sha256sum "$REPO_ROOT/native/web/server.py" | cut -d' ' -f1)"
H="$(health)"
RUN_SHA="$(field "$H" server_sha256)"
if [ -z "$H" ]; then
    REASON="server not answering on $BASE"
elif [ -z "$RUN_SHA" ]; then
    REASON="running server predates the server_sha256 health field"
elif [ "$RUN_SHA" != "$WANT_SHA" ]; then
    REASON="running server.py ${RUN_SHA:0:12} != this tree's ${WANT_SHA:0:12}"
elif [ "$FORCE_RESTART" = 1 ]; then
    REASON="--restart"
else
    REASON=""
fi
if [ -n "$REASON" ]; then
    say "restarting $UNIT ($REASON)"
    if ! systemctl --user restart "$UNIT"; then
        echo "deploy: RESTART FAILED -- systemctl --user restart $UNIT" >&2
        exit 2
    fi
    deadline=$((SECONDS + 30))
    RUN_SHA=""
    while [ $SECONDS -lt $deadline ]; do
        RUN_SHA="$(field "$(health)" server_sha256)"
        [ "$RUN_SHA" = "$WANT_SHA" ] && break
        sleep 0.5
    done
    if [ "$RUN_SHA" != "$WANT_SHA" ]; then
        echo "deploy: SERVER DID NOT COME BACK with this tree's server.py within 30 s (running: ${RUN_SHA:-nothing})" >&2
        systemctl --user status "$UNIT" --no-pager -n 20 >&2 || true
        exit 2
    fi
    say "restarted; server runs server.py ${WANT_SHA:0:12}"
else
    say "no restart needed: server runs this tree's server.py (${WANT_SHA:0:12})"
fi

# --- smoke test ----------------------------------------------------------------
FAILS=0
probe() { # probe NAME URL [method]
    local code
    code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 ${3:+-I} "$BASE$2" || echo 000)"
    if [ "$code" = 200 ]; then echo "  ok   $1 ($2)"; else echo "  FAIL $1 ($2): HTTP $code" >&2; FAILS=$((FAILS + 1)); fi
}
say "smoke test against $BASE"
probe "page" "/index.html"
probe "audio worklet" "/audio-worklet.js"
for v in release debug; do
    if [ -f "$REPO_ROOT/native/web/build/$v/dc3-web.wasm" ]; then
        probe "$v wasm" "/$v/dc3-web.wasm" head
        probe "$v js" "/$v/dc3-web.js" head
    fi
done
probe "pose tasks-vision (no CDN)" "/pose/vendor/tasks-vision/vision_bundle.mjs" head
probe "pose model" "/pose/models/pose_landmarker_full.task" head
probe "asset API (intro movie)" "/api/file/videos/intro.webm" head
# An SFX sidecar out of the manifest: the route a stale server.py 404'd (W-7).
KEY="$(grep -v '^#' "$REPO_ROOT/native/web/sfx_sidecars.manifest" 2>/dev/null | grep -m1 -o '[0-9a-f]\{16\}' || true)"
if [ -n "$KEY" ]; then
    probe "SFX sidecar" "/api/file/sfx/gen/xma_pcm/$KEY.ogg" head
else
    echo "  FAIL SFX sidecar: no key in native/web/sfx_sidecars.manifest" >&2; FAILS=$((FAILS + 1))
fi
if [ "$BROWSER" = 1 ]; then
    say "browser smoke test (scripts/web/smoke-test.mjs --port $PORT)"
    if ! node "$REPO_ROOT/scripts/web/smoke-test.mjs" --port "$PORT"; then FAILS=$((FAILS + 1)); fi
fi
if [ "$FAILS" -gt 0 ]; then
    echo "deploy: SMOKE TEST FAILED ($FAILS)" >&2
    exit 4
fi
say "DEPLOYED: $UNIT on port $PORT, smoke test passed"
