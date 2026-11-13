#!/usr/bin/env bash
#
# Build one or more targets in the Hedgr release build directory.
#
# The release directory is configured on first use with the private sysroot and
# the pre-built OreStudio, then reused. Adding source files regenerates the
# component file lists and re-runs CMake, so this is the only command needed
# after editing a component.
#
# Usage:
#     build-target.sh hedgr.store.tests
#     build-target.sh hedgr.model.lib hedgr.store.tests
#     build-target.sh            # builds everything
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# The client reads the market-data operations (list_instruments, get_rate_ladder,
# get_fx_montage, get_curve_analytics), which this checkout carries. A checkout
# without them fails to compile rather than failing at run time, which is the
# point: the header is the contract.
ores_source_dir=${ORES_SOURCE_DIR:-/home/marco/Development/OreStudio/ores_dev_prime_origin}
ores_build_dir=${ORES_BUILD_DIR:-${ores_source_dir}/build/output/linux-clang-release-ninja}
build_dir=${HEDGR_BUILD_DIR:-${repo_dir}/build/output/linux-clang-release}

sysroot="${repo_dir}/.cache/sysroot"
if [ -d "${sysroot}/usr/include" ]; then
    export PKG_CONFIG_PATH="${sysroot}/usr/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
    export CPATH="${sysroot}/usr/include${CPATH:+:${CPATH}}"
    export LIBRARY_PATH="${sysroot}/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:${LIBRARY_PATH}}"
    export CMAKE_INCLUDE_PATH="${sysroot}/usr/include${CMAKE_INCLUDE_PATH:+:${CMAKE_INCLUDE_PATH}}"
    export CMAKE_LIBRARY_PATH="${sysroot}/usr/lib/x86_64-linux-gnu${CMAKE_LIBRARY_PATH:+:${CMAKE_LIBRARY_PATH}}"
    export CMAKE_PREFIX_PATH="${sysroot}/usr${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
    export PATH="${sysroot}/usr/bin${PATH:+:${PATH}}"
fi

export VCPKG_DISABLE_METRICS=1

# Configure on every run, not only the first. A cache remembers which OreStudio
# checkout it was pointed at, and reusing one that disagrees with the checkout
# asked for here either fails to compile or, worse, compiles against an older
# API and fails at run time. cmake re-running is cheap; a stale cache is not.
if [ ! -f "${build_dir}/CMakeCache.txt" ]; then
    echo "configuring ${build_dir}"
else
    configured=$(sed -n 's/^ORES_SOURCE_DIR:PATH=//p' "${build_dir}/CMakeCache.txt")
    if [ "${configured}" != "${ores_source_dir}" ]; then
        echo "re-pointing ${build_dir} at ${ores_source_dir} (was ${configured})"
    fi
fi
cmake -S "${repo_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DORES_SOURCE_DIR="${ores_source_dir}" \
    -DORES_BUILD_DIR="${ores_build_dir}" \
    -DVCPKG_OVERLAY_PORTS="${repo_dir}/build/vcpkg-overlays" \
    -DVCPKG_BINARY_SOURCES="clear;files,${repo_dir}/.cache/vcpkg-binary,readwrite;files,${HOME}/.cache/vcpkg/archives,read" \
    > /dev/null

if [ "$#" -eq 0 ]; then
    cmake --build "${build_dir}"
else
    cmake --build "${build_dir}" --target "$@"
fi
