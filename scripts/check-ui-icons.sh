#!/usr/bin/env bash
# Host-only raster provenance check. No graphics tools run in recovery.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
assets="$component/src/device/xiaomi/uke/ui-icons"
sources="$component/referances/lucide-0.563.0"
work=$(mktemp -d "$component/build/icon-check-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
[[ $(jq -r .revision "$assets/ORIGINS.json") == e9e060a851a44ab9c33e296caeec9c6c778c51d1 ]]
cmp "$assets/LICENSE" "$sources/LICENSE"
while read -r name svg_sha png_sha; do
    [[ $name =~ ^[a-z][a-z0-9-]+$ ]]
    [[ $(sha256sum "$sources/icons/$name.svg" | cut -d' ' -f1) == "$svg_sha" ]]
    [[ $(sha256sum "$assets/$name.png" | cut -d' ' -f1) == "$png_sha" ]]
    sed 's/currentColor/#FFFFFF/g' "$sources/icons/$name.svg" > "$work/$name.svg"
    magick -background none "$work/$name.svg" -resize 72x72 -strip "PNG32:$work/$name.png"
    # PNG metadata timestamps differ; verify actual RGBA pixels independently.
    cmp <(magick "$assets/$name.png" -depth 8 rgba:-) <(magick "$work/$name.png" -depth 8 rgba:-)
done < <(jq -r '.icons[]|"\(.name) \(.svg_sha256) \(.png_sha256)"' "$assets/ORIGINS.json")
echo 'Pinned Lucide sources, license, committed PNG hashes and independently rasterized RGBA pixels match.'
