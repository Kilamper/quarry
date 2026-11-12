#!/usr/bin/env bash
# Idempotent macOS/Linux dev-environment setup: compiler, CMake, Ninja, Git, Python,
# aqtinstall (in a private venv), Qt (version from .qt-version) and vcpkg.
# Only installs what is missing. Re-running is safe.
#
# Usage: scripts/setup.sh [--non-interactive]
# Env:   QT_BASE_DIR (default ~/Qt, must match CMakePresets.json), VCPKG_ROOT (default ~/vcpkg)
set -euo pipefail

NON_INTERACTIVE=0
[[ "${1:-}" == "--non-interactive" ]] && NON_INTERACTIVE=1

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_VERSION="$(head -n1 "$REPO_ROOT/.qt-version" | tr -d '[:space:]')"
QT_BASE_DIR="${QT_BASE_DIR:-$HOME/Qt}"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
AQT_VENV="${XDG_CACHE_HOME:-$HOME/.cache}/quarry/aqt-venv"
MIN_CMAKE="3.24"
OS="$(uname -s)"
ARCH="$(uname -m)"
SUMMARY=()

step()    { printf '\033[36m==> %s\033[0m\n' "$*"; }
result()  { SUMMARY+=("$(printf '%-12s %-10s %s' "$1" "$2" "$3")"); }
have()    { command -v "$1" >/dev/null 2>&1; }
confirm() {
    [[ $NON_INTERACTIVE -eq 1 ]] && return 0
    read -r -p "$1 [y/N] " answer
    [[ "$answer" =~ ^([yYsS]|yes|si)$ ]]
}
version_ge() { [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" == "$2" ]]; }

# --- 1. System packages ---------------------------------------------------------
if [[ "$OS" == "Darwin" ]]; then
    step "Xcode Command Line Tools"
    if xcode-select -p >/dev/null 2>&1; then
        result "clang" "ok" "$(clang --version | head -n1)"
    else
        xcode-select --install || true
        echo "Finish the Xcode CLT installer dialog, then re-run this script."
        exit 1
    fi

    step "Homebrew packages"
    have brew || { echo "Homebrew not found: install it from https://brew.sh and re-run."; exit 1; }
    missing=()
    for pkg in cmake ninja git python autoconf autoconf-archive automake libtool; do
        brew list --formula "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    [[ ${#missing[@]} -gt 0 ]] && brew install "${missing[@]}"
    result "brew" "ok" "cmake ninja git python autotools"

    QT_HOST="mac"; QT_ARCH="clang_64"; QT_DIR_NAME="macos"
elif [[ "$OS" == "Linux" ]]; then
    step "System packages (needs sudo)"
    if have apt-get; then
        pkgs=(build-essential cmake ninja-build git python3-pip python3-venv curl zip unzip tar pkg-config
              autoconf autoconf-archive automake libtool
              libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libfontconfig1-dev libegl1
              libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1)
        missing=()
        for p in "${pkgs[@]}"; do dpkg -s "$p" >/dev/null 2>&1 || missing+=("$p"); done
        if [[ ${#missing[@]} -gt 0 ]]; then
            confirm "Run 'sudo apt-get install ${missing[*]}'?" || { echo "Aborted."; exit 1; }
            sudo apt-get update && sudo apt-get install -y "${missing[@]}"
        fi
    elif have dnf; then
        echo "Fedora detected (best effort; package names may need adjusting)."
        confirm "Run 'sudo dnf install' of the build and Qt runtime packages?" || { echo "Aborted."; exit 1; }
        sudo dnf install -y gcc-c++ cmake ninja-build git python3-pip curl zip unzip tar pkgconf \
            autoconf autoconf-archive automake libtool \
            mesa-libGL-devel libxkbcommon-devel libxkbcommon-x11 xcb-util-cursor fontconfig-devel mesa-libEGL \
            xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil
    elif have pacman; then
        echo "Arch detected (best effort)."
        confirm "Run 'sudo pacman -S --needed' of the build and Qt runtime packages?" || { echo "Aborted."; exit 1; }
        sudo pacman -S --needed --noconfirm base-devel cmake ninja git python python-pip curl zip unzip tar pkgconf \
            autoconf-archive \
            mesa libxkbcommon libxkbcommon-x11 xcb-util-cursor fontconfig xcb-util-wm xcb-util-image \
            xcb-util-keysyms xcb-util-renderutil
    else
        echo "Unsupported package manager: install gcc>=11, cmake>=$MIN_CMAKE, ninja, git, python3 manually."
    fi

    gcc_ver="$(gcc -dumpfullversion 2>/dev/null || echo 0)"
    if version_ge "$gcc_ver" "11"; then result "gcc" "ok" "$gcc_ver"; else result "gcc" "TOO OLD" "$gcc_ver (need >= 11)"; fi

    QT_HOST="linux"
    if [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
        QT_HOST="linux_arm64"; QT_ARCH="linux_gcc_arm64"; QT_DIR_NAME="gcc_arm64"
    else
        QT_ARCH="linux_gcc_64"; QT_DIR_NAME="gcc_64"
    fi
else
    echo "Unsupported OS: $OS (use scripts/setup.ps1 on Windows)"; exit 1
fi

# --- 2. CMake version (distro packages can be too old) ----------------------------
step "CMake >= $MIN_CMAKE"
cmake_ver="$(cmake --version 2>/dev/null | head -n1 | awk '{print $3}' || true)"
if [[ -z "$cmake_ver" ]] || ! version_ge "$cmake_ver" "$MIN_CMAKE"; then
    echo "System CMake '${cmake_ver:-none}' is too old; installing a user-level CMake from PyPI."
    python3 -m pip install --user --break-system-packages --upgrade cmake 2>/dev/null \
        || python3 -m pip install --user --upgrade cmake
    export PATH="$HOME/.local/bin:$PATH"
    cmake_ver="$(cmake --version | head -n1 | awk '{print $3}')"
    echo "Add \$HOME/.local/bin to PATH in your shell profile to keep this CMake."
fi
result "CMake" "ok" "$cmake_ver"
result "Ninja" "ok" "$(ninja --version)"
result "Git" "ok" "$(git --version | awk '{print $3}')"

# --- 3. aqtinstall in a private venv (avoids PEP 668 'externally-managed' errors) --
step "aqtinstall (pinned in scripts/requirements-aqt.txt)"
[[ -x "$AQT_VENV/bin/python" ]] || python3 -m venv "$AQT_VENV"
"$AQT_VENV/bin/python" -m pip install --disable-pip-version-check --quiet -r "$REPO_ROOT/scripts/requirements-aqt.txt"
result "aqtinstall" "ok" "$("$AQT_VENV/bin/python" -m pip show aqtinstall | awk '/^Version:/{print $2}')"

# --- 4. Qt ----------------------------------------------------------------------------
QT_DIR="$QT_BASE_DIR/$QT_VERSION/$QT_DIR_NAME"
step "Qt $QT_VERSION ($QT_ARCH)"
if [[ -d "$QT_DIR/lib/cmake/Qt6Quick" && -d "$QT_DIR/lib/cmake/Qt6ShaderTools" ]]; then
    result "Qt" "ok" "$QT_DIR"
else
    "$AQT_VENV/bin/python" -m aqt install-qt "$QT_HOST" desktop "$QT_VERSION" "$QT_ARCH" -m qtshadertools -O "$QT_BASE_DIR"
    result "Qt" "installed" "$QT_DIR"
fi

# --- 5. vcpkg ---------------------------------------------------------------------
step "vcpkg ($VCPKG_ROOT)"
[[ -f "$VCPKG_ROOT/.vcpkg-root" ]] || git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
[[ -x "$VCPKG_ROOT/vcpkg" ]] || "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
result "vcpkg" "ok" "$("$VCPKG_ROOT/vcpkg" version | head -n1 | awk '{print $NF}')"

# --- Summary ----------------------------------------------------------------------
printf '\n%s\n' "${SUMMARY[@]}"
preset="linux-debug"; [[ "$OS" == "Darwin" ]] && preset="macos-debug"
cat <<EOF

The script does not edit your shell profile. Add this line to it:
  export VCPKG_ROOT="$VCPKG_ROOT"
Then build:
  cmake --preset $preset && cmake --build --preset $preset && ctest --preset $preset
EOF
