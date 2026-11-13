#!/usr/bin/env bash
#
# Configure and build Hedgr for Linux.
#
# Hedgr consumes a pre-built OreStudio: its public headers come from the
# OreStudio source tree and its shared libraries from an OreStudio build
# output. Both are supplied here and can be overridden from the environment.
#
# The host ships the X11 and Wayland runtime libraries but neither set of -dev
# packages, and sudo is unavailable, so a private sysroot (see
# provision-dev-sysroot.sh) is layered in for the benefit of the vcpkg glfw3
# and glew builds. GLFW is built with both backends.
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

# The client reads the market-data operations (list_instruments, get_rate_ladder,
# get_fx_montage, get_curve_analytics), which this checkout carries. A checkout
# without them fails to compile rather than failing at run time, which is the
# point: the header is the contract.
ores_source_dir=${ORES_SOURCE_DIR:-/home/marco/Development/OreStudio/ores_dev_prime_origin}
ores_build_dir=${ORES_BUILD_DIR:-${ores_source_dir}/build/output/linux-clang-release-ninja}
build_dir=${repo_dir}/build/output/linux-clang-release

sysroot="${repo_dir}/.cache/sysroot"
if [ -d "${sysroot}/usr/include" ]; then
    export PKG_CONFIG_PATH="${sysroot}/usr/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    export CPATH="${sysroot}/usr/include${CPATH:+:${CPATH}}"
    export LIBRARY_PATH="${sysroot}/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:${LIBRARY_PATH}}"
    export CMAKE_INCLUDE_PATH="${sysroot}/usr/include${CMAKE_INCLUDE_PATH:+:${CMAKE_INCLUDE_PATH}}"
    export CMAKE_LIBRARY_PATH="${sysroot}/usr/lib/x86_64-linux-gnu${CMAKE_LIBRARY_PATH:+:${CMAKE_LIBRARY_PATH}}"
    export CMAKE_PREFIX_PATH="${sysroot}/usr${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
    # wayland-scanner (from libwayland-bin) is a build-time tool GLFW needs.
    export PATH="${sysroot}/usr/bin${PATH:+:${PATH}}"
else
    echo "warning: ${sysroot} not found; run provision-dev-sysroot.sh if glfw3 fails to build" >&2
fi

export VCPKG_DISABLE_METRICS=1

cmake -S "${repo_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DORES_SOURCE_DIR="${ores_source_dir}" \
    -DORES_BUILD_DIR="${ores_build_dir}" \
    -DVCPKG_OVERLAY_PORTS="${repo_dir}/build/vcpkg-overlays" \
    -DVCPKG_BINARY_SOURCES="clear;files,${repo_dir}/.cache/vcpkg-binary,readwrite;files,${HOME}/.cache/vcpkg/archives,read"

cmake --build "${build_dir}"

echo
echo "Built: ${build_dir}/publish/bin/hedgr.imgui"
