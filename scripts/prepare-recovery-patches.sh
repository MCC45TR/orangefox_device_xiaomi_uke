#!/usr/bin/env bash
# Accept only the exact pinned recovery prefix and complete reviewed patch stack.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
exec bash "$component/scripts/prepare-reviewed-patches.sh" "${1:?Pinned active recovery source is required}" "${2:-apply}" recovery
