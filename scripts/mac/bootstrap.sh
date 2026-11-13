#!/usr/bin/env bash
# bootstrap.sh -- check, configure, build and launch the DC3 native port on macOS.
#
# One command on a fresh Mac (from the dc3-decomp checkout):
#
#     scripts/mac/bootstrap.sh --fix
#
# Without --fix it only CHECKS and prints, for each missing piece, the exact
# command that fixes it. With --fix it runs the fixes it safely can: brew
# installs, the Dawn prebuilt download, the pose venv and model, unpacking an
# assets tarball. It never uses sudo and never touches anything outside the
# checkout, its parent directory (dawn-prebuilt/) and $DC3_POSE_VENV.
#
# Then it configures native/build, builds dc3-native + milo-viewer (+
# milo-tests when googletest is installed) and launches the game in a window.
#
# Options:
#   --fix            install / download what is missing (see above)
#   --no-launch      stop after the build
#   --smoke          after the build, run the headless YMCA flow first
#                    (same check as scripts/mac/libcxx_proxy_build.sh --smoke)
#   --assets PATH    game data: a directory holding gen/main_xbox.hdr (linked as
#                    <repo>/orig-assets), or an orig-assets.tar.xz (unpacked
#                    into the repo; needs --fix, ~25 GB)
#   --jobs N         build parallelism (default: all cores)
#   -h, --help
#
# Environment:
#   DAWN_PREFIX      an unpacked Dawn tarball to use instead of the default
#                    <parent>/dawn-prebuilt
#   DC3_POSE_VENV    Python venv for the pose server (default
#                    ~/.local/share/dc3/pose-venv)
#   MILO_ENGINE_PATH the shared engine checkout (default <parent>/milo-native-engine)
#
# Every run writes its logs to native/build/mac-bootstrap/<stamp>/ and prints
# that path; send that directory back when something fails (MACOS.md).
#
# Testing from Linux (nothing is installed, downloaded or built; each macOS
# probe is printed instead of run):
#     MILO_BOOTSTRAP_PLATFORM=Darwin MILO_BOOTSTRAP_DRY_RUN=1 scripts/mac/bootstrap.sh --fix
#
# Exit codes: 0 ok · 1 usage · 2 prerequisites missing (all listed) ·
#             6 build failed · 8 smoke failed · 9/10 configure failed
#             (scripts/native_configure.sh's codes) · 11 game exited non-zero
#
# Plain bash 3.2 + BSD userland (macOS's /bin/bash): no associative arrays,
# no mapfile, no GNU-only flags.

set -o pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REPOS_ROOT="$(dirname "$REPO_ROOT")"
_common="$(git -C "$REPO_ROOT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
[ -n "$_common" ] && REPOS_ROOT="$(dirname "$(dirname "$_common")")"

PLATFORM="${MILO_BOOTSTRAP_PLATFORM:-$(uname -s)}"
DRY="${MILO_BOOTSTRAP_DRY_RUN:-0}"
FIX=0; LAUNCH=1; SMOKE=0; ASSETS=""; JOBS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --fix) FIX=1 ;;
        --no-launch) LAUNCH=0 ;;
        --smoke) SMOKE=1 ;;
        --assets) ASSETS="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

if [ "$PLATFORM" != "Darwin" ]; then
    echo "error: this is the macOS bootstrap. On Linux use scripts/native_configure.sh," >&2
    echo "       or test this script with MILO_BOOTSTRAP_PLATFORM=Darwin MILO_BOOTSTRAP_DRY_RUN=1." >&2
    exit 1
fi

BUILD_DIR="$REPO_ROOT/native/build"
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$BUILD_DIR/mac-bootstrap/$STAMP"
[ "$DRY" = "1" ] && LOG_DIR="${TMPDIR:-/tmp}/dc3-mac-bootstrap-dry-$STAMP"
mkdir -p "$LOG_DIR"

