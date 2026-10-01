#!/usr/bin/env bash
# Audit bounded embedded ZIPs after extraction. Never run archive entries.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
payload=${1:?Usage: check-nested-payloads.sh RAMDISK_ROOT}
[[ -d $payload && ! -L $payload ]] || exit 2
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
count=0
total_bytes=0
is_zip() {
    case "$1" in *.zip|*.apk|*.jar|*.apex) return 0;; esac
    local magic
    magic=$(head -c 4 -- "$1" | od -An -tx1 | tr -d ' \n')
    [[ $magic == 504b0304 || $magic == 504b0506 || $magic == 504b0708 ]]
}
scan_archive() {
    local archive=$1 depth=$2 bytes member extracted
    ((depth<=4)) || { echo 'Embedded ZIP nesting limit exceeded' >&2; exit 1; }
    count=$((count+1))
    ((count<=128)) || { echo 'Embedded ZIP count limit exceeded' >&2; exit 1; }
    unzip -t "$archive" >/dev/null
    bytes=$(unzip -l "$archive" | tail -n1 | awk '{print $1}')
    [[ $bytes =~ ^[0-9]+$ && $bytes -le 16777216 ]] || { echo 'Oversized embedded ZIP' >&2; exit 1; }
    total_bytes=$((total_bytes+bytes))
    ((total_bytes<=67108864)) || { echo 'Embedded ZIP total extraction budget exceeded' >&2; exit 1; }
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
    while IFS= read -r -d '' member; do
        if is_zip "$member"; then scan_archive "$member" "$((depth+1))"; fi
    done < <(find "$extracted" -type f -print0)
}
while IFS= read -r -d '' archive; do
    if is_zip "$archive"; then scan_archive "$archive" 1; fi
done < <(find "$payload" -type f -print0)
printf 'Embedded ZIP integrity, bounded extraction, privacy and Python scans passed: %s archives.\n' "$count"
