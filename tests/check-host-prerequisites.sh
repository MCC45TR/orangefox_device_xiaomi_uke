#!/usr/bin/env bash
# Source preparation must reject a missing search tool before staging anything.
set -euo pipefail
export LC_ALL=C LANG=C
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d "$component/build/prerequisite-refusal-XXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/empty-path"
for script in prepare-build-tree.sh prepare-text-layout-sources.sh; do
    status=0
    PATH="$work/empty-path" /bin/bash "$component/scripts/$script" \
        > "$work/$script.log" 2>&1 || status=$?
    [[ $status == 127 ]]
    printf '%s\n' 'Required host tool is unavailable: rg' > "$work/expected"
    cmp "$work/expected" "$work/$script.log"
done
printf '%s\n' 'Both source preparation entrypoints reject missing host tools before staging.'
