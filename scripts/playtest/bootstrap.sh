#!/usr/bin/env bash
# bootstrap.sh -- make a Linux machine ready to PLAY the DC3 native port with a
# webcam (PLAN L-1; docs/native/PLAYTEST.md "Setting up a machine").
#
# One command on the player box, from its dc3-decomp checkout:
#
#     scripts/playtest/bootstrap.sh --fix
#
# Without --fix it only CHECKS, and names each missing thing in plain words
# with the command that fixes it. With --fix it does what it safely can
# without sudo: the pose venv (uv, Python 3.10, the pinned packages), the pose
# model (sha256-checked download), Dawn (Google's release, sha256-checked) when
# ../dc3-decomp-deps/dawn is absent, the engine sibling moved to the pinned
# commit (only when its checkout is clean), the native build (dc3-native only),
# and -- with --assets-from -- the game data the desktop reads.
#
# A player box needs none of the decomp toolchain: no wibo/MSVC, no
# build/373307D9, no decomp.db, no Ghidra. Only dc3-native is built.
#
# Options:
#   --fix                 install / download / build what is missing
#   --assets-from SRC     rsync source for the game data, e.g.
#                         free@main-box:/home/free/code/milohax/dc3-decomp/orig-assets
#                         (only what the desktop reads: gen/ + 3 loose files +
#                         the song folder list, 6.6 GB; see asset_manifest.py)
#   --hash-assets         also sha256 the archives against the manifest (~1 min)
#   --jobs N              build parallelism (default: all cores)
#   --no-build            skip configure + build
#   -h, --help
#
# Environment: DC3_POSE_VENV (default ~/.local/share/dc3/pose-venv),
#   MILO_ENGINE_PATH (default <parent>/milo-native-engine), DC3_DATA,
#   DAWN_PREFIX (an unpacked Dawn release to use).
#
# Logs: native/build/playtest-bootstrap/<stamp>/ (printed). Exit: 0 ready ·
# 1 usage · 2 something required is missing (all listed) · 6 build failed ·
# 9/10 configure failed (native_configure.sh's codes).

set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPOS_ROOT="$(dirname "$REPO_ROOT")"
_common="$(git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
[ -n "$_common" ] && REPOS_ROOT="$(dirname "$(dirname "$_common")")"

FIX=0; ASSETS_FROM=""; HASH=0; JOBS=""; BUILD=1
while [ $# -gt 0 ]; do
    case "$1" in
        --fix) FIX=1 ;;
        --assets-from) ASSETS_FROM="$2"; shift ;;
        --hash-assets) HASH=1 ;;
        --jobs) JOBS="$2"; shift ;;
        --no-build) BUILD=0 ;;
        -h|--help) sed -n '2,38p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

BUILD_DIR="$REPO_ROOT/native/build"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$BUILD_DIR/playtest-bootstrap/$STAMP"
mkdir -p "$LOG_DIR"

POSE_VENV="${DC3_POSE_VENV:-$HOME/.local/share/dc3/pose-venv}"
POSE_REQ="$REPO_ROOT/native/scripts/pose-requirements.txt"
POSE_PY_VERSION="3.10"
MODEL_DIR="$REPO_ROOT/native/models"
MODEL_URL_BASE="https://storage.googleapis.com/mediapipe-models/pose_landmarker"
MODEL_FULL_SHA256="4eaa5eb7a98365221087693fcc286334cf0858e2eb6e15b506aa4a7ecdcec4ad"
DAWN_TAG="v20260423.175430"
DAWN_TREE="Dawn-31e25af254ab572c77054edec4946d2244e184dd-ubuntu-latest-Release"
DAWN_SHA256="ac76fac090162dc1ecea5ed0f28a557bb8f49efc47faab01886105ace82b7b64"
DAWN_URL="https://github.com/google/dawn/releases/download/$DAWN_TAG/$DAWN_TREE.tar.gz"
ENGINE="${MILO_ENGINE_PATH:-$REPOS_ROOT/milo-native-engine}"
DATA="${DC3_DATA:-$REPO_ROOT/orig-assets}"

