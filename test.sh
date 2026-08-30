#!/usr/bin/env bash
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

backend=${1:-serial}
build_type=Release
case ${2:-release} in
    debug) build_type=Debug ;;
    release) ;;
    *)
        echo "Usage: ./test.sh [serial|serial-cuda|parallel-cpu|all] [debug|release]" >&2
        exit 1
        ;;
esac

run_tests() {
    local test_backend=$1
    local build_dir="build/linux/$test_backend"
    [[ $build_type == Debug ]] && build_dir+="-debug"
    cmake \
        -DBUILD_DIR:PATH="$PWD/$build_dir" \
        -DBACKEND="$test_backend" \
        -DBUILD_TYPE="$build_type" \
        -DREPORT_FILE:FILEPATH="$PWD/test-results/tests.json" \
        -P tests/run_tests.cmake
}

case $backend in
    serial|serial-cuda|parallel-cpu)
        run_tests "$backend"
        ;;
    all)
        result=0
        for test_backend in serial serial-cuda parallel-cpu; do
            run_tests "$test_backend" || result=$?
        done
        exit "$result"
        ;;
    *)
        echo "Usage: ./test.sh [serial|serial-cuda|parallel-cpu|all] [debug|release]" >&2
        exit 1
        ;;
esac