# Dawn: the release whose webgpu.h / webgpu_cpp.h are byte-identical to the
# Linux build's (dc3-decomp-deps), and the Linux CI's DAWN_RELEASE_TAG.
DAWN_TAG="v20260423.175430"
DAWN_HASH="31e25af254ab572c77054edec4946d2244e184dd"
ARCH="${MILO_BOOTSTRAP_ARCH:-$(uname -m)}"
case "$ARCH" in
    arm64) DAWN_FLAVOUR="macos-latest-Release"
           DAWN_SHA256="8d1db80315ab55ad2c61857d1c7ae4c242a9cc67d0639c709a052fc6af3a7bac" ;;
    x86_64) DAWN_FLAVOUR="macos-15-intel-Release"
           DAWN_SHA256="c1c53665e71413e04c62136f36b3b0fd69691d1d6f5bdca23d453f9885430d28" ;;
    *) echo "error: unsupported CPU '$ARCH'" >&2; exit 1 ;;
esac
DAWN_TREE="Dawn-$DAWN_HASH-$DAWN_FLAVOUR"
DAWN_URL="https://github.com/google/dawn/releases/download/$DAWN_TAG/$DAWN_TREE.tar.gz"
DAWN_DEFAULT="$REPOS_ROOT/dawn-prebuilt"

POSE_VENV="${DC3_POSE_VENV:-$HOME/.local/share/dc3/pose-venv}"
POSE_MODEL="$REPO_ROOT/native/models/pose_landmarker_full.task"
POSE_MODEL_URL="https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_full/float16/latest/pose_landmarker_full.task"
POSE_MODEL_SHA256="4eaa5eb7a98365221087693fcc286334cf0858e2eb6e15b506aa4a7ecdcec4ad"
MEDIAPIPE_VERSION="1.0.0"   # what the Linux pose server and the web worker use

ENGINE="${MILO_ENGINE_PATH:-$REPOS_ROOT/milo-native-engine}"

# coreutils: gtimeout, which the viewer and gesture-menu tests use when
# there is no `timeout` (macOS has none).
BREW_FORMULAE="cmake ninja pkgconf glfw jpeg-turbo libvorbis libogg ffmpeg coreutils"