missing=0
say()  { printf '%s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
ok()   { printf '    ok      %s\n' "$*"; }
warn() { printf '    WARN    %s\n' "$*"; }
need() { printf '    MISSING %s\n' "$1"; shift
         for l in "$@"; do printf '            -> %s\n' "$l"; done
         missing=$((missing + 1)); }
logrun() { local name="$1"; shift; "$@" >>"$LOG_DIR/$name.log" 2>&1; }
sha256() { sha256sum "$1" | cut -d' ' -f1; }

say "DC3 playtest bootstrap ($STAMP)"
say "    repo : $REPO_ROOT ($(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null))"
say "    logs : $LOG_DIR"
{ uname -a; echo "repo HEAD $(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null)";
  echo "engine HEAD $(git -C "$ENGINE" rev-parse HEAD 2>/dev/null)"; } >"$LOG_DIR/system.txt" 2>&1

# ---- 1. system packages ----------------------------------------------------
step "System packages (installed with your package manager; this script never uses sudo)"
absent_cmd=""
for c in clang clang++ cmake ninja pkg-config git curl rsync; do
    command -v "$c" >/dev/null 2>&1 || absent_cmd="$absent_cmd $c"
done
absent_pc=""
for m in glfw3 x11 libavformat libavcodec libswscale libswresample libavutil vorbis ogg libjpeg; do
    pkg-config --exists "$m" 2>/dev/null || absent_pc="$absent_pc $m"
done
if [ -z "$absent_cmd$absent_pc" ]; then
    ok "build tools and libraries (clang, cmake, ninja, glfw, ffmpeg, vorbis, jpeg)"
else
    need "build tools/libraries:${absent_cmd}${absent_pc}" \
         "Arch:   sudo pacman -S --needed clang cmake ninja pkgconf git curl rsync glfw libx11 ffmpeg libvorbis libogg libjpeg-turbo" \
         "Debian: sudo apt install clang cmake ninja-build pkg-config git curl rsync libglfw3-dev libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libvorbis-dev libjpeg-dev libx11-dev"
fi
if command -v vulkaninfo >/dev/null 2>&1 && vulkaninfo --summary >/dev/null 2>&1; then
    ok "Vulkan: $(vulkaninfo --summary 2>/dev/null | sed -n 's/.*deviceName *= *//p' | sort -u | paste -sd, -)"
else
    need "a working Vulkan driver (the game renders through WebGPU on Vulkan)" \
         "Arch AMD: sudo pacman -S --needed vulkan-radeon vulkan-tools   (NVIDIA: nvidia-utils, then reboot)"
fi
command -v uv >/dev/null 2>&1 && ok "uv $(uv --version 2>/dev/null | cut -d' ' -f2)" || \
    need "uv (makes the pose tracker's Python 3.10 venv; the system Python is too new for its packages)" \
         "Arch: sudo pacman -S uv    (or: curl -LsSf https://astral.sh/uv/install.sh | sh)"

# ---- 2. the shared engine at the pinned commit -----------------------------
step "milo-native-engine (sibling checkout, pinned)"
pin="$(sed -n 's/^set(MILO_ENGINE_PIN "\([0-9a-f]*\)".*/\1/p' "$REPO_ROOT/native/CMakeLists.txt" | head -1)"
if [ ! -f "$ENGINE/CMakeLists.txt" ]; then
    origin="$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null | sed 's/dc3-decomp\(\.git\)\{0,1\}$/milo-native-engine.git/')"
    need "the engine checkout at $ENGINE" \
         "git clone ${origin:-<milo-native-engine repo URL>} \"$ENGINE\" && git -C \"$ENGINE\" checkout $pin"
else
    head="$(git -C "$ENGINE" rev-parse HEAD 2>/dev/null)"
    if [ -n "$pin" ] && [ "${head#"$pin"}" != "$head" ]; then
        ok "$ENGINE at ${head:0:12} = MILO_ENGINE_PIN"
    elif git -C "$ENGINE" merge-base --is-ancestor "$pin" HEAD 2>/dev/null; then
        warn "$ENGINE at ${head:0:12} is newer than MILO_ENGINE_PIN $pin (works, but it is not what was tested)"
    elif [ "$FIX" = "1" ] && [ -z "$(git -C "$ENGINE" status --porcelain 2>/dev/null)" ]; then
        say "    moving the engine to $pin (its checkout is clean)"
        if ! git -C "$ENGINE" cat-file -e "$pin^{commit}" 2>/dev/null; then
            logrun engine git -C "$ENGINE" fetch --all
        fi
        if logrun engine git -C "$ENGINE" checkout --detach "$pin"; then
            ok "engine at $pin"
        else
            need "engine commit $pin" "git -C \"$ENGINE\" fetch && git -C \"$ENGINE\" checkout $pin  (see $LOG_DIR/engine.log)"
        fi
    else
        need "the engine at MILO_ENGINE_PIN $pin; $ENGINE is at ${head:0:12}$( [ -n "$(git -C "$ENGINE" status --porcelain 2>/dev/null)" ] && echo ' with uncommitted changes')" \
             "git -C \"$ENGINE\" fetch && git -C \"$ENGINE\" checkout $pin   (or re-run with --fix on a clean checkout)"
    fi
fi

# ---- 3. Dawn (WebGPU) -------------------------------------------------------
step "Dawn (WebGPU prebuilt)"
DAWN_USE="${DAWN_PREFIX:-}"
if [ -z "$DAWN_USE" ] && [ -f "$REPOS_ROOT/dc3-decomp-deps/dawn/lib/cmake/Dawn/DawnConfig.cmake" ]; then
    ok "$REPOS_ROOT/dc3-decomp-deps/dawn"
elif [ -n "$DAWN_USE" ] && { [ -f "$DAWN_USE/lib/cmake/Dawn/DawnConfig.cmake" ] || [ -f "$DAWN_USE/lib64/cmake/Dawn/DawnConfig.cmake" ]; }; then
    ok "DAWN_PREFIX=$DAWN_USE"
elif [ -f "$REPOS_ROOT/$DAWN_TREE/lib64/cmake/Dawn/DawnConfig.cmake" ]; then
    DAWN_USE="$REPOS_ROOT/$DAWN_TREE"
    ok "$DAWN_USE"
elif [ "$FIX" = "1" ]; then
    tgz="$REPOS_ROOT/$DAWN_TREE.tar.gz"
    say "    downloading Dawn $DAWN_TAG (29 MB)"
    if logrun dawn curl -fL --retry 3 -o "$tgz" "$DAWN_URL" && [ "$(sha256 "$tgz")" = "$DAWN_SHA256" ]; then
        logrun dawn tar -xzf "$tgz" -C "$REPOS_ROOT" && DAWN_USE="$REPOS_ROOT/$DAWN_TREE"
        [ -f "$DAWN_USE/lib64/cmake/Dawn/DawnConfig.cmake" ] && ok "unpacked $DAWN_USE" || need "Dawn unpack" "see $LOG_DIR/dawn.log"
    else
        need "Dawn download or checksum (expected sha256 $DAWN_SHA256)" "curl -fLO $DAWN_URL  (network?), see $LOG_DIR/dawn.log"
    fi
else
    need "Dawn: neither $REPOS_ROOT/dc3-decomp-deps/dawn nor a Dawn release" \
         "re-run with --fix (downloads $DAWN_TREE.tar.gz, 29 MB, sha256-checked)"
fi

# ---- 4. pose tracker: venv + model -----------------------------------------
step "Pose tracker (Python venv + MediaPipe model)"
py="$POSE_VENV/bin/python3"
want_mp="$(sed -n 's/^mediapipe==//p' "$POSE_REQ")"
have_mp="$( [ -x "$py" ] && "$py" -I -c 'import mediapipe, cv2, numpy; print(mediapipe.__version__)' 2>/dev/null)"
if [ -n "$have_mp" ] && [ "$have_mp" = "$want_mp" ]; then
    ok "venv $POSE_VENV (mediapipe $have_mp)"
elif [ "$FIX" = "1" ] && command -v uv >/dev/null 2>&1; then
    say "    making $POSE_VENV (Python $POSE_PY_VERSION + native/scripts/pose-requirements.txt, ~450 MB)"
    if logrun pose-venv uv venv --clear --python "$POSE_PY_VERSION" "$POSE_VENV" \
       && logrun pose-venv uv pip install --python "$py" -r "$POSE_REQ" \
       && "$py" -I -c 'import mediapipe, cv2, numpy' >/dev/null 2>&1; then
        ok "venv $POSE_VENV (mediapipe $("$py" -I -c 'import mediapipe; print(mediapipe.__version__)'))"
    else
        need "the pose venv" "see $LOG_DIR/pose-venv.log"
    fi
else
    need "the pose tracker's packages (mediapipe, opencv, numpy) in $POSE_VENV${have_mp:+ (has mediapipe $have_mp, want $want_mp)}" \
         "re-run with --fix (needs uv), or by hand:" \
         "uv venv --python $POSE_PY_VERSION \"$POSE_VENV\" && uv pip install --python \"$py\" -r \"$POSE_REQ\""
fi
mkdir -p "$MODEL_DIR"
for m in full lite heavy; do
    f="$MODEL_DIR/pose_landmarker_$m.task"
    if [ -f "$f" ]; then
        [ "$m" = full ] && [ "$(sha256 "$f")" != "$MODEL_FULL_SHA256" ] && \
            warn "$f is not the tested model (sha256 differs)" || ok "model $(basename "$f")"
    elif [ "$FIX" = "1" ]; then
        if logrun model curl -fL --retry 3 -o "$f.part" "$MODEL_URL_BASE/pose_landmarker_$m/float16/latest/pose_landmarker_$m.task"; then
            mv "$f.part" "$f"
            if [ "$m" = full ] && [ "$(sha256 "$f")" != "$MODEL_FULL_SHA256" ]; then
                warn "downloaded $f but its sha256 is not the tested one ($MODEL_FULL_SHA256): Google changed 'latest'"
            else
                ok "model $(basename "$f") downloaded"
            fi
        else
            [ "$m" = full ] && need "the pose model $f" "curl -fL -o \"$f\" $MODEL_URL_BASE/pose_landmarker_full/float16/latest/pose_landmarker_full.task" \
                            || warn "optional model $m not downloaded"
        fi
    elif [ "$m" = full ]; then
        need "the pose model $f (9 MB)" "re-run with --fix (sha256-checked download)"
    else
        say "    (optional model $m absent; only the web build uses it)"
    fi
done

# ---- 5. game data -----------------------------------------------------------
step "Game data ($DATA)"
if [ -n "$ASSETS_FROM" ]; then
    if [ "$FIX" = "1" ]; then
        say "    copying from $ASSETS_FROM (the archives are 6.6 GB; resumable)"
        mkdir -p "$DATA/gen" "$DATA/extracted/songs"
        man="$REPO_ROOT/native/playtest/assets-manifest.json"
        files_list="$LOG_DIR/assets-files.txt"
        python3 -I -c 'import json,sys; m=json.load(open(sys.argv[1]))
for e in m["archives"]+m["loose"]: print(e["path"])
for s in m["song_dirs"]: print("extracted/songs/"+s+"/")' "$man" >"$files_list"
        if rsync -a --partial --info=progress2 --files-from="$files_list" "$ASSETS_FROM/" "$DATA/" 2>&1 | tee "$LOG_DIR/assets.log"; then
            ok "copied"
        else
            need "the game data copy" "see $LOG_DIR/assets.log; it resumes when re-run"
        fi
    else
        say "    (--assets-from given without --fix: not copying)"
    fi
fi
hash_flag=""; [ "$HASH" = "1" ] && hash_flag="--hash"
if python3 -I "$REPO_ROOT/scripts/playtest/asset_manifest.py" check --data "$DATA" $hash_flag >"$LOG_DIR/assets-check.txt" 2>&1; then
    ok "$(tail -1 "$LOG_DIR/assets-check.txt")"
else
    sed 's/^/            /' "$LOG_DIR/assets-check.txt"
    need "game data the desktop needs (above)" \
         "re-run with --fix --assets-from <user@host:/path/to/orig-assets> (copies only what the desktop reads)" \
         "or copy orig-assets/gen/ and the files above by hand; docs/native/PLAYTEST.md 'Setting up a machine'"
fi

if [ "$missing" -gt 0 ]; then
    printf '\n%d thing(s) missing (listed above with the command that fixes each).\n' "$missing"
    [ "$FIX" = "0" ] && say "Most are fixed by re-running with --fix."
    say "Logs: $LOG_DIR"
    exit 2
fi

# ---- 6. configure + build dc3-native ----------------------------------------
if [ "$BUILD" = "1" ]; then
    step "Build dc3-native"
    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
        # googletest is only for the developer's test suite; without it the
        # build skips the tests (a player box needs dc3-native only).
        tests=ON; pkg-config --exists gtest 2>/dev/null || tests=OFF
        [ "$tests" = OFF ] && say "    googletest not installed: configuring without the test suite"
        env ${DAWN_USE:+DAWN_PREFIX="$DAWN_USE"} BUILD_TESTS="$tests" \
            "$REPO_ROOT/scripts/native_configure.sh" "$BUILD_DIR" >"$LOG_DIR/configure.log" 2>&1
        rc=$?
        if [ "$rc" -ne 0 ]; then
            say "CONFIGURE FAILED (exit $rc; 9 = a dependency it names is missing). Log: $LOG_DIR/configure.log"
            tail -15 "$LOG_DIR/configure.log"
            exit "$rc"
        fi
        ok "configured $BUILD_DIR"
    else
        ok "already configured ($BUILD_DIR)"
    fi
    say "    building (the first build takes 10-20 minutes)"
    if ! cmake --build "$BUILD_DIR" --target dc3-native -j "${JOBS:-$(nproc)}" >"$LOG_DIR/build.log" 2>&1; then
        say "BUILD FAILED. The first error:"
        grep -m1 -B2 -A8 -E 'error:|FAILED' "$LOG_DIR/build.log"
        say "Full log: $LOG_DIR/build.log"
        exit 6
    fi
    ok "built $BUILD_DIR/dc3-native"
fi

# ---- 7. preflight -----------------------------------------------------------
step "Preflight (scripts/play.sh --check, without looking for a person)"
"$REPO_ROOT/scripts/play.sh" --check --no-person 2>&1 | tee "$LOG_DIR/preflight.txt"
say
say "Ready to set up the camera and play: docs/native/PLAYTEST.md, then  scripts/play.sh"
say "Logs: $LOG_DIR"
exit 0
