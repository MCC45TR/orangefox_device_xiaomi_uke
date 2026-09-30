#!/usr/bin/env bash
# Audit bounded embedded ZIPs after extraction. Never run archive entries.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
payload=${1:?Usage: check-nested-payloads.sh RAMDISK_ROOT}
[[ -d $payload && ! -L $payload ]] || exit 2
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
count=0
while IFS= read -r -d '' archive; do
    count=$((count+1))
    unzip -t "$archive" >/dev/null
    bytes=$(unzip -l "$archive" | tail -n1 | awk '{print $1}')
    [[ $bytes =~ ^[0-9]+$ && $bytes -le 16777216 ]] || { echo 'Oversized embedded ZIP' >&2; exit 1; }
    if unzip -Z -l "$archive" | grep -E '^l' >/dev/null; then
        echo 'Embedded ZIP symlink rejected' >&2; exit 1
    fi
    while IFS= read -r member; do
        case "$member" in
            /*|*\\*|../*|*/../*|*/..|[A-Za-z]:*) echo 'Embedded ZIP traversal rejected' >&2; exit 1;;
        esac
    done < <(unzip -Z -1 "$archive")
    extracted="$scratch/$count"
    mkdir "$extracted"
    unzip -q "$archive" -d "$extracted"
    "$component/tests/check-payload.sh" "$extracted" >/dev/null
done < <(find "$payload" -type f -name '*.zip' -print0)
printf 'Embedded ZIP integrity, bounded extraction, privacy and Python scans passed: %s archives.\n' "$count"
