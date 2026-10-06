#!/usr/bin/env bash
# Accept only an exact reviewed prefix, then stage the complete reviewed stack.
# Overlapping patches must not weaken earlier context checks or admit local edits.
set -euo pipefail
component=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_path=${1:?Pinned active source is required}
mode=${2:-apply}
kind=${3:?Reviewed component kind is required}
[[ $mode == apply || $mode == check ]]
case $kind in
    recovery) pin=3d733672081bca3af42475a286145f4a8cdce4e7; patch_list=recovery-patches.list;;
    fastboot) pin=1efa79514b2f520c20a837c9216ff6b6e7e0dda3; patch_list=fastboot-patches.list;;
    soong) pin=6dc77879464584ef3f178cae622134ed0bf19e1e; patch_list=soong-patches.list;;
    blueprint) pin=dcb14f2e146f40cf1f212efb220e9aa1f3cfc280; patch_list=blueprint-patches.list;;
    freetype) pin=d968d2541f7158e18ab22680bfa08a538019bf6a; patch_list=freetype-patches.list;;
    boot-control) pin=bdefb2a8bce20dc15882d4ab668fb628c427e26b; patch_list=boot-control-patches.list;;
    *) echo 'Unknown reviewed component' >&2; exit 1;;
esac
[[ $(git -C "$source_path" rev-parse HEAD) == "$pin" ]]
mkdir -p "$component/build"
scratch=$(mktemp -d "$component/build/reviewed-stack-XXXXXX")
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
done < "$component/configs/$patch_list"
if [[ $mode == check ]]; then
    matches_index || { echo 'Active source differs from the complete reviewed stack' >&2; exit 1; }
    exit 0
fi
$known || { echo 'Unknown active source changes; no source files were replaced' >&2; exit 1; }
while IFS= read -r path; do
    GIT_INDEX_FILE="$index" git -C "$source_path" show ":$path" > "$scratch/file"
    cmp -s "$scratch/file" "$source_path/$path" || cp -- "$scratch/file" "$source_path/$path"
done < <(GIT_INDEX_FILE="$index" git -C "$source_path" diff --cached --name-only HEAD)
matches_index
