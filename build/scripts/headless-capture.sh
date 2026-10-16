#!/usr/bin/env bash
#
# Capture one frame of the client against a headless compositor.
#
# A capture is an unattended photograph, and the client used to take it on
# whatever display the person running the test was using. That mapped a window
# on the desk, took the focus away from them while the frame was drawn, and made
# the answer depend on their session. This wrapper puts the client in front of a
# headless Weston on a socket of its own instead, so the same frame is taken
# every time and the operator's session is never touched.
#
# Usage:
#     headless-capture.sh <directory> <frame.ppm> [frame-number]
#
# The arguments are capture.sh's arguments and mean the same thing, so an
# existing invocation keeps its meaning when it is run through this wrapper.
# HEDGR_CAPTURE_FOCUS and HEDGR_CAPTURE_INPUT_FILE are read from this script's
# environment and handed to the client unchanged, which is what drives the
# scripted gestures.
#
# The compositor is started when nothing is listening on the socket and stopped
# again on the way out, including when the capture fails. A compositor that this
# script did not start is reused and left running, and the run says so rather
# than killing something it does not own.
#
# The runtime directory lives under the checkout as tmp/wl because the sandbox
# this runs in gives no write access to /run/user, which is where a Wayland
# socket would normally go. It holds only this compositor's socket and its log
# and it is ignored by git, so nothing of the operator's is touched.
#
# The compositor's output is 1920 by 1080 because that is the size every check
# in this project reads a frame against. The client asks the output for its mode
# in capture mode and creates its window at that size, so a compositor started
# at any other size would write a frame the checks cannot read.
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
runtime_dir=${repo_dir}/tmp/wl
socket_name=hedgr-test
socket_path=${runtime_dir}/${socket_name}
weston_log=${runtime_dir}/weston.log

if [ $# -lt 2 ]; then
    echo "usage: headless-capture.sh <directory> <frame.ppm> [frame-number]" >&2
    exit 1
fi

started_compositor=0
compositor_pid=

cleanup() {
    local status=$?
    trap - EXIT INT TERM
    if [ "${started_compositor}" -eq 1 ] && [ -n "${compositor_pid}" ]; then
        kill "${compositor_pid}" 2>/dev/null || true
        wait "${compositor_pid}" 2>/dev/null || true
    fi
    exit "${status}"
}
trap cleanup EXIT INT TERM

# A live compositor holds an exclusive lock on the socket's lock file, so a lock
# that cannot be taken here means something is already listening and this run
# must reuse it. A stale socket left by a compositor that died leaves the lock
# free, and is removed below before a new one is started.
compositor_is_listening() {
    [ -S "${socket_path}" ] || return 1
    if flock -n "${socket_path}.lock" -c true 2>/dev/null; then
        return 1
    fi
    return 0
}

mkdir -p "${runtime_dir}"
chmod 700 "${runtime_dir}"

if compositor_is_listening; then
    echo "reusing the headless compositor on ${socket_path}" >&2
else
    echo "starting a headless compositor on ${socket_path}" >&2
    rm -f "${socket_path}" "${socket_path}.lock"
    XDG_RUNTIME_DIR="${runtime_dir}" weston \
        --backend=headless-backend.so --width=1920 --height=1080 \
        --socket="${socket_name}" --idle-time=0 \
        > "${weston_log}" 2>&1 &
    compositor_pid=$!
    started_compositor=1

    for _ in $(seq 1 100); do
        [ -S "${socket_path}" ] && break
        if ! kill -0 "${compositor_pid}" 2>/dev/null; then
            echo "error: the headless compositor exited before its socket appeared;" >&2
            echo "error: see ${weston_log}" >&2
            exit 1
        fi
        sleep 0.1
    done
    if [ ! -S "${socket_path}" ]; then
        echo "error: the headless compositor did not create ${socket_path}" >&2
        exit 1
    fi
fi

# Wayland is named explicitly and X11 is taken out of the environment, so a
# failure to reach the headless compositor cannot quietly fall back to the
# operator's own display and put a window on it.
XDG_RUNTIME_DIR="${runtime_dir}" \
WAYLAND_DISPLAY="${socket_name}" \
DISPLAY= \
    "${repo_dir}/build/scripts/capture.sh" "$@"