# ---------------------------------------------------------------------------
missing=0
say()  { printf '%s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
ok()   { printf '    ok    %s\n' "$*"; }
warn() { printf '    WARN  %s\n' "$*"; }
need() { # need <what> <fix line> [more fix lines...]
    printf '    MISSING %s\n' "$1"; shift
    for l in "$@"; do printf '            -> %s\n' "$l"; done
    missing=$((missing + 1))
}
run() { # run <log-name> <cmd...>: honour dry-run, tee into the log dir
    local name="$1"; shift
    if [ "$DRY" = "1" ]; then
        printf '    DRY RUN: %s\n' "$*"
        return 0
    fi
    "$@" 2>&1 | tee -a "$LOG_DIR/$name.log"
}
probe() { # probe <cmd...>: a macOS-only check; in dry-run it is assumed to pass
    if [ "$DRY" = "1" ]; then
        printf '    (dry run, assumed ok: %s)\n' "$*"
        return 0
    fi
    "$@" >/dev/null 2>&1
}
sha256() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1; fi
}
ncpu() { sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

say "DC3 native macOS bootstrap ($STAMP)"
say "    repo      : $REPO_ROOT"
say "    logs      : $LOG_DIR"
[ "$DRY" = "1" ] && say "    mode      : DRY RUN (platform=$PLATFORM arch=$ARCH)"

# System facts for the log, whatever happens next.
if [ "$DRY" != "1" ]; then
    {
        sw_vers 2>&1; uname -a; echo
        echo "xcode-select: $(xcode-select -p 2>&1)"
        clang++ --version 2>&1 | head -2
        echo "ld: $(ld -v 2>&1 | head -1)"
        command -v brew >/dev/null 2>&1 && brew --version 2>&1 | head -1
        command -v cmake >/dev/null 2>&1 && cmake --version | head -1
        echo "repo HEAD: $(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null)"
        echo "engine HEAD: $(git -C "$ENGINE" rev-parse HEAD 2>/dev/null)"
    } > "$LOG_DIR/system.txt" 2>&1
fi

# ---- 1. Xcode command-line tools ------------------------------------------
step "Xcode command-line tools"
if probe xcode-select -p && probe clang++ --version; then
    ok "$( [ "$DRY" = "1" ] && echo "clang++" || clang++ --version | head -1)"
else
    need "Xcode command-line tools (clang, ld, the macOS SDK)" \
         "xcode-select --install     # then re-run this script"
fi

# ---- 2. Homebrew and formulae ---------------------------------------------
step "Homebrew"
if [ "$DRY" != "1" ] && ! command -v brew >/dev/null 2>&1; then
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        [ -x "$b" ] && eval "$("$b" shellenv)" && break
    done
fi
if [ "$DRY" = "1" ] || command -v brew >/dev/null 2>&1; then
    [ "$DRY" != "1" ] && [ -z "${HOMEBREW_PREFIX:-}" ] && eval "$(brew shellenv)"
    ok "brew at ${HOMEBREW_PREFIX:-<dry run>}"
    want="$BREW_FORMULAE"
    tests_on=OFF
    if [ "$DRY" = "1" ] || brew list --versions googletest >/dev/null 2>&1; then
        tests_on=ON
    fi
    absent=""
    for f in $want; do
        probe brew list --versions "$f" || absent="$absent $f"
    done
    if [ -n "$absent" ]; then
        if [ "$FIX" = "1" ]; then
            run brew-install brew install $absent || need "brew formulae:$absent" "brew install$absent"
        else
            need "brew formulae:$absent" "brew install$absent     (or re-run with --fix)"
        fi
    else
        ok "formulae: $want"
    fi
    if [ "$tests_on" = "ON" ]; then
        ok "googletest present: milo-tests will be built (BUILD_TESTS=ON)"
    else
        warn "googletest not installed: building without milo-tests (BUILD_TESTS=OFF)."
        say  "            For the test suite: brew install googletest"
    fi
else
    need "Homebrew" \
         "/bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"" \
         "then open a new terminal (or eval \"\$(/opt/homebrew/bin/brew shellenv)\") and re-run"
    tests_on=OFF
fi

# ---- 3. the shared engine -------------------------------------------------
step "milo-native-engine (sibling checkout)"
if [ -f "$ENGINE/CMakeLists.txt" ]; then
    pin="$(sed -n 's/^set(MILO_ENGINE_PIN "\([0-9a-f]*\)".*/\1/p' "$REPO_ROOT/native/CMakeLists.txt" | head -1)"
    head="$(git -C "$ENGINE" rev-parse HEAD 2>/dev/null || echo unknown)"
    # 4e738fe carries the macOS engine pieces (libc++ shim, Apple flag
    # exemptions, weak fallbacks: PLAN M-1/M-2/M-4).
    if git -C "$ENGINE" merge-base --is-ancestor 4e738fe HEAD 2>/dev/null; then
        ok "$ENGINE at ${head:0:12} (has 4e738fe, the macOS engine pieces)"
    else
        need "an engine at or after 4e738fe (macOS shim/flags/weak fallbacks); $ENGINE is at ${head:0:12}" \
             "git -C \"$ENGINE\" fetch && git -C \"$ENGINE\" checkout ${pin:-4e738fe}"
    fi
    [ -n "$pin" ] && [ "$head" != "$pin" ] && \
        warn "engine HEAD ${head:0:12} differs from MILO_ENGINE_PIN ${pin:0:12} (a warning, not an error)"
else
    origin="$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null | sed 's/dc3-decomp\(\.git\)\{0,1\}$/milo-native-engine.git/')"
    need "$ENGINE" "git clone ${origin:-<milo-native-engine repo URL>} \"$REPOS_ROOT/milo-native-engine\"" \
         "(or set MILO_ENGINE_PATH to an existing checkout)"
fi

# ---- 4. Dawn prebuilt -----------------------------------------------------
step "Dawn (WebGPU) prebuilt, $DAWN_TAG $DAWN_FLAVOUR"
DAWN_USE="${DAWN_PREFIX:-$DAWN_DEFAULT}"
if [ -f "$DAWN_USE/lib/cmake/Dawn/DawnConfig.cmake" ]; then
    ok "$DAWN_USE"
elif [ -n "${DAWN_PREFIX:-}" ]; then
    need "DAWN_PREFIX=$DAWN_PREFIX has no lib/cmake/Dawn/DawnConfig.cmake" \
         "point DAWN_PREFIX at the unpacked $DAWN_TREE directory, or unset it to use $DAWN_DEFAULT"
elif [ "$FIX" = "1" ]; then
    tgz="$REPOS_ROOT/$DAWN_TREE.tar.gz"
    say "    downloading $DAWN_URL"
    if run dawn-download curl -fL --retry 3 -o "$tgz" "$DAWN_URL"; then
        if [ "$DRY" = "1" ] || [ "$(sha256 "$tgz")" = "$DAWN_SHA256" ]; then
            stage="$REPOS_ROOT/.dawn-unpack-$STAMP"
            run dawn-unpack mkdir -p "$stage"
            run dawn-unpack tar -xzf "$tgz" -C "$stage"
            run dawn-unpack mv "$stage/$DAWN_TREE" "$DAWN_DEFAULT"
            run dawn-unpack rmdir "$stage"
            [ "$DRY" = "1" ] || [ -f "$DAWN_DEFAULT/lib/cmake/Dawn/DawnConfig.cmake" ] \
                && ok "unpacked to $DAWN_DEFAULT" \
                || need "Dawn unpack into $DAWN_DEFAULT" "see $LOG_DIR/dawn-unpack.log"
        else
            need "Dawn tarball checksum (got $(sha256 "$tgz"), expected $DAWN_SHA256)" \
                 "delete $tgz and re-run; if it repeats, the release asset changed: tell the repo owner"
        fi
    else
        need "Dawn download" "curl -fLO $DAWN_URL   (network?), then re-run"
    fi
else
    need "Dawn prebuilt at $DAWN_DEFAULT" \
         "re-run with --fix (downloads and checks $DAWN_TREE.tar.gz, ~9 MB)" \
         "or: curl -fLO $DAWN_URL && tar -xzf $DAWN_TREE.tar.gz && DAWN_PREFIX=\$PWD/$DAWN_TREE $0"
fi

# ---- 5. game data ---------------------------------------------------------
step "Game data (orig-assets)"
DATA="$REPO_ROOT/orig-assets"
if [ -n "$ASSETS" ]; then
    if [ -f "$ASSETS/gen/main_xbox.hdr" ]; then
        if [ -e "$DATA" ] && [ ! -L "$DATA" ]; then
            need "--assets given but $DATA already exists and is not a link" \
                 "move it aside, or drop --assets to use it as is"
        else
            run assets-link ln -sfn "$ASSETS" "$DATA"
        fi
    elif [ -f "$ASSETS" ] && case "$ASSETS" in *.tar.xz) true ;; *) false ;; esac; then
        if [ "$FIX" = "1" ]; then
            say "    unpacking $ASSETS into $REPO_ROOT (~25 GB; several minutes)"
            run assets-unpack tar -xJf "$ASSETS" -C "$REPO_ROOT"
        else
            need "assets tarball not unpacked yet" "re-run with --fix --assets \"$ASSETS\""
        fi
    else
        need "--assets $ASSETS: neither a directory with gen/main_xbox.hdr nor an orig-assets.tar.xz" \
             "pass the directory that CONTAINS gen/ (e.g. .../orig-assets), or the tarball"
    fi
