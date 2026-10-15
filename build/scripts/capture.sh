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
# A frame on its own shows what the interface drew first, and a tree that cannot
# be expanded draws first just as convincingly as one that can. Two variables
# drive the interface instead, so the frame shows the result of an interaction:
#
#     HEDGR_CAPTURE_INPUT     the gestures, one per line, as
#                             "<gesture> <row> [label]", where the gestures are
#                             click, click_arrow, expand, collapse and focus.
#                             Rows are named as an operator sees them.
#     HEDGR_CAPTURE_FOCUS     the id of the window the frame is about, which is
#                             brought to the front for it. Windows float, and a
#                             frame of a window something else covers shows the
#                             wrong surface without saying so.
#
# The client's own output, including the capture hook's complaints about a step
# that named nothing, lands in <directory>/client.log.
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

# The windows are laid out from an ini file this script writes, rather than from
# a file beside the binary, because a capture is about the rows and not about
# where a window happened to be left. Two things matter in the fixture and both
# are about being able to read the frame: the Instances window is big enough for
# its tree and its list to be side by side, and it is written last so that ImGui
# draws it over the windows that open on top of it. A fresh file each run also
# makes two runs' frames comparable, since ImGui writes the layout back on exit.
#
# The password the seeder sealed the store with, presented without a person.
# It is a capture fixture and not a secret: it exists only in the store this
# script has just made under the directory it was given.
cat > "${data_dir}/imgui.ini" <<'LAYOUT'
[Window][accounts]
Pos=700,560
Size=1180,470
LastUsed=1

[Window][workspace]
Pos=560,300
Size=700,460
LastUsed=1

[Window][instance-manager]
Pos=24,24
Size=1860,1010
LastUsed=2
LAYOUT

export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-0}
export HEDGR_IMGUI_INI="${data_dir}/imgui.ini"
XDG_DATA_HOME="${data_dir}" \
    HEDGR_CAPTURE="${capture}" \
    HEDGR_CAPTURE_FRAME="${frame}" \
    HEDGR_MASTER_PASSWORD="${HEDGR_MASTER_PASSWORD:-Secure-Password-123}" \
    "${repo_dir}/build/scripts/run.sh" --log-enabled --log-level info \
    > "${data_dir}/client.log" 2>&1

echo "captured ${capture}"
