#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${CITY_HOST_BUILD_DIR:-${repo_root}/build/host}"

cmake \
    -S "${repo_root}/tests" \
    -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCITY_ENABLE_SANITIZERS="${CITY_ENABLE_SANITIZERS:-ON}"
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure
