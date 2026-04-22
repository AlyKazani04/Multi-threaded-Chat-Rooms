#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# setup.sh  –  Install all dependencies for OS-Level Chat Server
# CS2006 Operating Systems  |  Spring 2026
#
# Works on Ubuntu/Debian and Arch Linux.
# Run ONCE before building:
#   chmod +x setup.sh
#   ./setup.sh
# ─────────────────────────────────────────────────────────────────────────────

set -e   # exit on first error

echo "=== CS2006 Chat Sim – Dependency Setup ==="
echo ""

# ── Detect distribution and package manager ───────────────────────────────
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO_ID="$ID"
        DISTRO_LIKE="$ID_LIKE"
    else
        DISTRO_ID="unknown"
    fi

    # Check for package manager availability
    if command -v apt-get &>/dev/null; then
        PKG_MANAGER="apt"
        INSTALL_CMD="sudo apt-get install -y"
        UPDATE_CMD="sudo apt-get update -qq"
    elif command -v pacman &>/dev/null; then
        PKG_MANAGER="pacman"
        INSTALL_CMD="sudo pacman -S --noconfirm --needed"
        UPDATE_CMD="sudo pacman -Sy --noconfirm"
    else
        echo "ERROR: Unsupported package manager. Only apt and pacman are supported."
        exit 1
    fi

    echo "Detected distribution: ${DISTRO_ID} (using ${PKG_MANAGER})"
    echo ""
}

# ── Install system packages ────────────────────────────────────────────────
install_packages() {
    echo "[1/5] Installing system packages..."

    # Update package database
    $UPDATE_CMD

    if [ "$PKG_MANAGER" = "apt" ]; then
        $INSTALL_CMD \
            gcc make git curl wget valgrind \
            libgl1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
            libasound2-dev \
            libpthread-stubs0-dev
    elif [ "$PKG_MANAGER" = "pacman" ]; then
        $INSTALL_CMD \
            base-devel git curl wget valgrind \
            mesa \
            libx11 libxrandr libxinerama libxcursor libxi \
            alsa-lib
        # Note: pthread is part of glibc, no separate package needed.
    fi

    echo "  → System packages installed."
}

# ── Install raylib ─────────────────────────────────────────────────────────
install_raylib() {
    echo "[2/5] Installing raylib..."

    RAYLIB_INSTALLED_VIA_PKG=0

    if [ "$PKG_MANAGER" = "apt" ]; then
        if apt-cache show libraylib-dev &>/dev/null; then
            $INSTALL_CMD libraylib-dev
            RAYLIB_INSTALLED_VIA_PKG=1
            echo "  → raylib installed via apt."
        else
            echo "  → raylib not in apt. Will build from source."
        fi
    elif [ "$PKG_MANAGER" = "pacman" ]; then
        # Arch has raylib in community repo
        if pacman -Si raylib &>/dev/null; then
            $INSTALL_CMD raylib
            RAYLIB_INSTALLED_VIA_PKG=1
            echo "  → raylib installed via pacman."
        else
            echo "  → raylib not in pacman repos. Will build from source."
        fi
    fi

    # If not installed via package manager, build from source
    if [ "$RAYLIB_INSTALLED_VIA_PKG" -eq 0 ]; then
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
        echo "[3/5] (skipped – raylib already installed via package manager)"
    fi
}

# ── Download raygui.h ──────────────────────────────────────────────────────
download_raygui() {
    echo "[4/5] Downloading raygui.h..."
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    RAYGUI_PATH="$SCRIPT_DIR/src/raygui.h"

    if [ -f "$RAYGUI_PATH" ]; then
        echo "  → raygui.h already present."
    else
        mkdir -p "$SCRIPT_DIR/src"
        curl -L -o "$RAYGUI_PATH" \
            "https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h"
        echo "  → raygui.h downloaded to src/"
    fi
}

# ── Verify installation ────────────────────────────────────────────────────
verify_install() {
    echo "[5/5] Verifying installation..."

    # Check raylib version via pkg-config
    if pkg-config --modversion raylib &>/dev/null; then
        echo "  → raylib version: $(pkg-config --modversion raylib)"
    elif [ -f /usr/local/lib/libraylib.so ] || [ -f /usr/lib/libraylib.so ]; then
        echo "  → raylib library found (pkg-config not available)."
    else
        echo "  → WARNING: raylib library not found. Build may fail."
    fi

    # Check for raygui.h
    if [ -f "$RAYGUI_PATH" ]; then
        echo "  → raygui.h present."
    else
        echo "  → WARNING: raygui.h missing. Please place it in src/."
    fi
}

# ── Main ───────────────────────────────────────────────────────────────────
main() {
    detect_distro
    install_packages
    install_raylib
    download_raygui
    verify_install

    echo ""
    echo "=== Setup complete! ==="
    echo ""
    echo "Next steps:"
    echo "  cd $(dirname "$SCRIPT_DIR")"
    echo "  make"
    echo "  ./chat_sim --threads 8 --clients 30 --duration 60"
    echo ""
}

main "$@"
