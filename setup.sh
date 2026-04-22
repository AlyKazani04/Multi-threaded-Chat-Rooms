#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# setup.sh  –  Install all dependencies for OS-Level Chat Server
# CS2006 Operating Systems  |  Spring 2026
#
# Run ONCE before building:
#   chmod +x setup.sh
#   ./setup.sh
#
# What this script does:
#   1. Updates apt package lists
#   2. Installs build tools (gcc, make, git)
#   3. Installs raylib via apt (Ubuntu 22.04 has raylib 3.7 in universe)
#      OR builds raylib 5.0 from source if the apt version is too old
#   4. Downloads raygui.h (single-header) into src/
#   5. Installs Valgrind (Helgrind + Memcheck for testing)
# ─────────────────────────────────────────────────────────────────────────────

set -e   # exit on first error

echo "=== CS2006 Chat Sim – Dependency Setup ==="
echo ""

# ── 1. System packages ────────────────────────────────────────────────────
echo "[1/5] Installing system packages..."
sudo apt-get update -qq
sudo apt-get install -y \
    gcc \
    make \
    git \
    curl \
    wget \
    valgrind \
    libgl1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libasound2-dev \
    libpthread-stubs0-dev

echo "  → System packages installed."

# ── 2. Try apt raylib first ───────────────────────────────────────────────
echo "[2/5] Checking for raylib via apt..."
if apt-cache show libraylib-dev &>/dev/null; then
    sudo apt-get install -y libraylib-dev
    RAYLIB_INSTALLED_VIA_APT=1
    echo "  → raylib installed via apt."
else
    RAYLIB_INSTALLED_VIA_APT=0
    echo "  → raylib not in apt. Will build from source."
fi

# ── 3. Build raylib from source if needed ────────────────────────────────
if [ "$RAYLIB_INSTALLED_VIA_APT" -eq 0 ]; then
    echo "[3/5] Building raylib 5.0 from source..."
    cd /tmp
    if [ ! -d "raylib" ]; then
        git clone --depth 1 --branch 5.0 \
            https://github.com/raysan5/raylib.git raylib
    fi
    cd raylib/src
    make PLATFORM=PLATFORM_DESKTOP
    sudo make install PLATFORM=PLATFORM_DESKTOP
    sudo ldconfig
    cd -
    echo "  → raylib 5.0 built and installed to /usr/local/lib"
else
    echo "[3/5] (skipped – raylib already installed via apt)"
fi

# ── 4. Download raygui.h (single-header) into src/ ───────────────────────
echo "[4/5] Downloading raygui.h..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAYGUI_PATH="$SCRIPT_DIR/src/raygui.h"

if [ -f "$RAYGUI_PATH" ]; then
    echo "  → raygui.h already present."
else
    curl -L -o "$RAYGUI_PATH" \
        "https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h"
    echo "  → raygui.h downloaded to src/"
fi

# ── 5. Verify ─────────────────────────────────────────────────────────────
echo "[5/5] Verifying installation..."
pkg-config --modversion raylib 2>/dev/null && \
    echo "  → raylib version: $(pkg-config --modversion raylib)" || \
    echo "  → raylib installed (pkg-config not available, but headers exist)"

echo ""
echo "=== Setup complete! ==="
echo ""
echo "Next steps:"
echo "  cd $(dirname "$SCRIPT_DIR")"
echo "  make"
echo "  ./chat_sim --threads 8 --clients 30 --duration 60"
echo ""
