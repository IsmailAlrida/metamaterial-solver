#!/usr/bin/env bash
set -euo pipefail

mode=${1:-install}
packages=(
    build-essential
    git
    curl
    cmake
    ninja-build
    pkg-config
    gfortran
    openmpi-bin
    libopenmpi-dev
    libopenblas-dev
    liblapack-dev
    libgl1-mesa-dev
    libx11-dev
    libxext-dev
    libxrandr-dev
    libxinerama-dev
    libxcursor-dev
    libxi-dev
    mesa-utils
)

usage() {
    echo "Usage: ./setup.sh [install|check|help]"
}

check() {
    local missing=0
    local command
    for command in git cmake ninja c++ gfortran mpiexec mpicc mpicxx mpifort; do
        if command -v "$command" >/dev/null; then
            echo "[ok] $command: $(command -v "$command")"
        else
            echo "[missing] $command"
            missing=$((missing + 1))
        fi
    done

    if command -v cmake >/dev/null &&
       [[ $(printf '%s\n' 3.25 "$(cmake --version | awk 'NR == 1 { print $3 }')" | sort -V | head -n1) != 3.25 ]]; then
        echo "[missing] CMake 3.25 or newer"
        missing=$((missing + 1))
    fi

    for package in "${packages[@]}"; do
        if dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed'; then
            echo "[ok] $package"
        else
            echo "[missing] $package"
            missing=$((missing + 1))
        fi
    done

    (( missing == 0 )) || {
        echo "$missing prerequisite(s) missing. Run ./setup.sh install." >&2
        return 1
    }
    echo "All Linux native prerequisites are available."
}

case $mode in
    install)
        command -v apt-get >/dev/null || {
            echo "setup.sh currently supports Debian and Ubuntu through apt." >&2
            exit 1
        }
        missing_packages=()
        for package in "${packages[@]}"; do
            dpkg-query -W -f='${Status}' "$package" 2>/dev/null |
                grep -q 'install ok installed' || missing_packages+=("$package")
        done
        if (( ${#missing_packages[@]} )); then
            if (( EUID == 0 )); then
                apt-get update
                apt-get install -y "${missing_packages[@]}"
            else
                sudo apt-get update
                sudo apt-get install -y "${missing_packages[@]}"
            fi
        else
            echo "Linux build prerequisites are already installed."
        fi
        check
        ;;
    check)
        check
        ;;
    help)
        usage
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
