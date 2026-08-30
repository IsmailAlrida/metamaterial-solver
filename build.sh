#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

mode=build
backend=${1:-serial}
build_type=Release
cuda_arch=
glvis_revision=1b9988ade7b78f125377a3be5c2b8514eafbcf0c

if [[ $backend == deps ]]; then
    mode=deps
    shift
    backend=${1:-serial}
    if (( $# )); then shift; fi
else
    if (( $# )); then shift; fi
fi

for option in "$@"; do
    case $option in
        debug) build_type=Debug ;;
        release) build_type=Release ;;
        *)
            if [[ -n $cuda_arch ]]; then
                echo "Unexpected build option: $option" >&2
                exit 1
            fi
            cuda_arch=$option
            ;;
    esac
done

case $backend in
    serial|serial-cuda|parallel-cpu|parallel-cpu-cuda) ;;
    *)
        echo "Usage: ./build.sh [serial|serial-cuda|parallel-cpu|parallel-cpu-cuda] [CUDA architecture] [debug]" >&2
        echo "       ./build.sh deps [serial|serial-cuda|parallel-cpu|parallel-cpu-cuda] [CUDA architecture] [debug]" >&2
        exit 1
        ;;
esac

for command in cmake ninja git c++; do
    command -v "$command" >/dev/null || {
        echo "$command is required but was not found on PATH." >&2
        exit 1
    }
done

if [[ $backend == *cuda* ]]; then
    command -v nvcc >/dev/null || {
        echo "CUDA Toolkit not found: nvcc is not on PATH." >&2
        exit 1
    }
    cuda_arch=${cuda_arch:-native}
elif [[ -n $cuda_arch ]]; then
    echo "A CUDA architecture can only be supplied for a CUDA backend." >&2
    exit 1
fi

build_dir="build/linux/$backend"
[[ $build_type == Debug ]] && build_dir+="-debug"
deps_source_dir="$PWD/build/linux/deps/src"
glvis_source_dir="$deps_source_dir/glvis-src"

echo "Backend: $backend"
echo "Build type: $build_type"
echo "Build dir: $build_dir"
echo "Deps source dir: $deps_source_dir"
echo "GLVis source dir: $glvis_source_dir"
[[ -n $cuda_arch ]] && echo "CUDA architecture: $cuda_arch"

mkdir -p "$deps_source_dir"
if [[ ! -d $glvis_source_dir/.git ]]; then
    echo "Fetching GLVis source into $glvis_source_dir..."
    git clone https://github.com/GLVis/glvis.git "$glvis_source_dir"
else
    echo "GLVis source already exists at $glvis_source_dir."
fi
git -C "$glvis_source_dir" checkout --detach "$glvis_revision"

glvis_patch="$PWD/patches/glvis-embedded-window.patch"
if ! git -C "$glvis_source_dir" apply --reverse --check "$glvis_patch" >/dev/null 2>&1; then
    git -C "$glvis_source_dir" apply --check "$glvis_patch" || {
        echo "GLVis host-window patch does not apply to the downloaded revision." >&2
        exit 1
    }
    git -C "$glvis_source_dir" apply "$glvis_patch"
fi

cmake_args=(
    -S .
    -B "$build_dir"
    -G Ninja
    -DCMAKE_BUILD_TYPE="$build_type"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DMETAMATERIAL_DEMO_MODE=OFF
    -DMETAMATERIAL_MFEM_BACKEND="$backend"
    -DMETAMATERIAL_DEPS_SOURCE_DIR="$deps_source_dir"
    -DGLVIS_SOURCE_DIR="$glvis_source_dir"
)
[[ -n $cuda_arch ]] && cmake_args+=("-DCMAKE_CUDA_ARCHITECTURES=$cuda_arch")

cmake "${cmake_args[@]}"
cp "$build_dir/compile_commands.json" compile_commands.json

if [[ $mode == deps ]]; then
    echo "LSP configuration completed for $backend. The app was not built."
    exit 0
fi

cmake --build "$build_dir"
