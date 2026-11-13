#!/usr/bin/env bash
#
# Screenshot a prototype with Playwright.
#
#     prototypes/capture.sh <file.html> [out.png]
#
# The tooling lives under build/proto-tools because it is a developer aid and
# not part of the product; this script is the one entry point so the command
# does not have to be remembered. It launches the system Chrome by channel,
# because the Playwright browser cache on this machine holds a build this
# Playwright release does not expect.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tools="${repo}/build/proto-tools"
input="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
output="${2:-${input%.html}.png}"

if [ ! -d "${tools}/node_modules/playwright" ]; then
    mkdir -p "${tools}" "${repo}/build/npm-cache"
    npm install --prefix "${tools}" --cache "${repo}/build/npm-cache" \
        --no-audit --no-fund playwright
fi

node "${tools}/shoot.js" "${input}" "${output}"
