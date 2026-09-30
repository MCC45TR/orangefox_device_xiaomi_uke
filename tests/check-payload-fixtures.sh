#!/usr/bin/env bash
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/system/bin"
"$component/tests/check-payload.sh" "$fixture" >/dev/null
reject() {
    if "$component/tests/check-payload.sh" "$fixture" >/dev/null 2>&1; then
        echo 'Unsafe payload fixture was accepted' >&2; exit 1
    fi
}
touch "$fixture/system/bin/python3"
reject
rm "$fixture/system/bin/python3"
ln -s /system/bin/python3 "$fixture/system/bin/python3"
reject
rm "$fixture/system/bin/python3"
printf '/%s/%s/source.cc\n' home fixture > "$fixture/system/bin/test"
reject
rm "$fixture/system/bin/test"
private_root=home
ln -s "/$private_root/fixture/source" "$fixture/system/bin/test"
reject
echo 'Payload path and Python rejection fixtures passed.'
