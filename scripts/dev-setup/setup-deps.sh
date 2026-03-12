#!/usr/bin/env bash
#
# DC3 Decomp — Dependency Setup
#
# Clones all required sibling repositories and installs system/Python
# dependencies needed to build and work on the DC3 decompilation.
#
# Usage:
#   ./scripts/dev-setup/setup-deps.sh           # Clone repos + show system dep hints
#   ./scripts/dev-setup/setup-deps.sh --skip-clone   # Only show system deps / setup venv
#   ./scripts/dev-setup/setup-deps.sh --help
#
# Assumptions:
#   - dc3-decomp is at ~/code/milohax/dc3-decomp
#   - Sibling repos go into ~/code/milohax/<repo>
#   - You have SSH keys configured for GitHub

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MILOHAX_DIR="$(cd "$REPO_ROOT/.." && pwd)"

SKIP_CLONE=false
SKIP_BUILD=false
SKIP_VENV=false

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --skip-clone    Skip git clone steps (repos already exist)"
    echo "  --skip-build    Skip building Rust/C tools from source"
    echo "  --skip-venv     Skip Python venv creation"
    echo "  --help          Show this help"
    exit 0
}

for arg in "$@"; do
    case "$arg" in
        --skip-clone) SKIP_CLONE=true ;;
        --skip-build) SKIP_BUILD=true ;;
        --skip-venv)  SKIP_VENV=true ;;
        --help)       usage ;;
        *) echo "Unknown option: $arg"; usage ;;
    esac
done

info()  { echo -e "\033[1;34m==>\033[0m \033[1m$*\033[0m"; }
warn()  { echo -e "\033[1;33m==> WARNING:\033[0m $*"; }
err()   { echo -e "\033[1;31m==> ERROR:\033[0m $*"; }
ok()    { echo -e "\033[1;32m  ✓\033[0m $*"; }
skip()  { echo -e "\033[1;33m  •\033[0m $* (already exists, skipping)"; }

# ─── Preflight checks ────────────────────────────────────────────────

info "Checking prerequisites..."

missing=()
command -v git    >/dev/null || missing+=(git)
command -v python3 >/dev/null || missing+=(python3)
command -v ninja  >/dev/null || missing+=(ninja)
command -v cmake  >/dev/null || missing+=(cmake)

