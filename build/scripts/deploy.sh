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
# The listing is read to the end rather than stopped at the first match,
# because grep -q closes the pipe, tar then dies of SIGPIPE, and pipefail turns
# a successful match into a failed pipeline.
if ! tar -tzf "${package}" | grep -Fx "${desktop_entry}" >/dev/null; then
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

# A shell scans its own applications directory, not the one inside the bundle,
# so the entry has to be copied out of the tree that was just unpacked. It is
# copied rather than symlinked, because removing a version would then leave a
# launcher pointing at a file that is gone, and it is overwritten so a redeploy
# carries the current Exec with it.
applications_dir="${HOME}/.local/share/applications"
mkdir -p "${applications_dir}"
entry=org.hedgr.Hedgr.desktop
install -Dm644 "current/share/applications/${entry}" "${applications_dir}/${entry}"

# Exec=hedgr.imgui assumes the binary resolves on PATH, and the bundle never
# puts bin on PATH, so the launcher would find nothing to run. An absolute path
# is written in rather than a wrapper in ~/.local/bin, because that keeps the
# launcher to one file and lets it follow the current symlink this script
# already moves. The path belongs to the remote, so it is resolved here instead
# of being baked in at build time.
# Only the command is rewritten. The arguments the entry carries, the shared
# logging flags among them, are the entry's own and a replacement of the whole
# line would drop every one of them.
sed -i "s|^Exec=hedgr.imgui|Exec=${PWD}/current/bin/hedgr.imgui|" \
    "${applications_dir}/${entry}"

# A shell can read the desktop database rather than the directory, so refresh
# it when the host has the tool. The entry is still in a directory a shell
# scans without it, so a host that lacks the tool must not fail the deploy.
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${applications_dir}" || true
fi

# A deploy that leaves the old process running leaves the operator looking at a
# version that is no longer on disk, which is the one thing a deploy exists to
# change. The old process is stopped and the new one started, so what is on the
# screen afterwards is what was just unpacked.
#
# The replacement is detached and its output redirected, because a process
# started from an ssh command dies when that command's session ends and its
# output would otherwise go nowhere. The display variables are passed
# explicitly rather than inherited: an ssh session has no display of its own,
# so a client started without them opens nothing and exits, which looks exactly
# like a deploy that worked. Set HEDGR_DEPLOY_NO_RESTART to leave the machine
# alone, for a deploy to a host nobody is sitting in front of.
if [ -n "${HEDGR_DEPLOY_NO_RESTART:-}" ]; then
    echo "not restarting: HEDGR_DEPLOY_NO_RESTART is set"
else
    pkill -x hedgr.imgui >/dev/null 2>&1 || true
    systemctl --user reset-failed hedgr.service >/dev/null 2>&1 || true
    sleep 1

    # Started through the user's own systemd manager rather than straight from
    # this script. An ssh session is not the desktop session: it has no display
    # of its own, and it cannot reach the session bus or the audio server, so a
    # process launched from here gets a window at best and silence at worst. A
    # transient unit runs inside the session the desktop is already in, with
    # the environment that session has, which is where the compositor and the
    # sound server both expect a client to be.
    systemd-run --user --unit=hedgr --collect \
        "${PWD}/current/bin/hedgr.imgui" \
        --log-enabled --log-filename hedgr.imgui.log \
        --log-directory "${HOME}" >/dev/null 2>&1 || \
        echo "could not start the client as a user unit" >&2

    sleep 3

    if pgrep -x hedgr.imgui >/dev/null 2>&1; then
        echo "restarted the client in the desktop session"
    else
        echo "the client did not come up, see ~/hedgr.imgui.log" >&2
    fi
fi

echo "unpacked into ${remote_dir}/${version}, current now points at it"
echo "installed ${entry} into ${applications_dir}"
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