fi
if [ -f "$DATA/gen/main_xbox.hdr" ] || { [ -n "${DC3_DATA:-}" ] && [ -f "${DC3_DATA}/gen/main_xbox.hdr" ]; }; then
    ok "$( [ -n "${DC3_DATA:-}" ] && echo "DC3_DATA=$DC3_DATA" || echo "$DATA" )"
elif [ "$DRY" = "1" ] && [ -n "$ASSETS" ]; then
    ok "(dry run) would use $ASSETS"
else
    tarball="$REPOS_ROOT/dc3-decomp-deps/orig-assets.tar.xz"
    if [ -f "$tarball" ]; then
        need "game data in $DATA" "re-run with --fix --assets \"$tarball\""
    else
        need "game data: $DATA/gen/main_xbox.hdr" \
             "copy it from the Linux box, e.g.  rsync -a <linux-host>:<repo>/orig-assets/ \"$DATA/\"" \
             "or copy dc3-decomp-deps/orig-assets.tar.xz over and pass --fix --assets <that file>" \
             "or pass --assets <dir with gen/main_xbox.hdr> to link an existing copy"
    fi
fi

# ---- 6. pose server (optional: the game runs without it) --------------------
step "Pose server (webcam; optional)"
pose_ok=0
py="$POSE_VENV/bin/python3"
if [ "$DRY" != "1" ] && [ -x "$py" ] && "$py" -c 'import mediapipe, cv2, numpy' >/dev/null 2>&1; then
    pose_ok=1
    ok "venv $POSE_VENV (mediapipe $("$py" -c 'import mediapipe; print(mediapipe.__version__)' 2>/dev/null))"