if [ ${#missing[@]} -gt 0 ]; then
    err "Missing required tools: ${missing[*]}"
    echo "  Install them with your package manager, e.g.:"
    echo "    sudo pacman -S ${missing[*]}    # Arch"
    echo "    sudo apt install ${missing[*]}  # Debian/Ubuntu"
    exit 1
fi

if ! command -v uv >/dev/null; then
    echo "  Installing uv (needed for pyghidra-mcp)..."
    sudo pacman -S --noconfirm --needed uv
    ok "Installed uv"
fi

# ─── Clone helper ─────────────────────────────────────────────────────

clone_repo() {
    local url="$1"
    local dir="$2"
    local branch="${3:-}"

    if [ -d "$dir/.git" ]; then
        skip "$dir"
        return 0
    fi

    if [ "$SKIP_CLONE" = true ]; then
        warn "Skipping clone of $dir (--skip-clone)"
        return 0
    fi

    if [ -n "$branch" ]; then
        git clone --branch "$branch" "$url" "$dir"
    else
        git clone "$url" "$dir"
    fi
    ok "Cloned $dir"
}

# ─── 1. Core build tools (required for decomp) ───────────────────────

info "Core build tool repositories..."
echo "  These are built from source and referenced by configure.py."
echo ""

# jeff — DTK fork (decomp-toolkit for XEX splitting/linking)
clone_repo "git@github.com:freeqaz/jeff.git" "$MILOHAX_DIR/jeff"

# objdiff — Assembly diff tool (our fork)
clone_repo "git@github.com:freeqaz/objdiff.git" "$MILOHAX_DIR/objdiff"

# wibo — Wine/Windows binary wrapper (runs MSVC cl.exe on Linux)
clone_repo "git@github.com:freeqaz/wibo.git" "$MILOHAX_DIR/wibo"

# ─── 2. Build the Rust/C tools ───────────────────────────────────────

if [ "$SKIP_BUILD" = false ]; then
    info "Building tools from source..."

    # Check for Rust toolchain
    if ! command -v cargo >/dev/null; then
        warn "Rust (cargo) not found. Install via https://rustup.rs/"
        warn "Skipping Rust builds — you'll need to build jeff and objdiff manually."
    else
        # jeff (DTK)
        if [ -d "$MILOHAX_DIR/jeff" ]; then
            if [ ! -f "$MILOHAX_DIR/jeff/target/release/dtk" ]; then
                echo "  Building jeff (dtk)..."
                (cd "$MILOHAX_DIR/jeff" && cargo build --release)
                ok "Built jeff → target/release/dtk"
            else
                skip "jeff already built"
            fi
        fi

        # objdiff
        if [ -d "$MILOHAX_DIR/objdiff" ]; then
            if [ ! -f "$MILOHAX_DIR/objdiff/target/release/objdiff-cli" ]; then
                echo "  Building objdiff..."
                (cd "$MILOHAX_DIR/objdiff" && cargo build --release --bin objdiff-cli)
                ok "Built objdiff → target/release/objdiff-cli"
            else
                skip "objdiff already built"
            fi
        fi
    fi

    # wibo (C, uses cmake)
    if [ -d "$MILOHAX_DIR/wibo" ]; then
        if [ ! -f "$MILOHAX_DIR/wibo/build/release/wibo" ]; then
            echo "  Building wibo..."
            mkdir -p "$MILOHAX_DIR/wibo/build/release"
            (cd "$MILOHAX_DIR/wibo/build/release" && cmake -DCMAKE_BUILD_TYPE=Release ../.. && make -j"$(nproc)")
            ok "Built wibo → build/release/wibo"
        else
            skip "wibo already built"
        fi
    fi
else
    info "Skipping tool builds (--skip-build)"
fi

# ─── 3. Reference / analysis repos (optional but recommended) ────────

info "Reference repositories..."
echo "  These are used for cross-referencing, analysis tools, and assets."
echo ""

# rb3 — Rock Band 3 decomp (shared Milo engine, used by lookup_rb3)
clone_repo "git@github.com:DarkRTA/rb3.git" "$MILOHAX_DIR/rb3"

# m2c — Machine code to C decompiler (local fork with MSVC PPC support)
clone_repo "git@github.com:freeqaz/m2c.git" "$MILOHAX_DIR/m2c"

# milo-executable-library — XEX binary, map files, ark tools
clone_repo "git@github.com:hmxmilohax/milo-executable-library.git" "$MILOHAX_DIR/milo-executable-library"

# Copy the target XEX and map file into orig/373307D9/
XEX_SRC="$MILOHAX_DIR/milo-executable-library/dc3/9.16.12 (Final Debug) - No Checksum"
ORIG_DIR="$REPO_ROOT/orig/373307D9"
if [ -d "$XEX_SRC" ]; then
    mkdir -p "$ORIG_DIR"
    if [ ! -f "$ORIG_DIR/default.xex" ]; then
        cp "$XEX_SRC/default.xex" "$ORIG_DIR/default.xex"
        ok "Copied default.xex → orig/373307D9/"
    else
        skip "orig/373307D9/default.xex"
    fi
    if [ ! -f "$ORIG_DIR/ham_xbox_r.map" ]; then
        cp "$XEX_SRC/ham_xbox_r.map" "$ORIG_DIR/ham_xbox_r.map"
        ok "Copied ham_xbox_r.map → orig/373307D9/"
    else
        skip "orig/373307D9/ham_xbox_r.map"
    fi
else
    warn "XEX source not found at: $XEX_SRC"
    warn "You may need to place default.xex and ham_xbox_r.map in orig/373307D9/ manually."
fi

# pyghidra-mcp — Ghidra MCP integration for decompilation analysis
clone_repo "git@github.com:freeqaz/pyghidra-mcp.git" "$MILOHAX_DIR/pyghidra-mcp"

# XEXLoaderWV — Ghidra extension for loading Xbox 360 XEX binaries
clone_repo "git@github.com:zeroKilo/XEXLoaderWV.git" "$MILOHAX_DIR/XEXLoaderWV"

# ghidra — Our fork with Xbox 360 / VMX128 support
clone_repo "git@github.com:freeqaz/ghidra.git" "$MILOHAX_DIR/ghidra"

# Build Ghidra and extract to a stable path
if [ "$SKIP_BUILD" = false ] && [ -d "$MILOHAX_DIR/ghidra" ]; then
    if [ ! -d "$MILOHAX_DIR/ghidra/build/ghidra" ]; then
        if command -v gradle >/dev/null || [ -x "$MILOHAX_DIR/ghidra/gradlew" ]; then
            echo "  Building Ghidra (this takes a while)..."
            (cd "$MILOHAX_DIR/ghidra" && ./gradlew buildGhidra -x test)
            # Extract the latest zip to build/ghidra/
            GHIDRA_ZIP=$(ls -t "$MILOHAX_DIR/ghidra/build/dist/"ghidra_*_linux_x86_64.zip 2>/dev/null | head -1)
            if [ -n "$GHIDRA_ZIP" ]; then
                mkdir -p "$MILOHAX_DIR/ghidra/build/ghidra"
                unzip -qo "$GHIDRA_ZIP" -d "$MILOHAX_DIR/ghidra/build/ghidra-tmp"
                # The zip contains a versioned dir — move contents up to build/ghidra/
                mv "$MILOHAX_DIR/ghidra/build/ghidra-tmp"/ghidra_*/* "$MILOHAX_DIR/ghidra/build/ghidra/"
                rm -rf "$MILOHAX_DIR/ghidra/build/ghidra-tmp"
                ok "Built + extracted Ghidra → build/ghidra/"
            else
                warn "Ghidra built but no zip found in build/dist/"
            fi
        else
            warn "Gradle not found — skipping Ghidra build. Install gradle or use ./gradlew"
        fi
    else
        skip "Ghidra already built"
    fi
fi

GHIDRA_DIR="$MILOHAX_DIR/ghidra/build/ghidra"

# Build XEXLoaderWV extension and install into Ghidra
if [ "$SKIP_BUILD" = false ] && [ -d "$MILOHAX_DIR/XEXLoaderWV" ] && [ -d "$GHIDRA_DIR" ]; then
    if [ -d "$GHIDRA_DIR/Ghidra/Extensions/XEXLoaderWV" ]; then
        skip "XEXLoaderWV already installed in Ghidra"
    else
        echo "  Building XEXLoaderWV extension..."
        XEXLOADER_BUILD_DIR="$MILOHAX_DIR/XEXLoaderWV/XEXLoaderWV"
        if [ -d "$XEXLOADER_BUILD_DIR" ]; then
            (cd "$XEXLOADER_BUILD_DIR" && \
                GHIDRA_INSTALL_DIR="$GHIDRA_DIR" \
                "$GHIDRA_DIR/support/gradle/gradlew" \
                -PGHIDRA_INSTALL_DIR="$GHIDRA_DIR")
            # Install: extract the built extension zip into Ghidra's Extensions dir
            XEXLOADER_ZIP=$(ls -t "$XEXLOADER_BUILD_DIR/dist/"*XEXLoaderWV.zip 2>/dev/null | head -1)
            if [ -n "$XEXLOADER_ZIP" ]; then
                unzip -qo "$XEXLOADER_ZIP" -d "$GHIDRA_DIR/Ghidra/Extensions/"
                ok "Built + installed XEXLoaderWV into Ghidra"
            else
                warn "XEXLoaderWV built but no zip found in dist/"
            fi
        else
            warn "XEXLoaderWV/XEXLoaderWV subdirectory not found"
        fi
    fi
elif [ ! -d "$GHIDRA_DIR" ]; then
    warn "Skipping XEXLoaderWV — Ghidra not built yet"
fi

# pyghidra-mcp is ready — uv was installed in preflight, service runs via uv
if [ -d "$MILOHAX_DIR/pyghidra-mcp" ]; then
    ok "pyghidra-mcp ready (run via: tools/ghidra/pyghidra-service.sh start)"
fi

# ─── 4. Native port dependencies ─────────────────────────────────────

info "Native port dependencies..."
echo "  Pre-built Dawn (WebGPU) for the native renderer."
echo ""

# dc3-decomp-deps — vendored pre-built dependencies (Dawn, etc.)
clone_repo "git@github.com:freeqaz/dc3-decomp-deps.git" "$MILOHAX_DIR/dc3-decomp-deps"

# ─── 5. Emulation / testing repos (optional) ─────────────────────────

info "Emulation & testing repositories (optional)..."
echo "  These are needed for runtime validation and behavioral testing."
echo ""

# xenia — Xbox 360 emulator (for booting the decomp XEX)
clone_repo "git@github.com:freeqaz/xenia.git" "$MILOHAX_DIR/xenia"

# unicorn — CPU emulator for behavioral testing (ppc64 branch)
clone_repo "git@github.com:freeqaz/unicorn.git" "$MILOHAX_DIR/unicorn" "ppc64"

# ─── 6. Harmonix asset repos (large, needed for native port) ──────────

info "Harmonix asset repositories (needed for native port)..."
echo "  Clones 13 Harmonix sub-repos into ~/code/milohax/milo-engine-libs/harmonix-repos/"
echo "  See scripts/dev-setup/HARMONIX_REPOS.md for repo descriptions."
echo ""

if [ -d "$MILOHAX_DIR/milo-engine-libs/harmonix-repos/milo-rnd-library/.git" ]; then
    skip "milo-engine-libs/harmonix-repos already populated"
elif [ "$SKIP_CLONE" = true ]; then
    warn "Skipping Harmonix sub-repo clone (--skip-clone)"
else
    echo "  This may take a while — milo-rnd-library alone is ~63GB..."
    bash "$SCRIPT_DIR/clone-harmonix-repos.sh"
    ok "Cloned Harmonix sub-repos"
fi

# ─── 7. Python venv ──────────────────────────────────────────────────

if [ "$SKIP_VENV" = false ]; then
    info "Setting up Python virtual environment..."

    if [ ! -d "$REPO_ROOT/venv" ]; then
        python3 -m venv "$REPO_ROOT/venv"
        ok "Created venv"
    else
        skip "venv already exists"
    fi

    source "$REPO_ROOT/venv/bin/activate"
    pip install --quiet --upgrade pip
    pip install --quiet -r "$REPO_ROOT/requirements.txt"
    ok "Installed Python dependencies"
else
    info "Skipping venv setup (--skip-venv)"
fi

# ─── 8. Configure the build ──────────────────────────────────────────

info "Configuring the build system..."

# Verify core tools exist
tools_ok=true
for tool_path in \
    "$MILOHAX_DIR/jeff/target/release/dtk" \
    "$MILOHAX_DIR/objdiff/target/release/objdiff-cli" \
    "$MILOHAX_DIR/wibo/build/release/wibo"; do
    if [ ! -f "$tool_path" ]; then
        warn "Missing: $tool_path"
        tools_ok=false
    fi
done

if [ "$tools_ok" = true ]; then
    (cd "$REPO_ROOT" && python3 configure.py \
        --dtk "$MILOHAX_DIR/jeff/target/release/dtk" \
        --objdiff "$MILOHAX_DIR/objdiff/target/release/objdiff-cli" \
        --wibo "$MILOHAX_DIR/wibo/build/release/wibo")
    ok "Build configured — run 'ninja' to build"
else
    warn "Some tools are missing. Build configure.py manually after building them."
fi

# ─── 9. Manual steps / summary ───────────────────────────────────────

echo ""
info "Setup complete! Summary:"
echo ""
echo "  Repositories cloned:"
echo "    ~/code/milohax/dc3-decomp              ← this repo"
echo "    ~/code/milohax/jeff                    ← DTK fork (Rust)"
echo "    ~/code/milohax/objdiff                 ← objdiff (Rust)"
echo "    ~/code/milohax/wibo                    ← wibo (C)"
echo "    ~/code/milohax/rb3                     ← Rock Band 3 decomp (reference)"
echo "    ~/code/milohax/m2c                     ← m2c decompiler (MSVC PPC fork)"
echo "    ~/code/milohax/milo-executable-library ← XEX, map files"
echo "    ~/code/milohax/pyghidra-mcp            ← Ghidra MCP server"
echo "    ~/code/milohax/XEXLoaderWV             ← Ghidra XEX loader extension"
echo "    ~/code/milohax/ghidra                  ← Ghidra fork (Xbox 360 / VMX128 support)"
echo "    ~/code/milohax/dc3-decomp-deps         ← pre-built deps (Dawn WebGPU)"
echo "    ~/code/milohax/xenia                   ← Xbox 360 emulator"
echo "    ~/code/milohax/unicorn                 ← Unicorn CPU emulator"
echo "    ~/code/milohax/milo-engine-libs/       ← index repo + 13 Harmonix sub-repos"
echo ""
echo "  System packages still required:"
echo "    Native port: clang, glfw3, FFmpeg (Dawn is built from source above)"
echo "      Arch:   pacman -S clang glfw ffmpeg"
echo "      Ubuntu: apt install clang libglfw3-dev libavformat-dev libavcodec-dev libswscale-dev"
echo "    Ghidra:   install Ghidra + XEXLoaderWV extension"
echo ""
echo "  Game assets (native port runtime):"
echo "    The native port needs DC3 ark data at orig-assets/gen/."
echo "    These are NOT in any git repo — you need the game disc or a dump."
echo "    Place main_xbox.hdr + main_xbox_*.ark (segments 0-9) there."
echo ""
echo "  Quick start (decomp):"
echo "    source scripts/setup-env.sh   # activate venv"
echo "    ninja                         # build everything"
echo ""
echo "  Quick start (native port):"
echo "    cd native && mkdir -p build && cd build"
echo "    CC=clang CXX=clang++ cmake -DDawn_DIR=../../dc3-decomp-deps/dawn/lib/cmake/Dawn .."
echo "    make -j\$(nproc)"
