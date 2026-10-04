#!/usr/bin/env bash
# Accept only an exact reviewed prefix, then stage the complete reviewed stack.
# Overlapping patches must not weaken earlier context checks or admit local edits.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_path=${1:?Pinned active recovery source is required}
mode=${2:-apply}
[[ $mode == apply || $mode == check ]]
[[ $(git -C "$source_path" rev-parse HEAD) == 3d733672081bca3af42475a286145f4a8cdce4e7 ]]
mkdir -p "$component/build"
scratch=$(mktemp -d "$component/build/recovery-stack-XXXXXX")
trap '[[ ! -f $scratch/index ]] || unlink "$scratch/index"; [[ ! -f $scratch/file ]] || unlink "$scratch/file"; rmdir "$scratch"' EXIT
index="$scratch/index"
GIT_INDEX_FILE="$index" git -C "$source_path" read-tree HEAD
matches_index() {
    cmp -s <(GIT_INDEX_FILE="$index" git -C "$source_path" diff --cached --name-only HEAD) \
        <(git -C "$source_path" diff --name-only HEAD) || return 1
    local path
    while IFS= read -r path; do
        cmp -s <(GIT_INDEX_FILE="$index" git -C "$source_path" show ":$path") "$source_path/$path" || return 1
    done < <(GIT_INDEX_FILE="$index" git -C "$source_path" diff --cached --name-only HEAD)
}
known=false
if matches_index; then known=true; fi
while IFS= read -r patch; do
    [[ $patch =~ ^[0-9]{4}-[a-z0-9-]+\.patch$ ]]
    GIT_INDEX_FILE="$index" git -C "$source_path" apply --cached --unidiff-zero "$component/patches/$patch"
    if matches_index; then known=true; fi
done < "$component/configs/recovery-patches.list"
if [[ $mode == check ]]; then
    matches_index || { echo 'Active recovery differs from the complete reviewed stack' >&2; exit 1; }
    exit 0
fi
$known || { echo 'Unknown active recovery changes; no source files were replaced' >&2; exit 1; }
while IFS= read -r path; do
    GIT_INDEX_FILE="$index" git -C "$source_path" show ":$path" > "$scratch/file"
    cmp -s "$scratch/file" "$source_path/$path" || cp -- "$scratch/file" "$source_path/$path"
done < <(GIT_INDEX_FILE="$index" git -C "$source_path" diff --cached --name-only HEAD)
matches_index
