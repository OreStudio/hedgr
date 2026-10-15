#!/usr/bin/env bash
#
# Capture one frame of the client from a store that already has rows in it.
#
# A screenshot is how the interface is verified: a compile is not evidence that
# a window draws. A fresh store opens on the create-master-password prompt, so a
# capture of anything behind the lock needs a store that has been seeded and a
# password the client can present without a person typing it. This script does
# both, in the order the store requires.
#
# The rows come from the runtime's own seeder (hedgr.runtime.tests "[seed]"),
# which writes them through the manager that owns the schema and the sealing.
# Writing them with sqlite3 from here would be a second description of the same
# tables, and the one that drifts is the one that makes a capture lie.
#
# Usage:
#     capture.sh <directory> <frame.ppm> [frame-number]
#
# The directory is created fresh, so two runs never see each other's rows. The
# frame number defaults to 60: the store's key derivation is deliberately slow
# and a capture of a surface behind it is not on screen by ImGui's usual eighth
# frame.
#
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=${HEDGR_BUILD_DIR:-${repo_dir}/build/output/linux-clang-release}

if [ $# -lt 2 ]; then
    echo "usage: capture.sh <directory> <frame.ppm> [frame-number]" >&2
    exit 1
fi

data_dir=$1
capture=$2
frame=${3:-60}

tests=${build_dir}/publish/bin/hedgr.runtime.tests
if [ ! -x "${tests}" ]; then
    echo "error: ${tests} not found; run build/scripts/build-target.sh first" >&2
    exit 1
fi

rm -rf "${data_dir}"
mkdir -p "${data_dir}"

# Exported rather than passed inline so the seeder and the client below
# seal and open the store with the same password.
export HEDGR_MASTER_PASSWORD="${HEDGR_MASTER_PASSWORD:-Secure-Password-123}"

HEDGR_SEED_DB="${data_dir}/hedgr/hedgr.db" \
    "${tests}" "[seed]" > /dev/null

# The windows are laid out by the client's own imgui.ini rather than by anything
# here, because that is where a window's position lives. The checked-in file
# carries a sensible size for each surface; an operator who wants a different
# arrangement edits it as they would in any ImGui application, and ImGui writes
# the result back.
# The password the seeder sealed the store with, presented without a person.
# It is a capture fixture and not a secret: it exists only in the store this
# script has just made under the directory it was given.
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
XDG_DATA_HOME="${data_dir}" \
    HEDGR_CAPTURE="${capture}" \
    HEDGR_CAPTURE_FRAME="${frame}" \
    HEDGR_MASTER_PASSWORD="${HEDGR_MASTER_PASSWORD:-Secure-Password-123}" \
    "${repo_dir}/build/scripts/run.sh"

echo "captured ${capture}"