elif [ "$FIX" = "1" ]; then
    base_py="$(command -v python3 || true)"
    if [ -z "$base_py" ] && [ "$DRY" != "1" ]; then
        warn "no python3: brew install python, then re-run --fix. Playing without pose."
    else
        run pose-venv python3 -m venv "$POSE_VENV" \
            && run pose-pip "$POSE_VENV/bin/python3" -m pip install --upgrade pip \
            && run pose-pip "$POSE_VENV/bin/python3" -m pip install "mediapipe==$MEDIAPIPE_VERSION" opencv-python numpy \
            && pose_ok=1
        [ "$pose_ok" = "1" ] && ok "venv $POSE_VENV" \
            || warn "pose venv setup failed (see $LOG_DIR/pose-pip.log). Playing without pose."
    fi
else
    warn "no pose venv at $POSE_VENV: the game runs, pose tracking is off."
    say  "            -> re-run with --fix, or by hand:"
    say  "               python3 -m venv \"$POSE_VENV\" && \"$POSE_VENV/bin/pip\" install mediapipe==$MEDIAPIPE_VERSION opencv-python numpy"
fi
if [ -f "$POSE_MODEL" ]; then
    [ "$(sha256 "$POSE_MODEL")" = "$POSE_MODEL_SHA256" ] && ok "model $POSE_MODEL" \
        || warn "model $POSE_MODEL differs from the one the Linux box uses (sha256 $POSE_MODEL_SHA256)"
elif [ "$FIX" = "1" ]; then
    run pose-model mkdir -p "$(dirname "$POSE_MODEL")"
    run pose-model curl -fL --retry 3 -o "$POSE_MODEL" "$POSE_MODEL_URL"
    if [ "$DRY" = "1" ] || [ "$(sha256 "$POSE_MODEL")" = "$POSE_MODEL_SHA256" ]; then
        ok "model downloaded"
    else
        warn "downloaded model's sha256 differs from the tested one ($POSE_MODEL_SHA256); kept anyway"
    fi
else
    warn "no pose model at $POSE_MODEL"
    say  "            -> re-run with --fix, or: curl -fL -o \"$POSE_MODEL\" $POSE_MODEL_URL"
fi

# ---- stop here if anything required is missing ----------------------------
if [ "$missing" -gt 0 ]; then
    printf '\n%d required item(s) missing (listed above with the command that fixes each).\n' "$missing"
    [ "$FIX" = "0" ] && say "Most can be fixed by re-running with --fix."
    say "Logs: $LOG_DIR"
    exit 2
fi

# ---- 7. configure ---------------------------------------------------------
step "Configure $BUILD_DIR"
cfg_env="MILO_CONFIGURE_PLATFORM=Darwin DAWN_PREFIX=$DAWN_USE BUILD_TESTS=$tests_on"
[ -n "${MILO_ENGINE_PATH:-}" ] && cfg_extra="-DMILO_ENGINE_PATH=$MILO_ENGINE_PATH" || cfg_extra=""
if [ "$DRY" = "1" ]; then
    env MILO_CONFIGURE_PLATFORM=Darwin MILO_CONFIGURE_DRY_RUN=1 DAWN_PREFIX="$DAWN_USE" \
        BUILD_TESTS="$tests_on" "$REPO_ROOT/scripts/native_configure.sh" "$BUILD_DIR" $cfg_extra
    rc=$?
else
    env DAWN_PREFIX="$DAWN_USE" BUILD_TESTS="$tests_on" \
        "$REPO_ROOT/scripts/native_configure.sh" "$BUILD_DIR" $cfg_extra 2>&1 | tee "$LOG_DIR/configure.log"
    rc=${PIPESTATUS[0]}
fi
if [ "$rc" -ne 0 ]; then
    say "CONFIGURE FAILED (native_configure.sh exit $rc; 9 = a dependency it names is missing)."
    say "Logs: $LOG_DIR/configure.log, $BUILD_DIR/CMakeFiles/CMakeConfigureLog.yaml"
    exit "$rc"
