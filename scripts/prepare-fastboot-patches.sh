#!/usr/bin/env bash
# Accept only the exact pinned fastbootd prefix and its complete reviewed stack.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec bash "$component/scripts/prepare-reviewed-patches.sh" "${1:?Pinned system/core source is required}" "${2:-apply}" fastboot
