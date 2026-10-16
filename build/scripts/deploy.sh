#!/usr/bin/env bash
#
# Package the publish tree and deploy it to a host over SSH.
#
# The publish tree is already self-contained, because the build bundles the
# OreStudio libraries into it and the RUNPATH is $ORIGIN relative. So deploying
# is a copy and an unpack, and there is nothing to stage or rewrite on the way.
#
# The remote host gets a directory per version and a "current" symlink, so a
# deploy never overwrites a running version and a rollback is a symlink swap.
#
# Usage:
#     deploy.sh                  # package, copy, unpack, smoke test
#     deploy.sh goedel           # a different host
#     HEDGR_SKIP_BUILD=1 deploy.sh
#
# Environment:
#     HEDGR_DEPLOY_HOST   host to deploy to, default goedel
#     HEDGR_DEPLOY_DIR    directory on the host, default hedgr
#     HEDGR_SKIP_BUILD    set to 1 to deploy the existing package
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=${HEDGR_BUILD_DIR:-${repo_dir}/build/output/linux-clang-release}
host=${1:-${HEDGR_DEPLOY_HOST:-goedel}}
remote_dir=${HEDGR_DEPLOY_DIR:-hedgr}

if [ "${HEDGR_SKIP_BUILD:-0}" != "1" ]; then
    "${repo_dir}/build/scripts/build-target.sh" package_hedgr
fi

package=$(ls -t "${build_dir}"/hedgr-*.tar.gz 2>/dev/null | head -1 || true)
if [ -z "${package}" ]; then
    echo "error: no package in ${build_dir}; run without HEDGR_SKIP_BUILD=1" >&2
    exit 1
fi

version=$(basename "${package}" .tar.gz)
echo "deploying ${version} to ${host}:${remote_dir}"

# The desktop entry is what lets the shell name the window, so a package that
# lost it would deploy a client the app switcher still calls unknown. Check the
# archive rather than the build tree, because the build tree can hold a file the
# packaging step forgot to tar.
desktop_entry=publish/share/applications/org.hedgr.Hedgr.desktop
if ! tar -tzf "${package}" | grep -qx "${desktop_entry}"; then
    echo "error: ${package} has no ${desktop_entry}" >&2
    exit 1
fi

ssh "${host}" "mkdir -p '${remote_dir}'"
scp -q "${package}" "${host}:${remote_dir}/"

# Unpack beside the previous version rather than over it, so the machine keeps
# a version to fall back to and a running process keeps its files.
ssh "${host}" bash -s -- "${remote_dir}" "${version}" <<'REMOTE'
set -euo pipefail
remote_dir=$1
version=$2
cd "${remote_dir}"
rm -rf "${version}"
mkdir -p "${version}"
tar xzf "${version}.tar.gz" -C "${version}"
ln -sfn "${version}/publish" current
echo "unpacked into ${remote_dir}/${version}, current now points at it"
REMOTE

echo
echo "smoke test"
# The loader resolves every needed library before main runs, so a version that
# prints at all has proved that the bundle is complete. --version is handled by
# the argument parser and never opens a window, which is what makes this safe on
# a host with no display.
if ssh "${host}" "cd '${remote_dir}/current' && ./bin/hedgr.imgui --version"; then
    echo "the bundle resolves"
else
    echo "the bundle does not resolve. Missing libraries:" >&2
    ssh "${host}" "cd '${remote_dir}/current' && ldd bin/hedgr.imgui | grep 'not found'" >&2 || true
    exit 1
fi

echo
echo "deployed ${version} to ${host}:${remote_dir}/current"
