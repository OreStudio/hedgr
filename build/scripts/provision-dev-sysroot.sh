#!/usr/bin/env bash
#
# Provision a local development sysroot for third-party X11 and Wayland
# dependencies.
#
# Hedgr's vcpkg builds of glfw3 and glew need development headers and
# pkg-config files the host does not have, and sudo is unavailable, so we
# fetch the .deb files as an unprivileged user and unpack them into a private
# prefix under .cache/. The runtime libraries themselves are already
# installed; only the -dev packages and the wayland-scanner tool are missing.
#
# Covers:
#   * X11: Xinerama, Xcursor, Xrandr, Xi, Xext, Xrender, Xfixes, Xdamage
#     (glfw3's X11 backend)
#   * Wayland: wayland-client, wayland-cursor, wayland-egl, xkbcommon and the
#     wayland-scanner build tool (glfw3's Wayland backend)
#   * EGL (GLEW's EGL context support, required to initialise under Wayland)
#
# Re-run any time; it is idempotent.
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
sysroot="${repo_dir}/.cache/sysroot"
work="${repo_dir}/.cache/devdeb"

packages=(
    # X11
    libxinerama-dev
    libxcursor-dev
    libxrandr-dev
    libxi-dev
    libxext-dev
    libxrender-dev
    libxfixes-dev
    libxdamage-dev
    # Wayland
    libwayland-dev
    libwayland-bin
    libxkbcommon-dev
    # EGL
    libegl-dev
)

mkdir -p "${work}" "${sysroot}"
(cd "${work}" && apt-get download "${packages[@]}")

for f in "${work}"/*.deb; do
    dpkg-deb -x "${f}" "${sysroot}"
done

# The -dev packages ship only the unversioned .so symlink, pointing at a
# versioned library that lives in the (already installed) runtime package.
# Repoint each symlink at the system copy so the linker can resolve it.
libdir="${sysroot}/usr/lib/x86_64-linux-gnu"
libraries=(
    Xinerama Xcursor Xrandr Xi Xext Xrender Xfixes Xdamage
    wayland-client wayland-cursor wayland-egl xkbcommon EGL
)
for lib in "${libraries[@]}"; do
    target=$(ls /usr/lib/x86_64-linux-gnu/lib"${lib}".so.* 2>/dev/null | head -n 1)
    if [ -n "${target}" ]; then
        ln -sf "${target}" "${libdir}/lib${lib}.so"
    else
        echo "warning: no system runtime for lib${lib}" >&2
    fi
done

echo "Development sysroot ready at ${sysroot}"