fi
say "    ($cfg_env)"

# ---- 8. build -------------------------------------------------------------
targets="dc3-native milo-viewer"
[ "$tests_on" = "ON" ] && targets="$targets milo-tests"
step "Build: $targets"
jobs="${JOBS:-$(ncpu)}"
if ! run build cmake --build "$BUILD_DIR" --target $targets -j "$jobs"; then
    say "BUILD FAILED. The first error in $LOG_DIR/build.log is the one to send back."
    exit 6
fi
[ "$DRY" = "1" ] || ok "built: $(cd "$BUILD_DIR" && ls -1 $targets 2>/dev/null | tr '\n' ' ')"

# ---- 9. optional headless smoke -------------------------------------------
if [ "$SMOKE" = "1" ]; then
    step "Smoke: headless YMCA flow, DtaFlowTest's environment (8000 frames)"
    smoke_dir="$LOG_DIR/smoke"
    if [ "$DRY" = "1" ]; then
        say "    DRY RUN: MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 DC3_TEL=1 DC3_TEL_INTERVAL=1 MILO_MAX_FRAMES=8000 ... $BUILD_DIR/dc3-native"
    else
        # An EMPTY private cache and save dir, as DtaFlowTest uses: ymca.txt
        # drives the first-run move tutorial, so ~/.local/share/dc3 (or a
        # cache that has seen the tutorial) would stall it at multiuser_screen.
        # GPU on: under MILO_NORENDER frames run far faster than the script's
        # waits and it stops at tutorial_perform_it_screen.
        mkdir -p "$smoke_dir/cache" "$smoke_dir/saves"
        ( cd "$REPO_ROOT" && env DC3_DATA="${DC3_DATA:-$DATA}" MILO_HEADLESS=1 MILO_FATAL_FAILS=0 \
            DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 DC3_TEL=1 DC3_TEL_INTERVAL=1 \
            MILO_MAX_FRAMES=8000 DC3_CACHE_DIR="$smoke_dir/cache" DC3_SAVE_DIR="$smoke_dir/saves" \
            MILO_INPUT_SCRIPT="$REPO_ROOT/scripts/dc3-input-flows/ymca.txt" "$BUILD_DIR/dc3-native" ) \
            > "$smoke_dir/run.log" 2>&1
        src=$?
        playing=$(grep -c 'DC3_TEL: .* state=playing screen=game_screen' "$smoke_dir/run.log")
        exit_line="$(grep 'DC3_EXIT:' "$smoke_dir/run.log" | tail -1)"
        say "    exit $src; frames playing on game_screen: $playing"
        say "    ${exit_line:-no DC3_EXIT line}"
        if [ "$src" -ne 0 ] || [ "$playing" -eq 0 ] \
           || ! printf '%s\n' "$exit_line" | grep -q 'detail=frame_cap_reached code=0'; then
            say "SMOKE FAILED: $smoke_dir/run.log"
            exit 8
        fi
        ok "smoke"
    fi
fi

[ "$LAUNCH" = "1" ] || { say; say "Built. Launch: scripts/start-mac.sh   (logs: $LOG_DIR)"; exit 0; }

# ---- 10. launch -----------------------------------------------------------
step "Launch (windowed)"
say "    First run: macOS asks for camera access for this TERMINAL app when the pose"
say "    server opens the webcam. Allow it, then quit (Esc / close) and relaunch if"
say "    the game says no camera was found. Details: docs/native/MACOS.md"
say "    Log: $LOG_DIR/game.log"
launch_path="$PATH"
[ "$pose_ok" = "1" ] && launch_path="$POSE_VENV/bin:$PATH"
if [ "$DRY" = "1" ]; then
    say "    DRY RUN: PATH=$POSE_VENV/bin:\$PATH $REPO_ROOT/scripts/start-mac.sh"
    exit 0
fi
PATH="$launch_path" "$REPO_ROOT/scripts/start-mac.sh" 2>&1 | tee "$LOG_DIR/game.log"
grc=${PIPESTATUS[0]}
say
say "Game exited with status $grc. $(grep 'DC3_EXIT:' "$LOG_DIR/game.log" | tail -1)"
say "Logs: $LOG_DIR"
[ "$grc" -eq 0 ] || exit 11
