#!/usr/bin/env bash
# Host-only extraction of boot/partition metadata. Never runs stock flash scripts.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
profile=${1:?Usage: extract-boot-reference.sh PROFILE_ID}
manifest="$component/manifests/firmware.lock.json"
entry=$(jq -ce --arg id "$profile" '.profiles[]|select(.id==$id)' "$manifest") || { echo 'Unknown firmware profile' >&2; exit 2; }
url=$(jq -r .url <<<"$entry")
region=$(jq -r .region <<<"$entry" | tr '[:upper:]' '[:lower:]')
raw="$component/referances/firmware/$region/raw/${url##*/}"
[[ -f $raw ]] || { echo 'Verified stock archive is missing' >&2; exit 1; }
expected_sha=$(jq -r .sha256 <<<"$entry")
[[ $expected_sha =~ ^[0-9a-f]{64}$ ]] || { echo 'Firmware manifest lacks a verified SHA-256' >&2; exit 1; }
[[ $(sha256sum "$raw" | cut -d' ' -f1) == "$expected_sha" ]] || { echo 'Stock archive checksum mismatch' >&2; exit 1; }
mkdir -p "$component/reports/private" "$component/referances/firmware/$region/derived"
index="$component/reports/private/$profile-tar-index.txt"
tar -tf "$raw" > "$index"
awk '/(^\/|(^|\/)\.\.\/|(^|\/)\.\/)/ {bad=1} END {exit bad}' "$index" || { echo 'Unsafe path in firmware archive' >&2; exit 1; }
mapfile -t selected < <(rg '/images/(boot|init_boot|vendor_boot|recovery|rescue|dtbo|vbmeta|vbmeta_system)\.img$|/images/(gpt_(main|backup|both)[0-5]\.bin|rawprogram[0-5]\.xml|patch[0-5]\.xml)$' "$index")
((${#selected[@]} >= 25)) || { echo 'Expected boot/partition metadata entries missing' >&2; exit 1; }
dest="$component/referances/firmware/$region/derived"
tar -xzf "$raw" -C "$dest" --no-same-owner --no-same-permissions --no-overwrite-dir -- "${selected[@]}"
find "$dest" -type l -print -quit | rg -q . && { echo 'Unexpected symlink in extracted reference' >&2; exit 1; }
report="$component/reports/private/$profile-extraction.tsv"
printf 'source_sha256\t%s\ncommand\t%s\n' "$expected_sha" 'extract-boot-reference.sh' > "$report"
for member in "${selected[@]}"; do
  file="$dest/$member"
  printf '%s\t%s\t%s\n' "${file#"$component/"}" "$(stat -c %s "$file")" "$(sha256sum "$file" | cut -d' ' -f1)" >> "$report"
done
printf '%s: extracted %d boot and partition entries; checksums recorded locally\n' "$profile" "${#selected[@]}"
