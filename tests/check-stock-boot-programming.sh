#!/usr/bin/env bash
# Independent shell reconstruction; no device target or OEM script execution.
set -euo pipefail
component=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
inputs="$component/referances/firmware/global/derived/stock-payloads-global-os3.0.303.0/uke_global_images_OS3.0.303.0.WOZMIXM_16.0/images"
bash "$component/scripts/describe-stock-boot-programming.sh" "$inputs" "$work/catalog.json"
cmp "$work/catalog.json" "$component/manifests/stock-boot-programming-global.json"
if bash "$component/scripts/describe-stock-boot-programming.sh" "$inputs" "$work/catalog.json"; then
    echo 'Existing catalog was unexpectedly overwritten' >&2; exit 1
fi
echo 'Canonical boot programming catalog and fresh-output refusal passed.'
